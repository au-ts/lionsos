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
from ctypes import *

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

arp_cache_entries = 512
arp_cache_region_size = round_up_to_Page(24 * arp_cache_entries)

arp_packet_queue_region_size = round_up_to_Page(dma_queue_capacity * 24)

routing_table_capacity = 256
routing_table_size = round_up_to_Page(routing_table_capacity * 24)

filter_rule_capacity = 256
filter_rule_region_size = round_up_to_Page(filter_rule_capacity * 28)

instances_capacity = 512
instances_region_size = round_up_to_Page(instances_capacity * 20)

max_conns = 61

EXT_IDX = 0
INT_IDX = 1

FILTER_ACTION_ALLOW = 1
FILTER_ACTION_DROP = 2
FILTER_ACTION_CONNECT = 3

macs = [
    [0x00, 0x01, 0xc0, 0x39, 0xd5, 0x15], # External network, ETH1
    [0x00, 0x01, 0xc0, 0x39, 0xd5, 0x1d] # Internal network, ETH2
]

ips = [
    3338669322, # 10.13.0.199
    2205888 # 192.168.33.0
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

class RegionResourceStruct(LittleEndianStructure):
    _fields_ = [("vaddr", c_uint64),
                 ("size", c_uint64)]

class DeviceRegionResourceStruct(LittleEndianStructure):
    _fields_ = [("region", RegionResourceStruct),
                  ("io_addr", c_uint64)]

class FirewallConnectionResourceStruct(LittleEndianStructure):
    _fields_ = [("queue", RegionResourceStruct),
                 ("capacity", c_uint16),
                 ("ch", c_uint8)]

class FirewallDataConnectionResourceStruct(LittleEndianStructure):
    _fields_ = [("conn", FirewallConnectionResourceStruct),
                  ("data", DeviceRegionResourceStruct)]

class FirewallNetVirtTxConfigStruct(LittleEndianStructure):
    _fields_ = [("active_clients", FirewallDataConnectionResourceStruct * max_conns),
                  ("num_active_clients", c_uint8),
                  ("free_clients", FirewallDataConnectionResourceStruct * max_conns),
                  ("num_free_clients", c_uint8)]

class FirewallNetVirtRxConfigStruct(LittleEndianStructure):
    _fields_ = [("active_client_protocols", c_uint16 * max_conns),
                  ("free_clients", FirewallConnectionResourceStruct * max_conns),
                  ("num_free_clients", c_uint8)]

class FirewallArpRequesterConfigStruct(LittleEndianStructure):
    _fields_ = [("mac_addr", c_uint8 * 6),
                 ("ip", c_uint32),
                 ("clients", FirewallConnectionResourceStruct * 2),
                 ("num_arp_clients", c_uint8),
                 ("arp_cache", RegionResourceStruct),
                 ("arp_cache_capacity", c_uint16)]

class FirewallArpResponderConfigStruct(LittleEndianStructure):
    _fields_ = [("mac_addr", c_uint8 * 6),
                  ("ip", c_uint32)]

class FirewallWebserverRouterConfigStruct(LittleEndianStructure):
    _fields_ = [("routing_ch", c_uint8),
                  ("routing_table", RegionResourceStruct),
                  ("routing_table_capacity", c_uint16)]

class FirewallRouterConfigStruct(LittleEndianStructure):
    _fields_ = [("mac_addr", c_uint8 * 6),
                  ("ip", c_uint32),
                  ("rx_free", FirewallConnectionResourceStruct),
                  ("rx_active", FirewallConnectionResourceStruct),
                  ("tx_active", FirewallConnectionResourceStruct),
                  ("data", RegionResourceStruct),
                  ("arp_queue", FirewallConnectionResourceStruct),
                  ("arp_cache", RegionResourceStruct),
                  ("arp_cache_capacity", c_uint16),
                  ("packet_queue", RegionResourceStruct),
                  ("webserver", FirewallWebserverRouterConfigStruct),
                  ("filters", FirewallConnectionResourceStruct * max_conns),
                  ("num_filters", c_uint8)]

class FirewallWebserverFilterConfigStruct(LittleEndianStructure):
    _fields_ = [("protocol", c_uint16),
                  ("ch", c_uint8),
                  ("default_action", c_uint8),
                  ("rules", RegionResourceStruct)]

class FirewallFilterConfigStruct(LittleEndianStructure):
    _fields_ = [("mac_addr", c_uint8 * 6),
                  ("ip", c_uint32),
                  ("rules_capacity", c_uint16),
                  ("instances_capacity", c_uint16),
                  ("router", FirewallConnectionResourceStruct),
                  ("webserver", FirewallWebserverFilterConfigStruct),
                  ("internal_instances", RegionResourceStruct),
                  ("external_instances", RegionResourceStruct)]

class FirewallWebserverConfigStruct(LittleEndianStructure):
    _fields_ = [("mac_addr", c_uint8 * 6),
                  ("filter_iface_id", c_uint8 * (max_conns * 2)),
                  ("ip", c_uint32),
                  ("rx_active", FirewallConnectionResourceStruct),
                  ("data", RegionResourceStruct),
                  ("routers", FirewallWebserverRouterConfigStruct * 2),
                  ("rx_free", FirewallConnectionResourceStruct),
                  ("arp_queue", FirewallConnectionResourceStruct),
                  ("filters", FirewallWebserverFilterConfigStruct * (max_conns * 2)),
                  ("num_filters", c_uint8),
                  ("rules_capacity", c_uint16)]


class Serializable():
    def serialise(self):
        return bytes(self.to_struct())

# Largest element = 8
class FirewallRegionResource(Serializable):
    def __init__(self, vaddr: int, size: int):
        self.vaddr = vaddr
        self.size = size

    def to_struct(self) -> RegionResourceStruct:
        return RegionResourceStruct(self.vaddr, self.size)

# Largest element = 8
class FirewallDeviceRegionResource(Serializable):
    def __init__(self, region: FirewallRegionResource, io_addr: int):
        self.region = region
        self.io_addr = io_addr

    def to_struct(self) -> DeviceRegionResourceStruct:
        return DeviceRegionResourceStruct(self.region.to_struct(), self.io_addr)


# Largest element = 8
class FirewallConnectionResource(Serializable):
    def __init__(self, queue: FirewallRegionResource, capacity: int, ch: int):
        self.queue = queue
        self.capacity = capacity
        self.ch = ch

    def to_struct(self) -> FirewallConnectionResourceStruct:
        return FirewallConnectionResourceStruct(self.queue.to_struct(), self.capacity, self.ch)

# Largest element = 8
class FirewallDataConnectionResource(Serializable):
    def __init__(self, conn: FirewallConnectionResource, data: FirewallDeviceRegionResource):
        self.conn = conn
        self.data = data

    def to_struct(self) -> FirewallDataConnectionResourceStruct:
        return FirewallDataConnectionResourceStruct(self.conn.to_struct(), self.data.to_struct())

# Largest element = 8
class FirewallNetVirtTxConfig(Serializable):
    def __init__(self, active_clients: List[FirewallDataConnectionResource], free_clients: List[FirewallDataConnectionResource]):
        self.active_clients = active_clients
        self.free_clients = free_clients
        self.section_name = "firewall_net_virt_tx_config"

    def to_struct(self) -> FirewallNetVirtTxConfigStruct:
        c_active_clients = [x.to_struct() for x in self.active_clients] + ((max_conns - len(self.active_clients)) * [FirewallDataConnectionResourceStruct()])
        c_free_clients = [x.to_struct() for x in self.free_clients] + ((max_conns - len(self.free_clients)) * [FirewallDataConnectionResourceStruct()])
        return FirewallNetVirtTxConfigStruct(convert_to_c_array(FirewallDataConnectionResourceStruct, max_conns, self.active_clients), len(self.active_clients),
                                             convert_to_c_array(FirewallDataConnectionResourceStruct, max_conns, self.free_clients), len(self.free_clients))

# Largest element = 8
class FirewallNetVirtRxConfig(Serializable):
    def __init__(self, active_client_protocols: List[int], free_clients: List[FirewallConnectionResource]):
        self.active_client_protocols = active_client_protocols
        self.free_clients = free_clients
        self.section_name = "firewall_net_virt_rx_config"

    def to_struct(self) -> FirewallNetVirtRxConfigStruct:
        self.active_client_protocols += [0] * (max_conns - len(self.active_client_protocols))
        return FirewallNetVirtRxConfigStruct((c_uint16 * max_conns)(*self.active_client_protocols),
                                             convert_to_c_array(FirewallConnectionResourceStruct, max_conns, self.free_clients),
                                             len(self.free_clients))

# Largest element = 8
class FirewallArpRequesterConfig(Serializable):
    def __init__(self, mac_addr: List[int], ip: int, clients: List[FirewallConnectionResource], arp_cache: FirewallRegionResource):
        self.mac_addr = mac_addr
        self.ip = ip
        self.clients = clients
        self.arp_cache = arp_cache
        self.section_name = "firewall_arp_requester_config"

    def to_struct(self) -> FirewallArpRequesterConfigStruct:
        return FirewallArpRequesterConfigStruct(
            (c_uint8 * 6)(*self.mac_addr),
            self.ip,
            convert_to_c_array(FirewallConnectionResourceStruct, 2, self.clients),
            len(self.clients),
            self.arp_cache.to_struct(),
            arp_cache_entries
        )


# Largest element = 4
class FirewallArpResponderConfig(Serializable):
    def __init__(self, mac_addr: List[int], ip: int):
        self.mac_addr = mac_addr
        self.ip = ip
        self.section_name = "firewall_arp_responder_config"

    def to_struct(self) -> FirewallArpRequesterConfigStruct:
        return FirewallArpResponderConfigStruct((c_uint8 * 6)(*self.mac_addr), self.ip)


# Largest element = 8
class FirewallWebserverRouterConfig(Serializable):
    def __init__(self, routing_ch: int, routing_table: FirewallRegionResource):
        self.routing_ch = routing_ch
        self.routing_table = routing_table

    def to_struct(self) -> FirewallWebserverRouterConfigStruct:
        return FirewallWebserverRouterConfigStruct(self.routing_ch, self.routing_table.to_struct(), routing_table_capacity)

# Largest element = 8
class FirewallRouterConfig(Serializable):
    def __init__(self, mac_addr: List[int], ip: int, rx_free: FirewallConnectionResource, rx_active, tx_active: FirewallConnectionResource,
                 data: FirewallRegionResource, arp_queue: FirewallConnectionResource, arp_cache: FirewallRegionResource,
                 packet_queue: FirewallRegionResource, webserver: FirewallWebserverRouterConfig,
                 filters: List[FirewallConnectionResource]):
        self.mac_addr = mac_addr
        self.ip = ip
        self.rx_free = rx_free
        self.rx_active = rx_active
        self.tx_active = tx_active
        self.data = data
        self.arp_queue = arp_queue
        self.arp_cache = arp_cache
        self.packet_queue = packet_queue
        self.webserver = webserver
        self.filters = filters
        self.section_name = "firewall_router_config"

    def to_struct(self) -> FirewallRouterConfigStruct:
        c_filters = [x.to_struct() for x in self.filters] + ((max_conns - len(self.filters)) * [FirewallConnectionResourceStruct()])

        return FirewallRouterConfigStruct(
            (c_uint8 * 6)(*self.mac_addr),
            self.ip,
            self.rx_free.to_struct(),
            FirewallConnectionResourceStruct() if self.rx_active == None else self.rx_active.to_struct(),
            self.tx_active.to_struct(),
            self.data.to_struct(),
            self.arp_queue.to_struct(),
            self.arp_cache.to_struct(),
            arp_cache_entries,
            self.packet_queue.to_struct(),
            self.webserver.to_struct(),
            convert_to_c_array(FirewallConnectionResourceStruct, max_conns, self.filters),
            len(self.filters)
        )


# Largest element = 8
class FirewallWebserverFilterConfig(Serializable):
    def __init__(self, protocol: int, ch: int, default_action: int, rules: FirewallRegionResource):
        self.protocol = protocol
        self.ch = ch
        self.default_action = default_action
        self.rules = rules

    def to_struct(self) -> FirewallWebserverFilterConfigStruct:
        return FirewallWebserverFilterConfigStruct(
            self.protocol,
            self.ch,
            self.default_action,
            self.rules.to_struct()
        )

# Largest element = 8
class FirewallFilterConfig(Serializable):
    def __init__(self, mac_addr: List[int], ip: int, router: FirewallConnectionResource, webserver: FirewallWebserverFilterConfig,
                 internal_instances: FirewallRegionResource, external_instances: FirewallRegionResource):
        self.mac_addr = mac_addr
        self.ip = ip
        self.router = router
        self.webserver = webserver
        self.internal_instances = internal_instances
        self.external_instances = external_instances
        self.section_name = "firewall_filter_config"

    def to_struct(self) -> FirewallFilterConfigStruct:
        return FirewallFilterConfigStruct(
            (c_uint8 * 6)(*self.mac_addr),
            self.ip,
            filter_rule_capacity,
            instances_capacity,
            self.router.to_struct(),
            self.webserver.to_struct(),
            self.internal_instances.to_struct(),
            self.external_instances.to_struct(),
        )


def convert_to_c_array(convert_type, max_length, array):
    c_arr = [x.to_struct() for x in array] + ((max_length - len(array)) * [convert_type()])
    return (convert_type * max_length)(*c_arr)

# Largest element = 8
class FirewallWebserverConfig(Serializable):
    def __init__(self, mac_addr: List[int], filters_iface: List[int], ip: int, rx_active: FirewallConnectionResource, data: FirewallRegionResource,
                 routers: List[FirewallWebserverRouterConfig], rx_free: FirewallConnectionResource, arp_queue: FirewallConnectionResource,
                 filters: List[FirewallWebserverFilterConfig]):
        self.mac_addr = mac_addr
        self.filters_iface = filters_iface
        self.ip = ip
        self.rx_active = rx_active
        self.data = data
        self.routers = routers
        self.rx_free = rx_free
        self.arp_queue = arp_queue
        self.filters = filters
        self.section_name = "firewall_webserver_config"

    def to_struct(self) -> FirewallWebserverConfigStruct:
        self.filters_iface += [0] * (2 * max_conns - len(self.filters_iface))

        return FirewallWebserverConfigStruct(
            (c_uint8 * 6)(*self.mac_addr),
            (c_uint8 * (max_conns * 2))(*self.filters_iface),
            self.ip,
            self.rx_active.to_struct(),
            self.data.to_struct(),
            convert_to_c_array(FirewallWebserverRouterConfigStruct, 2, self.routers),
            self.rx_free.to_struct(),
            self.arp_queue.to_struct(),
            convert_to_c_array(FirewallWebserverFilterConfigStruct, 2 * max_conns, self.filters),
            len(self.filters)
        )

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

# Map a mr into a pd to create a firewall region
def firewall_region(pd: SystemDescription.ProtectionDomain, mr: SystemDescription.MemoryRegion, perms: str, region_size: int):
    vaddr = next_vaddr()
    pd.add_map(Map(mr, vaddr, perms=perms))
    region_resource = FirewallRegionResource(vaddr, region_size)

    return region_resource

# Map a physical mr into a pd to create a firewall device region
def firewall_device_region(pd: SystemDescription.ProtectionDomain, mr: SystemDescription.MemoryRegion, perms: str, region_size: int):
    region = firewall_region(pd, mr, perms, region_size)
    device_region = FirewallDeviceRegionResource(
            region,
            mr.paddr.value
        )
    return device_region

# Create a firewall connection and map a physical mr to create a firewall data connection
def firewall_data_connection(pd1: SystemDescription.ProtectionDomain , pd2: SystemDescription.ProtectionDomain, 
                             capacity: int, queue_size: int, data: SystemDescription.MemoryRegion, 
                             data_perms1: str, data_perms2: str, data_size: int):
    connection = firewall_connection(pd1, pd2, capacity, queue_size)
    data_region1 = firewall_device_region(pd1, data, data_perms1, data_size)
    data_region2 = firewall_device_region(pd2, data, data_perms2, data_size)

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
    mr = MemoryRegion(region_name, region_size)
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
    networks[EXT_IDX]["driver"] = ProtectionDomain("ethernet_driver_dwmac", "eth_driver_dwmac.elf", priority=101, budget=100, period=400)
    networks[INT_IDX]["out_virt"] = ProtectionDomain("net_virt_tx0", "firewall_network_virt_tx0.elf", priority=100, budget=20000)
    networks[EXT_IDX]["in_virt"] = ProtectionDomain("net_virt_rx0", "firewall_network_virt_rx0.elf", priority=99)

    networks[EXT_IDX]["rx_dma_region"] = MemoryRegion("rx_dma_region0", dma_region_size, paddr = 0x80_000_000)
    sdf.add_mr(networks[EXT_IDX]["rx_dma_region"])

    # Create network 1 subsystem pds
    networks[INT_IDX]["driver"] = ProtectionDomain("ethernet_driver_imx", "eth_driver_imx.elf", priority=101, budget=100, period=400)
    networks[EXT_IDX]["out_virt"] = ProtectionDomain("net_virt_tx1", "firewall_network_virt_tx1.elf", priority=100, budget=20000)
    networks[INT_IDX]["in_virt"] = ProtectionDomain("net_virt_rx1", "firewall_network_virt_rx1.elf", priority=99)

    networks[INT_IDX]["rx_dma_region"] = MemoryRegion("rx_dma_region1", dma_region_size, paddr = 0x75_000_000)
    sdf.add_mr(networks[INT_IDX]["rx_dma_region"])

    # Create network subsystems
    networks[EXT_IDX]["in_net"] = Sddf.Net(sdf, ethernet_node1, networks[EXT_IDX]["driver"], networks[INT_IDX]["out_virt"], networks[EXT_IDX]["in_virt"], networks[EXT_IDX]["rx_dma_region"])
    networks[INT_IDX]["out_net"] = networks[EXT_IDX]["in_net"]


    networks[INT_IDX]["in_net"] = Sddf.Net(sdf, ethernet_node0, networks[INT_IDX]["driver"], networks[EXT_IDX]["out_virt"], networks[INT_IDX]["in_virt"], networks[INT_IDX]["rx_dma_region"])
    networks[EXT_IDX]["out_net"] = networks[INT_IDX]["in_net"]

    # Create firewall pds
    networks[EXT_IDX]["router"] = ProtectionDomain("routing_external", "routing_external.elf", priority=97, budget=20000)
    networks[INT_IDX]["router"] = ProtectionDomain("routing_internal", "routing_internal.elf", priority=94, budget=20000)

    networks[EXT_IDX]["arp_resp"] = ProtectionDomain("arp_responder0", "arp_responder0.elf", priority=95, budget=20000)
    networks[INT_IDX]["arp_resp"] = ProtectionDomain("arp_responder1", "arp_responder1.elf", priority=93, budget=20000)

    networks[EXT_IDX]["arp_req"] = ProtectionDomain("arp_requester0", "arp_requester0.elf", priority=98, budget=20000)
    networks[INT_IDX]["arp_req"] = ProtectionDomain("arp_requester1", "arp_requester1.elf", priority=95, budget=20000)

    # Create the webserver component
    webserver = ProtectionDomain("micropython", "micropython.elf", priority=1, budget=20000)
    common_pds.append(webserver)

    # Webserver is a serial and timer client
    serial_system.add_client(webserver)
    timer_system.add_client(webserver)

    networks[EXT_IDX]["filters"] = {}
    networks[EXT_IDX]["filters"][0x01] = ProtectionDomain("icmp_filter0", "icmp_filter0.elf", priority=90, budget=20000)
    networks[EXT_IDX]["filters"][0x11] = ProtectionDomain("udp_filter0", "udp_filter0.elf", priority=91, budget=20000)
    networks[EXT_IDX]["filters"][0x06] = ProtectionDomain("tcp_filter0", "tcp_filter0.elf", priority=92, budget=20000)

    networks[INT_IDX]["filters"] = {}
    networks[INT_IDX]["filters"][0x01] = ProtectionDomain("icmp_filter1", "icmp_filter1.elf", priority=93, budget=20000)
    networks[INT_IDX]["filters"][0x11] = ProtectionDomain("udp_filter1", "udp_filter1.elf", priority=91, budget=20000)
    networks[INT_IDX]["filters"][0x06] = ProtectionDomain("tcp_filter1", "tcp_filter1.elf", priority=92, budget=20000)

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
    networks[INT_IDX]["in_net"].add_client_with_copier(webserver, rx=False)

    # Webserver receives traffic from the internal -> external router
    router_webserver_conn = firewall_connection(networks[INT_IDX]["router"], webserver, dma_queue_capacity, dma_queue_region_size)

    # Webserver returns packets to interior rx virtualiser
    webserver_in_virt_conn = firewall_connection(webserver, networks[INT_IDX]["in_virt"], dma_queue_capacity, dma_queue_region_size)

    # Webserver needs access to rx dma region
    webserver_data_region = firewall_region(webserver, networks[INT_IDX]["rx_dma_region"], "rw", dma_queue_region_size)

    # Webserver has arp channel for arp requests/responses
    webserver_arp_conn = firewall_connection(webserver, networks[EXT_IDX]["arp_req"], arp_queue_capacity, arp_queue_region_size)

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
        []
    )

    for network in networks:
        router = network["router"]
        out_virt = network["out_virt"]
        in_virt = network["in_virt"]
        arp_req = network["arp_req"]
        arp_resp = network["arp_resp"]

        # Create a firewall data connection between router and output virt with the rx dma region as data region
        router_out_virt_conn = firewall_data_connection(router, out_virt, dma_queue_capacity, dma_queue_region_size, 
                                                        network["rx_dma_region"], "rw", "r", dma_region_size)

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
            arp_cache[0]
        )

        # Create arp resp config
        network["configs"][arp_resp] = FirewallArpResponderConfig(
            network["mac"],
            network["ip"]
        )

        # Create arp packet queue
        arp_packet_queue_mr = MemoryRegion("arp_packet_queue_" + router.name, arp_packet_queue_region_size)
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
            routing_table[0]
        )

        webserver_router_config = FirewallWebserverRouterConfig(
            router_update_ch.pd_a_id,
            routing_table[1]
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
                filter_router_conn[0],
                filter_webserver_config,
                None,
                None
            )

            network["configs"][router].filters.append((filter_router_conn[1]))
            webserver_config.filters.append(webserver_filter_config)
            webserver_config.filters_iface.append(network["num"])

        # Make router and arp components serial clients
        serial_system.add_client(router)
        serial_system.add_client(arp_req)
        serial_system.add_client(arp_resp)

        network["in_net"].connect()
        network["in_net"].serialise_config(network["out_dir"])

    # Add webserver as a free client of interior rx virt
    networks[INT_IDX]["configs"][networks[INT_IDX]["in_virt"]].free_clients.append(webserver_in_virt_conn[1])

    # Add webserver as an arp requester client outputting to the internal network
    networks[EXT_IDX]["configs"][networks[EXT_IDX]["arp_req"]].clients.append(webserver_arp_conn[1])

    # Add a firewall connection to the webserver from the internal router for packet transmission
    networks[INT_IDX]["configs"][networks[INT_IDX]["router"]].rx_active = router_webserver_conn[0]

    # Create filter instance regions
    for (protocol, filter_pd) in networks[INT_IDX]["filters"].items():
        
        mirror_filter = networks[EXT_IDX]["filters"][protocol]
        int_instances = firewall_shared_region(filter_pd, mirror_filter, "rw", "r", "instances", instances_region_size)
        ext_instances = firewall_shared_region(mirror_filter, filter_pd, "rw", "r", "instances", instances_region_size)

        networks[INT_IDX]["configs"][filter_pd].internal_instances = int_instances[0]
        networks[INT_IDX]["configs"][filter_pd].external_instances = ext_instances[1]
    
        networks[EXT_IDX]["configs"][mirror_filter].internal_instances = ext_instances[0]
        networks[EXT_IDX]["configs"][mirror_filter].external_instances = int_instances[1]

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

    data_path = output_dir + "/firewall_config_webserver.data"
    with open(data_path, "wb+") as f:
        f.write(webserver_config.serialise())
    update_elf_section(webserver.elf, webserver_config.section_name, data_path)

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
