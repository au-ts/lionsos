# Copyright 2025, UNSW SPDX-License-Identifier: BSD-2-Clause
import argparse
import subprocess
import shutil
from os import path
from itertools import combinations
from dataclasses import dataclass
from typing import List, Dict, Any, Tuple
from importlib.metadata import version
import ipaddress

from sdfgen import SystemDescription, Sddf, DeviceTree

assert version("sdfgen").split(".")[1] == "28", "Unexpected sdfgen version"

from pyfw.specs import (
    TrackedNet, QueueConnection, ArpConnection,
    PairedRegion, PrivateRegion, ReMappableRegion,
)
from pyfw.config_structs import (
    FwDataConnectionResource, FwWebserverFilterConfig,
    FwWebserverRouterConfig, FwWebserverInterfaceConfig,
)
from pyfw.components import (
    NetworkInterface, InterfacePriorities,
    NetVirtRx, NetVirtTx, ArpRequester, ArpResponder, Filter,
    Router, Webserver, IcmpModule,
)
from pyfw.topology import generate_topology_dot

SDF_ProtectionDomain = SystemDescription.ProtectionDomain
SDF_MemoryRegion = SystemDescription.MemoryRegion
SDF_Map = SystemDescription.Map
SDF_Channel = SystemDescription.Channel


def copy_elf(source_elf: str, new_elf: str, elf_number=None):
    """Creates a new elf with elf_number as prefix. Adds '.elf' to elf strings."""
    source_elf += ".elf"
    if elf_number is not None:
        new_elf += str(elf_number)
    new_elf += ".elf"
    assert path.isfile(source_elf)
    return shutil.copyfile(source_elf, new_elf)


def update_elf_section(obj_copy: str, elf_name: str, section_name: str, data_name: str):
    """Copies data region data_name into section_name of elf_name."""
    assert path.isfile(elf_name)
    assert path.isfile(data_name)
    assert (
        subprocess.run(
            [obj_copy, "--update-section", "." + section_name + "=" + data_name, elf_name]
        ).returncode
        == 0
    )

def encode_iface_name(name: str) -> List[int]:
    raw = name.encode("ascii", "ignore")[:31]
    return list(raw)


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
                f"FirewallDataStructure: Calculated size of structure with c name {self.c_name},"
                    "entry size {self.entry_size} and capacity {self.capacity} was 0!"
            )

    # Call to recalculate size after data structure update
    def update_size(self):
        if not self.entry_size:
            raise Exception(
                f"FirewallDataStructure: Entry size of structure with c name {self.c_name} was 0 during size recalculation!"
            )
        self.size = self.size_formula(self)
        if not self.size:
            raise Exception(
                    f"FirewallDataStructure: Recalculated size of structure with c name {self.c_name},"
                        "entry size {self.entry_size} and capacity {self.capacity} was 0!"
            )

# Class for creating firewall memory regions to be mapped into components.
# Memory regions can be created directly, or by listing the data structures to
# be held within them. Data structures are encoded using the
# FirewallDataStructure class. This allows the metaprogram to extract the size
# of the data structures and calculate the size a memory region requires in
# order to hold all the data structures within it.
class FirewallMemoryRegions:
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

    # Call to recalculate size after data structure update
    def update_size(self):
        for structure in self.data_structures:
            structure.update_size()
        self.min_size = self.size_formula(self.data_structures)
        if not self.min_size:
            raise Exception(
                f"FirewallMemoryRegions: Recalculated minimum size of region with data structure list {self.data_structures} was 0!"
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
)
dma_buffer_queue_region = FirewallMemoryRegions(
    data_structures=[fw_queue_wrapper, dma_buffer_queue]
)
dma_buffer_region = FirewallMemoryRegions(min_size=dma_buffer_queue.capacity * 2048)

arp_queue_buffer = FirewallDataStructure(
    elf_name="arp_requester.elf", c_name="fw_arp_request", capacity=512
)
arp_queue_region = FirewallMemoryRegions(
    data_structures=[fw_queue_wrapper, arp_queue_buffer]
)

icmp_queue_buffer = FirewallDataStructure(
    elf_name="icmp_module.elf", c_name="icmp_req", capacity=128
)
icmp_queue_region = FirewallMemoryRegions(
    data_structures=[fw_queue_wrapper, icmp_queue_buffer]
)

arp_cache_buffer = FirewallDataStructure(
    elf_name="arp_requester.elf", c_name="fw_arp_entry", capacity=512
)
arp_cache_region = FirewallMemoryRegions(data_structures=[arp_cache_buffer])

arp_packet_queue_buffer = FirewallDataStructure(
    elf_name="routing.elf",
    c_name="pkt_waiting_node",
    capacity=dma_buffer_queue.capacity,
)
arp_packet_queue_region = FirewallMemoryRegions(
    data_structures=[arp_packet_queue_buffer]
)

routing_table_wrapper = FirewallDataStructure(
    elf_name="routing.elf", c_name="routing_table"
)
routing_table_buffer = FirewallDataStructure(
    elf_name="routing.elf", c_name="routing_entry", capacity=256
)
routing_table_region = FirewallMemoryRegions(
    data_structures=[routing_table_wrapper, routing_table_buffer]
)

filter_rules_wrapper = FirewallDataStructure(
    elf_name="icmp_filter.elf", c_name="fw_rule_table"
)
filter_rules_buffer = FirewallDataStructure(
    elf_name="icmp_filter.elf", c_name="fw_rule", capacity=256
)
filter_rules_region = FirewallMemoryRegions(
    data_structures=[filter_rules_wrapper, filter_rules_buffer]
)

filter_instances_wrapper = FirewallDataStructure(
    elf_name="icmp_filter.elf", c_name="fw_instances_table"
)
filter_instances_buffer = FirewallDataStructure(
    elf_name="icmp_filter.elf", c_name="fw_instance", capacity=256
)
filter_instances_region = FirewallMemoryRegions(
    data_structures=[filter_instances_wrapper, filter_instances_buffer]
)

filter_rule_bitmap_wrapper = FirewallDataStructure(
    elf_name="icmp_filter.elf", c_name="fw_rule_id_bitmap"
)
filter_rule_bitmap_buffer = FirewallDataStructure(
    entry_size=Uint64_Bytes, capacity=(filter_rules_buffer.capacity + 63) // 64
)
filter_rule_bitmap_region = FirewallMemoryRegions(
    data_structures=[filter_rule_bitmap_wrapper, filter_rule_bitmap_buffer]
)

# Filter action encodings
FILTER_ACTION_ALLOW = 1
FILTER_ACTION_DROP = 2
FILTER_ACTION_CONNECT = 3

# Ethernet types of Rx components
eththype_ip = 0x0800
ethtype_arp = 0x0806

# IP protocol numbers of filters
ip_protocol_icmp = 0x01
ip_protocol_tcp = 0x06
ip_protocol_udp = 0x11

# ARP ethernet opcodes of ARP components
arp_eth_opcode_request = 1
arp_eth_opcode_response = 2

def create_common_subsystems(
    sdf_obj: SystemDescription, dtb: DeviceTree, board_obj: Board,
) -> Tuple[Sddf.Timer, Sddf.Serial, SDF_ProtectionDomain, SDF_ProtectionDomain, SDF_ProtectionDomain]:
    """Create timer and serial subsystems."""
    serial_node = dtb.node(board_obj.serial)
    assert serial_node is not None
    timer_node = dtb.node(board_obj.timer)
    assert timer_node is not None

    timer_driver = SDF_ProtectionDomain("timer_driver", "timer_driver.elf", priority=101)
    timer_system = Sddf.Timer(sdf_obj, timer_node, timer_driver)

    serial_driver = SDF_ProtectionDomain("serial_driver", "serial_driver.elf", priority=100)
    serial_virt_tx = SDF_ProtectionDomain("serial_virt_tx", "serial_virt_tx.elf", priority=99)
    serial_system = Sddf.Serial(sdf_obj, serial_node, serial_driver, serial_virt_tx)

    return timer_system, serial_system, timer_driver, serial_driver, serial_virt_tx


def create_net_subsystem(sdf_obj: SystemDescription, dtb: DeviceTree, iface: NetworkInterface) -> None:
    """Create the sDDF Net subsystem for a single interface."""
    ethernet_node = dtb.node(iface.ethernet_node_path)
    assert ethernet_node is not None, (
        f"Could not find device tree node: {iface.ethernet_node_path}"
    )

    iface.net_system = TrackedNet(
        sdf_obj,
        ethernet_node,
        iface.driver,
        iface.tx_virt.pd,
        iface.rx_virt.pd,
        iface.rx_dma_region.get_mr(),
        interface_index=iface.index,
    )


def register_pds(
    sdf_obj: SystemDescription,
    interfaces: List[NetworkInterface],
    timer_driver: SDF_ProtectionDomain,
    serial_driver: SDF_ProtectionDomain,
    serial_virt_tx: SDF_ProtectionDomain,
    router: Router,
    webserver: Webserver,
    icmp_module: IcmpModule,
) -> None:
    """Register all PDs with the SDF and copy ELFs for per-interface components."""
    for pd in [timer_driver, serial_driver, serial_virt_tx, webserver.pd, icmp_module.pd, router.pd]:
        sdf_obj.add_pd(pd)

    for iface in interfaces:
        for comp in [iface.tx_virt, iface.rx_virt, iface.arp_requester, iface.arp_responder]:
            copy_elf(comp.pd.program_image[:-5], comp.pd.program_image[:-5], iface.index)
            sdf_obj.add_pd(comp.pd)

        sdf_obj.add_pd(iface.driver)

        for filt in iface.filters.values():
            copy_elf(filt.pd.program_image[:-5], filt.pd.program_image[:-5], iface.index)
            sdf_obj.add_pd(filt.pd)


def wire_interface_connections(
    sdf_obj: SystemDescription,
    interfaces: List[NetworkInterface],
    router: Router,
    webserver: Webserver,
    serial_system: Sddf.Serial,
    timer_system: Sddf.Timer,
) -> None:
    """Setup Per Interface Connections"""
    for iface in interfaces:
        # ARP requester is network client
        iface.net_system.add_client_with_copier(iface.arp_requester.pd)

        # Router -> RX virt queue (buffer return)
        router_rx_virt = QueueConnection(
            sdf=sdf_obj,
            name="router_rx_virt",
            src_pd=router.pd,
            dst_pd=iface.rx_virt.pd,
            capacity=dma_buffer_queue.capacity,
            queue_size=dma_buffer_queue_region.region_size,
            category="buffer_return",
            interface_index=iface.index,
        )
        router._connections[f"iface{iface.index}_router_rx_virt"] = router_rx_virt

        # RX virt: register ARP requester ethtype
        iface.rx_virt.add_active_client(ethtype_arp, arp_eth_opcode_response)
        # Add buffer return from router as free client
        iface.rx_virt.add_free_client(router_rx_virt.dst)

        # ARP requester needs timer and serial access
        timer_system.add_client(iface.arp_requester.pd)
        serial_system.add_client(iface.arp_requester.pd)

        # ARP responder is network and serial client
        iface.net_system.add_client_with_copier(iface.arp_responder.pd)
        serial_system.add_client(iface.arp_responder.pd)
        iface.rx_virt.add_active_client(ethtype_arp, arp_eth_opcode_request)

        # Router <-> ARP requester
        router_arp = ArpConnection(
            sdf=sdf_obj,
            name="router_arp",
            pd1=router.pd,
            pd2=iface.arp_requester.pd,
            capacity=arp_queue_buffer.capacity,
            queue_size=arp_queue_region.region_size,
            interface_index=iface.index,
        )
        router._connections[f"iface{iface.index}_router_arp_resolution"] = router_arp
        iface.arp_requester.add_arp_client(router_arp.pd2)

        # ARP cache
        arp_cache = PairedRegion(
            sdf=sdf_obj,
            name="arp_cache",
            size=arp_cache_region.region_size,
            owner_pd=iface.arp_requester.pd,
            peer_pd=router.pd,
            owner_perms="rw",
            peer_perms="r",
            category="arp_cache",
            interface_index=iface.index,
        )
        iface.arp_requester._regions[f"iface{iface.index}_arp_cache"] = arp_cache
        iface.arp_requester.set_cache(arp_cache.owner, arp_cache_buffer.capacity)

        # Create RouterInterface
        ri = router.create_interface()
        ri.rx_free = router_rx_virt.src
        ri.data = iface.rx_dma_region.map(pd=router.pd, perms="rw")
        ri.arp_queue = router_arp.pd1
        ri.arp_cache = arp_cache.peer
        ri.arp_cache_capacity = arp_cache_buffer.capacity
        ri.mac_addr = iface.mac_list
        ri.ip = iface.ip_int
        ri.subnet = iface.subnet_bits
        iface.router_interface = ri

        # Filters
        for protocol, filt in iface.filters.items():
            # Filter -> router connection
            filter_router = QueueConnection(
                sdf=sdf_obj,
                name=f"filter_{filt.name}_router",
                src_pd=filt.pd,
                dst_pd=router.pd,
                capacity=dma_buffer_queue.capacity,
                queue_size=dma_buffer_queue_region.region_size,
                category="filter",
                interface_index=iface.index,
            )
            filt._connections[f"iface{iface.index}_filter_{filt.name}_router"] = filter_router

            # Connect filter as RX-only network client
            iface.net_system.add_client_with_copier(filt.pd, tx=False)
            iface.rx_virt.add_active_client(eththype_ip, protocol)

            # Rule bitmap
            rule_bitmap = PrivateRegion(
                sdf=sdf_obj,
                name="rule_bitmap_" + filt.name,
                size=filter_rule_bitmap_region.region_size,
                pd=filt.pd,
                category="rule_bitmap",
                interface_index=iface.index,
            )
            filt._regions[f"iface{iface.index}_rule_bitmap_{filt.name}"] = rule_bitmap

            # Filter rules region
            filter_rules = PairedRegion(
                sdf=sdf_obj,
                name="filter_rules",
                size=filter_rules_region.region_size,
                owner_pd=filt.pd,
                peer_pd=webserver.pd,
                owner_perms="rw",
                peer_perms="r",
                category="filter_rules",
                interface_index=iface.index,
            )
            filt._regions[f"iface{iface.index}_filter_rules_{filt.name}"] = filter_rules

            # SDF_Channel for rule updates
            filter_update_ch = SDF_Channel(webserver.pd, filt.pd, pp_a=True)
            sdf_obj.add_channel(filter_update_ch)

            # Store webserver filter config for this filter
            filt._webserver_filter_config = FwWebserverFilterConfig(
                protocol=protocol,
                ch=filter_update_ch.pd_a_id,
                rules=filter_rules.peer,
                rules_capacity=filter_rules_buffer.capacity,
            )

            # Populate filter component state
            filt.set_router_connection(filter_router.src)
            filt.set_rules_region(filter_rules.owner, filter_rules_buffer.capacity)
            filt.set_rule_bitmap(rule_bitmap.resource)
            filt._instances_capacity = filter_instances_buffer.capacity

            ri.add_filter(filter_router.dst)


def wire_routing_connections(
    sdf_obj: SystemDescription,
    interfaces: List[NetworkInterface],
    router: Router,
    serial_system: Sddf.Serial,
) -> None:
    """Wire cross-interface routing: router->tx_virt data + tx_virt->rx_virt return."""
    # Add the routing component as a serial_client
    serial_system.add_client(router.pd)
    for src_iface in interfaces:
        for dst_iface in interfaces:
            # Router -> Tx_virt queue, note we do this for every possible src_iface -> dst_iface.
            # !! This allows us to maintain which interface a given buffer originates from
            # Alternative buffer descriptors could track this information essentially for free, however at the moment sddf
            # does not offer flexibility to modify the definition of these buffers without modifying the API itself or
            # giving components other than the virtualiser/driver knowledge about physical addresses.
            # This could be fixed by applying similar config generation used in this implementation for SDDF queues and the API
            # in general. !!
            router_tx_virt = QueueConnection(
                sdf=sdf_obj,
                name=f"router_tx_virt_{src_iface.index}_{dst_iface.index}",
                src_pd=router.pd,
                dst_pd=dst_iface.tx_virt.pd,
                capacity=dma_buffer_queue.capacity,
                queue_size=dma_buffer_queue_region.region_size,
                name_suffix=f"{src_iface.index}{dst_iface.index}",
                category="data",
                interface_index=dst_iface.index,
            )
            router._connections[f"iface{dst_iface.index}_router_tx_virt_{src_iface.index}_{dst_iface.index}"] = router_tx_virt

            # SDF_Map src DMA region into dst tx_virt for read access
            tx_virt_dma = src_iface.rx_dma_region.map_device(pd=dst_iface.tx_virt.pd, perms="r")

            # TX virt active client (queue + data)
            dst_iface.tx_virt.add_active_client(
                FwDataConnectionResource(conn=router_tx_virt.dst, data=tx_virt_dma)
            )
            # Router tx_active (queue only)
            src_iface.router_interface.set_tx_active(dst_iface.index, router_tx_virt.src)

            # dst.tx_virt -> src.rx_virt buffer return queue
            tx_rx_return = QueueConnection(
                sdf=sdf_obj,
                name=f"tx_rx_return_{src_iface.index}_{dst_iface.index}",
                src_pd=dst_iface.tx_virt.pd,
                dst_pd=src_iface.rx_virt.pd,
                capacity=dma_buffer_queue.capacity,
                queue_size=dma_buffer_queue_region.region_size,
                name_suffix=f"{src_iface.index}{dst_iface.index}",
                category="buffer_return",
            )
            dst_iface.tx_virt._connections[f"iface{src_iface.index}_tx_rx_return_{src_iface.index}_{dst_iface.index}"] = tx_rx_return

            # TX free client (queue + data reusing tx_virt's existing DMA mapping)
            dst_iface.tx_virt.add_free_client(
                FwDataConnectionResource(conn=tx_rx_return.src, data=tx_virt_dma)
            )
            # RX virt free client (queue only)
            src_iface.rx_virt.add_free_client(tx_rx_return.dst)

def wire_webserver_connections(
    sdf_obj: SystemDescription,
    interfaces: List[NetworkInterface],
    router: Router,
    webserver: Webserver,
    serial_system: Sddf.Serial,
    timer_system: Sddf.Timer,
    webserver_interface_idx: int,
) -> Sddf.Lwip:
    """Wire webserver connections."""
    ws_iface = interfaces[webserver_interface_idx]
    serial_system.add_client(webserver.pd)
    timer_system.add_client(webserver.pd)

    # Webserver is TX client of webserver interface network
    ws_iface.net_system.add_client_with_copier(webserver.pd, rx=False)

    # Webserver uses lib sDDF LWIP
    webserver_lib_sddf_lwip = Sddf.Lwip(sdf_obj, ws_iface.net_system._net, webserver.pd)

    # Router -> webserver
    router_ws = QueueConnection(
        sdf=sdf_obj,
        name="router_webserver",
        src_pd=router.pd,
        dst_pd=webserver.pd,
        capacity=dma_buffer_queue.capacity,
        queue_size=dma_buffer_queue_region.region_size,
        category="data",
    )
    router._connections["router_webserver"] = router_ws
    webserver.set_rx_active(router_ws.dst)
    router.set_webserver_rx(router_ws.src)

    # Webserver -> ws_iface.rx_virt (buffer return)
    ws_rx_virt = QueueConnection(
        sdf=sdf_obj,
        name="webserver_rx_virt_return",
        src_pd=webserver.pd,
        dst_pd=ws_iface.rx_virt.pd,
        capacity=dma_buffer_queue.capacity,
        queue_size=dma_buffer_queue_region.region_size,
        category="buffer_return",
        interface_index=ws_iface.index,
    )
    webserver._connections["webserver_rx_virt_return"] = ws_rx_virt
    webserver.set_rx_free(ws_rx_virt.src)
    ws_iface.rx_virt.add_free_client(ws_rx_virt.dst)

    # Webserver DMA access
    webserver_data_region = ws_iface.rx_dma_region.map(pd=webserver.pd, perms="rw")
    webserver.set_data(webserver_data_region)

    # Webserver ARP
    ws_arp = ArpConnection(
        sdf=sdf_obj,
        name="webserver_arp",
        pd1=webserver.pd,
        pd2=ws_iface.arp_requester.pd,
        capacity=arp_queue_buffer.capacity,
        queue_size=arp_queue_region.region_size,
        interface_index=ws_iface.index,
    )
    webserver._connections["webserver_arp"] = ws_arp
    webserver.set_arp_connection(ws_arp.pd1)
    ws_iface.arp_requester.add_arp_client(ws_arp.pd2)

    # Routing table shared between router and webserver
    routing_table = PairedRegion(
        sdf=sdf_obj,
        name="routing_table",
        size=routing_table_region.region_size,
        owner_pd=router.pd,
        peer_pd=webserver.pd,
        owner_perms="rw",
        peer_perms="r",
        category="routing_table",
    )
    router._regions["routing_table"] = routing_table

    # PP channel for routing table updates
    router_update_ch = SDF_Channel(webserver.pd, router.pd, pp_a=True)
    sdf_obj.add_channel(router_update_ch)

    # Router webserver config
    router_ws_config = FwWebserverRouterConfig(
        routing_ch=router_update_ch.pd_b_id,
        routing_table=routing_table.owner,
        routing_table_capacity=routing_table_buffer.capacity,
    )
    router.set_webserver_config(router_ws_config)

    webserver_router_config = FwWebserverRouterConfig(
        routing_ch=router_update_ch.pd_a_id,
        routing_table=routing_table.peer,
        routing_table_capacity=routing_table_buffer.capacity,
    )
    webserver.set_router_config(webserver_router_config)
    webserver.set_interface(webserver_interface_idx)
    router.set_webserver_interface(webserver_interface_idx)

    # Router packet queue
    arp_pq = PrivateRegion(
        sdf=sdf_obj,
        name="arp_packet_queue",
        size=arp_packet_queue_region.region_size,
        pd=router.pd,
        category="arp_packet_queue",
    )
    router._regions["arp_packet_queue"] = arp_pq
    router.set_packet_queue(arp_pq.resource, arp_packet_queue_buffer.capacity)

    # Webserver interface configs
    for iface in interfaces:
        ws_filter_configs = []
        for proto in sorted(iface.filters.keys()):
            filt = iface.filters[proto]
            ws_filter_configs.append(filt._webserver_filter_config)

        ws_iface_cfg = FwWebserverInterfaceConfig(
            mac_addr=iface.mac_list,
            ip=iface.ip_int,
            filters=ws_filter_configs,
            name=encode_iface_name(iface.name),
        )
        webserver.add_interface_config(ws_iface_cfg)

    return webserver_lib_sddf_lwip


def wire_icmp_connections(
    sdf_obj: SystemDescription,
    interfaces: List[NetworkInterface],
    router: Router,
    icmp_module: IcmpModule,
) -> None:
    """Wire ICMP module connections."""
    for iface in interfaces:
        iface.net_system.add_client_with_copier(icmp_module.pd, rx=False)
        icmp_module.add_ip(iface.ip_int)

    icmp_conn = QueueConnection(
        sdf=sdf_obj,
        name="icmp_router",
        src_pd=router.pd,
        dst_pd=icmp_module.pd,
        capacity=icmp_queue_buffer.capacity,
        queue_size=icmp_queue_region.region_size,
        category="icmp",
    )
    router._connections["icmp_router"] = icmp_conn
    router.set_icmp_connection(icmp_conn.src)
    icmp_module.set_router_connection(icmp_conn.dst)


def wire_filter_instances(
    sdf_obj: SystemDescription, interfaces: List[NetworkInterface],
) -> None:
    """Create filter instance sharing between interface pairs."""
    if len(interfaces) < 2:
        return

    for iface, other_iface in combinations(interfaces, 2):
        for protocol in iface.filters.keys():
            filt = iface.filters[protocol]
            mirror_filt = other_iface.filters[protocol]

            # local_instances: filt owns, mirror_filt reads
            local_instances = PairedRegion(
                sdf=sdf_obj,
                name="instances",
                size=filter_instances_region.region_size,
                owner_pd=filt.pd,
                peer_pd=mirror_filt.pd,
                owner_perms="rw",
                peer_perms="r",
                category="filter_instances",
                interface_index=iface.index,
            )
            filt._regions[f"iface{iface.index}_instances_{filt.name}_{mirror_filt.name}"] = local_instances

            # remote_instances: mirror_filt owns, filt reads
            remote_instances = PairedRegion(
                sdf=sdf_obj,
                name="instances",
                size=filter_instances_region.region_size,
                owner_pd=mirror_filt.pd,
                peer_pd=filt.pd,
                owner_perms="rw",
                peer_perms="r",
                category="filter_instances",
                interface_index=other_iface.index,
            )
            mirror_filt._regions[f"iface{other_iface.index}_instances_{mirror_filt.name}_{filt.name}"] = remote_instances

            # Update both filters
            filt.set_instances(local_instances.owner, remote_instances.peer, filter_instances_buffer.capacity)
            mirror_filt.set_instances(remote_instances.owner, local_instances.peer, filter_instances_buffer.capacity)


def finalize_all_configs(
    interfaces: List[NetworkInterface], router: Router, webserver: Webserver, icmp_module: IcmpModule,
) -> None:
    """Finalize configs for all components."""
    for iface in interfaces:
        for comp in iface.all_components():
            comp.finalize_config()
    router.finalize_config()
    webserver.finalize_config()
    icmp_module.finalize_config()


def serialize_all(
    output_dir: str,
    interfaces: List[NetworkInterface],
    router: Router,
    webserver: Webserver,
    icmp_module: IcmpModule,
    obj_copy_path: str,
) -> None:
    """Serialize configs to data files and update ELF sections."""
    for iface in interfaces:
        iface_out_dir = f"{output_dir}/{iface.out_dir}"

        for comp in iface.all_components():
            data_path = f"{iface_out_dir}/firewall_config_{comp.name}.data"
            with open(data_path, "wb+") as f:
                f.write(comp.config.serialise())
            update_elf_section(obj_copy_path, comp.pd.program_image, comp.config.section_name, data_path)

    # Router
    data_path = f"{output_dir}/firewall_config_routing.data"
    with open(data_path, "wb+") as f:
        f.write(router.config.serialise())
    update_elf_section(obj_copy_path, router.pd.program_image, router.config.section_name, data_path)

    # Webserver
    data_path = f"{output_dir}/firewall_config_webserver.data"
    with open(data_path, "wb+") as f:
        f.write(webserver.config.serialise())
    update_elf_section(obj_copy_path, webserver.pd.program_image, webserver.config.section_name, data_path)

    # ICMP module
    data_path = f"{output_dir}/firewall_icmp_module_config.data"
    with open(data_path, "wb+") as f:
        f.write(icmp_module.config.serialise())
    update_elf_section(obj_copy_path, icmp_module.pd.program_image, icmp_module.config.section_name, data_path)


def generate(sdf_file: str, output_dir: str, dtb: DeviceTree) -> None:
    webserver_interface_idx = 0

    interfaces = [
        NetworkInterface(
            index=0,
            name="internal",
            ethernet_node_path=board.ethernet0,
            mac=(0x00, 0x01, 0xC0, 0x39, 0xD5, 0x10),
            ip="192.168.1.1",
            subnet_bits=24,
            priorities=InterfacePriorities(
                arp_requester=95,
                arp_responder=93,
                icmp_filter=93,
            ),
        ),
        NetworkInterface(
            index=1,
            name="external",
            ethernet_node_path=board.ethernet1,
            mac=(0x00, 0x01, 0xC0, 0x39, 0xD5, 0x18),
            ip="172.16.2.1",
            subnet_bits=16,
            priorities=InterfacePriorities(
                arp_requester=98,
                arp_responder=95,
                icmp_filter=90,
            ),
        ),
    ]

    # Phase 1: Create component objects
    for iface in interfaces:
        prio = iface.priorities

        iface.driver = SDF_ProtectionDomain(
            f"ethernet_driver{iface.index}",
            f"eth_driver{iface.index}.elf",
            priority=prio.driver,
            budget=100,
            period=400,
        )

        iface.rx_virt = NetVirtRx(iface.index, sdf, prio.rx_virt)
        iface.tx_virt = NetVirtTx(iface.index, sdf, prio.tx_virt)
        iface.arp_requester = ArpRequester(
            iface.index, sdf, iface.mac_list, iface.ip_int, prio.arp_requester,
        )
        iface.arp_responder = ArpResponder(
            iface.index, sdf, iface.mac_list, iface.ip_int, prio.arp_responder,
        )

        iface.filters = {
            ip_protocol_icmp: Filter(iface.index, ip_protocol_icmp, sdf, prio.icmp_filter),
            ip_protocol_udp: Filter(iface.index, ip_protocol_udp, sdf, prio.udp_filter),
            ip_protocol_tcp: Filter(iface.index, ip_protocol_tcp, sdf, prio.tcp_filter),
        }

        dma_region = SDF_MemoryRegion(
            sdf, f"rx_dma_region{iface.index}", dma_buffer_region.region_size, physical=True
        )

        iface.rx_dma_region = ReMappableRegion(name=f"rx_dma_region{iface.index}", mr=dma_region, size=dma_region.size, category="dma_buffer")
        sdf.add_mr(iface.rx_dma_region.get_mr())

        iface_out_dir = f"{output_dir}/{iface.out_dir}"
        if not path.isdir(iface_out_dir):
            assert subprocess.run(["mkdir", iface_out_dir]).returncode == 0

    router = Router(sdf)
    webserver = Webserver(sdf)
    icmp_module = IcmpModule(sdf)

    # Phase 2: Create common subsystems
    timer_system, serial_system, timer_driver, serial_driver, serial_virt_tx = (
        create_common_subsystems(sdf, dtb, board)
    )

    # Create net subsystems
    for iface in interfaces:
        create_net_subsystem(sdf, dtb, iface)

    # Register all PDs
    register_pds(sdf, interfaces, timer_driver, serial_driver, serial_virt_tx,
                 router, webserver, icmp_module)

    # Phase 3: Wire per interface connections
    wire_interface_connections(
        sdf, interfaces, router, webserver, serial_system, timer_system
    )
    wire_routing_connections(sdf, interfaces, router, serial_system)
    webserver_lib_sddf_lwip = wire_webserver_connections(
        sdf, interfaces, router, webserver, serial_system, timer_system, webserver_interface_idx
    )
    wire_icmp_connections(sdf, interfaces, router, icmp_module)
    wire_filter_instances(sdf, interfaces)

    # Phase 4: Finalize configs
    finalize_all_configs(interfaces, router, webserver, icmp_module)

    # Phase 5: Connect and serialize subsystems
    for iface in interfaces:
        assert iface.net_system.connect()
        iface_out_dir = f"{output_dir}/{iface.out_dir}"
        assert iface.net_system.serialise_config(iface_out_dir)

    assert serial_system.connect()
    assert serial_system.serialise_config(output_dir)
    assert timer_system.connect()
    assert timer_system.serialise_config(output_dir)

    ws_iface = interfaces[webserver_interface_idx]
    assert webserver_lib_sddf_lwip.connect()
    assert webserver_lib_sddf_lwip.serialise_config(f"{output_dir}/{ws_iface.out_dir}")

    # Serialize firewall configs
    serialize_all(output_dir, interfaces, router, webserver, icmp_module, obj_copy)

    # Render SDF
    with open(f"{output_dir}/{sdf_file}", "w+") as f:
        f.write(sdf.render())

    # Generate topology graph
    all_connections = {}
    all_regions = {}
    for comp in [router, webserver, icmp_module]:
        all_connections.update(comp.topology_connections())
        all_regions.update(comp.topology_regions())
    for iface in interfaces:
        for comp in iface.all_components():
            all_connections.update(comp.topology_connections())
            all_regions.update(comp.topology_regions())
        all_regions[iface.rx_dma_region.name] = iface.rx_dma_region

    all_net_edges = []
    for iface in interfaces:
        all_net_edges.extend(iface.net_system.net_edges)

    dot_str = generate_topology_dot(
        interfaces=interfaces,
        router=router,
        webserver=webserver,
        icmp_module=icmp_module,
        all_connections=all_connections,
        all_regions=all_regions,
        all_net_edges=all_net_edges,
    )
    with open(f"{output_dir}/topology.dot", "w+") as f:
        f.write(dot_str)


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

    sdf = SystemDescription(board.arch, board.paddr_top)
    sddf = Sddf(args.sddf)

    global obj_copy
    obj_copy = args.objcopy

    with open(args.dtb, "rb") as f:
        dtb = DeviceTree(f.read())

    for region in FirewallMemoryRegions.regions:
        if region.region_size:
            continue

        for structure in region.data_structures:
            if structure.size:
                continue
            try:
                if not path.exists(structure.elf_name):
                    raise Exception(
                        f"ERROR: ELF name '{structure.elf_name}' does not exist"
                    )
                output = subprocess.run(
                    ["llvm-dwarfdump", structure.elf_name],
                    capture_output=True,
                    text=True,
                    check=True,
                )
                dwarfdump = output.stdout.split()
                for i in range(len(dwarfdump)):
                    if dwarfdump[i] == "DW_TAG_structure_type":
                        if dwarfdump[i + 2] == f'("{structure.c_name}")':
                            assert dwarfdump[i + 3] == "DW_AT_byte_size"
                            size_fmt = dwarfdump[i + 4].strip("(").strip(")")
                            structure.entry_size = int(size_fmt, base=16)
            except Exception as e:
                raise Exception(
                    f"Error calculating {structure.c_name} size using llvm-dwarf dump on {structure.elf_name}): {e}"
                )
            structure.calculate_size()
        region.calculate_size()

    generate(args.sdf, args.output, dtb)
