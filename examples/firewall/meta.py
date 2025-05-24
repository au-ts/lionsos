# Copyright 2025, UNSW
# SPDX-License-Identifier: BSD-2-Clause
import argparse
import subprocess
from os import path
from dataclasses import dataclass
from typing import List
from sdfgen import SystemDescription, Sddf, DeviceTree
from ctypes import *
from importlib.metadata import version
import ipaddress

assert version('sdfgen').split(".")[1] == "24", "Unexpected sdfgen version"

from sdfgen_helper import *
from config_structs import *

ProtectionDomain = SystemDescription.ProtectionDomain
MemoryRegion = SystemDescription.MemoryRegion
Map = SystemDescription.Map
Channel = SystemDescription.Channel

page_size = 0x1000

def round_up_to_Page(region_size: int) -> int:
    if (region_size < page_size):
        return page_size
    elif (region_size % page_size == 0):
        return region_size
    else:
        return region_size + (page_size - (region_size % page_size))

dma_queue_capacity = 512
dma_queue_region_size = round_up_to_Page(8 + 16 * dma_queue_capacity)

dma_buffer_size = 2048
dma_region_size = round_up_to_Page(dma_queue_capacity * dma_buffer_size)

arp_queue_capacity = 512
arp_queue_region_size = round_up_to_Page(2 * (4 + 16 * arp_queue_capacity) + 4)

arp_cache_capacity = 512
arp_cache_region_size = round_up_to_Page(24 * arp_cache_capacity)

arp_packet_queue_region_size = round_up_to_Page(dma_queue_capacity * 24)

routing_table_capacity = 256
routing_table_size = round_up_to_Page(routing_table_capacity * 24)

filter_rule_capacity = 256
filter_rule_region_size = round_up_to_Page(filter_rule_capacity * 28)

instances_capacity = 512
instances_region_size = round_up_to_Page(instances_capacity * 20)

ext_net = 0
int_net = 1

FILTER_ACTION_ALLOW = 1
FILTER_ACTION_DROP = 2
FILTER_ACTION_CONNECT = 3

def ip_to_int(ipString: str):
    ipaddress.IPv4Address(ipString)
     
    # Switch little to big endian
    ipSplit = ipString.split(".")
    ipSplit.reverse()
    reversedIp = ".".join(ipSplit)
    return int(ipaddress.IPv4Address(reversedIp))

macs = [
    [0x00, 0x01, 0xc0, 0x39, 0xd5, 0x15], # External network, ETH1
    [0x00, 0x01, 0xc0, 0x39, 0xd5, 0x1d] # Internal network, ETH2
]

ips = [
    ip_to_int("172.16.2.1"), # 16912556
    ip_to_int("192.168.33.1") # 18983104
]

arp_responder_protocol = 0x92
arp_requester_protocol = 0x93

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
        paddr_top=0x60_000_000,
        serial="pl011@9000000",
        timer="timer",
        ethernet0="virtio_mmio@a003e00",
        ethernet1="virtio_mmio@a002300"
    ),
    Board(
        name="imx8mp_evk",
        arch=SystemDescription.Arch.AARCH64,
        paddr_top=0x70_000_000,
        serial="soc@0/bus@30800000/spba-bus@30800000/serial@30890000",
        timer="soc@0/bus@30000000/timer@302d0000",
        ethernet0="soc@0/bus@30800000/ethernet@30be0000", #IMX
        ethernet1="soc@0/bus@30800000/ethernet@30bf0000" #DWMAC
    ),
]

# Create a firewall connection, which is a single queue and a channel. Data must be created and mapped separately
def firewall_connection(pd1: SystemDescription.ProtectionDomain , pd2: SystemDescription.ProtectionDomain, capacity: int, region_size: int):
    queue_name = "firewall_queue_" + pd1.name + "_" + pd2.name
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

    pd1_conn = FirewallConnectionResource(pd1_region, capacity, ch.pd_a_id)
    pd2_conn = FirewallConnectionResource(pd2_region, capacity, ch.pd_b_id)

    return [pd1_conn, pd2_conn]

# Map a mr into a pd to create a firewall region
def firewall_region(pd: SystemDescription.ProtectionDomain, mr: SystemDescription.MemoryRegion, perms: str, region_size: int):
    pd_map = Map(mr, pd.get_map_vaddr(mr), perms=perms)
    pd.add_map(pd_map)
    region_resource = RegionResource(pd_map.vaddr, region_size)
    
    return region_resource

# Map a physical mr into a pd to create a firewall device region
def firewall_device_region(pd: SystemDescription.ProtectionDomain, mr: SystemDescription.MemoryRegion, perms: str):
    region = firewall_region(pd, mr, perms, mr.size)
    device_region = DeviceRegionResource(
            region,
            mr.paddr.value
        )
    return device_region

# Create a firewall connection and map a physical mr to create a firewall data connection
def firewall_data_connection(pd1: SystemDescription.ProtectionDomain , pd2: SystemDescription.ProtectionDomain, 
                             capacity: int, queue_size: int, data: SystemDescription.MemoryRegion, 
                             data_perms1: str, data_perms2: str):
    connection = firewall_connection(pd1, pd2, capacity, queue_size)
    data_region1 = firewall_device_region(pd1, data, data_perms1)
    data_region2 = firewall_device_region(pd2, data, data_perms2)

    data_connection1 = FirewallDataConnectionResource(
        connection[0],
        data_region1
    )
    data_connection2 = FirewallDataConnectionResource(
        connection[1],
        data_region2
    )

    return [data_connection1, data_connection2]


# Create a shared memory region between two pds from a mr
def firewall_shared_region(pd1: SystemDescription.ProtectionDomain , pd2: SystemDescription.ProtectionDomain, 
                           perms1: str, perms2: str, name_prefix: str, region_size: int):
    # Create rule memory region
    region_name = name_prefix + "_" + pd1.name + "_" + pd2.name
    mr = MemoryRegion(sdf, region_name, region_size)
    sdf.add_mr(mr)

    # Map rule into pd1
    region1 = firewall_region(pd1, mr, perms1, region_size)

    # Map rule memory region into webserver
    region2 = firewall_region(pd2, mr, perms2, region_size)

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
            "mac": macs[i],
            "ip": ips[i],
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
    networks[ext_net]["driver"] = ProtectionDomain("ethernet_driver_dwmac", "eth_driver_dwmac.elf", priority=101, budget=100, period=400)
    networks[int_net]["out_virt"] = ProtectionDomain("net_virt_tx0", "firewall_network_virt_tx0.elf", priority=100, budget=20000)
    networks[ext_net]["in_virt"] = ProtectionDomain("net_virt_rx0", "firewall_network_virt_rx0.elf", priority=99)

    networks[ext_net]["rx_dma_region"] = MemoryRegion(sdf, "rx_dma_region0", dma_region_size, physical=True)
    sdf.add_mr(networks[ext_net]["rx_dma_region"])

    # Create network 1 subsystem pds
    networks[int_net]["driver"] = ProtectionDomain("ethernet_driver_imx", "eth_driver_imx.elf", priority=101, budget=100, period=400)
    networks[ext_net]["out_virt"] = ProtectionDomain("net_virt_tx1", "firewall_network_virt_tx1.elf", priority=100, budget=20000)
    networks[int_net]["in_virt"] = ProtectionDomain("net_virt_rx1", "firewall_network_virt_rx1.elf", priority=99)

    networks[int_net]["rx_dma_region"] = MemoryRegion(sdf, "rx_dma_region1", dma_region_size, physical=True)
    sdf.add_mr(networks[int_net]["rx_dma_region"])

    # Create network subsystems
    networks[ext_net]["in_net"] = Sddf.Net(sdf, ethernet_node1, networks[ext_net]["driver"], networks[int_net]["out_virt"], networks[ext_net]["in_virt"], networks[ext_net]["rx_dma_region"])
    networks[int_net]["out_net"] = networks[ext_net]["in_net"]


    networks[int_net]["in_net"] = Sddf.Net(sdf, ethernet_node0, networks[int_net]["driver"], networks[ext_net]["out_virt"], networks[int_net]["in_virt"], networks[int_net]["rx_dma_region"])
    networks[ext_net]["out_net"] = networks[int_net]["in_net"]

    # Create firewall pds
    networks[ext_net]["router"] = ProtectionDomain("routing_external", "routing_external.elf", priority=97, budget=20000)
    networks[int_net]["router"] = ProtectionDomain("routing_internal", "routing_internal.elf", priority=94, budget=20000)

    networks[ext_net]["arp_resp"] = ProtectionDomain("arp_responder0", "arp_responder0.elf", priority=95, budget=20000)
    networks[int_net]["arp_resp"] = ProtectionDomain("arp_responder1", "arp_responder1.elf", priority=93, budget=20000)

    networks[ext_net]["arp_req"] = ProtectionDomain("arp_requester0", "arp_requester0.elf", priority=98, budget=20000)
    networks[int_net]["arp_req"] = ProtectionDomain("arp_requester1", "arp_requester1.elf", priority=95, budget=20000)

    # Create the webserver component
    webserver = ProtectionDomain("micropython", "micropython.elf", priority=1, budget=20000)
    common_pds.append(webserver)

    # Webserver is a serial and timer client
    serial_system.add_client(webserver)
    timer_system.add_client(webserver)

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

    # Initial network loop to maintain client ordering consistency across networks
    for network in networks:
        # Add all pds to the system
        for maybe_pd in network.values():
            if type(maybe_pd) == ProtectionDomain:
                # Drivers and routers do not need to be copied
                if maybe_pd != network["driver"] and maybe_pd != network["router"]:
                    # remove x.elf suffix from elf
                    copy_elf(maybe_pd.elf[:-5], maybe_pd.elf[:-5], network["num"])
                sdf.add_pd(maybe_pd)

        for filter_pd in network["filters"].values():
            copy_elf(filter_pd.elf[:-5], filter_pd.elf[:-5], network["num"])
            sdf.add_pd(filter_pd)

        # Ensure arp requester is client 0 for each network
        network["out_net"].add_client_with_copier(network["arp_req"])

    # Webserver is a tx client of the internal network
    networks[int_net]["in_net"].add_client_with_copier(webserver, rx=False)

    # Webserver receives traffic from the internal -> external router
    router_webserver_conn = firewall_connection(networks[int_net]["router"], webserver, dma_queue_capacity, dma_queue_region_size)

    # Webserver returns packets to interior rx virtualiser
    webserver_in_virt_conn = firewall_connection(webserver, networks[int_net]["in_virt"], dma_queue_capacity, dma_queue_region_size)

    # Webserver needs access to rx dma region
    webserver_data_region = firewall_region(webserver, networks[int_net]["rx_dma_region"], "rw", dma_queue_region_size)

    # Webserver has arp channel for arp requests/responses
    webserver_arp_conn = firewall_connection(webserver, networks[ext_net]["arp_req"], arp_queue_capacity, arp_queue_region_size)

    # Create webserver config
    webserver_config = FirewallWebserverConfig(
        network["mac"],
        [],
        network["ip"],
        router_webserver_conn[1],
        webserver_data_region,
        [],
        webserver_in_virt_conn[0],
        webserver_arp_conn[0],
        [],
        filter_rule_capacity
    )

    for network in networks:
        router = network["router"]
        out_virt = network["out_virt"]
        in_virt = network["in_virt"]
        arp_req = network["arp_req"]
        arp_resp = network["arp_resp"]

        # Create a firewall data connection between router and output virt with the rx dma region as data region
        router_out_virt_conn = firewall_data_connection(router, out_virt, dma_queue_capacity, dma_queue_region_size, 
                                                        network["rx_dma_region"], "rw", "r")

        # Create a firewall connection for output virt to return buffers to input virt
        output_in_virt_conn = firewall_connection(out_virt, in_virt, dma_queue_capacity, dma_queue_region_size)
        out_virt_in_virt_data_conn = FirewallDataConnectionResource(
            output_in_virt_conn[0],
            router_out_virt_conn[1].data
        )

        # Create output virt config
        network["configs"][out_virt] = FirewallNetVirtTxConfig(
            [router_out_virt_conn[1]],
            [out_virt_in_virt_data_conn]
        )

        # Create a firewall connection for router to return free buffers to receive virtualiser on interior network
        router_in_virt_conn = firewall_connection(router, in_virt, dma_queue_capacity, dma_queue_region_size)

        # Create input virt config
        network["configs"][in_virt] = FirewallNetVirtRxConfig(
            [],
            [router_in_virt_conn[1], output_in_virt_conn[1]]
        )

        # Add arp requester protocol for input virt client 0 - this is for the previously added arp requester which is always client 0
        network["configs"][in_virt].active_client_protocols.append(arp_requester_protocol)

        # Arp requester needs timer access to handle arp timeouts
        timer_system.add_client(arp_req)

        # Add arp responder filter pd as a network client
        network["in_net"].add_client_with_copier(arp_resp)
        network["configs"][in_virt].active_client_protocols.append(arp_responder_protocol)

        # Create arp queue firewall connection
        router_arp_conn = firewall_connection(router, arp_req, arp_queue_capacity, arp_queue_region_size)

        # Create arp cache
        arp_cache = firewall_shared_region(arp_req, router, "rw", "r", "arp_cache", arp_cache_region_size)

        # Create arp req config
        network["configs"][arp_req] = FirewallArpRequesterConfig(
            network["mac"],
            network["ip"],
            [router_arp_conn[1]],
            arp_cache[0],
            arp_cache_capacity
        )

        # Create arp resp config
        network["configs"][arp_resp] = FirewallArpResponderConfig(
            network["mac"],
            network["ip"]
        )

        # Create arp packet queue
        arp_packet_queue_mr = MemoryRegion(sdf, "arp_packet_queue_" + router.name, arp_packet_queue_region_size)
        sdf.add_mr(arp_packet_queue_mr)
        arp_packet_queue = firewall_region(router, arp_packet_queue_mr, "rw", arp_packet_queue_region_size)

        # Create routing table
        routing_table = firewall_shared_region(router, webserver, "rw", "r", "routing_table", routing_table_size)

        # Create pp channel for routing table updates
        router_update_ch = Channel(webserver, router, pp_a=True)
        sdf.add_channel(router_update_ch)

        # Create router webserver config
        router_webserver_config = FirewallWebserverRouterConfig(
            router_update_ch.pd_b_id,
            routing_table[0],
            routing_table_capacity
        )

        webserver_router_config = FirewallWebserverRouterConfig(
            router_update_ch.pd_a_id,
            routing_table[1],
            routing_table_capacity
        )

        webserver_config.routers.append(webserver_router_config)

        # Create router config
        network["configs"][router] = FirewallRouterConfig(
            network["mac"],
            network["ip"],
            router_in_virt_conn[0],
            None,
            router_out_virt_conn[0].conn,
            router_out_virt_conn[0].data.region,
            router_arp_conn[0],
            arp_cache[1],
            arp_cache_capacity,
            arp_packet_queue,
            router_webserver_config,
            []
        )

        for (protocol, filter_pd) in network["filters"].items():
            # Create a firewall connection for filter to transmit buffers to router
            filter_router_conn = firewall_connection(filter_pd, router, dma_queue_capacity, dma_queue_region_size)

            # Connect filter as rx only network client
            network["in_net"].add_client_with_copier(filter_pd, tx=False)
            network["configs"][in_virt].active_client_protocols.append(protocol)

            # Create rule region
            filter_rules = firewall_shared_region(filter_pd, webserver, "rw", "r", "filter_rules", filter_rule_region_size)

            # Create pp channel between webserver and filter for rule updates
            filter_update_ch = Channel(webserver, filter_pd, pp_a = True)
            sdf.add_channel(filter_update_ch)

            # Create webserver configs
            filter_webserver_config = FirewallWebserverFilterConfig(
                protocol,
                filter_update_ch.pd_b_id,
                FILTER_ACTION_ALLOW,
                filter_rules[0]
            )

            webserver_filter_config = FirewallWebserverFilterConfig(
                protocol,
                filter_update_ch.pd_a_id,
                FILTER_ACTION_ALLOW,
                filter_rules[1]
            )

            # Create filter config
            network["configs"][filter_pd] = FirewallFilterConfig(
                network["mac"],
                network["ip"],
                filter_rule_capacity,
                instances_capacity,
                filter_router_conn[0],
                filter_webserver_config,
                None,
                None
            )

            network["configs"][router].filters.append((filter_router_conn[1]))
            webserver_config.filters.append(webserver_filter_config)
            webserver_config.filter_iface_id.append(network["num"])

        # Make router and arp components serial clients
        serial_system.add_client(router)
        serial_system.add_client(arp_req)
        serial_system.add_client(arp_resp)

        network["in_net"].connect()
        network["in_net"].serialise_config(network["out_dir"])


    # Add webserver as a free client of interior rx virt
    networks[int_net]["configs"][networks[int_net]["in_virt"]].free_clients.append(webserver_in_virt_conn[1])

    # Add webserver as an arp requester client outputting to the internal network
    networks[ext_net]["configs"][networks[ext_net]["arp_req"]].arp_clients.append(webserver_arp_conn[1])

    # Add a firewall connection to the webserver from the internal router for packet transmission
    networks[int_net]["configs"][networks[int_net]["router"]].rx_active = router_webserver_conn[0]

    # Create filter instance regions
    for (protocol, filter_pd) in networks[int_net]["filters"].items():
        mirror_filter = networks[ext_net]["filters"][protocol]
        int_instances = firewall_shared_region(filter_pd, mirror_filter, "rw", "r", "instances", instances_region_size)
        ext_instances = firewall_shared_region(mirror_filter, filter_pd, "rw", "r", "instances", instances_region_size)

        networks[int_net]["configs"][filter_pd].internal_instances = int_instances[0]
        networks[int_net]["configs"][filter_pd].external_instances = ext_instances[1]
        networks[ext_net]["configs"][mirror_filter].internal_instances = ext_instances[0]
        networks[ext_net]["configs"][mirror_filter].external_instances = int_instances[1]

    assert serial_system.connect()
    assert serial_system.serialise_config(output_dir)
    assert timer_system.connect()
    assert timer_system.serialise_config(output_dir)

    for network in networks:
        for pd, config in network["configs"].items():

            data_path = network["out_dir"] + "/firewall_config_" + pd.name + ".data"
            with open(data_path, "wb+") as f:
                f.write(config.serialise())
            update_elf_section(obj_copy, pd.elf, config.section_name, data_path)

    data_path = output_dir + "/firewall_config_webserver.data"
    with open(data_path, "wb+") as f:
        f.write(webserver_config.serialise())
    update_elf_section(obj_copy, webserver.elf, webserver_config.section_name, data_path)

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

    args = parser.parse_args()

    board = next(filter(lambda b: b.name == args.board, BOARDS))

    sdf = SystemDescription(board.arch, board.paddr_top)
    sddf = Sddf(args.sddf)

    global obj_copy
    obj_copy = args.objcopy

    with open(args.dtb, "rb") as f:
        dtb = DeviceTree(f.read())

    generate(args.sdf, args.output, dtb)
