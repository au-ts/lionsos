# Copyright 2025, UNSW SPDX-License-Identifier: BSD-2-Clause
import argparse
import subprocess
from os import path
from dataclasses import dataclass
from typing import List
from sdfgen import SystemDescription, Sddf, DeviceTree
from ctypes import *
from importlib.metadata import version
import ipaddress

assert version('sdfgen').split(".")[1] == "26", "Unexpected sdfgen version"

from sdfgen_helper import *

ProtectionDomain = SystemDescription.ProtectionDomain
MemoryRegion = SystemDescription.MemoryRegion
Map = SystemDescription.Map
Channel = SystemDescription.Channel

# System network constants
ext_net = 0
int_net = 1

macs = [[0x00, 0x01, 0xc0, 0x39, 0xd5, 0x18], # External network
        [0x00, 0x01, 0xc0, 0x39, 0xd5, 0x10]] # Internal network

subnet_bits = [12, # External network
               24] # Internal network

ips = ["172.16.2.1",  # External network
       "192.168.1.1"] # Internal network

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
        ethernet1="virtio_mmio@a003e00"
    ),
    Board(
        name="imx8mp_iotgate",
        arch=SystemDescription.Arch.AARCH64,
        paddr_top=0x70_000_000,
        serial="soc@0/bus@30800000/serial@30890000",
        timer="soc@0/bus@30000000/timer@302d0000",
        ethernet0="soc@0/bus@30800000/ethernet@30be0000", #IMX
        ethernet1="soc@0/bus@30800000/ethernet@30bf0000" #DWMAC
    ),
]

# Memory region size helper functions
page_size = 0x1000

def round_up_to_Page(region_size: int) -> int:
    if (region_size < page_size):
        return page_size
    elif (region_size % page_size == 0):
        return region_size
    else:
        return region_size + (page_size - (region_size % page_size))

# Elf file containing variables holding the size of structs required for
# calculating memory region sizes
entry_size_extraction_elf = "routing.elf"
class FirewallMemoryRegions():
    # Store all config structs
    all_mrs = []

    def __init__(self, c_name, capacity, region_size_formula, entry_size = 0, region_size = 0):
        # Name of variable in routing.elf holding size of table entry
        self.c_name = c_name
        # Capacity of data structure held in memory region
        self.capacity = capacity
        # Formula for calculating memory region size from class object
        self.region_size_formula = region_size_formula
        # Size of individual entries of data structure
        self.entry_size = entry_size
        # Size of region calculated using region_size_formula
        self.region_size = region_size
        FirewallMemoryRegions.all_mrs.append(self)

    def region_size(self):
        property
        if self.region_size == 0:
            print("Extracted region size of memory region {self.c_name} was 0!")
            sys.exit()
        return self.region_size

    def calculate_size(self):
        if self.entry_size == 0:
            print("Entry size of memory region {self.c_name} was 0 during region size calculation!")
            sys.exit()
        self.region_size = round_up_to_Page(self.region_size_formula(self))
        if self.region_size == 0:
            print("Calculate region size of memory region {self.c_name} was 0!")
            sys.exit()

# Firewall memory region object declarations, update region capacities here
dma_buffer_queue_region = FirewallMemoryRegions("fw_buffer_queue_entry_size",
                                                512,
                                                lambda x: 16 + x.capacity * x.entry_size)

dma_buffer_region = FirewallMemoryRegions(None,
                                          dma_buffer_queue_region.capacity,
                                          lambda x: x.capacity * x.entry_size,
                                          2048)

arp_queue_region = FirewallMemoryRegions("fw_arp_queue_entry_size",
                                         512,
                                         lambda x: 16 + x.capacity * x.entry_size)

icmp_queue_region = FirewallMemoryRegions("fw_icmp_queue_entry_size",
                                         128,
                                         lambda x: 16 + x.capacity * x.entry_size)

arp_cache_region = FirewallMemoryRegions("fw_arp_entry_size",
                                         512,
                                         lambda x: x.capacity * x.entry_size)

arp_packet_queue_region = FirewallMemoryRegions("fw_arp_pkt_node_size",
                                         dma_buffer_queue_region.capacity,
                                         lambda x: x.capacity * x.entry_size)

routing_table_region = FirewallMemoryRegions("fw_routing_entry_size",
                                         256,
                                         lambda x: 4 + x.capacity * x.entry_size)

filter_rules_region = FirewallMemoryRegions("fw_rule_size",
                                         256,
                                         lambda x: x.capacity * x.entry_size)

filter_instances_region = FirewallMemoryRegions("fw_instance_size",
                                         512,
                                         lambda x: x.capacity * x.entry_size)

# Filter action encodings
FILTER_ACTION_ALLOW = 1
FILTER_ACTION_DROP = 2
FILTER_ACTION_CONNECT = 3

# ARP ethernet type numbers
arp_responder_protocol = 0x92
arp_requester_protocol = 0x93

# Helper functions used to generate firewall structures
def ip_to_int(ipString: str):
    ipaddress.IPv4Address(ipString)

    # Switch little to big endian
    ipSplit = ipString.split(".")
    ipSplit.reverse()
    reversedIp = ".".join(ipSplit)
    return int(ipaddress.IPv4Address(reversedIp))

# Create a firewall connection, which is a single queue and a channel. Data must
# be created and mapped separately
def fw_connection(pd1: SystemDescription.ProtectionDomain ,
                  pd2: SystemDescription.ProtectionDomain,
                  capacity: int,
                  region_size: int):
    queue_name = "fw_queue_" + pd1.name + "_" + pd2.name
    queue = MemoryRegion(sdf, queue_name, region_size)
    sdf.add_mr(queue)

    pd1_map = Map(queue, pd1.get_map_vaddr(queue), perms="rw")
    pd1.add_map(pd1_map)
    pd1_region = RegionResource(pd1_map.vaddr, region_size)

    pd2_map = Map(queue, pd2.get_map_vaddr(queue), perms="rw")
    pd2.add_map(pd2_map)
    pd2_region = RegionResource(pd2_map.vaddr, region_size)

    ch = Channel(pd1, pd2)
    sdf.add_channel(ch)

    pd1_conn = FwConnectionResource(pd1_region, capacity, ch.pd_a_id)
    pd2_conn = FwConnectionResource(pd2_region, capacity, ch.pd_b_id)

    return [pd1_conn, pd2_conn]

# Create a firewall arp connection, which is two queues of the same capacity and
# a channel
def fw_arp_connection(pd1: SystemDescription.ProtectionDomain ,
                      pd2: SystemDescription.ProtectionDomain,
                      capacity: int,
                      region_size: int):
    req_queue_name = "fw_req_queue_" + pd1.name + "_" + pd2.name
    req_queue = MemoryRegion(sdf, req_queue_name, region_size)
    sdf.add_mr(req_queue)

    pd1_req_map = Map(req_queue, pd1.get_map_vaddr(req_queue), perms="rw")
    pd1.add_map(pd1_req_map)
    pd1_req_region = RegionResource(pd1_req_map.vaddr, region_size)

    pd2_req_map = Map(req_queue, pd2.get_map_vaddr(req_queue), perms="rw")
    pd2.add_map(pd2_req_map)
    pd2_req_region = RegionResource(pd2_req_map.vaddr, region_size)

    res_queue_name = "fw_res_queue_" + pd1.name + "_" + pd2.name
    res_queue = MemoryRegion(sdf, res_queue_name, region_size)
    sdf.add_mr(res_queue)

    pd1_res_map = Map(res_queue, pd1.get_map_vaddr(res_queue), perms="rw")
    pd1.add_map(pd1_res_map)
    pd1_res_region = RegionResource(pd1_res_map.vaddr, region_size)

    pd2_res_map = Map(res_queue, pd2.get_map_vaddr(res_queue), perms="rw")
    pd2.add_map(pd2_res_map)
    pd2_res_region = RegionResource(pd2_res_map.vaddr, region_size)

    ch = Channel(pd1, pd2)
    sdf.add_channel(ch)

    pd1_conn = FwArpConnection(pd1_req_region, pd1_res_region, capacity, ch.pd_a_id)
    pd2_conn = FwArpConnection(pd2_req_region, pd2_res_region, capacity, ch.pd_b_id)

    return [pd1_conn, pd2_conn]

# Map a mr into a pd to create a firewall region
def fw_region(pd: SystemDescription.ProtectionDomain,
              mr: SystemDescription.MemoryRegion,
              perms: str,
              region_size: int):
    pd_map = Map(mr, pd.get_map_vaddr(mr), perms=perms)
    pd.add_map(pd_map)
    region_resource = RegionResource(pd_map.vaddr, region_size)

    return region_resource

# Map a physical mr into a pd to create a firewall device region
def fw_device_region(pd: SystemDescription.ProtectionDomain, mr: SystemDescription.MemoryRegion, perms: str):
    region = fw_region(pd, mr, perms, mr.size)
    device_region = DeviceRegionResource(
            region,
            mr.paddr.value
        )
    return device_region

# Create a firewall connection and map a physical mr to create a firewall data
# connection
def fw_data_connection(pd1: SystemDescription.ProtectionDomain ,
                       pd2: SystemDescription.ProtectionDomain,
                       capacity: int,
                       queue_size: int,
                       data: SystemDescription.MemoryRegion,
                       data_perms1: str,
                       data_perms2: str):
    connection = fw_connection(pd1, pd2, capacity, queue_size)
    data_region1 = fw_device_region(pd1, data, data_perms1)
    data_region2 = fw_device_region(pd2, data, data_perms2)

    data_connection1 = FwDataConnectionResource(
        connection[0],
        data_region1
    )
    data_connection2 = FwDataConnectionResource(
        connection[1],
        data_region2
    )

    return [data_connection1, data_connection2]


# Create a shared memory region between two pds from a mr
def fw_shared_region(pd1: SystemDescription.ProtectionDomain,
                     pd2: SystemDescription.ProtectionDomain,
                     perms1: str,
                     perms2: str,
                     name_prefix: str,
                     region_size: int):
    # Create rule memory region
    region_name = name_prefix + "_" + pd1.name + "_" + pd2.name
    mr = MemoryRegion(sdf, region_name, region_size)
    sdf.add_mr(mr)

    # Map rule into pd1
    region1 = fw_region(pd1, mr, perms1, region_size)

    # Map rule memory region into webserver
    region2 = fw_region(pd2, mr, perms2, region_size)

    return [region1, region2]

def generate(sdf_file: str, output_dir: str, dtb: DeviceTree):
    serial_node = dtb.node(board.serial)
    assert serial_node is not None
    ethernet_node0 = dtb.node(board.ethernet0)
    assert ethernet_node0 is not None
    ethernet_node1 = dtb.node(board.ethernet1)
    assert ethernet_node1 is not None
    timer_node = dtb.node(board.timer)
    assert serial_node is not None

    common_pds = []

    # Initialise network info dictionaries
    networks = []
    for i in range(2):
        networks.append({
            "num": i,
            "out_num": (i + 1) % 2,
            "mac": macs[i],
            "ip": ip_to_int(ips[i]),
            "out_dir": output_dir + "/net_data" + str(i),
            "configs":{},
            })
        if not path.isdir(networks[i]["out_dir"]):
            assert subprocess.run(["mkdir", networks[i]["out_dir"]]).returncode == 0

    # Create timer subsystem
    common_pds.append(ProtectionDomain("timer_driver", "timer_driver.elf", priority=101))
    timer_system = Sddf.Timer(sdf, timer_node, common_pds[-1])

    # Create serial subsystem
    common_pds.append(ProtectionDomain("serial_driver", "serial_driver.elf", priority=100))
    common_pds.append(ProtectionDomain("serial_virt_tx", "serial_virt_tx.elf", priority=99))
    serial_system = Sddf.Serial(sdf, serial_node, common_pds[-2], common_pds[-1])

    # Create network 0 pds
    networks[ext_net]["driver"] = ProtectionDomain("ethernet_driver0", "eth_driver0.elf", priority=101, budget=100, period=400)
    networks[int_net]["out_virt"] = ProtectionDomain("net_virt_tx0", "firewall_network_virt_tx0.elf", priority=100, budget=20000)
    networks[ext_net]["in_virt"] = ProtectionDomain("net_virt_rx0", "firewall_network_virt_rx0.elf", priority=99)

    networks[ext_net]["rx_dma_region"] = MemoryRegion(sdf, "rx_dma_region0", dma_buffer_region.region_size, physical=True)
    sdf.add_mr(networks[ext_net]["rx_dma_region"])

    # Create network 1 subsystem pds
    networks[int_net]["driver"] = ProtectionDomain("ethernet_driver1", "eth_driver1.elf", priority=101, budget=100, period=400)
    networks[ext_net]["out_virt"] = ProtectionDomain("net_virt_tx1", "firewall_network_virt_tx1.elf", priority=100, budget=20000)
    networks[int_net]["in_virt"] = ProtectionDomain("net_virt_rx1", "firewall_network_virt_rx1.elf", priority=99)

    networks[int_net]["rx_dma_region"] = MemoryRegion(sdf, "rx_dma_region1", dma_buffer_region.region_size, physical=True)
    sdf.add_mr(networks[int_net]["rx_dma_region"])

    # Create network subsystems
    networks[ext_net]["in_net"] = Sddf.Net(sdf, ethernet_node1, networks[ext_net]["driver"], networks[int_net]["out_virt"],
                                           networks[ext_net]["in_virt"], networks[ext_net]["rx_dma_region"])
    networks[int_net]["out_net"] = networks[ext_net]["in_net"]


    networks[int_net]["in_net"] = Sddf.Net(sdf, ethernet_node0, networks[int_net]["driver"], networks[ext_net]["out_virt"],
                                           networks[int_net]["in_virt"], networks[int_net]["rx_dma_region"])
    networks[ext_net]["out_net"] = networks[int_net]["in_net"]

    # Create firewall pds
    networks[ext_net]["router"] = ProtectionDomain("routing0", "routing0.elf", priority=97, budget=20000)
    networks[int_net]["router"] = ProtectionDomain("routing1", "routing1.elf", priority=94, budget=20000)

    networks[ext_net]["arp_resp"] = ProtectionDomain("arp_responder0", "arp_responder0.elf", priority=95, budget=20000)
    networks[int_net]["arp_resp"] = ProtectionDomain("arp_responder1", "arp_responder1.elf", priority=93, budget=20000)

    networks[ext_net]["arp_req"] = ProtectionDomain("arp_requester0", "arp_requester0.elf", priority=98, budget=20000)
    networks[int_net]["arp_req"] = ProtectionDomain("arp_requester1", "arp_requester1.elf", priority=95, budget=20000)

    # Create the webserver component
    webserver = ProtectionDomain("micropython", "micropython.elf", priority=1, budget=20000, stack_size=0x10000)
    common_pds.append(webserver)

    # Webserver is a serial and timer client
    serial_system.add_client(webserver)
    timer_system.add_client(webserver)

    # Create ICMP Module component
    icmp_module = ProtectionDomain("icmp_module", "icmp_module.elf", priority=100, budget=20000)
    common_pds.append(icmp_module)

    networks[ext_net]["filters"] = {}
    networks[ext_net]["filters"][0x01] = ProtectionDomain("icmp_filter0", "icmp_filter0.elf", priority=90, budget=20000)
    networks[ext_net]["filters"][0x11] = ProtectionDomain("udp_filter0", "udp_filter0.elf", priority=91, budget=20000)
    networks[ext_net]["filters"][0x06] = ProtectionDomain("tcp_filter0", "tcp_filter0.elf", priority=92, budget=20000)

    networks[int_net]["filters"] = {}
    networks[int_net]["filters"][0x01] = ProtectionDomain("icmp_filter1", "icmp_filter1.elf", priority=93, budget=20000)
    networks[int_net]["filters"][0x11] = ProtectionDomain("udp_filter1", "udp_filter1.elf", priority=91, budget=20000)
    networks[int_net]["filters"][0x06] = ProtectionDomain("tcp_filter1", "tcp_filter1.elf", priority=92, budget=20000)

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
                    copy_elf(maybe_pd.program_image[:-5], maybe_pd.program_image[:-5], network["num"])
                sdf.add_pd(maybe_pd)

        for filter_pd in network["filters"].values():
            copy_elf(filter_pd.program_image[:-5], filter_pd.program_image[:-5], network["num"])
            sdf.add_pd(filter_pd)

        # Ensure arp requester is client 0 for each network
        network["out_net"].add_client_with_copier(network["arp_req"])

    # Webserver is a tx client of the internal network
    networks[int_net]["in_net"].add_client_with_copier(webserver, rx=False)

    # Webserver uses lib sDDF LWIP
    webserver_lib_sddf_lwip = Sddf.Lwip(sdf, networks[int_net]["in_net"], webserver)

    # Webserver receives traffic from the internal -> external router
    router_webserver_conn = fw_connection(networks[int_net]["router"], webserver,
                                          dma_buffer_queue_region.capacity,
                                          dma_buffer_queue_region.region_size)

    # Webserver returns packets to interior rx virtualiser
    webserver_in_virt_conn = fw_connection(webserver, networks[int_net]["in_virt"],
                                           dma_buffer_queue_region.capacity,
                                           dma_buffer_queue_region.region_size)

    # Webserver needs access to rx dma region
    webserver_data_region = fw_region(webserver, networks[int_net]["rx_dma_region"],
                                      "rw", dma_buffer_queue_region.region_size)

    # Webserver has arp channel for arp requests/responses
    webserver_arp_conn = fw_arp_connection(webserver, networks[ext_net]["arp_req"],
                                           arp_queue_region.capacity, arp_queue_region.region_size)

    # ICMP Module needs to be able to transmit out of both NICs
    networks[ext_net]["out_net"].add_client_with_copier(icmp_module, rx=False)
    networks[int_net]["out_net"].add_client_with_copier(icmp_module, rx=False)

    # ICMP Module needs to be connected to both routers.
    icmp_int_router_conn = fw_connection(networks[int_net]["router"], icmp_module,
                                        icmp_queue_region.capacity, icmp_queue_region.region_size)
    icmp_ext_router_conn = fw_connection(networks[ext_net]["router"], icmp_module,
                                        icmp_queue_region.capacity, icmp_queue_region.region_size)

    icmp_module_config = FwIcmpModuleConfig(
        list(ip_to_int(ip) for ip in ips),
        [icmp_ext_router_conn[1], icmp_int_router_conn[1]],
        2
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
        router_out_virt_conn = fw_data_connection(router, out_virt, dma_buffer_queue_region.capacity,
                                                  dma_buffer_queue_region.region_size,
                                                  network["rx_dma_region"], "rw", "r")

        # Create a firewall connection for output virt to return buffers to
        # input virt
        output_in_virt_conn = fw_connection(out_virt, in_virt, dma_buffer_queue_region.capacity,
                                            dma_buffer_queue_region.region_size)
        out_virt_in_virt_data_conn = FwDataConnectionResource(
            output_in_virt_conn[0],
            router_out_virt_conn[1].data
        )

        # Create output virt config
        network["configs"][out_virt] = FwNetVirtTxConfig(
            network["num"],
            [router_out_virt_conn[1]],
            [out_virt_in_virt_data_conn]
        )

        # Create a firewall connection for router to return free buffers to
        # receive virtualiser on interior network
        router_in_virt_conn = fw_connection(router, in_virt, dma_buffer_queue_region.capacity,
                                            dma_buffer_queue_region.region_size)

        # Create input virt config
        network["configs"][in_virt] = FwNetVirtRxConfig(
            network["num"],
            [],
            [router_in_virt_conn[1], output_in_virt_conn[1]]
        )

        # Add arp requester protocol for input virt client 0 - this is for the
        # previously added arp requester which is always client 0
        network["configs"][in_virt].active_client_protocols.append(arp_requester_protocol)

        # Arp requester needs timer access to handle arp timeouts
        timer_system.add_client(arp_req)

        # Add arp responder filter pd as a network client
        network["in_net"].add_client_with_copier(arp_resp)
        network["configs"][in_virt].active_client_protocols.append(arp_responder_protocol)

        # Create arp queue firewall connection
        router_arp_conn = fw_arp_connection(router, arp_req, arp_queue_region.capacity,
                                            arp_queue_region.region_size)

        # Create arp cache
        arp_cache = fw_shared_region(arp_req, router, "rw", "r",
                                     "arp_cache", arp_cache_region.region_size)

        # Create arp req config
        network["configs"][arp_req] = FwArpRequesterConfig(
            network["num"],
            macs[network["out_num"]],
            ip_to_int(ips[network["out_num"]]),
            [router_arp_conn[1]],
            arp_cache[0],
            arp_cache_region.capacity
        )

        # Create arp resp config
        network["configs"][arp_resp] = FwArpResponderConfig(
            network["num"],
            network["mac"],
            network["ip"]
        )

        # Create arp packet queue
        arp_packet_queue_mr = MemoryRegion(sdf, "arp_packet_queue_" + router.name,
                                           arp_packet_queue_region.region_size)
        sdf.add_mr(arp_packet_queue_mr)
        arp_packet_queue = fw_region(router, arp_packet_queue_mr, "rw",
                                     arp_packet_queue_region.region_size)

        # Create routing table
        routing_table = fw_shared_region(router, webserver, "rw", "r",
                                         "routing_table",
                                         routing_table_region.region_size)

        # Create pp channel for routing table updates
        router_update_ch = Channel(webserver, router, pp_a=True)
        sdf.add_channel(router_update_ch)

        # Create router webserver config
        router_webserver_config = FwWebserverRouterConfig(
            router_update_ch.pd_b_id,
            routing_table[0],
            routing_table_region.capacity
        )

        webserver_router_config = FwWebserverRouterConfig(
            router_update_ch.pd_a_id,
            routing_table[1],
            routing_table_region.capacity
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
            arp_cache_region.capacity,
            arp_packet_queue,
            router_webserver_config,
            network["icmp_module"],
            []
        )

        webserver_interface_config = FwWebserverInterfaceConfig(
            network["mac"],
            network["ip"],
            webserver_router_config,
            []
        )

        for (protocol, filter_pd) in network["filters"].items():
            # Create a firewall connection for filter to transmit buffers to
            # router
            filter_router_conn = fw_connection(filter_pd, router,
                                               dma_buffer_queue_region.capacity,
                                               dma_buffer_queue_region.region_size)

            # Connect filter as rx only network client
            network["in_net"].add_client_with_copier(filter_pd, tx=False)
            network["configs"][in_virt].active_client_protocols.append(protocol)

            # Create rule region
            filter_rules = fw_shared_region(filter_pd, webserver, "rw",
                                            "r", "filter_rules",
                                            filter_rules_region.region_size)

            # Create pp channel between webserver and filter for rule updates
            filter_update_ch = Channel(webserver, filter_pd, pp_a = True)
            sdf.add_channel(filter_update_ch)

            # Create webserver configs
            filter_webserver_config = FwWebserverFilterConfig(
                protocol,
                filter_update_ch.pd_b_id,
                FILTER_ACTION_ALLOW,
                filter_rules[0],
                filter_rules_region.capacity
            )

            webserver_filter_config = FwWebserverFilterConfig(
                protocol,
                filter_update_ch.pd_a_id,
                FILTER_ACTION_ALLOW,
                filter_rules[1],
                filter_rules_region.capacity
            )

            # Create filter config
            network["configs"][filter_pd] = FwFilterConfig(
                network["num"],
                filter_instances_region.capacity,
                filter_router_conn[0],
                filter_webserver_config,
                None,
                None
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
    networks[int_net]["configs"][networks[int_net]["in_virt"]].free_clients.append(webserver_in_virt_conn[1])

    # Add webserver as an arp requester client outputting to the internal
    # network
    networks[ext_net]["configs"][networks[ext_net]["arp_req"]].arp_clients.append(webserver_arp_conn[1])

    # Add a firewall connection to the webserver from the internal router for
    # packet transmission
    networks[int_net]["configs"][networks[int_net]["router"]].rx_active = router_webserver_conn[0]

    # Add ICMP module


    # Create filter instance regions
    for (protocol, filter_pd) in networks[int_net]["filters"].items():
        mirror_filter = networks[ext_net]["filters"][protocol]
        int_instances = fw_shared_region(filter_pd, mirror_filter, "rw", "r",
                                         "instances", filter_instances_region.region_size)
        ext_instances = fw_shared_region(mirror_filter, filter_pd, "rw", "r",
                                         "instances", filter_instances_region.region_size)

        networks[int_net]["configs"][filter_pd].internal_instances = int_instances[0]
        networks[int_net]["configs"][filter_pd].external_instances = ext_instances[1]
        networks[ext_net]["configs"][mirror_filter].internal_instances = ext_instances[0]
        networks[ext_net]["configs"][mirror_filter].external_instances = int_instances[1]

    assert serial_system.connect()
    assert serial_system.serialise_config(output_dir)
    assert timer_system.connect()
    assert timer_system.serialise_config(output_dir)

    # Serialise webservers lib sDDF LWIP config
    assert webserver_lib_sddf_lwip.connect()
    assert webserver_lib_sddf_lwip.serialise_config(networks[int_net]["out_dir"])

    for network in networks:
        for pd, config in network["configs"].items():

            data_path = network["out_dir"] + "/firewall_config_" + pd.name + ".data"
            with open(data_path, "wb+") as f:
                f.write(config.serialise())
            update_elf_section(obj_copy, pd.program_image,
                               config.section_name,
                               data_path)

    data_path = output_dir + "/firewall_config_webserver.data"
    with open(data_path, "wb+") as f:
        f.write(webserver_config.serialise())
    update_elf_section(obj_copy, webserver.program_image,
                       webserver_config.section_name,
                       data_path)

    data_path = output_dir + "/firewall_icmp_module_config.data"
    with open(data_path, "wb+") as f:
        f.write(icmp_module_config.serialise())
    update_elf_section(obj_copy, icmp_module.program_image,
                       icmp_module_config.section_name,
                       data_path)

    with open(f"{output_dir}/{sdf_file}", "w+") as f:
        f.write(sdf.render())


if __name__ == '__main__':
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

    board = next(filter(lambda b: b.name == args.board, BOARDS))

    sdf = SystemDescription(board.arch, board.paddr_top)
    sddf = Sddf(args.sddf)

    global obj_copy
    obj_copy = args.objcopy

    global obj_dump
    obj_dump = args.objdump

    with open(args.dtb, "rb") as f:
        dtb = DeviceTree(f.read())

    # For memory regions holding arrays of firewall structs, we require the size
    # of these structs to ensure our memory regions are the correct size. The
    # elf file for the routing component defines a set of const size_t variables
    # holding these sizes. We us objdump to extract these values.

    # Dump the elf file of the routing component
    objdump_process = subprocess.run([obj_dump, "-DlSx", entry_size_extraction_elf],
                                     stdout=subprocess.PIPE,
                                     stderr=subprocess.PIPE,
                                     check=True)
    assert objdump_process.returncode == 0

    for mem_region in FirewallMemoryRegions.all_mrs:
        entry_size = mem_region.entry_size
        if entry_size == 0:
            # Extract lines that hold the value of the size variable. NOTE: we
            # assume the value is < UINT32_MAX
            entry_size_lines = subprocess.run(["grep", "-A", "1", "-E", f"^[0-9a-f]+ <{mem_region.c_name}>"],
                                              input=objdump_process.stdout,
                                              stdout=subprocess.PIPE,
                                              stderr=subprocess.PIPE,
                                              check=True)
            # Isolate value line
            size_line = subprocess.run(args=["grep", "0x"],
                                              input=entry_size_lines.stdout,
                                              stdout=subprocess.PIPE,
                                              stderr=subprocess.PIPE,
                                              check=True)
            # Clean line
            size_bytes = subprocess.run(["sed", "s/.*\\.word\\t*//g"],
                                              input=size_line.stdout,
                                              capture_output=True,
                                              check=True)

            size_hex_string = str(size_bytes.stdout[:-1])[2:-1]
            entry_size = int(size_hex_string, 16)
            mem_region.entry_size = entry_size

        mem_region.calculate_size()

    generate(args.sdf, args.output, dtb)
