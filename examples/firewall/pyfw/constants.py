from dataclasses import dataclass
from typing import Tuple, List
from sdfgen import SystemDescription
from pyfw.memory_layout import (
    FirewallDataStructure,
    FirewallMemoryRegions,
    UINT64_BYTES,
)
from pyfw.config_structs import FwRule

### ----------------------------------------------------------------------- ###
### System constants set pre-build, or immediately by the metaprogram ###
### ----------------------------------------------------------------------- ###
sdf: SystemDescription

@dataclass
class NetworkInterface:
    index: int
    name: str
    mac: Tuple[int, ...]
    ip: str
    subnet_bits: int

    @property
    def ip_int(self) -> int:
        import ipaddress

        ip_split = self.ip.split(".")
        ip_split.reverse()
        reversed_ip = ".".join(ip_split)
        return int(ipaddress.IPv4Address(reversed_ip))

    @property
    def mac_list(self) -> List[int]:
        return list(self.mac)

# Network interface configuration
interfaces = [
    NetworkInterface(
        index=0,
        name="external",
        mac=(0x00, 0x01, 0xC0, 0x39, 0xD5, 0x18),
        ip="172.16.2.1",
        subnet_bits=16,
    ),
    NetworkInterface(
        index=1,
        name="internal",
        mac=(0x00, 0x01, 0xC0, 0x39, 0xD5, 0x10),
        ip="192.168.1.1",
        subnet_bits=24,
    ),
]

# Root output directory
output_dir: str

# TODO: Webserver can only transmit out one interface
webserver_tx_interface_idx = 1

### ----------------------------------------------------------------------- ###
### Filtering ###
### ----------------------------------------------------------------------- ###
supported_protocols = {0x01: "icmp", 0x06: "tcp", 0x11: "udp"}

FILTER_ACTION_ALLOW = 1
FILTER_ACTION_DROP = 2
FILTER_ACTION_CONNECT = 3

# Initial rules - protocol->rule dictionary for each interface
# The first rule must always be the default action
def default_action_rule(action: int) -> FwRule:
    assert action in (FILTER_ACTION_ALLOW, FILTER_ACTION_DROP, FILTER_ACTION_CONNECT)
    return FwRule(action, 0, 0, 0, 0, 0, 0, True, True, 0)

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
]

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
