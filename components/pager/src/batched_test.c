#include "pager_config.h"

#ifdef PAGER_TEST_BATCHED_MAPPINGS

#include "frame_table.h"
#include "batched.h"
#include <sddf/util/printf.h>

#define N seL4_MaxBatchedMap
#define VADDR(i) (0x30600000UL + (i) * PAGE_SIZE)

void pager_test_batched_mappings(void)
{
    static struct folio *frames[N];
    static seL4_CPtr caps[N];
    static seL4_ARM_BatchedMapping maps[N];
    for (uint32_t level = 0, ips; level < 3 && (ips = get_ips()); ++level) {
        seL4_Error err = seL4_ARM_PageTable_Map(ips_cptr(ips), PAGER_OWN_VSPACE_CPTR, VADDR(0), seL4_ARM_Default_VMAttributes);
        if (err) put_ips(ips);
    }
    uint32_t got = 0;
    for (; got < N && (frames[got] = get_frame()); ++got) {
        caps[got] = frame_cptr(frames[got]->frame_page);
    }
    bool ok = got == N;
    for (uint32_t pass = 0; ok && pass < 2; ++pass) {
        for (uint32_t i = 0; i < N; ++i) {
            maps[i] = (seL4_ARM_BatchedMapping){ caps[i], VADDR(pass ? N - 1 - i : i) };
        }
        ok = !seL4_ARM_VSpace_Map_Batched(PAGER_OWN_VSPACE_CPTR, N, maps, seL4_ReadWrite, seL4_ARM_Default_VMAttributes);
        for (uint32_t i = 0; ok && i < N; ++i) {
            volatile seL4_Word *word = (volatile seL4_Word *)maps[i].vaddr;
            if (pass) ok = *word == i; else *word = i;
        }
        ok = !seL4_ARM_VSpace_Unmap_Batched(PAGER_OWN_VSPACE_CPTR, N, caps) && ok;
    }
    for (uint32_t i = 0; ok && i < N; ++i) {
        frame_table_zero_frame(frames[i]->frame_page);
        put_frame(frames[i]);
    }
    sddf_printf("pager: batched mapping test %s\n", ok ? "PASSED" : "FAILED");
}

#endif
