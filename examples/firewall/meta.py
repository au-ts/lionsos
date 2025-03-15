# Copyright 2025, UNSW
# SPDX-License-Identifier: BSD-2-Clause
import argparse
import struct
import subprocess
import shutil
from os import path
from dataclasses import dataclass
from typing import List
from sdfgen import SystemDescription, Sddf, DeviceTree

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
arp_queue_region_size = round_up_to_Page(2 * (4 + 12 * arp_queue_capacity) + 4)

arp_cache_entries = 512
arp_cache_region_size = round_up_to_Page(8 * arp_cache_entries)

arp_packet_queue_region_size = round_up_to_Page(dma_queue_capacity * dma_buffer_size)

macs = [
    [0x00, 0x01, 0xc0, 0x39, 0xd5, 0x10], # External network, ETH1
    [0x00, 0x01, 0xc0, 0x39, 0xd5, 0x18] # Internal network, ETH2
]

ips = [
    3322416394, # 10.13.8.198
    2271424 # 192.168.34.0
]

arp_responder_protocol = 0x92
arp_requester_protocol = 0x93

# Global vaddr allocator... TODO: Improve this in future
vaddr = 0x3_000_000
def next_vaddr() -> int:
    global vaddr
    vaddr_curr = vaddr
    vaddr += 0x400_000
    return vaddr_curr

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
        ethernet0="virtio_mmio@a003e00",
        ethernet1="virtio_mmio@a002300"
    ),
    Board(
        name="imx8mp_evk",
        arch=SystemDescription.Arch.AARCH64,
        paddr_top=0x70000000,
        serial="soc@0/bus@30800000/spba-bus@30800000/serial@30890000",
        timer="soc@0/bus@30000000/timer@302d0000",
        ethernet0="soc@0/bus@30800000/ethernet@30be0000",
        ethernet1="soc@0/bus@30800000/ethernet@30bf0000"
    ),
]

class FirewallRegionResource:
    def __init__(self, vaddr: int, size: int):
        self.vaddr = vaddr
        self.size = size
    def serialise(self) -> bytes:
        pack_str = "<QQ"
        return struct.pack(pack_str, self.vaddr, self.size)

class FirewallDeviceRegionResource:
    def __init__(self, region: FirewallRegionResource, io_addr: int):
        self.region = region
        self.io_addr = io_addr
    def serialise(self) -> bytes:
        region_bytes = self.region.serialise()
        pack_str = "<Q"
        return region_bytes + struct.pack(pack_str, self.io_addr)

class FirewallConnectionResource:
    def __init__(self, queue: FirewallRegionResource, capacity: int, ch: int):
        self.queue = queue
        self.capacity = capacity
        self.ch = ch
    def serialise(self) -> bytes:
        queue_bytes = self.queue.serialise()
        pack_str = "<HBxxxxx"
        return queue_bytes + struct.pack(pack_str, self.capacity, self.ch)

class FirewallDataConnectionResource:
    def __init__(self, conn: FirewallConnectionResource, data: FirewallDeviceRegionResource):
        self.conn = conn
        self.data = data
    def serialise(self) -> bytes:
        '''
        QQHBxxxxxQQQ
        '''
        return self.conn.serialise() + self.data.serialise()

class FirewallNetVirtTxConfig:
    def __init__(self, active_clients: List[FirewallDataConnectionResource], free_clients: List[FirewallDataConnectionResource]):
        self.active_clients = active_clients
        self.free_clients = free_clients
        self.section_name = "firewall_net_virt_tx_config"
    def serialise(self) -> bytes:
        client_bytes = bytes()
        for client in self.active_clients:
            ''' QQHBxxxxxQQQ '''
            client_bytes += client.serialise()
        client_bytes = client_bytes.ljust(61 * 6 * 8, b'\0')
        for client in self.free_clients:
            ''' QQHBxxxxxQQQ '''
            client_bytes += client.serialise()
        client_bytes = client_bytes.ljust(2 * 61 * 6 * 8, b'\0')
        pack_str = "<BB"
        return client_bytes + struct.pack(pack_str, len(self.active_clients), len(self.free_clients))

class FirewallNetVirtRxConfig:
    def __init__(self, active_client_protocols: List[int], free_clients: List[FirewallConnectionResource]):
        self.active_client_protocols = active_client_protocols
        self.free_clients = free_clients
        self.section_name = "firewall_net_virt_rx_config"
    def serialise(self) -> bytes:
        protocol_bytes = bytes()
        for protocol in self.active_client_protocols:
            protocol_bytes += struct.pack("<H", protocol)
        protocol_bytes = protocol_bytes.ljust(61 * 2 + 6, b'\0')
        client_bytes = bytes()
        for client in self.free_clients:
            ''' QQHBxxxxx '''
            client_bytes += client.serialise()
        client_bytes = client_bytes.ljust(24 * 61, b'\0')
        pack_str = "<B"
        return protocol_bytes + client_bytes + struct.pack(pack_str, len(self.free_clients))

class FirewallArpRouterConnectionResource:
    def __init__(self, arp_queue: FirewallConnectionResource, arp_cache: FirewallRegionResource):
        self.arp_queue = arp_queue
        self.arp_cache = arp_cache
    def serialise(self) -> bytes:
        queue_bytes = self.arp_queue.serialise()
        cache_bytes = self.arp_cache.serialise()
        '''
        QQHBxxxxxQQ
        '''
        return queue_bytes + cache_bytes

class FirewallRouterConfig:
    def __init__(self, mac_addr: List[int], rx_free: FirewallDataConnectionResource, tx_active: FirewallConnectionResource,
                 arp: FirewallArpRouterConnectionResource, packet_queue: FirewallRegionResource,
                 filters: List[FirewallConnectionResource]):
        self.mac_addr = mac_addr
        self.rx_free = rx_free
        self.tx_active = tx_active
        self.arp = arp
        self.packet_queue = packet_queue
        self.filters = filters
        self.section_name = "firewall_router_config"
    def serialise(self) -> bytes:
        rx_free_bytes = self.rx_free.serialise()
        tx_free_bytes = self.tx_active.serialise()
        arp_bytes = self.arp.serialise()
        packet_bytes = self.packet_queue.serialise()
        filter_bytes = bytes()
        for filter_conn in self.filters:
            ''' QQHBxxxxx '''
            filter_bytes += filter_conn.serialise()
        filter_bytes = filter_bytes.ljust(24 * 61, b'\0')
        mac_bytes = bytes()
        for m in self.mac_addr:
            mac_bytes += struct.pack("<B", m)
        num_filter_bytes = struct.pack("<H", len(self.filters))
        return rx_free_bytes + tx_free_bytes + arp_bytes + packet_bytes + filter_bytes + mac_bytes + num_filter_bytes

class FirewallArpRequesterConfig:
    def __init__(self, router: FirewallConnectionResource, mac_addr: List[int], ip: int):
        self.router = router
        self.mac_addr = mac_addr
        self.ip = ip
        self.section_name = "firewall_arp_requester_config"
    def serialise(self) -> bytes:
        mac_bytes = bytes()
        for m in self.mac_addr:
            mac_bytes += struct.pack("<B", m)
        mac_bytes = mac_bytes.ljust(8, b'\0')
        ip_bytes = struct.pack("<L", self.ip)
        return self.router.serialise() + mac_bytes + ip_bytes

class FirewallArpResponderConfig:
    def __init__(self, mac_addr: List[int], ip: int):
        self.mac_addr = mac_addr
        self.ip = ip
        self.section_name = "firewall_arp_responder_config"
    def serialise(self) -> bytes:
        mac_bytes = bytes()
        for m in self.mac_addr:
            mac_bytes += struct.pack("<B", m)
        mac_bytes = mac_bytes.ljust(8, b'\0')
        return mac_bytes + struct.pack("<L", self.ip)

class FirewallFilterConfig:
    def __init__(self, mac_addr: List[int], protocol: int, router: FirewallConnectionResource):
        self.mac_addr = mac_addr
        self.router = router
        self.protocol = protocol
        self.section_name = "firewall_filter_config"
    def serialise(self) -> bytes:
        mac_bytes = bytes()
        for m in self.mac_addr:
            mac_bytes += struct.pack("<B", m)
        router_bytes = self.router.serialise()
        pack_str = "<H"
        return mac_bytes + struct.pack(pack_str, self.protocol) + router_bytes

# Creates a new elf with elf_number as prefix. Adds ".elf" to elf strings
def copy_elf(source_elf: str, new_elf: str, elf_number = None):
    source_elf += ".elf"
    if elf_number != None:
        new_elf += str(elf_number)
    new_elf += ".elf"
    assert path.isfile(source_elf)
    return shutil.copyfile(source_elf, new_elf)

# Copiers data region data_name into section_name of elf_name
def update_elf_section(elf_name: str, section_name: str, data_name: str):
    assert path.isfile(elf_name)
    assert path.isfile(data_name)
    assert subprocess.run([obj_copy, "--update-section", "." + section_name + "=" + data_name, elf_name]).returncode == 0

# Create a firewall connection, which is a single queue and a channel. Data must be created and mapped separately
def firewall_connection(pd1: SystemDescription.ProtectionDomain , pd2: SystemDescription.ProtectionDomain, capacity: int, region_size: int):
    queue_name = "firewall_queue_" + pd1.name + "_" + pd2.name
    queue = MemoryRegion(queue_name, region_size)
    sdf.add_mr(queue)

    pd1_vaddr = next_vaddr()
    pd1.add_map(Map(queue, pd1_vaddr, perms="rw"))
    pd1_region = FirewallRegionResource(pd1_vaddr, region_size)

    pd2_vaddr = next_vaddr()
    pd2.add_map(Map(queue, pd2_vaddr, perms="rw"))
    pd2_region = FirewallRegionResource(pd2_vaddr, region_size)

    ch = Channel(pd1, pd2)
    sdf.add_channel(ch)

    pd1_conn = FirewallConnectionResource(pd1_region, capacity, ch.pd_a_id)
    pd2_conn = FirewallConnectionResource(pd2_region, capacity, ch.pd_b_id)

    return [pd1_conn, pd2_conn]

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
            "configs":{}
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
    networks[0]["driver"] = ProtectionDomain("ethernet_driver_imx", "eth_driver_imx.elf", priority=101, budget=100, period=400)
    networks[1]["out_virt"] = ProtectionDomain("net_virt_tx0", "firewall_network_virt_tx0.elf", priority=100, budget=20000)
    networks[0]["in_virt"] = ProtectionDomain("net_virt_rx0", "firewall_network_virt_rx0.elf", priority=99)

    networks[0]["rx_dma_region"] = MemoryRegion("rx_dma_region0", dma_region_size, paddr = 0x50_000_000)
    sdf.add_mr(networks[0]["rx_dma_region"])

    # Create network 1 subsystem pds
    networks[1]["driver"] = ProtectionDomain("ethernet_driver_dwmac", "eth_driver_dwmac.elf", priority=101, budget=100, period=400)
    networks[0]["out_virt"] = ProtectionDomain("net_virt_tx1", "firewall_network_virt_tx1.elf", priority=100, budget=20000)
    networks[1]["in_virt"] = ProtectionDomain("net_virt_rx1", "firewall_network_virt_rx1.elf", priority=99)

    # Create network subsystems
    networks[0]["in_net"] = Sddf.Net(sdf, ethernet_node0, networks[0]["driver"], networks[1]["out_virt"], networks[0]["in_virt"], networks[0]["rx_dma_region"])
    networks[1]["out_net"] = networks[0]["in_net"]

    networks[1]["rx_dma_region"] = MemoryRegion("rx_dma_region1", dma_region_size, paddr = 0x55_000_000)
    sdf.add_mr(networks[1]["rx_dma_region"])

    networks[1]["in_net"] = Sddf.Net(sdf, ethernet_node1, networks[1]["driver"], networks[0]["out_virt"], networks[1]["in_virt"], networks[1]["rx_dma_region"])
    networks[0]["out_net"] = networks[1]["in_net"]

    # Create firewall pds
    networks[0]["router"] = ProtectionDomain("routing0", "routing0.elf", priority=97, budget=20000)
    networks[1]["router"] = ProtectionDomain("routing1", "routing1.elf", priority=94, budget=20000)

    networks[0]["arp_resp"] = ProtectionDomain("arp_responder0", "arp_responder0.elf", priority=95, budget=20000)
    networks[1]["arp_resp"] = ProtectionDomain("arp_responder1", "arp_responder1.elf", priority=93, budget=20000)

    networks[0]["arp_req"] = ProtectionDomain("arp_requester0", "arp_requester0.elf", priority=98, budget=20000)
    networks[1]["arp_req"] = ProtectionDomain("arp_requester1", "arp_requester1.elf", priority=95, budget=20000)

    # Internal arp needs timer to handle stale ARP cache entries.
    timer_system.add_client(networks[1]["arp_req"])

    networks[0]["filters"] = {}
    networks[0]["filters"][0x01] = ProtectionDomain("icmp_filter0", "icmp_filter0.elf", priority=90, budget=20000)
    networks[0]["filters"][0x11] = ProtectionDomain("udp_filter0", "udp_filter0.elf", priority=91, budget=20000)
    networks[0]["filters"][0x06] = ProtectionDomain("tcp_filter0", "tcp_filter0.elf", priority=92, budget=20000)

    networks[1]["filters"] = {}
    networks[1]["filters"][0x01] = ProtectionDomain("icmp_filter1", "icmp_filter1.elf", priority=93, budget=20000)

    for pd in common_pds:
        sdf.add_pd(pd)

    # Initial network loop to maintain client ordering consistency across networks
    for network in networks:
        # Add all pds to the system
        for maybe_pd in network.values():
            if type(maybe_pd) == ProtectionDomain:
                # Drivers do not need to be copied
                if maybe_pd != network["driver"]:
                    # remove x.elf suffix from elf
                    copy_elf(maybe_pd.elf[:-5], maybe_pd.elf[:-5], network["num"])
                sdf.add_pd(maybe_pd)

        for filter_pd in network["filters"].values():
            copy_elf(filter_pd.elf[:-5], filter_pd.elf[:-5], network["num"])
            sdf.add_pd(filter_pd)

        # Ensure arp requester is client 0 for each network
        network["out_net"].add_client_with_copier(network["arp_req"])

    for network in networks:
        router = network["router"]
        out_virt = network["out_virt"]
        in_virt = network["in_virt"]
        arp_req = network["arp_req"]
        arp_resp = network["arp_resp"]

        # Map the interior rx dma region into router
        router_rx_dma_vaddr = next_vaddr()
        router.add_map(Map(network["rx_dma_region"], router_rx_dma_vaddr, perms="rw"))
        router_rx_dma_region = FirewallRegionResource(
            router_rx_dma_vaddr,
            dma_region_size
        )
        router_rx_dma_device_region = FirewallDeviceRegionResource(
            router_rx_dma_region,
            network["rx_dma_region"].paddr.value
        )

        # Map the interior rx dma region into output virtualiser
        out_virt_rx_dma_vaddr = next_vaddr()
        out_virt.add_map(Map(network["rx_dma_region"], out_virt_rx_dma_vaddr, perms="r"))
        out_virt_data_region = FirewallRegionResource(
            out_virt_rx_dma_vaddr,
            dma_region_size
        )
        out_virt_device_data_region = FirewallDeviceRegionResource(
            out_virt_data_region,
            network["rx_dma_region"].paddr.value
        )

        # Create a firewall connection for router to transmit to virtualiser on exterior network
        router_out_virt_conn = firewall_connection(router, out_virt, dma_queue_capacity, dma_queue_region_size)
        out_virt_router_data_conn = FirewallDataConnectionResource(
            router_out_virt_conn[1],
            out_virt_device_data_region
        )

        # Create a firewall connection for output virt to return buffers to input virt
        output_in_virt_conn = firewall_connection(out_virt, in_virt, dma_queue_capacity, dma_queue_region_size)
        out_virt_in_virt_data_conn = FirewallDataConnectionResource(
            output_in_virt_conn[0],
            out_virt_device_data_region
        )
    
        # Create output virt config
        network["configs"][out_virt] = FirewallNetVirtTxConfig(
            [out_virt_router_data_conn],
            [out_virt_in_virt_data_conn]
        )

        # Create a firewall connection for router to return free buffers to receive virtualiser on interior network
        router_in_virt_conn = firewall_connection(router, in_virt, dma_queue_capacity, dma_queue_region_size)
        router_in_virt_data_conn = FirewallDataConnectionResource(
            router_in_virt_conn[0],
            router_rx_dma_device_region
        )

        # Create input virt config
        network["configs"][in_virt] = FirewallNetVirtRxConfig(
            [],
            [router_in_virt_conn[1], output_in_virt_conn[1]]
        )

        # Add arp requester protocol for input virt client 0 - this is the same across networks
        network["configs"][in_virt].active_client_protocols.append(arp_requester_protocol)

        # Add arp responder filter pd as a network client
        network["in_net"].add_client_with_copier(arp_resp)
        network["configs"][in_virt].active_client_protocols.append(arp_responder_protocol)

        # Create arp queue firewall connection
        arp_queue_conn = firewall_connection(router, arp_req, arp_queue_capacity, arp_queue_region_size)

        # Create arp cache
        arp_cache = MemoryRegion("arp_cache_" + router.name + "_" + arp_req.name, arp_cache_region_size)
        sdf.add_mr(arp_cache)

        router_arp_cache_vaddr = next_vaddr()
        router.add_map(Map(arp_cache, router_arp_cache_vaddr, perms="r"))
        router_arp_cache_region = FirewallRegionResource(router_arp_cache_vaddr, arp_cache_region_size)
        router_arp_conn = FirewallArpRouterConnectionResource(
            arp_queue_conn[0],
            router_arp_cache_region
        )

        arp_req_arp_cache_vaddr = next_vaddr()
        arp_req.add_map(Map(arp_cache, arp_req_arp_cache_vaddr, perms="rw"))
        arp_req_arp_cache_region = FirewallRegionResource(arp_req_arp_cache_vaddr, arp_cache_region_size)
        arp_router_conn = FirewallArpRouterConnectionResource(
            arp_queue_conn[1],
            arp_req_arp_cache_region
        )

        # Create arp req config
        network["configs"][arp_req] = FirewallArpRequesterConfig(
            arp_router_conn,
            network["mac"],
            network["ip"],
        )

        # Create arp resp config
        network["configs"][arp_resp] = FirewallArpResponderConfig(
            network["mac"],
            network["ip"]
        )

        # Create arp packet queue
        arp_packet_queue = MemoryRegion("arp_packet_queue_" + router.name, arp_packet_queue_region_size)
        sdf.add_mr(arp_packet_queue)

        router_arp_packet_queue_vaddr = next_vaddr()
        router.add_map(Map(arp_packet_queue, router_arp_packet_queue_vaddr, perms="rw"))
        arp_packet_queue_region = FirewallRegionResource(router_arp_packet_queue_vaddr, arp_packet_queue_region_size)

        # Create router config
        network["configs"][router] = FirewallRouterConfig(
            network["mac"],
            router_in_virt_data_conn,
            router_out_virt_conn[0],
            router_arp_conn,
            arp_packet_queue_region,
            []
        )

        for (protocol, filter_pd) in network["filters"].items():
            # Create a firewall connection for filter to transmit buffers to router
            filter_router_conn = firewall_connection(filter_pd, router, dma_queue_capacity, dma_queue_region_size)

            # Connect filter as rx only network client
            network["in_net"].add_client_with_copier(filter_pd, tx=False)
            network["configs"][in_virt].active_client_protocols.append(protocol)

            # Create filter config
            network["configs"][filter_pd] = FirewallFilterConfig(
                network["mac"],
                protocol,
                filter_router_conn[0]
            )
            network["configs"][router].filters.append((filter_router_conn[1]))

        # Make router and arp components serial clients
        serial_system.add_client(router)
        serial_system.add_client(arp_req)
        serial_system.add_client(arp_resp)

        network["in_net"].connect()
        network["in_net"].serialise_config(network["out_dir"])

    assert serial_system.connect()
    assert serial_system.serialise_config(output_dir)
    assert timer_system.connect()
    assert timer_system.serialise_config(output_dir)

    for network in networks:
        for pd, config in network["configs"].items():

            data_path = network["out_dir"] + "/firewall_config_" + pd.name + ".data"
            with open(data_path, "wb+") as f:
                f.write(config.serialise())
            update_elf_section(pd.elf, config.section_name, data_path)

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
