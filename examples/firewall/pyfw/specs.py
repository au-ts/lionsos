# Copyright 2026, UNSW SPDX-License-Identifier: BSD-2-Clause

from typing import Optional
from pathlib import Path
from tempfile import TemporaryDirectory
import xml.etree.ElementTree as ET
from sdfgen import SystemDescription, Sddf, DeviceTree
from pyfw.constants import BuildConstants
from config_structs import (
    RegionResource,
    DeviceRegionResource,
    NetDriverConfigStruct,
    NetVirtTxConfigStruct,
)

ProtectionDomain = SystemDescription.ProtectionDomain
MemoryRegion = SystemDescription.MemoryRegion
Map = SystemDescription.Map
Channel = SystemDescription.Channel

class FirewallMemoryRegion:
    """Unified memory region: create MR, add to SDF, map into PDs."""

    def __init__(self, name, size, physical=False):
        self.mr = MemoryRegion(BuildConstants.sdf(), name, size, physical=physical)
        self.size = size
        self.physical = physical
        BuildConstants.sdf().add_mr(self.mr)

    def map(self, pd, perms="rw") -> RegionResource:
        """Map the MR into pd with given perms. Returns RegionResource."""
        pd_map = Map(self.mr, pd.get_map_vaddr(self.mr), perms=perms)
        pd.add_map(pd_map)
        return RegionResource(vaddr=pd_map.vaddr, size=self.size)

    def map_device(self, pd, perms="rw") -> DeviceRegionResource:
        """Map the physical MR into pd with given perms. Returns DeviceRegionResource."""
        assert self.physical
        pd_map = Map(self.mr, pd.get_map_vaddr(self.mr), perms=perms)
        pd.add_map(pd_map)
        region_resource = RegionResource(vaddr=pd_map.vaddr, size=self.size)
        return DeviceRegionResource(region=region_resource, io_addr=self.mr.paddr)


class NetworkDevice:
    """Use sdfgen's device setup, discarding its temporary network wiring."""

    def __init__(self, device: DeviceTree.Node, driver: ProtectionDomain) -> None:
        self.driver = driver
        self.pds = [
            ProtectionDomain(f"{driver.name}_setup_{role}", "unused.elf")
            for role in ("tx", "rx", "client")
        ]
        for pd in self.pds:
            # Warning: this will result in PDs and their queues/mappings in the system description
            # Will need to clean this up during render_system
            BuildConstants.sdf().add_pd(pd)
        tx, rx, client = self.pds
        self._net = Sddf.Net(BuildConstants.sdf(), device, driver, tx, rx)
        # SDF rejects connecting a network with no clients, so add dummy here
        self._net.add_client_with_copier(client, rx=False)

    def connect(self) -> bool:
        return self._net.connect()

    def serialise_config(self, output_dir: str) -> None:
        # Let sdfgen create network config files for us in a tmp directory
        with TemporaryDirectory() as temporary_dir:
            assert self._net.serialise_config(temporary_dir)
            name = f"{self.driver.name}_device_resources.data"
            # Copy just the device resources to our actual config output dir
            # This way we can drop driver queue configs and/or any other config
            Path(output_dir, name).write_bytes(Path(temporary_dir, name).read_bytes())


class TrackedNet:
    """Wrapper around the Sddf.Net object"""

    def __init__(
        self,
        ethernet_node: Optional[DeviceTree.Node],
        driver: ProtectionDomain,
        virt_tx: ProtectionDomain,
        virt_rx: ProtectionDomain,
        rx_dma_region: MemoryRegion,
        *,
        interface_index: int,
    ):
        self._net = Sddf.Net(
            BuildConstants.sdf(), ethernet_node, driver, virt_tx, virt_rx, rx_dma_mr=rx_dma_region
        )
        self._driver = driver
        self._virt_tx = virt_tx
        self._virt_rx = virt_rx
        self._interface_index = interface_index
        self._ethernet_node = ethernet_node
        self._driver_config: Optional[NetDriverConfigStruct] = None
        self._tx_clients: list[ProtectionDomain] = []
        self._tx_data_regions: Optional[list[MemoryRegion]] = None

    def add_client_with_copier(
        self,
        client: ProtectionDomain,
        copier: Optional[ProtectionDomain] = None,
        *,
        mac_addr: Optional[str] = None,
        rx: Optional[bool] = None,
        tx: Optional[bool] = None,
    ) -> None:
        self._net.add_client_with_copier(
            client, copier, mac_addr=mac_addr, rx=rx, tx=tx
        )
        if tx is not False:
            self._tx_clients.append(client)

    def connect(self) -> bool:
        return self._net.connect()

    @property
    def driver_config(self) -> NetDriverConfigStruct:
        assert self._driver_config is not None
        return self._driver_config

    @property
    def tx_data_regions(self) -> list[MemoryRegion]:
        assert self._tx_data_regions is not None
        return self._tx_data_regions

    def serialise_config(self, output_dir: str) -> bool:
        if not self._net.serialise_config(output_dir):
            return False
        self._driver_config = NetDriverConfigStruct.from_buffer_copy(
            Path(output_dir, "net_driver.data").read_bytes()
        )
        if self._ethernet_node is None:
            tx_config = NetVirtTxConfigStruct.from_buffer_copy(
                Path(output_dir, "net_virt_tx.data").read_bytes()
            )
            assert tx_config.num_clients == len(self._tx_clients)
            self._tx_data_regions = []
            for client, entry in zip(self._tx_clients, tx_config.clients):
                # Check there is only one Tx pool region (currently this should always be the case)
                assert entry.num_regions == 1
                data = entry.regions[0].data
                # Use the name we will be picking via render_system
                self._tx_data_regions.append(MemoryRegion(
                    BuildConstants.sdf(),
                    f"interface{self._interface_index}/net/tx/data/client/{client.name}",
                    data.region.size,
                    paddr=data.io_addr,
                ))
        return True


def render_system(
    net_systems: list[TrackedNet], devices: Optional[list[NetworkDevice]] = None
) -> str:
    """Render SDF but handle renaming MRs for device-less networks
    
    sdfgen 0.35 gives device-less networks duplicate region names. e.g.
    <memory_region name="generic/net/rx/virt_metadata" ...>
    <memory_region name="generic/net/rx/virt_metadata" ...> instead of
    <memory_region name="interface0/net/rx/virt_metadata" ...>
    <memory_region name="interface1/net/rx/virt_metadata" ...>
    
    This function renames the regions, assuming the provided net_systems
    are in the same order as the .connect() calls that created them.
    
    This also updates corresponding PD mappings. i.e.
    <map mr="generic/net/tx/data/client/icmp_module" ...> becomes
    <map mr="interface0/net/tx/data/client/icmp_module" ...>
    
    For Rx metadata, this updates the Rx virtualiser's mapping.
    For each Tx pool, this updates both the Tx virtualiser's mapping and the Tx client's mapping.
    
    Also removes temporary PDs and MRS created by sdfgen during setup of physical drivers.
    For example:
    
    <memory_region name="ethernet_mmio" .../> KEEP
    <memory_region name="ethernet_driver1_rx_active" .../> KEEP
    <memory_region name="setup_rx_active" .../> REMOVE
    <memory_region name="setup_rx_pool" .../> REMOVE

    <protection_domain name="ethernet_driver1">
        <program_image path="eth_driver1.elf" />
        <map mr="ethernet_mmio" .../> KEEP
        <map mr="ethernet_driver1_rx_active" .../> KEEP
        <map mr="setup_rx_active" .../> REMOVE
        <irq irq="78" id="2" trigger="edge" />
    </protection_domain>

    <protection_domain name="firewall_bridge1"> KEEP ALL
        <program_image path="firewall_bridge1.elf" />
        <map mr="ethernet_driver1_rx_active" .../>
    </protection_domain>

    <protection_domain name="ethernet_driver1_setup_rx"> REMOVE ENITRE PD
        <program_image path="unused.elf" />
        <map mr="setup_rx_active" .../>
        <map mr="setup_rx_pool" .../>
    </protection_domain>

    <channel> KEEP
        <end pd="ethernet_driver1" id="0" />
        <end pd="firewall_bridge1" id="0" />
    </channel>

    <channel> REMOVE
        <end pd="ethernet_driver1" id="3" />
        <end pd="ethernet_driver1_setup_rx" id="0" />
    </channel>
    """
    root = ET.fromstring(BuildConstants.sdf().render())

    # Remove temporary PDs, MRs and channels created by sdfgen during device setup
    temporary_pds = {pd.name for device in devices or [] for pd in device.pds}
    drivers = {device.driver.name for device in devices or []}
    temporary_regions = set()
    for pd in root.findall("protection_domain"):
        if pd.get("name") in temporary_pds:
            temporary_regions.update(mapping.get("mr") for mapping in pd.findall("map"))
            root.remove(pd)
    for channel in root.findall("channel"):
        if any(end.get("pd") in temporary_pds for end in channel.findall("end")):
            root.remove(channel)
    for pd in root.findall("protection_domain"):
        for mapping in pd.findall("map"):
            if mapping.get("mr") in temporary_regions:
                assert pd.get("name") in drivers
                pd.remove(mapping)
    for region in root.findall("memory_region"):
        if region.get("name") in temporary_regions:
            root.remove(region)

    regions = root.findall("memory_region")
    pds = {pd.get("name"): pd for pd in root.findall("protection_domain")}

    for net in net_systems:
        if net._ethernet_node is not None:
            continue
        names = [("rx/virt_metadata", [net._virt_rx])]
        names.extend(
            (f"tx/data/client/{client.name}", [net._virt_tx, client])
            for client in net._tx_clients
        )
        for suffix, endpoints in names:
            old_name = f"generic/net/{suffix}"
            new_name = f"interface{net._interface_index}/net/{suffix}"
            region = next(region for region in regions if region.get("name") == old_name)
            region.set("name", new_name)
            for pd in endpoints:
                mapping = next(mapping for mapping in pds[pd.name].findall("map")
                               if mapping.get("mr") == old_name)
                mapping.set("mr", new_name)
    ET.indent(root, space="    ")
    return ET.tostring(root, encoding="unicode") + "\n"
