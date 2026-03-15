# Copyright 2025, UNSW SPDX-License-Identifier: BSD-2-Clause
import argparse
import subprocess
from os import path
from importlib.metadata import version
from sdfgen_helper import copy_elf, update_elf_section

from sdfgen import SystemDescription, Sddf, DeviceTree

assert version("sdfgen").split(".")[1] == "28", "Unexpected sdfgen version"

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
import pyfw.constants
from pyfw.constants import (
    BOARDS,
    interfaces,
    supported_protocols,
    webserver_tx_interface_idx,
    dma_buffer_region,
    ethtype_arp,
    arp_eth_opcode_request,
    arp_eth_opcode_response,
    eththype_ip,
)

SDF_ProtectionDomain = SystemDescription.ProtectionDomain
SDF_Channel = SystemDescription.Channel

# TODO: Why are build artifacts in the pyfw code directory (both config structs and __init__)?
# TODO Can we still import from config structs in the build directory?
# CALLUM: the __init__ is a python package identifier, this is being used when mypy runs otherwise it complains
# CALLUM: the config structs can be generated into the build directory but for type resolution I still have to symlink it

def generate(sdf_file: str, dtb: DeviceTree) -> None:
    # Create interfaces and component classes
    for iface in interfaces:
        iface.ethernet_driver = SDF_ProtectionDomain(
            f"ethernet_driver{iface.index}",
            f"eth_driver{iface.index}.elf",
            priority=iface.priorities.ethernet_driver,
            budget=100,
            period=400,
        )

        iface.rx_virtualiser = NetVirtRx(iface, iface.priorities.rx_virtualiser)
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

        # CALLUM: not sure what you had in mind as the ideal solution here, I changed it to at least make it clear when looking at constants.py which ethernet device you are selecting for each interface now
        ethernet_node_path = board.ethernet_node_path(iface.board_ethernet)
        ethernet_node = dtb.node(ethernet_node_path)
        assert ethernet_node is not None, (
            f"Could not find device tree node: {ethernet_node_path}"
        )

        assert iface.tx_virtualiser is not None
        assert iface.rx_virtualiser is not None
        iface.net_system = TrackedNet(
            ethernet_node,
            iface.ethernet_driver,
            iface.tx_virtualiser.pd,
            iface.rx_virtualiser.pd,
            iface.rx_dma_region.mr,
            interface_index=iface.index,
        )

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
    timer_system = Sddf.Timer(pyfw.constants.sdf, timer_node, timer_driver)

    # Add global component timer clients
    timer_system.add_client(webserver.pd)

    serial_driver = SDF_ProtectionDomain("serial_driver", "serial_driver.elf", priority=100)
    serial_virt_tx = SDF_ProtectionDomain("serial_virt_tx", "serial_virt_tx.elf", priority=99)
    serial_system = Sddf.Serial(pyfw.constants.sdf, serial_node, serial_driver, serial_virt_tx)

    # Add global component serial clients
    serial_system.add_client(router.pd)
    serial_system.add_client(webserver.pd)
    serial_system.add_client(icmp_module.pd)

    # Register all PDs
    register_pds(timer_driver, serial_driver, serial_virt_tx, router, webserver, icmp_module)

    # Wire per-interface connections
    wire_interface_connections(router, serial_system, timer_system)

    # Wire global component connections
    wire_virtualiser_connections()
    wire_icmp_connections(router, icmp_module)
    webserver_lib_sddf_lwip = wire_webserver_connections(router, webserver)

    # Connect sDDF systems and serialize subsystems
    for iface in interfaces:
        assert iface.net_system is not None
        assert iface.net_system.connect()
        assert iface.net_system.serialise_config(iface.out_dir)

    assert serial_system.connect()
    assert serial_system.serialise_config(pyfw.constants.output_dir)
    assert timer_system.connect()
    assert timer_system.serialise_config(pyfw.constants.output_dir)

    assert webserver_lib_sddf_lwip.connect()
    assert webserver_lib_sddf_lwip.serialise_config(pyfw.constants.output_dir)

    # Serialize firewall configs; component serialisation finalises configs first.
    serialize_all_fw_configs(router, webserver, icmp_module, obj_copy)

    # Render SDF
    with open(f"{pyfw.constants.output_dir}/{sdf_file}", "w+") as f:
        f.write(pyfw.constants.sdf.render())


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
        pyfw.constants.sdf.add_pd(pd)

    for iface in interfaces:
        assert iface.tx_virtualiser is not None
        assert iface.rx_virtualiser is not None
        assert iface.arp_requester is not None
        assert iface.arp_responder is not None
        for component in [
            iface.tx_virtualiser,
            iface.rx_virtualiser,
            iface.arp_requester,
            iface.arp_responder,
        ]:
            copy_elf(component.pd.program_image[:-5], component.pd.program_image[:-5], iface.index)
            pyfw.constants.sdf.add_pd(component.pd)

        assert iface.ethernet_driver is not None
        pyfw.constants.sdf.add_pd(iface.ethernet_driver)

        for ip_filter in iface.filters.values():
            copy_elf(ip_filter.pd.program_image[:-5], ip_filter.pd.program_image[:-5], iface.index)
            pyfw.constants.sdf.add_pd(ip_filter.pd)


def wire_interface_connections(
    router: Router,
    serial_system: Sddf.Serial,
    timer_system: Sddf.Timer,
) -> None:
    """Connect components which are duplicated per network interface"""
    for iface in interfaces:

        # ARP responder receives and transmits net traffic
        assert iface.rx_virtualiser is not None
        assert iface.arp_responder is not None
        iface.rx_virtualiser.add_active_net_client(
            iface.arp_responder, ethtype_arp, arp_eth_opcode_request, tx = True
        )

        # ARP requester receives and transmits net traffic
        assert iface.arp_requester is not None
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
            router_filters = router.interfaces[iface.index].filters
            assert router_filters is not None
            router_filters.append(ip_filter.connect_router(router))


        # Router needs access to the Rx DMA region
        assert iface.rx_dma_region is not None
        router.interfaces[iface.index].data = iface.rx_dma_region.map(router.pd, "rw")

        # Router returns dropped packets to the Rx virtualiser
        router.interfaces[iface.index].rx_free = iface.rx_virtualiser.add_free_fw_client(router)

        # Router transmits packets to the Tx virtualiser
        assert iface.tx_virtualiser is not None
        router.interfaces[iface.index].tx_active = iface.tx_virtualiser.add_active_fw_client(router)

        # Add serial clients
        serial_system.add_client(iface.arp_responder.pd)
        serial_system.add_client(iface.arp_requester.pd)

        # Add timer clients
        timer_system.add_client(iface.arp_requester.pd)


def wire_virtualiser_connections() -> None:
    """Wire Rx DMA region access and DMA buffer return queues between virtualisers."""

    for tx_virtualiser in list(interface.tx_virtualiser for interface in interfaces):
        for interface in interfaces:
            # Tx virtualiser returns freed packets to the Rx virtualiser
            assert interface.rx_virtualiser is not None
            assert tx_virtualiser is not None
            assert interface.rx_dma_region is not None
            free_conn = interface.rx_virtualiser.add_free_fw_client(tx_virtualiser)
            tx_virtualiser.add_free_fw_client(free_conn, interface.rx_dma_region, interface.index)


def wire_webserver_connections(
    router: Router,
    webserver: Webserver,
) -> Sddf.Lwip:
    # TODO: Currently we make an assumption that the webserver can only transmit out one interface (See git issues)
    tx_interface = interfaces[webserver_tx_interface_idx]

    # Webserver is a transmit net client
    assert tx_interface.net_system is not None
    tx_interface.net_system.add_client_with_copier(webserver.pd, rx=False)

    # Webserver uses lib sDDF LWIP
    webserver_lib_sddf_lwip = Sddf.Lwip(pyfw.constants.sdf, tx_interface.net_system._net, webserver.pd)

    # Webserver is an ARP client of its output interface
    assert tx_interface.arp_requester is not None
    webserver.arp_queue = tx_interface.arp_requester.add_arp_client(webserver)

    # Connect Webserver and router
    webserver.router = router.connect_webserver(webserver)

    for iface in interfaces:
        # Webserver needs access to the Rx DMA region
        assert iface.rx_dma_region is not None
        assert webserver.interfaces is not None
        webserver.interfaces[iface.index].data = iface.rx_dma_region.map(webserver.pd, "rw")

        # Webserver returns buffers to the Rx virtualiser
        assert iface.rx_virtualiser is not None
        webserver.interfaces[iface.index].rx_free = iface.rx_virtualiser.add_free_fw_client(webserver)

        # Webserver needs to be connected to all filters
        for ip_filter in iface.filters.values():
            filters = webserver.interfaces[iface.index].filters
            assert filters is not None
            filters.append(
                ip_filter.connect_webserver(webserver)
            )

    return webserver_lib_sddf_lwip


def wire_icmp_connections(
    router: Router,
    icmp_module: IcmpModule,
) -> None:
    """Wire ICMP module connections."""
    for iface in interfaces:
        assert iface.net_system is not None
        iface.net_system.add_client_with_copier(icmp_module.pd, rx=False)
        icmp_module.connect_interface_filters(iface=iface)

    router.icmp_module = icmp_module.connect_router(router)


def serialize_all_fw_configs(
    router: Router,
    webserver: Webserver,
    icmp_module: IcmpModule,
    obj_copy_path: str,
) -> None:
    """Serialize configs to data files and update ELF sections."""
    for iface in interfaces:
        for component in iface.all_components():
            data_path = f"{iface.out_dir}/firewall_config_{component.name}.data"
            with open(data_path, "wb+") as f:
                f.write(component.serialise())
            update_elf_section(obj_copy_path, component.pd.program_image, component.section_name, data_path)

    # Router
    data_path = f"{pyfw.constants.output_dir}/firewall_config_routing.data"
    with open(data_path, "wb+") as f:
        f.write(router.serialise())
    update_elf_section(obj_copy_path, router.pd.program_image, router.section_name, data_path)

    # Webserver
    data_path = f"{pyfw.constants.output_dir}/firewall_config_webserver.data"
    with open(data_path, "wb+") as f:
        f.write(webserver.serialise())
    update_elf_section(obj_copy_path, webserver.pd.program_image, webserver.section_name, data_path)

    # ICMP module
    data_path = f"{pyfw.constants.output_dir}/firewall_icmp_module_config.data"
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

    pyfw.constants.output_dir = args.output
    pyfw.constants.sdf = SystemDescription(board.arch, board.paddr_top)
    sddf = Sddf(args.sddf)

    global obj_copy
    obj_copy = args.objcopy

    with open(args.dtb, "rb") as f:
        dtb = DeviceTree(f.read())

    resolve_region_sizes()

    generate(args.sdf, dtb)

# BIG TODO:
# Rebase on main
# Run auto-test script, fix any issues
# Test connect rule and webserver
# Fix up all code TODOs
# Encode as many types as we can, for type checking later
# Think up a convention for which "connection" method belongs to which class
# Make a PR (or come up with a better solution) for necessary sDDF fix
# Check old PR comments for additional TODOs
