# Copyright 2026, UNSW SPDX-License-Identifier: BSD-2-Clause

import argparse
import subprocess
from os import path
from importlib.metadata import version
from sdfgen_helper import copy_elf, update_elf_section

from sdfgen import SystemDescription, Sddf, DeviceTree

assert version("sdfgen").split(".")[1] == "35", "Unexpected sdfgen version"

from typing import List
from config_structs import FwBridgeMaxPorts, NetDriverConfig
from pyfw.memory_layout import (
    resolve_region_sizes,
)
from pyfw.specs import TrackedNet, FirewallMemoryRegion, NetworkDevice, render_system
from pyfw.component_arp import ArpRequester, ArpResponder
from pyfw.component_bridge import Bridge
from pyfw.component_filter import Filter
from pyfw.component_icmp import IcmpModule
from pyfw.component_net_virt import NetVirtRx, NetVirtTx
from pyfw.component_port import FirewallPort
from pyfw.component_router import Router
from pyfw.component_webserver import Webserver
from pyfw.constants import (
    BuildConstants,
    BOARDS,
    FILTER_ACTION_REJECT,
    supported_protocols,
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
    used_ports = set()
    for net_iface in BuildConstants.interfaces():
        assert (
            1 <= len(net_iface.ports) <= FwBridgeMaxPorts
        ), f"Interface {net_iface.name} must have between 1 and {FwBridgeMaxPorts} ports"
        for idx in net_iface.ports:
            assert (
                0 <= idx < len(board.ethernet)
            ), f"Ethernet port {idx} is not available on {board.name}"
            assert (
                idx not in used_ports
            ), f"Ethernet port {idx} is assigned more than once"
            used_ports.add(idx)

    # Create interfaces and component classes
    for net_iface in BuildConstants.interfaces():
        iface = FirewallInterface(net_iface)
        fw_interfaces.append(iface)

        iface.rx_virtualiser = NetVirtRx(iface, None, iface.priorities.rx_virtualiser)
        iface.tx_virtualiser = NetVirtTx(iface, iface.priorities.tx_virtualiser)
        iface.arp_requester = ArpRequester(iface, iface.priorities.arp_requester)
        iface.arp_responder = ArpResponder(iface, iface.priorities.arp_responder)
        iface.bridge = Bridge(iface, iface.priorities.bridge)

        iface.filters = {
            protocol:
                Filter(iface.index, protocol, iface.priorities.filters[supported_protocols[protocol]])
            for protocol in supported_protocols.keys()
        }

        iface.rx_dma_region = FirewallMemoryRegion(
            f"rx_dma_region{iface.index}", dma_buffer_region.region_size, physical=True,
        )
        # First mapped data region should be the Rx region for this interface
        # (Easy for the bridge identify it without another config field)
        iface.bridge.map_data(iface.rx_dma_region.mr)

        # No physical device is set up here, and the virtualisers are connected to the bridge acting in place of a single physical device
        iface.net_system = TrackedNet(
            None,
            iface.bridge.pd,
            iface.tx_virtualiser.pd,
            iface.rx_virtualiser.pd,
            iface.rx_dma_region.mr,
            interface_index=iface.index,
        )

        iface.rx_virtualiser._sddf_net = iface.net_system

        if not path.isdir(iface.out_dir):
            assert subprocess.run(["mkdir", iface.out_dir]).returncode == 0

        for port_index, board_index in enumerate(iface.ports):
            driver = SDF_ProtectionDomain(
                f"ethernet_driver{board_index}",
                f"eth_driver{board_index}.elf",
                priority=iface.priorities.ethernet_driver,
                budget=100,
                period=400,
            )
            device = dtb.node(board.ethernet_node_path(board_index))
            assert device is not None
            device_system = NetworkDevice(device, driver)
            driver_rx, driver_tx = iface.bridge.connect_port(driver)
            port = FirewallPort(
                board_ethernet_idx=board_index,
                driver=driver,
                out_dir=f"{iface.out_dir}/port{port_index}",
                driver_config=NetDriverConfig(
                    magic="sDDF\x05", # From dep/sddf/include/sddf/network/config.h
                    virt_rx=driver_rx,
                    virt_tx=driver_tx,
                ),
                device_system=device_system,
            )
            iface.fw_ports.append(port)
            if not path.isdir(port.out_dir):
                assert subprocess.run(["mkdir", port.out_dir]).returncode == 0

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

    # Connect sDDF systems and serialize subsystems
    for iface in fw_interfaces:
        for port in iface.fw_ports:
            port.connect()
        assert iface.net_system.connect()
        assert iface.net_system.serialise_config(iface.out_dir)
        iface.bridge.connect_local(iface.net_system)
        for source in fw_interfaces:
            # No issue with mapping the interface's own Rx region again (bridge map function ignores duplicate mappings)
            iface.bridge.map_data(source.rx_dma_region.mr)

    assert serial_system.connect()
    assert serial_system.serialise_config(BuildConstants.output_dir())
    assert timer_system.connect()
    assert timer_system.serialise_config(BuildConstants.output_dir())

    assert webserver_lib_sddf_lwip.connect()
    assert webserver_lib_sddf_lwip.serialise_config(BuildConstants.output_dir())

    for iface in fw_interfaces:
        for port in iface.fw_ports:
            update_elf_section(
                obj_copy,
                port.driver.program_image,
                "device_resources",
                f"{port.out_dir}/{port.driver.name}_device_resources.data",
            )
            update_elf_section(
                obj_copy,
                port.driver.program_image,
                "net_driver_config",
                f"{port.out_dir}/net_driver.data",
            )

    # Serialize firewall configs- this implicitly finalises all configs
    serialize_all_fw_configs(router, webserver, icmp_module, obj_copy)

    # Render SDF
    with open(f"{BuildConstants.output_dir()}/{sdf_file}", "w+") as f:
        # Note that interfaces must be passed in the same order as the interface .connect() loop above
        devices = [
            port.device_system for iface in fw_interfaces for port in iface.fw_ports
        ]
        f.write(render_system([iface.net_system for iface in fw_interfaces], devices))


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
        for port in iface.fw_ports:
            BuildConstants.sdf().add_pd(port.driver)
        for component in [
            iface.tx_virtualiser,
            iface.rx_virtualiser,
            iface.arp_requester,
            iface.arp_responder,
            iface.bridge,
        ]:
            copy_elf(component.pd.program_image[:-5], component.pd.program_image[:-5], iface.index)
            BuildConstants.sdf().add_pd(component.pd)

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
        timer_system.add_client(iface.bridge.pd)


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

    tx_interface = fw_interfaces[BuildConstants.webserver_tx_interface_idx()]

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
    parser.add_argument("--num_interfaces", required=True)
    args = parser.parse_args()

    board = next(filter(lambda b: b.name == args.board, BOARDS))

    BuildConstants.set_output_dir(args.output)
    BuildConstants.set_sdf(SystemDescription(board.arch, board.paddr_top))
    BuildConstants.set_num_interfaces(int(args.num_interfaces))

    sddf = Sddf(args.sddf)

    global obj_copy
    obj_copy = args.objcopy

    with open(args.dtb, "rb") as f:
        dtb = DeviceTree(f.read())

    resolve_region_sizes()

    generate(args.sdf, dtb)
