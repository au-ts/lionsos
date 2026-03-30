# Copyright 2026, UNSW SPDX-License-Identifier: BSD-2-Clause

from typing import Optional, List
from sdfgen import SystemDescription
from pyfw.board import Board
from pyfw.memory_layout import (
    FirewallDataStructure,
    FirewallMemoryRegions,
    UINT64_BYTES,
)
from pyfw.component_net_interface import NetworkInterface
from config_structs import FwRule, FwRoutingEntry

### ----------------------------------------------------------------------- ###
### System constants set pre-build, or immediately by the metaprogram ###
### ----------------------------------------------------------------------- ###

BOARDS = [
    Board(
        name="qemu_virt_aarch64",
        arch=SystemDescription.Arch.AARCH64,
        paddr_top=0x6_0000_000,
        serial="pl011@9000000",
        timer="timer",
        ethernet=("virtio_mmio@a003e00", "virtio_mmio@a003c00") #, "virtio_mmio@a003a00")
    ),
    Board(
        name="imx8mp_iotgate",
        arch=SystemDescription.Arch.AARCH64,
        paddr_top=0x70_000_000,
        serial="soc@0/bus@30800000/serial@30890000",
        timer="soc@0/bus@30000000/timer@302d0000",
        ethernet=("soc@0/bus@30800000/ethernet@30bf0000", "soc@0/bus@30800000/ethernet@30be0000")
    ),
]

class BuildConstants:
    # System description file
    _sdf: Optional[SystemDescription] = None

    # Root output directory
    _output_dir: Optional[str] = None

    @classmethod
    def set_sdf(cls, sdf: SystemDescription) -> None:
        assert cls._sdf is None
        assert sdf is not None
        cls._sdf = sdf

    @classmethod
    def sdf(cls) -> SystemDescription:
        assert cls._sdf is not None
        return cls._sdf

    @classmethod
    def set_output_dir(cls, output_dir: str) -> None:
        assert cls._output_dir is None
        assert output_dir is not None
        cls._output_dir = output_dir

    @classmethod
    def output_dir(cls) -> str:
        assert cls._output_dir is not None
        return cls._output_dir

    @classmethod
    def interfaces(cls) -> list:
        return interfaces

    @classmethod
    def initial_rules(cls) -> list[dict[int, list[FwRule]]]:
        return initial_rules

# Network interface configuration
interfaces = [
    NetworkInterface(
        index=0,
        name="interface0",
        board_ethernet_idx=0,
        mac=(0x00, 0x01, 0xC0, 0x39, 0xD5, 0x18),
        ip="172.16.2.1",
        subnet_bits=16,
    ),
    NetworkInterface(
        index=1,
        name="interface1",
        board_ethernet_idx=1,
        mac=(0x00, 0x01, 0xC0, 0x39, 0xD5, 0x10),
        ip="192.168.1.1",
        subnet_bits=24,
    ),
    # Comment out interface2 when building for 2-interface boards.
    # NetworkInterface(
    #     index=2,
    #     name="interface2",
    #     board_ethernet_idx=2,
    #     mac=(0x00, 0x01, 0xC0, 0x39, 0xD5, 0x12),
    #     ip="10.0.2.1",
    #     subnet_bits=24,
    # ),
]

# Currently the webserver can only transmit out interface 1
webserver_tx_interface_idx = 1

### ----------------------------------------------------------------------- ###
### Filtering ###
### ----------------------------------------------------------------------- ###
supported_protocols = {0x01: "icmp", 0x06: "tcp", 0x11: "udp"}

FILTER_ACTION_ALLOW = 1
FILTER_ACTION_DROP = 2
FILTER_ACTION_REJECT = 3
FILTER_ACTION_CONNECT = 4

# If a filter supports action n, index n-1 is set to 1
supported_filter_actions = {
    0x01: [1, 1, 1, 1],
    0x06: [1, 1, 0, 1],
    0x11: [1, 1, 1, 1]
}

def construct_rule(action: int, src_ip: int, src_subnet: int, src_port: int, src_port_any: bool,
                   dst_ip: int, dst_subnet: int, dst_port: int, dst_port_any: bool) -> FwRule:
    assert action in (FILTER_ACTION_ALLOW, FILTER_ACTION_DROP, FILTER_ACTION_CONNECT)
    return FwRule(
        action=action,
        src_ip=src_ip,
        dst_ip=dst_ip,
        src_port=src_port,
        dst_port=dst_port,
        src_subnet=src_subnet,
        dst_subnet=dst_subnet,
        src_port_any=src_port_any,
        dst_port_any=dst_port_any,
        rule_id=0,
    )

def default_action_rule(action: int) -> FwRule:
    assert action in (FILTER_ACTION_ALLOW, FILTER_ACTION_DROP, FILTER_ACTION_CONNECT)
    return construct_rule(action, 0, 0, 0, True, 0, 0, 0, True)

# Initial rules - protocol->rule dictionary for each interface
# The first rule must always be the default action in each sublist
initial_rules = [
    {
        0x01: [default_action_rule(FILTER_ACTION_ALLOW)],
        0x06: [default_action_rule(FILTER_ACTION_ALLOW)],
        0x11: [default_action_rule(FILTER_ACTION_ALLOW)],
    },
    {
        0x01: [default_action_rule(FILTER_ACTION_ALLOW)],
        0x06: [default_action_rule(FILTER_ACTION_ALLOW)],
        0x11: [default_action_rule(FILTER_ACTION_ALLOW)],
    },
    # {
    #     0x01: [default_action_rule(FILTER_ACTION_ALLOW)],
    #     0x06: [default_action_rule(FILTER_ACTION_ALLOW)],
    #     0x11: [default_action_rule(FILTER_ACTION_ALLOW)],
    # },
]

### ----------------------------------------------------------------------- ###
### Routing ###
### ----------------------------------------------------------------------- ###

def construct_route(ip: int, subnet: int, interface: int, next_hop: int) -> FwRoutingEntry:
    assert interface < len(interfaces)
    return FwRoutingEntry(ip=ip, subnet=subnet, interface=interface, next_hop=next_hop)

# Initial routes, in addition to the direct routes for hosts on each interface's subnet
initial_routes: List[FwRoutingEntry] = []

### ----------------------------------------------------------------------- ###
### Firewall Data Structures & Memory Regions ###
### ----------------------------------------------------------------------- ###
# Used for memory region size calculations.
# See: https://lionsos.org/docs/examples/firewall/building/#data-structure-sizes-and-capacities

# --------------------------------------------- #
# Firewall queue indices
fw_queue_wrapper = FirewallDataStructure(
    elf_name="routing.elf", c_name="fw_queue"
)

# --------------------------------------------- #
# Firewall queue for passing DMA buffers
dma_buffer_queue = FirewallDataStructure(
    elf_name="routing.elf", c_name="net_buff_desc", capacity=512
)
dma_buffer_queue_region = FirewallMemoryRegions(
    data_structures=[fw_queue_wrapper, dma_buffer_queue]
)

# --------------------------------------------- #
# Rx DMA buffer memory region
dma_buffer_region = FirewallMemoryRegions(
    min_size=dma_buffer_queue.capacity * 2048
)

# --------------------------------------------- #
# Firewall queue for passing ARP requests
arp_queue_buffer = FirewallDataStructure(
    elf_name="arp_requester.elf", c_name="fw_arp_request", capacity=512
)
arp_queue_region = FirewallMemoryRegions(
    data_structures=[fw_queue_wrapper, arp_queue_buffer]
)

# --------------------------------------------- #
# Firewall queue for passing ICMP requests
icmp_queue_buffer = FirewallDataStructure(
    elf_name="icmp_module.elf", c_name="icmp_req", capacity=128
)
icmp_queue_region = FirewallMemoryRegions(
    data_structures=[fw_queue_wrapper, icmp_queue_buffer]
)

# --------------------------------------------- #
# ARP entry cache table
arp_cache_buffer = FirewallDataStructure(
    elf_name="arp_requester.elf", c_name="fw_arp_entry", capacity=512
)
arp_cache_region = FirewallMemoryRegions(data_structures=[arp_cache_buffer])

# --------------------------------------------- #
# Router packet waiting linked list node pool
arp_packet_queue_buffer = FirewallDataStructure(
    elf_name="routing.elf",
    c_name="pkt_waiting_node",
    capacity=dma_buffer_queue.capacity,
)
arp_packet_queue_region = FirewallMemoryRegions(
    data_structures=[arp_packet_queue_buffer]
)

# --------------------------------------------- #
# Routing table
routing_table_wrapper = FirewallDataStructure(
    elf_name="routing.elf", c_name="fw_routing_table"
)
routing_table_buffer = FirewallDataStructure(
    elf_name="routing.elf", c_name="fw_routing_entry", capacity=256
)
routing_table_region = FirewallMemoryRegions(
    data_structures=[routing_table_wrapper, routing_table_buffer]
)

# --------------------------------------------- #
# Filter rule table
filter_rules_wrapper = FirewallDataStructure(
    elf_name="icmp_filter.elf", c_name="fw_rule_table"
)
filter_rules_buffer = FirewallDataStructure(
    elf_name="icmp_filter.elf", c_name="fw_rule", capacity=256
)
filter_rules_region = FirewallMemoryRegions(
    data_structures=[filter_rules_wrapper, filter_rules_buffer]
)

# --------------------------------------------- #
# Filter rule ID bitmap
filter_rule_bitmap_wrapper = FirewallDataStructure(
    elf_name="icmp_filter.elf", c_name="fw_rule_id_bitmap"
)
filter_rule_bitmap_buffer = FirewallDataStructure(
    entry_size=UINT64_BYTES, capacity=(filter_rules_buffer.capacity + 63) // 64
)
filter_rule_bitmap_region = FirewallMemoryRegions(
    data_structures=[filter_rule_bitmap_wrapper, filter_rule_bitmap_buffer]
)

# --------------------------------------------- #
# Filter instance table
filter_instances_wrapper = FirewallDataStructure(
    elf_name="icmp_filter.elf", c_name="fw_instances_table"
)
filter_instances_buffer = FirewallDataStructure(
    elf_name="icmp_filter.elf", c_name="fw_instance", capacity=256
)
filter_instances_region = FirewallMemoryRegions(
    data_structures=[filter_instances_wrapper, filter_instances_buffer]
)

### ----------------------------------------------------------------------- ###
### Network constants ###
### ----------------------------------------------------------------------- ###
# Ethernet types
eththype_ip = 0x0800
ethtype_arp = 0x0806

# Ethernet ARP opcodes
arp_eth_opcode_request = 1
arp_eth_opcode_response = 2

maxPortNum = 65535

def htons(portNum: int):
    assert portNum >= 0 and portNum <= maxPortNum
    return ((portNum & 0xFF) << 8) | ((portNum & 0xFF00) >> 8)
