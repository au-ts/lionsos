# Copyright 2026, UNSW SPDX-License-Identifier: BSD-2-Clause

import argparse
import subprocess
from os import path
from importlib.metadata import version
from sdfgen_helper import copy_elf, update_elf_section

from sdfgen import SystemDescription, Sddf, DeviceTree

assert version("sdfgen").split(".")[1] == "28", "Unexpected sdfgen version"

<<<<<<< HEAD
ProtectionDomain = SystemDescription.ProtectionDomain
MemoryRegion = SystemDescription.MemoryRegion
Map = SystemDescription.Map
Channel = SystemDescription.Channel

# System network constants
ext_net = 0
int_net = 1

macs = [
    [0x00, 0x01, 0xC0, 0x39, 0xD5, 0x18],  # External network
    [0x00, 0x01, 0xC0, 0x39, 0xD5, 0x10],  # Internal network
]

subnet_bits = [12, 24]  # External network, Internal network

ips = ["172.16.2.1", "192.168.1.1"]  # External network, Internal network

@dataclass
class Board:
    name: str
    arch: SystemDescription.Arch
    paddr_top: int
    serial: str
    timer: str
    ethernet0: str
    ethernet1: str


BOARDS: List[Board] = [
    Board(
        name="qemu_virt_aarch64",
        arch=SystemDescription.Arch.AARCH64,
        paddr_top=0x6_0000_000,
        serial="pl011@9000000",
        timer="timer",
        ethernet0="virtio_mmio@a003c00",
        ethernet1="virtio_mmio@a003e00",
    ),
    Board(
        name="imx8mp_iotgate",
        arch=SystemDescription.Arch.AARCH64,
        paddr_top=0x70_000_000,
        serial="soc@0/bus@30800000/serial@30890000",
        timer="soc@0/bus@30000000/timer@302d0000",
        ethernet0="soc@0/bus@30800000/ethernet@30be0000",  # IMX
        ethernet1="soc@0/bus@30800000/ethernet@30bf0000",  # DWMAC
    ),
]

# Memory region size helper functions
page_size = 0x1000
Uint64_Bytes = 8


def round_up_to_Page(size: int) -> int:
    if size < page_size:
        return page_size
    elif size % page_size == 0:
        return size
    else:
        return size + (page_size - (size % page_size))


# Class for encoding data structures that are held inside memory regions. Allows
# the metaprogram to extract the size of struct types from firewall .elf files
class FirewallDataStructure:
    def __init__(
        self,
        *,
        size: int = 0,
        entry_size: int = 0,
        capacity: int = 1,
        size_formula=lambda x: x.entry_size * x.capacity,
        elf_name=None,
        c_name=None,
    ):

        self.size = size
        self.entry_size = entry_size
        self.capacity = capacity
        self.size_formula = size_formula
        self.elf_name = elf_name
        self.c_name = c_name

        if not size and (entry_size and capacity):
            self.size = size_formula(self)

        # If data structure has size 0, it needs to be calculated from elf files
        if not self.size and not (elf_name and c_name):
            raise Exception(
                "FirewallDataStructure: Structure of size 0 created with invalid .elf extraction parameters!"
            )

    # To be called after entry size has been extracted
    def calculate_size(self):
        if self.size:
            return
        if not self.entry_size:
            raise Exception(
                f"FirewallDataStructure: Entry size of structure with c name {self.c_name} was 0 during size calculation!"
            )

        self.size = self.size_formula(self)
        if not self.size:
            raise Exception(
                f"FirewallDataStructure: Calculated size of structure with c name {self.c_name}, entry size {self.entry_size} and capacity {self.capacity} was 0!"
            )


# Class for creating firewall memory regions to be mapped into components.
# Memory regions can be created directly, or by listing the data structures to
# be held within them. Data structures are encoded using the
# FirewallDataStructure class. This allows the metaprogram to extract the size
# of the data structures and calculate the size a memory region requires in
# order to hold all the data structures within it.
class FirewallMemoryRegions:
    # Store all memory regions
    regions = []

    def __init__(
        self,
        *,
        min_size: int = 0,
        data_structures: List[FirewallDataStructure] = [],
        size_formula=lambda list: sum(item.size for item in list),
    ):

        self.min_size = min_size
        self.data_structures = data_structures
        self.size_formula = size_formula

        if not min_size and not len(data_structures):
            raise Exception(
                "FirewallMemoryRegions: Region of size 0 created without internal data structure components"
            )

        FirewallMemoryRegions.regions.append(self)

    # To be called after data structure sizes have been calculated
    def calculate_size(self):
        if self.min_size:
            return

        self.min_size = self.size_formula(self.data_structures)
        if not self.min_size:
            raise Exception(
                f"FirewallMemoryRegions: Calculated minimum size of region with data structure list {self.data_structures} was 0!"
            )

    @property
    def region_size(self):
        if not self.min_size:
            return 0

        return round_up_to_Page(self.min_size)


# Firewall memory region and data structure object declarations, update region capacities here
fw_queue_wrapper = FirewallDataStructure(elf_name="routing.elf", c_name="fw_queue")

dma_buffer_queue = FirewallDataStructure(
    elf_name="routing.elf", c_name="net_buff_desc", capacity=512
=======
from typing import List
from pyfw.memory_layout import (
    resolve_region_sizes,
>>>>>>> main
)
from pyfw.specs import TrackedNet, FirewallMemoryRegion
from pyfw.component_arp import ArpRequester, ArpResponder
from pyfw.component_filter import Filter
from pyfw.component_icmp import IcmpModule
from pyfw.component_net_virt import NetVirtRx, NetVirtTx
from pyfw.component_router import Router
from pyfw.component_webserver import Webserver
from pyfw.constants import (
    BuildConstants,
    BOARDS,
    FILTER_ACTION_REJECT,
    interfaces,
    supported_protocols,
    webserver_tx_interface_idx,
    dma_buffer_region,
    ethtype_arp,
    arp_eth_opcode_request,
    arp_eth_opcode_response,
    eththype_ip,
)
from pyfw.component_fw_interface import FirewallInterface

SDF_ProtectionDomain = SystemDescription.ProtectionDomain
SDF_Channel = SystemDescription.Channel

fw_interfaces: List[FirewallInterface] = []

def generate(sdf_file: str, dtb: DeviceTree) -> None:
    # Create interfaces and component classes
    for net_iface in interfaces:
        iface = FirewallInterface(net_iface)
        fw_interfaces.append(iface)

        iface.ethernet_driver = SDF_ProtectionDomain(
            f"ethernet_driver{iface.index}",
            f"eth_driver{iface.index}.elf",
            priority=iface.priorities.ethernet_driver,
            budget=100,
            period=400,
        )

        iface.rx_virtualiser = NetVirtRx(iface, None, iface.priorities.rx_virtualiser)
        iface.tx_virtualiser = NetVirtTx(iface, iface.priorities.tx_virtualiser)
        iface.arp_requester = ArpRequester(iface, iface.priorities.arp_requester)
        iface.arp_responder = ArpResponder(iface, iface.priorities.arp_responder)

        iface.filters = {
            protocol:
                Filter(iface.index, protocol, iface.priorities.filters[supported_protocols[protocol]])
            for protocol in supported_protocols.keys()
        }

        iface.rx_dma_region = FirewallMemoryRegion(
            f"rx_dma_region{iface.index}", dma_buffer_region.region_size, physical=True,
        )

        ethernet_node_path = board.ethernet_node_path(iface.board_ethernet)
        ethernet_node = dtb.node(ethernet_node_path)
        assert ethernet_node is not None, (
            f"Could not find device tree node: {ethernet_node_path}"
        )

        iface.net_system = TrackedNet(
            ethernet_node,
            iface.ethernet_driver,
            iface.tx_virtualiser.pd,
            iface.rx_virtualiser.pd,
            iface.rx_dma_region.mr,
            interface_index=iface.index,
        )

        iface.rx_virtualiser._sddf_net = iface.net_system

        if not path.isdir(iface.out_dir):
            assert subprocess.run(["mkdir", iface.out_dir]).returncode == 0

    router = Router()
    webserver = Webserver()
    icmp_module = IcmpModule()

    # Create timer and serial subsystems
    serial_node = dtb.node(board.serial)
    assert serial_node is not None
    timer_node = dtb.node(board.timer)
    assert timer_node is not None

    timer_driver = SDF_ProtectionDomain("timer_driver", "timer_driver.elf", priority=101)
    timer_system = Sddf.Timer(BuildConstants.sdf(), timer_node, timer_driver)

    # Add global component timer clients
    timer_system.add_client(webserver.pd)

    serial_driver = SDF_ProtectionDomain("serial_driver", "serial_driver.elf", priority=100)
    serial_virt_tx = SDF_ProtectionDomain("serial_virt_tx", "serial_virt_tx.elf", priority=99)
    serial_system = Sddf.Serial(BuildConstants.sdf(), serial_node, serial_driver, serial_virt_tx)

    # Add global component serial clients
    serial_system.add_client(router.pd)
    serial_system.add_client(webserver.pd)
    serial_system.add_client(icmp_module.pd)

    # Register all PDs to the sdf
    register_pds(timer_driver, serial_driver, serial_virt_tx, router, webserver, icmp_module)

    # Wire per-interface connections for traffic forwarding
    wire_interface_connections(router, serial_system, timer_system)

    # Wire global component connections
    wire_virtualiser_connections()
    wire_icmp_connections(icmp_module, router)
    webserver_lib_sddf_lwip = wire_webserver_connections(webserver, router)

<<<<<<< HEAD
    networks[int_net]["rx_dma_region"] = MemoryRegion(
        sdf, "rx_dma_region1", dma_buffer_region.region_size, physical=True
    )
    sdf.add_mr(networks[int_net]["rx_dma_region"])

    # Create network subsystems
    networks[ext_net]["in_net"] = Sddf.Net(
        sdf,
        ethernet_node1,
        networks[ext_net]["driver"],
        networks[int_net]["out_virt"],
        networks[ext_net]["in_virt"],
        networks[ext_net]["rx_dma_region"],
    )
    networks[int_net]["out_net"] = networks[ext_net]["in_net"]

    networks[int_net]["in_net"] = Sddf.Net(
        sdf,
        ethernet_node0,
        networks[int_net]["driver"],
        networks[ext_net]["out_virt"],
        networks[int_net]["in_virt"],
        networks[int_net]["rx_dma_region"],
    )
    networks[ext_net]["out_net"] = networks[int_net]["in_net"]

    # Create firewall pds
    networks[ext_net]["router"] = ProtectionDomain(
        "routing0", "routing0.elf", priority=97, budget=20000
    )
    networks[int_net]["router"] = ProtectionDomain(
        "routing1", "routing1.elf", priority=94, budget=20000
    )

    networks[ext_net]["arp_resp"] = ProtectionDomain(
        "arp_responder0", "arp_responder0.elf", priority=95, budget=20000
    )
    networks[int_net]["arp_resp"] = ProtectionDomain(
        "arp_responder1", "arp_responder1.elf", priority=93, budget=20000
    )

    networks[ext_net]["arp_req"] = ProtectionDomain(
        "arp_requester0", "arp_requester0.elf", priority=98, budget=20000
    )
    networks[int_net]["arp_req"] = ProtectionDomain(
        "arp_requester1", "arp_requester1.elf", priority=95, budget=20000
    )

    # Create the webserver component
    webserver = ProtectionDomain(
        "micropython", "micropython.elf", priority=1, budget=20000, stack_size=0x10000
    )
    common_pds.append(webserver)

    # Webserver is a serial and timer client
    serial_system.add_client(webserver)
    timer_system.add_client(webserver)

    # Create ICMP Module component
    icmp_module = ProtectionDomain(
        "icmp_module", "icmp_module.elf", priority=100, budget=20000
    )
    common_pds.append(icmp_module)

    # Courtney: Not related to the TCP tracking but this was a fix I forgot to
    # add earlier!
    networks[ext_net]["filters"] = {}
    networks[ext_net]["filters"][ip_protocol_icmp] = ProtectionDomain(
        "icmp_filter0", "icmp_filter0.elf", priority=90, budget=20000
    )
    networks[ext_net]["filters"][ip_protocol_udp] = ProtectionDomain(
        "udp_filter0", "udp_filter0.elf", priority=91, budget=20000
    )
    networks[ext_net]["filters"][ip_protocol_tcp] = ProtectionDomain(
        "tcp_filter0", "tcp_filter0.elf", priority=92, budget=20000
    )

    networks[int_net]["filters"] = {}
    networks[int_net]["filters"][ip_protocol_icmp] = ProtectionDomain(
        "icmp_filter1", "icmp_filter1.elf", priority=93, budget=20000
    )
    networks[int_net]["filters"][ip_protocol_udp] = ProtectionDomain(
        "udp_filter1", "udp_filter1.elf", priority=91, budget=20000
    )
    networks[int_net]["filters"][ip_protocol_tcp] = ProtectionDomain(
        "tcp_filter1", "tcp_filter1.elf", priority=92, budget=20000
    )

    for pd in common_pds:
        sdf.add_pd(pd)

    # Initial network loop to maintain client ordering consistency across
    # networks
    for network in networks:
        # Add all pds to the system
        for maybe_pd in network.values():
            if type(maybe_pd) == ProtectionDomain:
                # Drivers and routers do not need to be copied
                if maybe_pd != network["driver"]:
                    # remove x.elf suffix from elf
                    copy_elf(
                        maybe_pd.program_image[:-5],
                        maybe_pd.program_image[:-5],
                        network["num"],
                    )
                sdf.add_pd(maybe_pd)

        for filter_pd in network["filters"].values():
            copy_elf(
                filter_pd.program_image[:-5],
                filter_pd.program_image[:-5],
                network["num"],
            )
            sdf.add_pd(filter_pd)

        # Since arp requesters are net clients of the output network, we add
        # them as network clients here first. This ensures that we do not
        # process and serialise any networks before the corresponding arp
        # requester has been added
        network["out_net"].add_client_with_copier(network["arp_req"])

    # Webserver is a tx client of the internal network
    networks[int_net]["in_net"].add_client_with_copier(webserver, rx=False)

    # Webserver uses lib sDDF LWIP
    webserver_lib_sddf_lwip = Sddf.Lwip(sdf, networks[int_net]["in_net"], webserver)

    # Webserver receives traffic from the internal -> external router
    router_webserver_conn = fw_connection(
        networks[int_net]["router"],
        webserver,
        dma_buffer_queue.capacity,
        dma_buffer_queue_region.region_size,
    )

    # Webserver returns packets to interior rx virtualiser
    webserver_in_virt_conn = fw_connection(
        webserver,
        networks[int_net]["in_virt"],
        dma_buffer_queue.capacity,
        dma_buffer_queue_region.region_size,
    )

    # Webserver needs access to rx dma region
    webserver_data_region = fw_region(
        webserver,
        networks[int_net]["rx_dma_region"],
        "rw",
        dma_buffer_queue_region.region_size,
    )

    # Webserver has arp channel for arp requests/responses
    webserver_arp_conn = fw_arp_connection(
        webserver,
        networks[ext_net]["arp_req"],
        arp_queue_buffer.capacity,
        arp_queue_region.region_size,
    )

    # ICMP Module needs to be able to transmit out of both NICs
    networks[ext_net]["out_net"].add_client_with_copier(icmp_module, rx=False)
    networks[int_net]["out_net"].add_client_with_copier(icmp_module, rx=False)

    # ICMP Module needs to be connected to both routers.
    icmp_int_router_conn = fw_connection(
        networks[int_net]["router"],
        icmp_module,
        icmp_queue_buffer.capacity,
        icmp_queue_region.region_size,
    )
    icmp_ext_router_conn = fw_connection(
        networks[ext_net]["router"],
        icmp_module,
        icmp_queue_buffer.capacity,
        icmp_queue_region.region_size,
    )

    icmp_module_config = FwIcmpModuleConfig(
        list(ip_to_int(ip) for ip in ips),
        [icmp_ext_router_conn[1], icmp_int_router_conn[1]],
        2,
    )

    networks[int_net]["icmp_module"] = icmp_int_router_conn[0]
    networks[ext_net]["icmp_module"] = icmp_ext_router_conn[0]

    # Create webserver config
    webserver_config = FwWebserverConfig(
        network["num"],
        router_webserver_conn[1],
        webserver_data_region,
        webserver_in_virt_conn[0],
        webserver_arp_conn[0],
        [],
    )

    for network in networks:
        router = network["router"]
        out_virt = network["out_virt"]
        in_virt = network["in_virt"]
        arp_req = network["arp_req"]
        arp_resp = network["arp_resp"]

        # Create a firewall data connection between router and output virt with
        # the rx dma region as data region
        router_out_virt_conn = fw_data_connection(
            router,
            out_virt,
            dma_buffer_queue.capacity,
            dma_buffer_queue_region.region_size,
            network["rx_dma_region"],
            "rw",
            "r",
        )

        # Create a firewall connection for output virt to return buffers to
        # input virt
        output_in_virt_conn = fw_connection(
            out_virt,
            in_virt,
            dma_buffer_queue.capacity,
            dma_buffer_queue_region.region_size,
        )
        out_virt_in_virt_data_conn = FwDataConnectionResource(
            output_in_virt_conn[0], router_out_virt_conn[1].data
        )

        # Create output virt config
        network["configs"][out_virt] = FwNetVirtTxConfig(
            network["num"], [router_out_virt_conn[1]], [out_virt_in_virt_data_conn]
        )

        # Create a firewall connection for router to return free buffers to
        # receive virtualiser on interior network
        router_in_virt_conn = fw_connection(
            router,
            in_virt,
            dma_buffer_queue.capacity,
            dma_buffer_queue_region.region_size,
        )

        # Create input virt config
        network["configs"][in_virt] = FwNetVirtRxConfig(
            network["num"], [], [], [router_in_virt_conn[1], output_in_virt_conn[1]]
        )

        # Add arp requester protocol for input virt client 0 - this is for the
        # previously added arp requester which is always client 0
        network["configs"][in_virt].active_client_ethtypes.append(ethtype_arp)
        network["configs"][in_virt].active_client_subtypes.append(
            arp_eth_opcode_response
        )

        # Arp requester needs timer access to handle arp timeouts
        timer_system.add_client(arp_req)

        # Add arp responder filter pd as a network client
        network["in_net"].add_client_with_copier(arp_resp)
        network["configs"][in_virt].active_client_ethtypes.append(ethtype_arp)
        network["configs"][in_virt].active_client_subtypes.append(
            arp_eth_opcode_request
        )

        # Create arp queue firewall connection
        router_arp_conn = fw_arp_connection(
            router, arp_req, arp_queue_buffer.capacity, arp_queue_region.region_size
        )

        # Create arp cache
        arp_cache = fw_shared_region(
            arp_req, router, "rw", "r", "arp_cache", arp_cache_region.region_size
        )

        # Create arp req config
        network["configs"][arp_req] = FwArpRequesterConfig(
            network["num"],
            macs[network["out_num"]],
            ip_to_int(ips[network["out_num"]]),
            [router_arp_conn[1]],
            arp_cache[0],
            arp_cache_buffer.capacity,
        )

        # Create arp resp config
        network["configs"][arp_resp] = FwArpResponderConfig(
            network["num"], network["mac"], network["ip"]
        )

        # Create arp packet queue
        arp_packet_queue_mr = MemoryRegion(
            sdf, "arp_packet_queue_" + router.name, arp_packet_queue_region.region_size
        )
        sdf.add_mr(arp_packet_queue_mr)
        arp_packet_queue = fw_region(
            router, arp_packet_queue_mr, "rw", arp_packet_queue_region.region_size
        )

        # Create routing table
        routing_table = fw_shared_region(
            router,
            webserver,
            "rw",
            "r",
            "routing_table",
            routing_table_region.region_size,
        )

        # Create pp channel for routing table updates
        router_update_ch = Channel(webserver, router, pp_a=True)
        sdf.add_channel(router_update_ch)

        # Create router webserver config
        router_webserver_config = FwWebserverRouterConfig(
            router_update_ch.pd_b_id, routing_table[0], routing_table_buffer.capacity
        )

        webserver_router_config = FwWebserverRouterConfig(
            router_update_ch.pd_a_id, routing_table[1], routing_table_buffer.capacity
        )

        # Create router config
        network["configs"][router] = FwRouterConfig(
            network["num"],
            macs[network["out_num"]],
            ip_to_int(ips[network["out_num"]]),
            subnet_bits[network["out_num"]],
            network["ip"],
            router_in_virt_conn[0],
            None,
            router_out_virt_conn[0].conn,
            router_out_virt_conn[0].data.region,
            router_arp_conn[0],
            arp_cache[1],
            arp_cache_buffer.capacity,
            arp_packet_queue,
            router_webserver_config,
            network["icmp_module"],
            [],
        )

        webserver_interface_config = FwWebserverInterfaceConfig(
            network["mac"], network["ip"], webserver_router_config, []
        )

        for protocol, filter_pd in network["filters"].items():
            # Create a firewall connection for filter to transmit buffers to
            # router
            filter_router_conn = fw_connection(
                filter_pd,
                router,
                dma_buffer_queue.capacity,
                dma_buffer_queue_region.region_size,
            )

            # Connect filter as rx only network client
            network["in_net"].add_client_with_copier(filter_pd, tx=False)
            network["configs"][in_virt].active_client_ethtypes.append(eththype_ip)
            network["configs"][in_virt].active_client_subtypes.append(protocol)

            # create bitmap
            rule_bitmap_mr = MemoryRegion(
                sdf,
                "rules_id_bitmap" + "_" + filter_pd.name,
                filter_rule_bitmap_region.region_size,
            )
            sdf.add_mr(rule_bitmap_mr)
            rule_bitmap_region = fw_region(
                filter_pd, rule_bitmap_mr, "rw", filter_rule_bitmap_region.region_size
            )

            # Create rule region
            filter_rules = fw_shared_region(
                filter_pd,
                webserver,
                "rw",
                "r",
                "filter_rules",
                filter_rules_region.region_size,
            )

            # Create pp channel between webserver and filter for rule updates
            filter_update_ch = Channel(webserver, filter_pd, pp_a=True)
            sdf.add_channel(filter_update_ch)

            # Create webserver configs
            filter_webserver_config = FwWebserverFilterConfig(
                protocol,
                filter_update_ch.pd_b_id,
                FILTER_ACTION_ALLOW,
                filter_rules[0],
                filter_rules_buffer.capacity,
            )

            webserver_filter_config = FwWebserverFilterConfig(
                protocol,
                filter_update_ch.pd_a_id,
                FILTER_ACTION_ALLOW,
                filter_rules[1],
                filter_rules_buffer.capacity,
            )

            # Create filter config
            network["configs"][filter_pd] = FwFilterConfig(
                network["num"],
                filter_instances_buffer.capacity,
                filter_router_conn[0],
                filter_webserver_config,
                None,
                None,
                rule_bitmap_region,
            )

            network["configs"][router].filters.append((filter_router_conn[1]))
            webserver_interface_config.filters.append(webserver_filter_config)

        webserver_config.interfaces.append(webserver_interface_config)

        # Make router and arp components serial clients
        serial_system.add_client(router)
        serial_system.add_client(arp_req)
        serial_system.add_client(arp_resp)

        assert network["in_net"].connect()
        assert network["in_net"].serialise_config(network["out_dir"])

    # Add webserver as a free client of interior rx virt
    networks[int_net]["configs"][networks[int_net]["in_virt"]].free_clients.append(
        webserver_in_virt_conn[1]
    )

    # Add webserver as an arp requester client outputting to the internal
    # network
    networks[ext_net]["configs"][networks[ext_net]["arp_req"]].arp_clients.append(
        webserver_arp_conn[1]
    )

    # Add a firewall connection to the webserver from the internal router for
    # packet transmission
    networks[int_net]["configs"][networks[int_net]["router"]].rx_active = (
        router_webserver_conn[0]
    )

    # Create filter instance regions
    for protocol, filter_pd in networks[int_net]["filters"].items():
        mirror_filter = networks[ext_net]["filters"][protocol]

        # Courtney: Unlike the other filters which only require read-only
        # permissions to inspect their neighbour's instance regions, the TCP
        # filters need write permission so they can update the state of the
        # connection as TCP syn-fin-etc packets are received
        if protocol == ip_protocol_tcp:
            perms2 = "rw"
        else:
            perms2 = "r"

        int_instances = fw_shared_region(
            filter_pd,
            mirror_filter,
            "rw",
            perms2,
            "instances",
            filter_instances_region.region_size,
        )
        ext_instances = fw_shared_region(
            mirror_filter,
            filter_pd,
            "rw",
            perms2,
            "instances",
            filter_instances_region.region_size,
        )

        networks[int_net]["configs"][filter_pd].local_instances = int_instances[0]
        networks[int_net]["configs"][filter_pd].extern_instances = ext_instances[1]
        networks[ext_net]["configs"][mirror_filter].local_instances = ext_instances[
            0
        ]
        networks[ext_net]["configs"][mirror_filter].extern_instances = int_instances[
            1
        ]
=======
    # Connect sDDF systems and serialize subsystems
    for iface in fw_interfaces:
        assert iface.net_system.connect()
        assert iface.net_system.serialise_config(iface.out_dir)
>>>>>>> main

    assert serial_system.connect()
    assert serial_system.serialise_config(BuildConstants.output_dir())
    assert timer_system.connect()
    assert timer_system.serialise_config(BuildConstants.output_dir())

    assert webserver_lib_sddf_lwip.connect()
    assert webserver_lib_sddf_lwip.serialise_config(BuildConstants.output_dir())

    # Serialize firewall configs- this implicitly finalises all configs
    serialize_all_fw_configs(router, webserver, icmp_module, obj_copy)

    # Render SDF
    with open(f"{BuildConstants.output_dir()}/{sdf_file}", "w+") as f:
        f.write(BuildConstants.sdf().render())


def register_pds(
    timer_driver: SDF_ProtectionDomain,
    serial_driver: SDF_ProtectionDomain,
    serial_virt_tx: SDF_ProtectionDomain,
    router: Router,
    webserver: Webserver,
    icmp_module: IcmpModule,
) -> None:
    """Register all PDs with SDF and copy ELFs for per-interface components."""
    for pd in [timer_driver, serial_driver, serial_virt_tx, webserver.pd, icmp_module.pd, router.pd]:
        BuildConstants.sdf().add_pd(pd)

    for iface in fw_interfaces:
        for component in [
            iface.tx_virtualiser,
            iface.rx_virtualiser,
            iface.arp_requester,
            iface.arp_responder,
        ]:
            copy_elf(component.pd.program_image[:-5], component.pd.program_image[:-5], iface.index)
            BuildConstants.sdf().add_pd(component.pd)

        BuildConstants.sdf().add_pd(iface.ethernet_driver)

        for ip_filter in iface.filters.values():
            copy_elf(ip_filter.pd.program_image[:-5], ip_filter.pd.program_image[:-5], iface.index)
            BuildConstants.sdf().add_pd(ip_filter.pd)


def wire_interface_connections(
    router: Router,
    serial_system: Sddf.Serial,
    timer_system: Sddf.Timer,
) -> None:
    """Connect components which are duplicated per network interface"""
    for iface in fw_interfaces:

        # ARP responder receives and transmits net traffic
        iface.rx_virtualiser.add_active_net_client(
            iface.arp_responder, ethtype_arp, arp_eth_opcode_request, tx = True
        )

        # ARP requester receives and transmits net traffic
        iface.rx_virtualiser.add_active_net_client(
            iface.arp_requester, ethtype_arp, arp_eth_opcode_response, tx = True
        )

        # Router is an ARP requester client
        assert router.interfaces is not None
        router.interfaces[iface.index].arp_queue = iface.arp_requester.add_arp_client(router)

        # Router needs access to the ARP cache
        router.interfaces[iface.index].arp_cache = iface.arp_requester.share_cache(router)

        for protocol, ip_filter in iface.filters.items():
            # Filter receives traffic from the Rx virtualiser
            iface.rx_virtualiser.add_active_net_client(
                ip_filter, eththype_ip, protocol
            )

            # Filter transmits traffic to the router
            assert router.interfaces[iface.index].filters is not None
            router.interfaces[iface.index].filters.append(
                ip_filter.connect_router(router)
            )


        # Router needs access to the Rx DMA region
        router.interfaces[iface.index].data = iface.rx_dma_region.map(router.pd, "rw")

        # Router returns dropped packets to the Rx virtualiser
        router.interfaces[iface.index].rx_free = iface.rx_virtualiser.add_free_fw_client(router)

        # Router transmits packets to the Tx virtualiser
        router.interfaces[iface.index].tx_active = iface.tx_virtualiser.add_active_fw_client(router)

        # Add serial clients
        serial_system.add_client(iface.arp_responder.pd)
        serial_system.add_client(iface.arp_requester.pd)

        # Add timer clients
        timer_system.add_client(iface.arp_requester.pd)


def wire_virtualiser_connections() -> None:
    """Wire Rx DMA region access and DMA buffer return queues between virtualisers."""

    for tx_virtualiser in (interface.tx_virtualiser for interface in fw_interfaces):
        for interface in fw_interfaces:
            # Tx virtualiser returns freed packets to the Rx virtualiser
            free_conn = interface.rx_virtualiser.add_free_fw_client(tx_virtualiser)
            tx_virtualiser.add_free_fw_client(free_conn, interface.rx_dma_region, interface.index)

def wire_icmp_connections(
    icmp_module: IcmpModule,
    router: Router,
) -> None:
    """Wire ICMP module connections."""
    for iface in fw_interfaces:
        iface.net_system.add_client_with_copier(icmp_module.pd, rx=False)

        # ICMP module needs to be connected to all filters supporting the reject action
        for ip_filter in iface.filters.values():
            assert ip_filter.webserver is not None
            assert ip_filter.webserver.actions is not None
            ip_filter.icmp_module = icmp_module.connect_filter(ip_filter,
                                                               iface.index,
                                                               ip_filter.webserver.actions[FILTER_ACTION_REJECT - 1])

    router.icmp_module = icmp_module.connect_router(router)

def wire_webserver_connections(
    webserver: Webserver,
    router: Router,
) -> Sddf.Lwip:

    tx_interface = fw_interfaces[webserver_tx_interface_idx]

    # Webserver is a transmit net client
    tx_interface.net_system.add_client_with_copier(webserver.pd, rx=False)

    # Webserver uses lib sDDF LWIP
    webserver_lib_sddf_lwip = Sddf.Lwip(BuildConstants.sdf(), tx_interface.net_system._net, webserver.pd)

    # Webserver is an ARP client of its output interface
    webserver.arp_queue = tx_interface.arp_requester.add_arp_client(webserver)

    # Connect Webserver and router
    webserver.router = router.connect_webserver(webserver)

    for iface in fw_interfaces:
        # Webserver needs access to the Rx DMA region
        assert webserver.interfaces is not None
        webserver.interfaces[iface.index].data = iface.rx_dma_region.map(webserver.pd, "rw")

        # Webserver returns buffers to the Rx virtualiser
        webserver.interfaces[iface.index].rx_free = iface.rx_virtualiser.add_free_fw_client(webserver)

        # Webserver needs to be connected to all filters
        for ip_filter in iface.filters.values():
            assert webserver.interfaces[iface.index].filters is not None
            webserver.interfaces[iface.index].filters.append(
                ip_filter.connect_webserver(webserver)
            )

    return webserver_lib_sddf_lwip

def serialize_all_fw_configs(
    router: Router,
    webserver: Webserver,
    icmp_module: IcmpModule,
    obj_copy_path: str,
) -> None:
    """Serialize configs to data files and update ELF sections."""
    for iface in fw_interfaces:
        for component in iface.all_components():
            data_path = f"{iface.out_dir}/firewall_config_{component.name}.data"
            with open(data_path, "wb+") as f:
                f.write(component.serialise())
            update_elf_section(obj_copy_path, component.pd.program_image, component.section_name, data_path)

    # Router
    data_path = f"{BuildConstants.output_dir()}/firewall_config_routing.data"
    with open(data_path, "wb+") as f:
        f.write(router.serialise())
    update_elf_section(obj_copy_path, router.pd.program_image, router.section_name, data_path)

    # Webserver
    data_path = f"{BuildConstants.output_dir()}/firewall_config_webserver.data"
    with open(data_path, "wb+") as f:
        f.write(webserver.serialise())
    update_elf_section(obj_copy_path, webserver.pd.program_image, webserver.section_name, data_path)

    # ICMP module
    data_path = f"{BuildConstants.output_dir()}/firewall_icmp_module_config.data"
    with open(data_path, "wb+") as f:
        f.write(icmp_module.serialise())
    update_elf_section(obj_copy_path, icmp_module.pd.program_image, icmp_module.section_name, data_path)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--dtb", required=True)
    parser.add_argument("--sddf", required=True)
    parser.add_argument("--board", required=True, choices=[b.name for b in BOARDS])
    parser.add_argument("--output", required=True)
    parser.add_argument("--sdf", required=True)
    parser.add_argument("--objcopy", required=True)
    parser.add_argument("--objdump", required=True)
    args = parser.parse_args()

    board = next(filter(lambda b: b.name == args.board, BOARDS))

    BuildConstants.set_output_dir(args.output)
    BuildConstants.set_sdf(SystemDescription(board.arch, board.paddr_top))
    sddf = Sddf(args.sddf)

    global obj_copy
    obj_copy = args.objcopy

    with open(args.dtb, "rb") as f:
        dtb = DeviceTree(f.read())

    resolve_region_sizes()

    generate(args.sdf, dtb)
