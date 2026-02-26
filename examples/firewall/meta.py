# Copyright 2025, UNSW SPDX-License-Identifier: BSD-2-Clause
import argparse
import subprocess
import shutil
from os import path
from itertools import combinations
from dataclasses import dataclass
from typing import List
from importlib.metadata import version
import ipaddress

from sdfgen import SystemDescription, Sddf, DeviceTree

assert version("sdfgen").split(".")[1] == "28", "Unexpected sdfgen version"

from pyfw.memory_layout import (
    FirewallDataStructure,
    FirewallMemoryRegions,
    UINT64_BYTES,
    resolve_region_sizes,
)
from pyfw.specs import TrackedNet, FirewallMemoryRegion
from pyfw.component_arp import ArpRequester, ArpResponder
from pyfw.component_filter import Filter
from pyfw.component_icmp import IcmpModule
from pyfw.component_models import InterfacePriorities, NetworkInterface
from pyfw.component_net_virt import NetVirtRx, NetVirtTx
from pyfw.component_router import Router
from pyfw.component_webserver import Webserver

SDF_ProtectionDomain = SystemDescription.ProtectionDomain
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

def ip_to_int(ipString: str) -> int:
    ipaddress.IPv4Address(ipString)
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
        ethernet0="soc@0/bus@30800000/ethernet@30bf0000",  # DWMAC
        ethernet1="soc@0/bus@30800000/ethernet@30be0000",  # IMX
    ),
]

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
    elf_name="routing.elf", c_name="fw_routing_table"
)
routing_table_buffer = FirewallDataStructure(
    elf_name="routing.elf", c_name="fw_routing_entry", capacity=256
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
    entry_size=UINT64_BYTES, capacity=(filter_rules_buffer.capacity + 63) // 64
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

def generate(sdf_file: str, output_dir: str, dtb: DeviceTree) -> None:
    webserver_interface_idx = 1

    interfaces = [
        NetworkInterface(
            index=0,
            name="external",
            ethernet_node_path=board.ethernet0,
            mac=(0x00, 0x01, 0xC0, 0x39, 0xD5, 0x18),
            ip="172.16.2.1",
            subnet_bits=16,
            priorities=InterfacePriorities(
                arp_requester=98,
                arp_responder=95,
                icmp_filter=90,
            ),
        ),
        NetworkInterface(
            index=1,
            name="internal",
            ethernet_node_path=board.ethernet1,
            mac=(0x00, 0x01, 0xC0, 0x39, 0xD5, 0x10),
            ip="192.168.1.1",
            subnet_bits=24,
            priorities=InterfacePriorities(
                arp_requester=95,
                arp_responder=93,
                icmp_filter=93,
            ),
        ),
    ]

    # Phase 1: Create component classes
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

        iface.rx_dma_region = FirewallMemoryRegion(
            sdf, f"rx_dma_region{iface.index}", dma_buffer_region.region_size, physical=True,
        )

        iface_out_dir = f"{output_dir}/{iface.out_dir}"
        if not path.isdir(iface_out_dir):
            assert subprocess.run(["mkdir", iface_out_dir]).returncode == 0

    router = Router(sdf)
    webserver = Webserver(sdf)
    icmp_module = IcmpModule(sdf)

    # Phase 2: Create timer and serial subsystems
    serial_node = dtb.node(board.serial)
    assert serial_node is not None
    timer_node = dtb.node(board.timer)
    assert timer_node is not None

    timer_driver = SDF_ProtectionDomain("timer_driver", "timer_driver.elf", priority=101)
    timer_system = Sddf.Timer(sdf, timer_node, timer_driver)

    serial_driver = SDF_ProtectionDomain("serial_driver", "serial_driver.elf", priority=100)
    serial_virt_tx = SDF_ProtectionDomain("serial_virt_tx", "serial_virt_tx.elf", priority=99)
    serial_system = Sddf.Serial(sdf, serial_node, serial_driver, serial_virt_tx)

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
    wire_filter_rules(interfaces, webserver_interface_idx)
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
        iface.rx_dma_region.mr,
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
    """Register all PDs with SDF and copy ELFs for per-interface components."""
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
    """These are the Per Interface Connections"""
    for iface in interfaces:
        # ARP requester is network client
        iface.net_system.add_client_with_copier(iface.arp_requester.pd)

        # Router -> RX virt queue (buffer return)
        router_rx_virt_mr = FirewallMemoryRegion(
            sdf_obj, "fw_queue_" + router.pd.name + "_" + iface.rx_virt.pd.name,
            dma_buffer_queue_region.region_size,
        )
        router_rx_virt_src = router_rx_virt_mr.map(router.pd, "rw")
        router_rx_virt_dst = router_rx_virt_mr.map(iface.rx_virt.pd, "rw")
        router_rx_virt_ch = SDF_Channel(router.pd, iface.rx_virt.pd)
        sdf_obj.add_channel(router_rx_virt_ch)

        # RX virt: register ARP ethtype (the response queue)
        iface.rx_virt.add_active_client(ethtype_arp, arp_eth_opcode_response)
        # Add buffer return from router as free client
        iface.rx_virt.add_free_client(
            router_rx_virt_dst, dma_buffer_queue.capacity, router_rx_virt_ch.pd_b_id,
        )

        # ARP requester needs timer and serial access
        timer_system.add_client(iface.arp_requester.pd)
        serial_system.add_client(iface.arp_requester.pd)

        # ARP responder is network and serial client
        iface.net_system.add_client_with_copier(iface.arp_responder.pd)
        serial_system.add_client(iface.arp_responder.pd)
        iface.rx_virt.add_active_client(ethtype_arp, arp_eth_opcode_request)

        # Router <-> ARP requester (request + response queues + channel)
        arp_req_mr = FirewallMemoryRegion(
            sdf_obj, "fw_req_queue_" + router.pd.name + "_" + iface.arp_requester.pd.name,
            arp_queue_region.region_size,
        )
        router_arp_req = arp_req_mr.map(router.pd, "rw")
        arp_req_arp_req = arp_req_mr.map(iface.arp_requester.pd, "rw")

        arp_res_mr = FirewallMemoryRegion(
            sdf_obj, "fw_res_queue_" + router.pd.name + "_" + iface.arp_requester.pd.name,
            arp_queue_region.region_size,
        )
        router_arp_res = arp_res_mr.map(router.pd, "rw")
        arp_req_arp_res = arp_res_mr.map(iface.arp_requester.pd, "rw")

        router_arp_ch = SDF_Channel(router.pd, iface.arp_requester.pd)
        sdf_obj.add_channel(router_arp_ch)

        iface.arp_requester.add_arp_client(
            arp_req_arp_req, arp_req_arp_res,
            arp_queue_buffer.capacity, router_arp_ch.pd_b_id,
        )

        # ARP cache
        arp_cache_mr = FirewallMemoryRegion(
            sdf_obj,
            "arp_cache_" + iface.arp_requester.pd.name + "_" + router.pd.name,
            arp_cache_region.region_size,
        )
        arp_cache_owner = arp_cache_mr.map(iface.arp_requester.pd, "rw")
        arp_cache_peer = arp_cache_mr.map(router.pd, "r")
        iface.arp_requester.set_cache(arp_cache_owner, arp_cache_buffer.capacity)

        # Create RouterInterface the order that we add here needs to align
        # with the order we add to the data regions to the tx virt currently
        # sequential on the interfaces list
        ri = router.create_interface()
        ri.set_rx_free(
            router_rx_virt_src, dma_buffer_queue.capacity, router_rx_virt_ch.pd_a_id,
        )
        ri.set_data(iface.rx_dma_region.map(router.pd, "rw"))
        ri.set_arp_queue(
            router_arp_req, router_arp_res,
            arp_queue_buffer.capacity, router_arp_ch.pd_a_id,
        )
        ri.set_arp_cache(arp_cache_peer, arp_cache_buffer.capacity)
        ri.set_network_info(iface.mac_list, iface.ip_int, iface.subnet_bits)
        iface.router_interface = ri

        # Add the No-Next-Hop route
        router.add_initial_route(ip=iface.ip_int, subnet=iface.subnet_bits,interface=iface.index, next_hop=0)

        # Filters
        for protocol, filt in iface.filters.items():
            # Filter -> router connection
            filter_router_mr = FirewallMemoryRegion(
                sdf_obj,
                "fw_queue_" + filt.pd.name + "_" + router.pd.name,
                dma_buffer_queue_region.region_size,
            )
            filter_router_src = filter_router_mr.map(filt.pd, "rw")
            filter_router_dst = filter_router_mr.map(router.pd, "rw")
            filter_router_ch = SDF_Channel(filt.pd, router.pd)
            sdf_obj.add_channel(filter_router_ch)

            # Connect filter as RX-only network client
            iface.net_system.add_client_with_copier(filt.pd, tx=False)
            iface.rx_virt.add_active_client(eththype_ip, protocol)

            # Rule bitmap
            rule_bitmap_mr = FirewallMemoryRegion(
                sdf_obj,
                "rule_bitmap_" + filt.name + "_" + filt.pd.name,
                filter_rule_bitmap_region.region_size,
            )
            rule_bitmap_res = rule_bitmap_mr.map(filt.pd, "rw")

            # Filter rules region
            filter_rules_mr = FirewallMemoryRegion(
                sdf_obj,
                "filter_rules_" + filt.pd.name + "_" + webserver.pd.name,
                filter_rules_region.region_size,
            )
            filter_rules_owner = filter_rules_mr.map(filt.pd, "rw")
            filter_rules_peer = filter_rules_mr.map(webserver.pd, "r")

            # SDF_Channel for rule updates
            filter_update_ch = SDF_Channel(webserver.pd, filt.pd, pp_a=True)
            sdf_obj.add_channel(filter_update_ch)

            # Store webserver-side view of this filter's rule table.
            webserver.set_filter_config(
                iface.index,
                protocol,
                filter_update_ch.pd_a_id, filter_rules_peer, filter_rules_buffer.capacity,
            )
            # Filter side keeps only the update channel id.
            filt.set_webserver_channel(filter_update_ch.pd_a_id)

            # Populate filter component state
            filt.set_router_connection(
                filter_router_src, dma_buffer_queue.capacity, filter_router_ch.pd_a_id,
            )
            filt.set_rules_region(filter_rules_owner, filter_rules_buffer.capacity)
            filt.set_rule_bitmap(rule_bitmap_res)
            filt.set_instances_capacity(filter_instances_buffer.capacity)

            ri.add_filter(
                filter_router_dst, dma_buffer_queue.capacity, filter_router_ch.pd_b_id,
            )


def wire_routing_connections(
    sdf_obj: SystemDescription,
    interfaces: List[NetworkInterface],
    router: Router,
    serial_system: Sddf.Serial,
) -> None:
    """Wire cross-interface routing: router->tx_virt data & tx_virt->rx_virt return."""
    # Add the routing component as a serial_client
    serial_system.add_client(router.pd)

    for dst_iface in interfaces:
        router_tx_virt_mr = FirewallMemoryRegion(
            sdf_obj,
            "fw_queue_" + router.pd.name + "_" + dst_iface.tx_virt.pd.name,
            dma_buffer_queue_region.region_size,
        )
        router_tx_virt_src = router_tx_virt_mr.map(router.pd, "rw")
        router_tx_virt_dst = router_tx_virt_mr.map(dst_iface.tx_virt.pd, "rw")
        router_tx_virt_ch = SDF_Channel(router.pd, dst_iface.tx_virt.pd)
        sdf_obj.add_channel(router_tx_virt_ch)

        dst_iface.router_interface.set_tx_active(
            router_tx_virt_src, dma_buffer_queue.capacity, router_tx_virt_ch.pd_a_id,
        )
        dst_iface.tx_virt.add_active_client(
            router_tx_virt_dst, dma_buffer_queue.capacity, router_tx_virt_ch.pd_b_id,
        )

        for src_iface in interfaces:
            # SDF_Map src DMA region into dst tx_virt with read access
            tx_virt_dma = src_iface.rx_dma_region.map_device(dst_iface.tx_virt.pd, "r")

            # This is sequential on the interface list matching with the routers interfaces ordering
            dst_iface.tx_virt.add_data_region(tx_virt_dma)

            # dst.tx_virt -> src.rx_virt buffer return queue
            tx_rx_return_mr = FirewallMemoryRegion(
                sdf_obj,
                "fw_queue_" + dst_iface.tx_virt.pd.name + "_" + src_iface.rx_virt.pd.name
                + f"{src_iface.index}{dst_iface.index}",
                dma_buffer_queue_region.region_size,
            )
            tx_rx_return_src = tx_rx_return_mr.map(dst_iface.tx_virt.pd, "rw")
            tx_rx_return_dst = tx_rx_return_mr.map(src_iface.rx_virt.pd, "rw")
            tx_rx_return_ch = SDF_Channel(dst_iface.tx_virt.pd, src_iface.rx_virt.pd)
            sdf_obj.add_channel(tx_rx_return_ch)

            # TX free client (queue + data reusing tx_virt's existing DMA mapping)
            dst_iface.tx_virt.add_free_client(
                tx_rx_return_src, dma_buffer_queue.capacity,
                tx_rx_return_ch.pd_a_id, tx_virt_dma,
            )
            # RX virt free client (queue only)
            src_iface.rx_virt.add_free_client(
                tx_rx_return_dst, dma_buffer_queue.capacity, tx_rx_return_ch.pd_b_id,
            )

def wire_webserver_connections(
    sdf_obj: SystemDescription,
    interfaces: List[NetworkInterface],
    router: Router,
    webserver: Webserver,
    serial_system: Sddf.Serial,
    timer_system: Sddf.Timer,
    webserver_interface_idx: int,
) -> Sddf.Lwip:
    # This setup makes an assumption on the Tx Interface used by the webserver
    ws_iface = interfaces[webserver_interface_idx]
    serial_system.add_client(webserver.pd)
    timer_system.add_client(webserver.pd)

    # Webserver is TX client of webserver interface network
    ws_iface.net_system.add_client_with_copier(webserver.pd, rx=False)

    # Webserver uses lib sDDF LWIP
    webserver_lib_sddf_lwip = Sddf.Lwip(sdf_obj, ws_iface.net_system._net, webserver.pd)

    # Router -> webserver
    router_ws_mr = FirewallMemoryRegion(
        sdf_obj,
        "fw_queue_" + router.pd.name + "_" + webserver.pd.name,
        dma_buffer_queue_region.region_size,
    )
    router_ws_src = router_ws_mr.map(router.pd, "rw")
    router_ws_dst = router_ws_mr.map(webserver.pd, "rw")
    router_ws_ch = SDF_Channel(router.pd, webserver.pd)
    sdf_obj.add_channel(router_ws_ch)

    webserver.set_rx_active(
        router_ws_dst, dma_buffer_queue.capacity, router_ws_ch.pd_b_id,
    )
    router.set_webserver_rx(
        router_ws_src, dma_buffer_queue.capacity, router_ws_ch.pd_a_id,
    )

    # Webserver per-interface resources: data region and buffer-return queue.
    for iface in interfaces:
        ws_rx_virt_mr = FirewallMemoryRegion(
            sdf_obj,
            "fw_queue_" + webserver.pd.name + "_" + iface.rx_virt.pd.name,
            dma_buffer_queue_region.region_size,
        )
        ws_rx_virt_src = ws_rx_virt_mr.map(webserver.pd, "rw")
        ws_rx_virt_dst = ws_rx_virt_mr.map(iface.rx_virt.pd, "rw")
        ws_rx_virt_ch = SDF_Channel(webserver.pd, iface.rx_virt.pd)
        sdf_obj.add_channel(ws_rx_virt_ch)

        webserver_data_region = iface.rx_dma_region.map_device(webserver.pd, "rw")
        webserver.set_interface_resources(
            iface.index,
            webserver_data_region,
            ws_rx_virt_src,
            dma_buffer_queue.capacity,
            ws_rx_virt_ch.pd_a_id,
        )

        iface.rx_virt.add_free_client(
            ws_rx_virt_dst, dma_buffer_queue.capacity, ws_rx_virt_ch.pd_b_id,
        )

    # Webser ARP will likely need to change in the future in order to handle Tx on any interface
    # Webserver ARP (request + response queues + channel)
    ws_arp_req_mr = FirewallMemoryRegion(
        sdf_obj,
        "fw_req_queue_" + webserver.pd.name + "_" + ws_iface.arp_requester.pd.name,
        arp_queue_region.region_size,
    )
    ws_arp_req_ws = ws_arp_req_mr.map(webserver.pd, "rw")
    ws_arp_req_arp = ws_arp_req_mr.map(ws_iface.arp_requester.pd, "rw")

    ws_arp_res_mr = FirewallMemoryRegion(
        sdf_obj,
        "fw_res_queue_" + webserver.pd.name + "_" + ws_iface.arp_requester.pd.name,
        arp_queue_region.region_size,
    )
    ws_arp_res_ws = ws_arp_res_mr.map(webserver.pd, "rw")
    ws_arp_res_arp = ws_arp_res_mr.map(ws_iface.arp_requester.pd, "rw")

    ws_arp_ch = SDF_Channel(webserver.pd, ws_iface.arp_requester.pd)
    sdf_obj.add_channel(ws_arp_ch)

    webserver.set_arp_connection(
        ws_arp_req_ws, ws_arp_res_ws,
        arp_queue_buffer.capacity, ws_arp_ch.pd_a_id,
    )
    ws_iface.arp_requester.add_arp_client(
        ws_arp_req_arp, ws_arp_res_arp,
        arp_queue_buffer.capacity, ws_arp_ch.pd_b_id,
    )

    # Routing table shared between router and webserver
    routing_table_mr = FirewallMemoryRegion(
        sdf_obj,
        "routing_table_" + router.pd.name + "_" + webserver.pd.name,
        routing_table_region.region_size,
    )
    routing_table_owner = routing_table_mr.map(router.pd, "rw")
    routing_table_peer = routing_table_mr.map(webserver.pd, "r")

    # PP channel for routing table updates
    router_update_ch = SDF_Channel(webserver.pd, router.pd, pp_a=True)
    sdf_obj.add_channel(router_update_ch)

    # Router webserver config
    router.set_webserver_config(
        router_update_ch.pd_b_id, routing_table_owner, routing_table_buffer.capacity,
    )

    webserver.set_router_config(
        router_update_ch.pd_a_id, routing_table_peer, routing_table_buffer.capacity,
    )
    webserver.set_interface(webserver_interface_idx)

    # Router packet queue
    arp_pq_mr = FirewallMemoryRegion(
        sdf_obj,
        "arp_packet_queue_" + router.pd.name,
        arp_packet_queue_region.region_size,
    )
    arp_pq_res = arp_pq_mr.map(router.pd, "rw")
    router.set_packet_queue(arp_pq_res, arp_packet_queue_buffer.capacity)

    # Webserver interface configs
    for iface in interfaces:
        webserver.add_interface_config(iface)

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

    icmp_mr = FirewallMemoryRegion(
        sdf_obj,
        "fw_queue_" + router.pd.name + "_" + icmp_module.pd.name,
        icmp_queue_region.region_size,
    )
    icmp_src = icmp_mr.map(router.pd, "rw")
    icmp_dst = icmp_mr.map(icmp_module.pd, "rw")
    icmp_ch = SDF_Channel(router.pd, icmp_module.pd)
    sdf_obj.add_channel(icmp_ch)

    router.set_icmp_connection(
        icmp_src, icmp_queue_buffer.capacity, icmp_ch.pd_a_id,
    )
    icmp_module.set_router_connection(
        icmp_dst, icmp_queue_buffer.capacity, icmp_ch.pd_b_id,
    )


def wire_filter_rules(interfaces: List[NetworkInterface], webserver_interface_idx: int) -> None:
    """Set default and initial filter rules for all interfaces."""
    actionNums = {"Allow": 1, "Drop": 2, "Connect": 3}
    ws_iface = interfaces[webserver_interface_idx]
    ws_ip = ws_iface.ip_int
    dst_subnet = 32

    # This loop will have to be updated based on what interfaces we want the webserver to be reachable through.
    for iface in interfaces:
        is_web_server = iface.index == webserver_interface_idx

        for protocol, filt in iface.filters.items():
            # This is treated as the DEFAULT rule by the filters init()
            filt.add_initial_rule(
                action=actionNums["Drop"],
                src_port_any=True,
                dst_port_any=True,
            )

            filt.add_initial_rule(
                action=actionNums["Drop"],
                dst_ip=ws_ip,
                dst_subnet=dst_subnet,
                src_port_any=True,
                dst_port_any=True,
            )
            if protocol == ip_protocol_tcp:
                if is_web_server:
                    filt.add_initial_rule(
                        action=actionNums["Connect"],
                        dst_ip=ws_ip,
                        dst_subnet=dst_subnet,
                        dst_port=htons(80),
                        src_port_any=True,
                    )

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
            local_mr = FirewallMemoryRegion(
                sdf_obj,
                "instances_" + filt.pd.name + "_" + mirror_filt.pd.name,
                filter_instances_region.region_size,
            )
            local_owner = local_mr.map(filt.pd, "rw")
            local_peer = local_mr.map(mirror_filt.pd, "r")

            # remote_instances: mirror_filt owns, filt reads
            remote_mr = FirewallMemoryRegion(
                sdf_obj,
                "instances_" + mirror_filt.pd.name + "_" + filt.pd.name,
                filter_instances_region.region_size,
            )
            remote_owner = remote_mr.map(mirror_filt.pd, "rw")
            remote_peer = remote_mr.map(filt.pd, "r")

            # Update both filters
            filt.set_instances(
                local_owner,
                remote_peer,
                filter_instances_buffer.capacity,
            )
            mirror_filt.set_instances(
                remote_owner,
                local_peer,
                filter_instances_buffer.capacity,
            )


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

    resolve_region_sizes()

    generate(args.sdf, args.output, dtb)
