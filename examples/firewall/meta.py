# Copyright 2025, UNSW SPDX-License-Identifier: BSD-2-Clause
import argparse
import subprocess
from os import path
import sys
from dataclasses import dataclass, field
from typing import List, Optional, Dict, Any, Tuple
from sdfgen import SystemDescription, Sddf, DeviceTree
from ctypes import *
from importlib.metadata import version
import ipaddress

assert version("sdfgen").split(".")[1] == "28", "Unexpected sdfgen version"

from sdfgen_helper import *

ProtectionDomain = SystemDescription.ProtectionDomain
MemoryRegion = SystemDescription.MemoryRegion
Map = SystemDescription.Map
Channel = SystemDescription.Channel

# System network constants
int_net = 0
ext_net = 1


# Helper functions used to generate firewall structures
def ip_to_int(ipString: str) -> int:
    ipaddress.IPv4Address(ipString)
    # Switch little to big endian
    ipSplit = ipString.split(".")
    ipSplit.reverse()
    reversedIp = ".".join(ipSplit)
    return int(ipaddress.IPv4Address(reversedIp))


maxPortNum = 65535


def htons(portNum):
    if portNum < 0 or portNum > maxPortNum:
        print(
            f"UI SERVER|ERR: Supplied port number {portNum} is negative or too large."
        )
    return ((portNum & 0xFF) << 8) | ((portNum & 0xFF00) >> 8)


def make_rule(
    action,
    srcIp,
    dstIp,
    srcSubnet,
    dstSubnet,
    srcPort,
    dstPort,
    srcPortAny,
    dstPortAny,
    ruleId,
):
    return FwRule(
        action=action,
        src_ip=srcIp,
        dst_ip=dstIp,
        src_port=srcPort,
        dst_port=dstPort,
        src_subnet=srcSubnet,
        dst_subnet=dstSubnet,
        src_port_any=srcPortAny,
        dst_port_any=dstPortAny,
        rule_id=ruleId,
    )


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

    # Call to recalculate size after data structure update
    def update_size(self):
        if not self.entry_size:
            raise Exception(
                f"FirewallDataStructure: Entry size of structure with c name {self.c_name} was 0 during size recalculation!"
            )

        self.size = self.size_formula(self)
        if not self.size:
            raise Exception(
                f"FirewallDataStructure: Recalculated size of structure with c name {self.c_name}, entry size {self.entry_size} and capacity {self.capacity} was 0!"
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


@dataclass
class InterfacePriorities:
    """Priority configuration for network interface components."""

    driver: int = 101
    tx_virt: int = 100
    rx_virt: int = 99
    router: int = 97
    arp_requester: int = 98
    arp_responder: int = 95
    icmp_filter: int = 90
    udp_filter: int = 91
    tcp_filter: int = 92


@dataclass
class NetworkInterface:
    """Complete definition for one network interface."""

    index: int
    name: str  # Label like "internal", "external"
    ethernet_node_path: str  # Device tree path
    mac: Tuple[int, ...]  # 6-byte MAC address
    ip: str
    subnet_bits: int

    priorities: InterfacePriorities = field(default_factory=InterfacePriorities)

    driver: Optional[ProtectionDomain] = None
    rx_virt: Optional[ProtectionDomain] = None
    tx_virt: Optional[ProtectionDomain] = None
    rx_dma_region: Optional[MemoryRegion] = None
    net_system: Optional[Any] = None
    arp_requester: Optional[ProtectionDomain] = None
    arp_responder: Optional[ProtectionDomain] = None
    filters: Dict[int, ProtectionDomain] = field(default_factory=dict)
    router: Optional[ProtectionDomain] = None
    router_config: Optional[Any] = None
    icmp_conn: Optional[Any] = None

    wiring: Optional["InterfaceWiring"] = None

    @property
    def out_dir(self) -> str:
        return f"net_data{self.index}"

    @property
    def ip_int(self) -> int:
        return ip_to_int(self.ip)

    @property
    def mac_list(self) -> List[int]:
        return list(self.mac)


class FirewallBuilder:
    def __init__(
        self,
        sdf_obj: SystemDescription,
        dtb: DeviceTree,
        output_dir: str,
        interfaces: List[NetworkInterface],
        webserver_interface: int = 0,
    ):
        self.sdf_obj = sdf_obj
        self.dtb = dtb
        self.output_dir = output_dir
        self.interfaces = interfaces
        self.webserver_interface_idx = webserver_interface

        for iface in self.interfaces:
            iface_out_dir = f"{output_dir}/{iface.out_dir}"
            if not path.isdir(iface_out_dir):
                assert subprocess.run(["mkdir", iface_out_dir]).returncode == 0

        self.webserver: Optional[ProtectionDomain] = None
        self.icmp_module: Optional[ProtectionDomain] = None
        self.timer_system: Optional[Any] = None
        self.serial_system: Optional[Any] = None
        self.timer_driver: Optional[ProtectionDomain] = None
        self.serial_driver: Optional[ProtectionDomain] = None
        self.serial_virt_tx: Optional[ProtectionDomain] = None

        self.router_webserver_conn = None
        self.webserver_rx_virt_conn = None
        self.webserver_data_region = None
        self.webserver_arp_conn = None
        self.icmp_module_config: Optional[Any] = None
        self.webserver_config: Optional[Any] = None
        self.webserver_lib_sddf_lwip: Optional[Any] = None

        self._webserver_interface_configs: List[Any] = []
        self._global_connections: Dict[str, Any] = {}
        self._global_regions: Dict[str, Any] = {}

    def _get_interface(self, index: int) -> NetworkInterface:
        return self.interfaces[index]

    def _get_other_interface(self, index: int) -> NetworkInterface:
        others = [iface for iface in self.interfaces if iface.index != index]
        assert len(others) == 1, "This firewall expects exactly two interfaces"
        return others[0]

    def create_common_pds(self):
        """Create timer and serial subsystems."""
        board_obj = board  # Use global board

        serial_node = self.dtb.node(board_obj.serial)
        assert serial_node is not None
        timer_node = self.dtb.node(board_obj.timer)
        assert timer_node is not None

        # Create timer subsystem
        self.timer_driver = ProtectionDomain(
            "timer_driver", "timer_driver.elf", priority=101
        )
        self.timer_system = Sddf.Timer(self.sdf_obj, timer_node, self.timer_driver)

        # Create serial subsystem
        self.serial_driver = ProtectionDomain(
            "serial_driver", "serial_driver.elf", priority=100
        )
        self.serial_virt_tx = ProtectionDomain(
            "serial_virt_tx", "serial_virt_tx.elf", priority=99
        )
        self.serial_system = Sddf.Serial(
            self.sdf_obj, serial_node, self.serial_driver, self.serial_virt_tx
        )

        return self

    def create_interface_pds(self):
        """Create PDs for each network interface."""
        for iface in self.interfaces:
            prio = iface.priorities

            # Driver for THIS interface's hardware
            iface.driver = ProtectionDomain(
                f"ethernet_driver{iface.index}",
                f"eth_driver{iface.index}.elf",
                priority=prio.driver,
                budget=100,
                period=400,
            )

            # TX virt: handles packets going OUT this interface
            iface.tx_virt = ProtectionDomain(
                f"net_virt_tx{iface.index}",
                f"firewall_network_virt_tx{iface.index}.elf",
                priority=prio.tx_virt,
                budget=20000,
            )

            # RX virt: receives from THIS interface
            iface.rx_virt = ProtectionDomain(
                f"net_virt_rx{iface.index}",
                f"firewall_network_virt_rx{iface.index}.elf",
                priority=prio.rx_virt,
            )

            # RX DMA region for this interface
            iface.rx_dma_region = MemoryRegion(
                self.sdf_obj,
                f"rx_dma_region{iface.index}",
                dma_buffer_region.region_size,
                physical=True,
            )
            self.sdf_obj.add_mr(iface.rx_dma_region)

            # ARP components for this interface
            iface.arp_responder = ProtectionDomain(
                f"arp_responder{iface.index}",
                f"arp_responder{iface.index}.elf",
                priority=prio.arp_responder,
                budget=20000,
            )

            iface.arp_requester = ProtectionDomain(
                f"arp_requester{iface.index}",
                f"arp_requester{iface.index}.elf",
                priority=prio.arp_requester,
                budget=20000,
            )

            # Filters for this interface
            iface.filters = {
                ip_protocol_icmp: ProtectionDomain(
                    f"icmp_filter{iface.index}",
                    f"icmp_filter{iface.index}.elf",
                    priority=prio.icmp_filter,
                    budget=20000,
                ),
                ip_protocol_udp: ProtectionDomain(
                    f"udp_filter{iface.index}",
                    f"udp_filter{iface.index}.elf",
                    priority=prio.udp_filter,
                    budget=20000,
                ),
                ip_protocol_tcp: ProtectionDomain(
                    f"tcp_filter{iface.index}",
                    f"tcp_filter{iface.index}.elf",
                    priority=prio.tcp_filter,
                    budget=20000,
                ),
            }

            # Initialize InterfaceWiring with back-reference to the interface
            iface.wiring = InterfaceWiring(interface=iface)

        return self

    def create_router_and_webserver(self):
        """Create routers, webserver, and ICMP module."""
        for iface in self.interfaces:
            prio = iface.priorities
            iface.router = ProtectionDomain(
                f"routing{iface.index}",
                f"routing{iface.index}.elf",
                priority=prio.router,
                budget=20000,
            )

        self.webserver = ProtectionDomain(
            "micropython",
            "micropython.elf",
            priority=1,
            budget=20000,
            stack_size=0x10000,
        )
        self.serial_system.add_client(self.webserver)
        self.timer_system.add_client(self.webserver)

        self.icmp_module = ProtectionDomain(
            "icmp_module", "icmp_module.elf", priority=100, budget=20000
        )

        return self

    def create_network_subsystems(self):
        """Create Sddf.Net subsystems for each interface."""
        for iface in self.interfaces:
            ethernet_node = self.dtb.node(iface.ethernet_node_path)
            assert ethernet_node is not None, (
                f"Could not find device tree node: {iface.ethernet_node_path}"
            )

            iface.net_system = TrackedNet(
                self.sdf_obj,
                ethernet_node,
                iface.driver,
                iface.tx_virt,
                iface.rx_virt,
                iface.rx_dma_region,
                interface_index=iface.index,
                wiring=iface.wiring,
            )

        for pd in [
            self.timer_driver,
            self.serial_driver,
            self.serial_virt_tx,
            self.webserver,
            self.icmp_module,
        ]:
            self.sdf_obj.add_pd(pd)

        # Add PDs and create per-interface ELF copies
        for iface in self.interfaces:
            for pd in [
                iface.router,
                iface.tx_virt,
                iface.rx_virt,
                iface.arp_requester,
                iface.arp_responder,
            ]:
                copy_elf(pd.program_image[:-5], pd.program_image[:-5], iface.index)
                self.sdf_obj.add_pd(pd)

            for filter_pd in iface.filters.values():
                copy_elf(
                    filter_pd.program_image[:-5],
                    filter_pd.program_image[:-5],
                    iface.index,
                )
                self.sdf_obj.add_pd(filter_pd)

            self.sdf_obj.add_pd(iface.driver)

            # Ensure ARP requester is a client of the outbound network
            out_iface = self._get_other_interface(iface.index)
            out_iface.net_system.add_client_with_copier(iface.arp_requester)

        return self

    def create_connections(self):
        """Create all connections between components."""
        self._create_webserver_connections()
        self._create_icmp_connections()

        for iface in self.interfaces:
            self._create_interface_connections(iface)

        self._create_filter_instance_regions()

        return self

    def _create_webserver_connections(self):
        """Create connections for the webserver component."""
        ws_iface = self._get_interface(self.webserver_interface_idx)

        # Webserver is a TX client of the webserver interface network
        ws_iface.net_system.add_client_with_copier(self.webserver, rx=False)

        # Webserver uses lib sDDF LWIP
        self.webserver_lib_sddf_lwip = Sddf.Lwip(
            self.sdf_obj, ws_iface.net_system, self.webserver
        )

        # Webserver receives traffic from router
        router_ws_spec = ConnectionSpec(
            name="router_webserver",
            sdf=self.sdf_obj,
            src_pd=ws_iface.router,
            dst_pd=self.webserver,
            capacity=dma_buffer_queue.capacity,
            queue_region_size=dma_buffer_queue_region.region_size,
            category="data",
        )
        self.router_webserver_conn = router_ws_spec.create()
        self._global_connections["router_webserver"] = router_ws_spec

        # Webserver returns packets to its interface's RX virtualiser
        ws_rx_virt_spec = ConnectionSpec(
            name="webserver_rx_virt_return",
            sdf=self.sdf_obj,
            src_pd=self.webserver,
            dst_pd=ws_iface.rx_virt,
            capacity=dma_buffer_queue.capacity,
            queue_region_size=dma_buffer_queue_region.region_size,
            category="buffer_return",
            interface_index=ws_iface.index,
        )
        self.webserver_rx_virt_conn = ws_rx_virt_spec.create()
        self._global_connections["webserver_rx_virt_return"] = ws_rx_virt_spec

        # Webserver needs access to its interface's RX DMA region
        ws_data_spec = MappedRegionSpec(
            name="webserver_dma",
            mr=ws_iface.rx_dma_region,
            pd=self.webserver,
            size=dma_buffer_queue_region.region_size,
            category="dma_buffer",
            interface_index=ws_iface.index,
        )
        self.webserver_data_region = ws_data_spec.map()

        # Webserver has ARP channel for requests/responses on the other interface
        arp_iface = self._get_other_interface(ws_iface.index)
        ws_arp_spec = ArpConnectionSpec(
            name="webserver_arp",
            sdf=self.sdf_obj,
            pd1=self.webserver,
            pd2=arp_iface.arp_requester,
            capacity=arp_queue_buffer.capacity,
            queue_region_size=arp_queue_region.region_size,
            interface_index=arp_iface.index,
        )
        self.webserver_arp_conn = ws_arp_spec.create()
        self._global_connections["webserver_arp"] = ws_arp_spec

    def _create_icmp_connections(self):
        """Create connections for the ICMP module."""
        for iface in self.interfaces:
            iface.net_system.add_client_with_copier(self.icmp_module, rx=False)

        icmp_router_conns = [None] * len(self.interfaces)
        for iface in self.interfaces:
            icmp_spec = ConnectionSpec(
                name=f"icmp_router_{iface.index}",
                sdf=self.sdf_obj,
                src_pd=iface.router,
                dst_pd=self.icmp_module,
                capacity=icmp_queue_buffer.capacity,
                queue_region_size=icmp_queue_region.region_size,
                category="icmp",
                interface_index=iface.index,
            )
            icmp_conn = icmp_spec.create()
            iface.icmp_conn = icmp_conn[0]
            icmp_router_conns[iface.index] = icmp_conn[1]
            iface.wiring.connections[f"icmp_router_{iface.index}"] = icmp_spec

        self.icmp_module_config = FwIcmpModuleConfig(
            [iface.ip_int for iface in self.interfaces],
            icmp_router_conns,
            len(self.interfaces),
        )

    def _create_interface_connections(self, iface: NetworkInterface):
        """Create all connections for a single interface."""
        out_iface = self._get_other_interface(iface.index)
        router = iface.router

        in_virt = iface.rx_virt
        out_virt = out_iface.tx_virt

        # Router -> out_virt data connection (uses iface RX DMA region as data)
        router_out_virt_spec = DataConnectionSpec(
            name=f"router_out_virt_{iface.index}",
            sdf=self.sdf_obj,
            src_pd=router,
            dst_pd=out_virt,
            capacity=dma_buffer_queue.capacity,
            queue_size=dma_buffer_queue_region.region_size,
            data_mr=iface.rx_dma_region,
            src_data_perms="rw",
            dst_data_perms="r",
            name_suffix=f"{iface.index}",
            category="data",
            interface_index=iface.index,
        )
        router_out_virt_conn = router_out_virt_spec.create()
        iface.wiring.connections[f"router_out_virt_{iface.index}"] = (
            router_out_virt_spec
        )

        # out_virt -> in_virt buffer return
        output_in_virt_spec = ConnectionSpec(
            name=f"out_virt_in_virt_{iface.index}",
            sdf=self.sdf_obj,
            src_pd=out_virt,
            dst_pd=in_virt,
            capacity=dma_buffer_queue.capacity,
            queue_region_size=dma_buffer_queue_region.region_size,
            name_suffix=f"{iface.index}",
            category="buffer_return",
        )
        output_in_virt_conn = output_in_virt_spec.create()
        iface.wiring.connections[f"out_virt_in_virt_{iface.index}"] = (
            output_in_virt_spec
        )
        out_virt_in_virt_data_conn = FwDataConnectionResource(
            output_in_virt_conn[0], router_out_virt_conn[1].data
        )

        # TX virt config (belongs to out_iface)
        out_iface.wiring.tx_virt_config = FwNetVirtTxConfig(
            iface.index,
            [router_out_virt_conn[1]],
            [out_virt_in_virt_data_conn],
        )

        # Router -> in_virt buffer return
        router_in_virt_spec = ConnectionSpec(
            name=f"router_in_virt_{iface.index}",
            sdf=self.sdf_obj,
            src_pd=router,
            dst_pd=in_virt,
            capacity=dma_buffer_queue.capacity,
            queue_region_size=dma_buffer_queue_region.region_size,
            category="buffer_return",
        )
        router_in_virt_conn = router_in_virt_spec.create()
        iface.wiring.connections[f"router_in_virt_{iface.index}"] = router_in_virt_spec

        # RX virt config
        rx_virt_cfg = FwNetVirtRxConfig(
            iface.index, [], [], [router_in_virt_conn[1], output_in_virt_conn[1]]
        )
        iface.wiring.rx_virt_config = rx_virt_cfg

        # Register ARP requester response ethtype
        rx_virt_cfg.active_client_ethtypes.append(ethtype_arp)
        rx_virt_cfg.active_client_subtypes.append(arp_eth_opcode_response)

        # ARP requester needs timer and serial access
        self.timer_system.add_client(iface.arp_requester)
        self.serial_system.add_client(iface.arp_requester)

        # Add ARP responder as network and serial client
        iface.net_system.add_client_with_copier(iface.arp_responder)
        self.serial_system.add_client(iface.arp_responder)
        rx_virt_cfg.active_client_ethtypes.append(ethtype_arp)
        rx_virt_cfg.active_client_subtypes.append(arp_eth_opcode_request)

        # Router as serial client
        self.serial_system.add_client(router)

        # Router <-> ARP requester connection
        arp_conn_spec = ArpConnectionSpec(
            name=f"router_arp_{iface.index}",
            sdf=self.sdf_obj,
            pd1=router,
            pd2=iface.arp_requester,
            capacity=arp_queue_buffer.capacity,
            queue_region_size=arp_queue_region.region_size,
            interface_index=iface.index,
        )
        router_arp_conn = arp_conn_spec.create()
        iface.wiring.connections[f"router_arp_{iface.index}"] = arp_conn_spec

        # ARP cache shared region
        arp_cache_spec = SharedRegionSpec(
            name="arp_cache",
            sdf=self.sdf_obj,
            size=arp_cache_region.region_size,
            owner_pd=iface.arp_requester,
            peer_pd=router,
            owner_perms="rw",
            peer_perms="r",
            category="arp_cache",
            interface_index=iface.index,
        )
        arp_cache = arp_cache_spec.map()
        iface.wiring.regions["arp_cache"] = arp_cache_spec

        # ARP requester config
        iface.wiring.arp_requester_config = FwArpRequesterConfig(
            iface.index,
            out_iface.mac_list,
            out_iface.ip_int,
            [router_arp_conn[1]],
            arp_cache[0],
            arp_cache_buffer.capacity,
        )

        # ARP responder config
        iface.wiring.arp_responder_config = FwArpResponderConfig(
            iface.index, iface.mac_list, iface.ip_int
        )

        # Router-internal buffer for packets waiting on ARP resolution
        arp_packet_queue_spec = PrivateRegionSpec(
            name="arp_packet_queue",
            sdf=self.sdf_obj,
            size=arp_packet_queue_region.region_size,
            pd=router,
            category="arp_packet_queue",
            interface_index=iface.index,
        )
        arp_packet_queue = arp_packet_queue_spec.create()
        iface.wiring.regions["arp_packet_queue"] = arp_packet_queue_spec

        # Routing table shared between router and webserver
        routing_table_spec = SharedRegionSpec(
            name="routing_table",
            sdf=self.sdf_obj,
            size=routing_table_region.region_size,
            owner_pd=router,
            peer_pd=self.webserver,
            owner_perms="rw",
            peer_perms="r",
            category="routing_table",
            interface_index=iface.index,
        )
        routing_table = routing_table_spec.map()
        iface.wiring.regions["routing_table"] = routing_table_spec

        # PP channel for routing table updates
        router_update_ch = Channel(self.webserver, router, pp_a=True)
        self.sdf_obj.add_channel(router_update_ch)

        # Router webserver config
        router_webserver_config = FwWebserverRouterConfig(
            router_update_ch.pd_b_id,
            routing_table[0],
            routing_table_buffer.capacity,
        )

        webserver_router_config = FwWebserverRouterConfig(
            router_update_ch.pd_a_id,
            routing_table[1],
            routing_table_buffer.capacity,
        )

        # Router config
        iface.router_config = FwRouterConfig(
            iface.index,
            out_iface.mac_list,
            out_iface.ip_int,
            out_iface.subnet_bits,
            iface.ip_int,
            router_in_virt_conn[0],
            None,  # rx_active set later
            router_out_virt_conn[0].conn,
            router_out_virt_conn[0].data.region,
            router_arp_conn[0],
            arp_cache[1],
            arp_cache_buffer.capacity,
            arp_packet_queue,
            router_webserver_config,
            iface.icmp_conn,
            [],
        )

        # Webserver interface config for this interface
        webserver_iface_config = FwWebserverInterfaceConfig(
            iface.mac_list,
            iface.ip_int,
            webserver_router_config,
            [],
        )

        # Filter connections
        actionNums = {"Allow": 1, "Drop": 2, "Connect": 3}
        ws_iface = self._get_interface(self.webserver_interface_idx)
        ws_ip = ws_iface.ip_int
        dst_subnet = 32
        is_web_server = iface.index == self.webserver_interface_idx

        for protocol, filter_pd in iface.filters.items():
            # Filter -> router connection
            filter_conn_spec = ConnectionSpec(
                name=f"filter_{filter_pd.name}_router",
                sdf=self.sdf_obj,
                src_pd=filter_pd,
                dst_pd=router,
                capacity=dma_buffer_queue.capacity,
                queue_region_size=dma_buffer_queue_region.region_size,
                category="filter",
                interface_index=iface.index,
            )
            filter_router_conn = filter_conn_spec.create()
            iface.wiring.connections[f"filter_{filter_pd.name}_router"] = (
                filter_conn_spec
            )

            # Connect filter as RX-only network client
            iface.net_system.add_client_with_copier(filter_pd, tx=False)
            rx_virt_cfg.active_client_ethtypes.append(eththype_ip)
            rx_virt_cfg.active_client_subtypes.append(protocol)

            # Create rule bitmap
            bitmap_spec = PrivateRegionSpec(
                name="rule_bitmap_" + filter_pd.name,
                sdf=self.sdf_obj,
                size=filter_rule_bitmap_region.region_size,
                pd=filter_pd,
                category="rule_bitmap",
                interface_index=iface.index,
            )
            rule_bitmap_region = bitmap_spec.create()
            iface.wiring.regions[f"rule_bitmap_{filter_pd.name}"] = bitmap_spec

            # Create filter rules shared region
            filter_rules_spec = SharedRegionSpec(
                name="filter_rules",
                sdf=self.sdf_obj,
                size=filter_rules_region.region_size,
                owner_pd=filter_pd,
                peer_pd=self.webserver,
                owner_perms="rw",
                peer_perms="r",
                category="filter_rules",
                interface_index=iface.index,
            )
            filter_rules = filter_rules_spec.map()
            iface.wiring.regions[f"filter_rules_{filter_pd.name}"] = filter_rules_spec

            # Create PP channel for rule updates
            filter_update_ch = Channel(self.webserver, filter_pd, pp_a=True)
            self.sdf_obj.add_channel(filter_update_ch)

            # Create webserver's view of the filter config
            webserver_filter_config = FwWebserverFilterConfig(
                protocol,
                filter_update_ch.pd_a_id,
                filter_rules[1],
                filter_rules_buffer.capacity,
            )

            # Create Default rule
            default_rule = make_rule(
                actionNums["Drop"], 0, 0, 0, 0, 0, 0, True, True, 0
            )

            initial_rules = []
            if protocol == ip_protocol_tcp:
                if is_web_server:
                    initial_rules.append(
                        make_rule(
                            actionNums["Connect"],
                            srcIp=0,
                            dstIp=ws_ip,
                            srcSubnet=0,
                            dstSubnet=dst_subnet,
                            srcPort=0,
                            dstPort=htons(80),
                            srcPortAny=True,
                            dstPortAny=False,
                            ruleId=0,
                        )
                    )
                    initial_rules.append(
                        make_rule(
                            actionNums["Drop"],
                            srcIp=0,
                            dstIp=ws_ip,
                            srcSubnet=0,
                            dstSubnet=dst_subnet,
                            srcPort=0,
                            dstPort=0,
                            srcPortAny=True,
                            dstPortAny=True,
                            ruleId=0,
                        )
                    )
                else:
                    initial_rules.append(
                        make_rule(
                            actionNums["Drop"],
                            srcIp=0,
                            dstIp=ws_ip,
                            srcSubnet=0,
                            dstSubnet=dst_subnet,
                            srcPort=0,
                            dstPort=0,
                            srcPortAny=True,
                            dstPortAny=True,
                            ruleId=0,
                        )
                    )
            else:
                initial_rules.append(
                    make_rule(
                        actionNums["Drop"],
                        srcIp=0,
                        dstIp=ws_ip,
                        srcSubnet=0,
                        dstSubnet=dst_subnet,
                        srcPort=0,
                        dstPort=0,
                        srcPortAny=True,
                        dstPortAny=True,
                        ruleId=0,
                    )
                )

            # Create filter config
            filter_cfg = FwFilterConfig(
                iface.index,
                default_rule,
                initial_rules,
                filter_instances_buffer.capacity,
                filter_router_conn[0],
                None,  # internal_instances set later
                None,  # external_instances set later
                filter_rules[0],
                filter_rules_buffer.capacity,
                rule_bitmap_region,
            )
            iface.wiring.filter_configs[protocol] = filter_cfg

            iface.router_config.filters.append(filter_router_conn[1])
            webserver_iface_config.filters.append(webserver_filter_config)

        self._webserver_interface_configs.append(webserver_iface_config)

    def _create_filter_instance_regions(self):
        """Create filter instance sharing between interface pairs."""
        if len(self.interfaces) < 2:
            return

        iface_a = self.interfaces[0]
        iface_b = self.interfaces[1]

        for protocol in iface_a.filters.keys():
            filter_a = iface_a.filters[protocol]
            filter_b = iface_b.filters[protocol]

            # a owns, b reads
            a_spec = SharedRegionSpec(
                name="instances",
                sdf=self.sdf_obj,
                size=filter_instances_region.region_size,
                owner_pd=filter_a,
                peer_pd=filter_b,
                owner_perms="rw",
                peer_perms="r",
                category="filter_instances",
                interface_index=iface_a.index,
            )
            a_instances = a_spec.map()
            iface_a.wiring.regions[f"instances_{filter_a.name}_{filter_b.name}"] = (
                a_spec
            )

            # b owns, a reads
            b_spec = SharedRegionSpec(
                name="instances",
                sdf=self.sdf_obj,
                size=filter_instances_region.region_size,
                owner_pd=filter_b,
                peer_pd=filter_a,
                owner_perms="rw",
                peer_perms="r",
                category="filter_instances",
                interface_index=iface_b.index,
            )
            b_instances = b_spec.map()
            iface_b.wiring.regions[f"instances_{filter_b.name}_{filter_a.name}"] = (
                b_spec
            )

            iface_a.wiring.filter_configs[protocol].internal_instances = a_instances[0]
            iface_a.wiring.filter_configs[protocol].external_instances = b_instances[1]
            iface_b.wiring.filter_configs[protocol].internal_instances = b_instances[0]
            iface_b.wiring.filter_configs[protocol].external_instances = a_instances[1]

    def build_configs(self):
        """Build remaining configuration structures."""
        ws_iface = self._get_interface(self.webserver_interface_idx)

        self.webserver_config = FwWebserverConfig(
            self.webserver_interface_idx,
            self.router_webserver_conn[1],
            self.webserver_data_region,
            self.webserver_rx_virt_conn[0],
            self.webserver_arp_conn[0],
            self._webserver_interface_configs,
        )

        # Add webserver as a free client of its interface's RX virt
        ws_iface.wiring.rx_virt_config.free_clients.append(
            self.webserver_rx_virt_conn[1]
        )

        # Add webserver as an ARP requester client on the other interface
        arp_iface = self._get_other_interface(ws_iface.index)
        arp_iface.wiring.arp_requester_config.arp_clients.append(
            self.webserver_arp_conn[1]
        )

        # Enable router -> webserver RX path on the webserver interface router
        ws_iface.router_config.rx_active = self.router_webserver_conn[0]

        return self

    def serialize_and_render(self, sdf_file: str):
        """Connect subsystems, serialize configs, and render SDF."""
        # Connect and serialize network subsystems
        for iface in self.interfaces:
            assert iface.net_system.connect()
            iface_out_dir = f"{self.output_dir}/{iface.out_dir}"
            assert iface.net_system.serialise_config(iface_out_dir)

        # Connect and serialize serial/timer systems
        assert self.serial_system.connect()
        assert self.serial_system.serialise_config(self.output_dir)
        assert self.timer_system.connect()
        assert self.timer_system.serialise_config(self.output_dir)

        # Serialize webserver lib sDDF LWIP config
        ws_iface = self._get_interface(self.webserver_interface_idx)
        assert self.webserver_lib_sddf_lwip.connect()
        assert self.webserver_lib_sddf_lwip.serialise_config(
            f"{self.output_dir}/{ws_iface.out_dir}"
        )

        # Serialize interface configs
        for iface in self.interfaces:
            iface_out_dir = f"{self.output_dir}/{iface.out_dir}"
            for pd, config in iface.wiring.configs_iter():
                data_path = f"{iface_out_dir}/firewall_config_{pd.name}.data"
                with open(data_path, "wb+") as f:
                    f.write(config.serialise())
                update_elf_section(
                    obj_copy, pd.program_image, config.section_name, data_path
                )

            if iface.router_config:
                data_path = f"{iface_out_dir}/firewall_config_{iface.router.name}.data"
                with open(data_path, "wb+") as f:
                    f.write(iface.router_config.serialise())
                update_elf_section(
                    obj_copy,
                    iface.router.program_image,
                    iface.router_config.section_name,
                    data_path,
                )

        # Serialize webserver config
        data_path = f"{self.output_dir}/firewall_config_webserver.data"
        with open(data_path, "wb+") as f:
            f.write(self.webserver_config.serialise())
        update_elf_section(
            obj_copy,
            self.webserver.program_image,
            self.webserver_config.section_name,
            data_path,
        )

        # Serialize ICMP module config
        data_path = f"{self.output_dir}/firewall_icmp_module_config.data"
        with open(data_path, "wb+") as f:
            f.write(self.icmp_module_config.serialise())
        update_elf_section(
            obj_copy,
            self.icmp_module.program_image,
            self.icmp_module_config.section_name,
            data_path,
        )

        # Render SDF
        with open(f"{self.output_dir}/{sdf_file}", "w+") as f:
            f.write(self.sdf_obj.render())

        # Generate topology graph
        from topology import generate_topology_dot

        dot_str = generate_topology_dot(self)
        with open(f"{self.output_dir}/topology.dot", "w+") as f:
            f.write(dot_str)

        return self


def generate(sdf_file: str, output_dir: str, dtb: DeviceTree):
    """Generate firewall system using the FirewallBuilder."""
    interfaces = [
        NetworkInterface(
            index=int_net,
            name="internal",
            ethernet_node_path=board.ethernet0,  # IMX hardware
            mac=(0x00, 0x01, 0xC0, 0x39, 0xD5, 0x10),
            ip="192.168.1.1",
            subnet_bits=24,
            priorities=InterfacePriorities(
                router=94,
                arp_requester=95,
                arp_responder=93,
                icmp_filter=93,
            ),
        ),
        NetworkInterface(
            index=ext_net,
            name="external",
            ethernet_node_path=board.ethernet1,  # DWMAC hardware
            mac=(0x00, 0x01, 0xC0, 0x39, 0xD5, 0x18),
            ip="172.16.2.1",
            subnet_bits=16,
            priorities=InterfacePriorities(
                router=97,
                arp_requester=98,
                arp_responder=95,
                icmp_filter=90,
            ),
        ),
    ]

    builder = FirewallBuilder(
        sdf_obj=sdf,
        dtb=dtb,
        output_dir=output_dir,
        interfaces=interfaces,
        webserver_interface=int_net,
    )

    (
        builder.create_common_pds()
        .create_interface_pds()
        .create_router_and_webserver()
        .create_network_subsystems()
        .create_connections()
        .build_configs()
        .serialize_and_render(sdf_file)
    )


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

    # Import the config structs module from the build directory
    sys.path.append(args.output)
    from config_structs import *
    from specs import *

    board = next(filter(lambda b: b.name == args.board, BOARDS))

    sdf = SystemDescription(board.arch, board.paddr_top)
    sddf = Sddf(args.sddf)

    global obj_copy
    obj_copy = args.objcopy

    global obj_dump
    obj_dump = args.objdump

    with open(args.dtb, "rb") as f:
        dtb = DeviceTree(f.read())

    for region in FirewallMemoryRegions.regions:
        if region.region_size:
            # Memory region size is fixed
            continue

        for structure in region.data_structures:
            if structure.size:
                # Data structure size has already been calculated
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
