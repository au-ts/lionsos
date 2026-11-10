#include "conn.h"
#include "cspace.h"
#include "page_table.h"
#include "pager.h"
#include "proc.h"
#include "untyped.h"

#include <microkit.h>
#include <sddf/util/printf.h>
#include <sddf/util/util.h>
#include <string.h>

#define PEER_CNODE_DEPTH 9

#define CONN_NO_PROCESS 0xFF
/* The process each spare is given to. */
static uint8_t spare_owner[PAGER_MAX_SPARES];

static inline seL4_CPtr pager_caps_cptr(void)
{
	return microkit_cspace_root_slot_to_cptr(pager_config.pager_caps.slot);
}

static inline seL4_CPtr peer_cnode(uint8_t peer)
{
	return pager_caps_cptr() + PAGER_PEER_CNODE_SLOT(peer);
}

static inline seL4_CPtr spare_frame(const pager_spare_conn_t *spare, uint8_t region, uint32_t frame)
{
	return microkit_cspace_root_slot_to_cptr(pager_config.connection_frames.slot) +
		   spare->frames[region] + frame;
}

static seL4_Error park_on_sink(const pager_spare_conn_t *spare)
{
	return seL4_CNode_Mint(peer_cnode(spare->peer), BASE_OUTPUT_NOTIFICATION_CAP + spare->peer_id,
						   PEER_CNODE_DEPTH, pager_caps_cptr(), PAGER_SINK_NTFN_SLOT,
						   PAGER_CNODE_DEPTH, seL4_AllRights, 0);
}

void conn_init(void)
{
	memset(spare_owner, CONN_NO_PROCESS, sizeof(spare_owner));
	if (pager_config.num_spares == 0) return;

	seL4_Error error = untyped_alloc(seL4_NotificationObject, 0, PAGER_SINK_NTFN_SLOT,
									 pager_caps_cptr(), 1);
	if (error != seL4_NoError) {
		sddf_printf("pager: could not create the sink notification: %d\n", error);
		return;
	}
	for (uint8_t i = 0; i < pager_config.num_spares; ++i) {
		error = park_on_sink(&pager_config.spares[i]);
		if (error != seL4_NoError) {
			sddf_printf("pager: could not park spare connection %u on peer %u: %d\n", i,
						pager_config.spares[i].peer, error);
		}
	}
}

static bool spare_fits(const pager_spare_conn_t *spare, const pager_conn_template_t *conn)
{
	if (spare->kind != conn->kind || spare->num_regions != conn->num_regions) return false;
	for (uint8_t r = 0; r < conn->num_regions; ++r) {
		if ((uint64_t)spare->num_frames[r] * PAGE_SIZE != conn->regions[r].size) return false;
	}
	return true;
}

static int claim_spare(uint32_t pid, const pager_conn_template_t *conn)
{
	for (uint8_t i = 0; i < pager_config.num_spares; ++i) {
		if (spare_owner[i] == CONN_NO_PROCESS && spare_fits(&pager_config.spares[i], conn)) {
			spare_owner[i] = pid;
			return i;
		}
	}
	return -1;
}

static int map_spare(uint32_t pid, const pager_spare_conn_t *spare,
					 const pager_conn_template_t *conn)
{
	for (uint8_t r = 0; r < spare->num_regions; ++r) {
		for (uint32_t f = 0; f < spare->num_frames[r]; ++f) {
			uintptr_t vaddr = (uintptr_t)conn->regions[r].vaddr + f * PAGE_SIZE;
			if (!make_page_table_entry(vaddr, pid)) return PROCESS_FORK_CAP;
			seL4_Error error = seL4_ARM_Page_Map(spare_frame(spare, r, f), vspaces[pid], vaddr,
												 create_cap_rights(true),
												 seL4_ARM_Default_VMAttributes | seL4_ARM_ExecuteNever);
			if (error != seL4_NoError) {
				sddf_printf("fork: could not map a connection of %u at 0x%lx: %d\n", pid, vaddr,
							error);
				return PROCESS_FORK_CAP;
			}
		}
	}
	return PROCESS_FORK_OK;
}

static int connect_spare(uint32_t pid, const pager_spare_conn_t *spare,
						 const pager_conn_template_t *conn, seL4_CPtr cspace,
						 seL4_CPtr ntfn_root, seL4_Word ntfn_slot)
{
	seL4_Error error;

	int result = map_spare(pid, spare, conn);
	if (result != PROCESS_FORK_OK) return result;

	error = seL4_CNode_Mint(cspace, BASE_OUTPUT_NOTIFICATION_CAP + conn->client_id,
							PROCESS_CSPACE_SIZE_BITS, pager_caps_cptr(),
							PAGER_PEER_NTFN_SLOT(spare->peer), PAGER_CNODE_DEPTH, seL4_AllRights,
							BIT(spare->peer_id));
	if (error != seL4_NoError) {
		sddf_printf("fork: could not give %u a channel to peer %u: %d\n", pid, spare->peer, error);
		return PROCESS_FORK_CAP;
	}

	/* And the peer signals the spare's channel as the process' client channel. */
	seL4_CNode_Delete(peer_cnode(spare->peer), BASE_OUTPUT_NOTIFICATION_CAP + spare->peer_id,
					  PEER_CNODE_DEPTH);
	error = seL4_CNode_Mint(peer_cnode(spare->peer), BASE_OUTPUT_NOTIFICATION_CAP + spare->peer_id,
							PEER_CNODE_DEPTH, ntfn_root, ntfn_slot, PAGER_CNODE_DEPTH,
							seL4_AllRights, BIT(conn->client_id));
	if (error != seL4_NoError) {
		sddf_printf("fork: could not give peer %u a channel to %u: %d\n", spare->peer, pid, error);
		return PROCESS_FORK_CAP;
	}
	return PROCESS_FORK_OK;
}

int conn_connect(uint32_t pid, uint8_t template_client, seL4_CPtr cspace, seL4_CPtr ntfn_root,
				 seL4_Word ntfn_slot)
{
	const pager_client_resource_t *client = &pager_config.clients[template_client];

	for (uint8_t c = 0; c < client->num_conns; ++c) {
		const pager_conn_template_t *conn = &client->conns[c];
		int spare = claim_spare(pid, conn);
		if (spare < 0) {
			sddf_printf("fork: no spare connection of kind %u left for %u\n", conn->kind, pid);
			return PROCESS_FORK_NO_SLOTS;
		}
		int result = connect_spare(pid, &pager_config.spares[spare], conn, cspace, ntfn_root,
								   ntfn_slot);
		if (result != PROCESS_FORK_OK) return result;
		sddf_dprintf("fork: %u has spare connection %d (kind %u) to peer %u on channel %u\n",
					 pid, spare, conn->kind, pager_config.spares[spare].peer, conn->client_id);
	}
	return PROCESS_FORK_OK;
}

/*
 * TODO: the queues are handed back as the process left them, and the peer is not told the
 * client went away, so a spare is not yet safe to give to another process.
 */
void conn_release(uint32_t pid)
{
	for (uint8_t i = 0; i < pager_config.num_spares; ++i) {
		if (spare_owner[i] != pid) continue;
		const pager_spare_conn_t *spare = &pager_config.spares[i];

		seL4_CNode_Delete(peer_cnode(spare->peer), BASE_OUTPUT_NOTIFICATION_CAP + spare->peer_id,
						  PEER_CNODE_DEPTH);
		park_on_sink(spare);
		for (uint8_t r = 0; r < spare->num_regions; ++r) {
			for (uint32_t f = 0; f < spare->num_frames[r]; ++f) {
				seL4_ARM_Page_Unmap(spare_frame(spare, r, f));
			}
		}
		spare_owner[i] = CONN_NO_PROCESS;
	}
}

bool conn_mac_addr(uint32_t pid, uint8_t mac_addr[6])
{
	for (uint8_t i = 0; i < pager_config.num_spares; ++i) {
		if (spare_owner[i] == pid && pager_config.spares[i].kind == PAGER_CONN_NET_RX) {
			memcpy(mac_addr, pager_config.spares[i].mac_addr, 6);
			return true;
		}
	}
	return false;
}
