# Copyright 2026, UNSW SPDX-License-Identifier: BSD-2-Clause

import argparse
import subprocess
from os import path
from importlib.metadata import version
from sdfgen_helper import copy_elf, update_elf_section

from sdfgen import SystemDescription, Sddf, DeviceTree

assert version("sdfgen").split(".")[1] == "28", "Unexpected sdfgen version"

from typing import List
from pyfw.memory_layout import (
    resolve_region_sizes,
)
from pyfw.specs import TrackedNet, FirewallMemoryRegion
from pyfw.component_arp import ArpRequester, ArpResponder
from pyfw.component_filter import Filter
from pyfw.component_icmp import IcmpModule
from pyfw.component_net_virt import NetVirtRx, NetVirtTx
from pyfw.component_router import Router
from pyfw.component_webserver import Webserver
from pyfw.constants import (
    BuildConstants,
    BOARDS,
    FILTER_ACTION_REJECT,
    interfaces,
    supported_protocols,
    webserver_tx_interface_idx,
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
    # Create interfaces and component classes
    for net_iface in interfaces:
        iface = FirewallInterface(net_iface)
        fw_interfaces.append(iface)

        iface.ethernet_driver = SDF_ProtectionDomain(
            f"ethernet_driver{iface.index}",
            f"eth_driver{iface.index}.elf",
            priority=iface.priorities.ethernet_driver,
            budget=100,
            period=400,
        )

        iface.rx_virtualiser = NetVirtRx(iface, None, iface.priorities.rx_virtualiser)
        iface.tx_virtualiser = NetVirtTx(iface, iface.priorities.tx_virtualiser)
        iface.arp_requester = ArpRequester(iface, iface.priorities.arp_requester)
        iface.arp_responder = ArpResponder(iface, iface.priorities.arp_responder)

        iface.filters = {
            protocol:
                Filter(iface.index, protocol, iface.priorities.filters[supported_protocols[protocol]])
            for protocol in supported_protocols.keys()
        }

        iface.rx_dma_region = FirewallMemoryRegion(
            f"rx_dma_region{iface.index}", dma_buffer_region.region_size, physical=True,
        )

        ethernet_node_path = board.ethernet_node_path(iface.board_ethernet)
        ethernet_node = dtb.node(ethernet_node_path)
        assert ethernet_node is not None, (
            f"Could not find device tree node: {ethernet_node_path}"
        )

        iface.net_system = TrackedNet(
            ethernet_node,
            iface.ethernet_driver,
            iface.tx_virtualiser.pd,
            iface.rx_virtualiser.pd,
            iface.rx_dma_region.mr,
            interface_index=iface.index,
        )
        # TODO: Needs optional type here
        iface.rx_virtualiser._sddf_net = iface.net_system

        if not path.isdir(iface.out_dir):
            assert subprocess.run(["mkdir", iface.out_dir]).returncode == 0

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
        assert iface.net_system.connect()
        assert iface.net_system.serialise_config(iface.out_dir)

    assert serial_system.connect()
    assert serial_system.serialise_config(BuildConstants.output_dir())
    assert timer_system.connect()
    assert timer_system.serialise_config(BuildConstants.output_dir())

    assert webserver_lib_sddf_lwip.connect()
    assert webserver_lib_sddf_lwip.serialise_config(BuildConstants.output_dir())

    # Serialize firewall configs- this implicitly finalises all configs
    serialize_all_fw_configs(router, webserver, icmp_module, obj_copy)

    # Render SDF
    with open(f"{BuildConstants.output_dir()}/{sdf_file}", "w+") as f:
        f.write(BuildConstants.sdf().render())


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
        for component in [
            iface.tx_virtualiser,
            iface.rx_virtualiser,
            iface.arp_requester,
            iface.arp_responder,
        ]:
            copy_elf(component.pd.program_image[:-5], component.pd.program_image[:-5], iface.index)
            BuildConstants.sdf().add_pd(component.pd)

        BuildConstants.sdf().add_pd(iface.ethernet_driver)

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
    # FUTURE WORK Currently we make an assumption that the webserver can only transmit out one interface (See git issues)
    tx_interface = fw_interfaces[webserver_tx_interface_idx]

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
    args = parser.parse_args()

    board = next(filter(lambda b: b.name == args.board, BOARDS))

    BuildConstants.set_output_dir(args.output)
    BuildConstants.set_sdf(SystemDescription(board.arch, board.paddr_top))
    sddf = Sddf(args.sddf)

    global obj_copy
    obj_copy = args.objcopy

    with open(args.dtb, "rb") as f:
        dtb = DeviceTree(f.read())

    resolve_region_sizes()

    generate(args.sdf, dtb)

# BIG TODO:
# Run auto-test script, fix any issues
# Makefile dependency tracking for python files
# Test connect rule and webserver
# Fix up all code TODOs FUTURE WORK
# Encode as many types as we can, for type checking later (maybe return non-optional type through method, assertion in  method)
# Think up a convention for which "connection" method belongs to which class
# Check old PR comments for additional TODOs
# TODO: Autotests should not be hardcoded with strings
