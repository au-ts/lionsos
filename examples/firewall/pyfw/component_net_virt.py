# Copyright 2026, UNSW SPDX-License-Identifier: BSD-2-Clause

from sdfgen import SystemDescription
from pyfw.component_base import Component
from pyfw.component_net_interface import NetworkInterface
from pyfw.constants import (
    BuildConstants,
    dma_buffer_queue,
    dma_buffer_queue_region,
)
from pyfw.specs import FirewallMemoryRegion, TrackedNet
from config_structs import (
    DeviceRegionResource,
    FwConnectionResource,
    FwDataConnectionResource,
    FwNetVirtRxConfig,
    FwNetVirtTxConfig,
)

SDF_Channel = SystemDescription.Channel

class NetVirtRx(Component, FwNetVirtRxConfig):
    def __init__(self,
                 net_interface: NetworkInterface,
                 sddf_net: TrackedNet,
                 priority: int
    ) -> None:
        # Initialise base component class
        super().__init__(
            f"net_virt_rx{net_interface.index}",
            f"firewall_network_virt_rx{net_interface.index}.elf",
            priority,
        )

        # Store the network interface so sDDF net clients can be added
        self._sddf_net: TrackedNet = sddf_net

        # Initialise Rx virtualiser config class
        FwNetVirtRxConfig.__init__(
            self,
            interface=net_interface.index,
            active_client_ethtypes=[],
            active_client_subtypes=[],
            free_clients=[],
        )

    def add_active_net_client(self,
                              client: Component,
                              ethtype: int,
                              subtype: int,
                              tx: bool = False
    ) -> None:

        # Add sDDF net client
        self._sddf_net.add_client_with_copier(client.pd, tx = tx)

        # Ensure traffic type is unique
        assert self.active_client_ethtypes is not None
        assert self.active_client_subtypes is not None
        for cli in range(len(self.active_client_ethtypes)):
            assert self.active_client_ethtypes[cli] != ethtype or self.active_client_subtypes != subtype
        # Set what traffic gets forwarded to the client
        self.active_client_ethtypes.append(ethtype)
        self.active_client_subtypes.append(subtype)

    def add_free_fw_client(self, client: Component) -> FwConnectionResource:
        # Create return queue for DMA buffers
        queue = FirewallMemoryRegion(
            "fw_queue_" + self.name + "_" + client.name,
            dma_buffer_queue_region.region_size,
        )

        # Create channel for notifying upon return
        ch = SDF_Channel(self.pd, client.pd)
        BuildConstants.sdf().add_channel(ch)

        assert self.free_clients is not None
        self.free_clients.append(FwConnectionResource(
            queue=queue.map(self.pd, "rw"),
            capacity=dma_buffer_queue.capacity,
            ch=ch.pd_a_id,
        ))

        return FwConnectionResource(
            queue=queue.map(client.pd, "rw"),
            capacity=dma_buffer_queue.capacity,
            ch=ch.pd_b_id,
        )

    def finalise_config(self) -> None:
        assert self.active_client_ethtypes is not None
        assert self.active_client_subtypes is not None
        assert len(self.active_client_ethtypes) == len(self.active_client_subtypes)


class NetVirtTx(Component, FwNetVirtTxConfig):
    def __init__(
        self,
        net_interface: NetworkInterface,
        priority: int,
        budget: int = 20000,
    ) -> None:
        # Initialise base component class
        super().__init__(
            f"net_virt_tx{net_interface.index}",
            f"firewall_network_virt_tx{net_interface.index}.elf",
            priority,
            budget,
        )

        # Store data region as a dictionary to be sorted into list upon finalisation
        self._data_regions: dict[int, DeviceRegionResource] = {}

        # Initialise Tx virtualiser config class
        FwNetVirtTxConfig.__init__(
            self,
            interface=net_interface.index,
            active_clients=[],
            data_regions=[],
            free_clients=[],
        )

    def add_active_fw_client(self, client: Component) -> FwConnectionResource:
        # Create tx queue
        queue = FirewallMemoryRegion(
            "fw_queue_" + self.name + "_" + client.name,
            dma_buffer_queue_region.region_size,
        )

        # Create channel for notifying upon return
        ch = SDF_Channel(self.pd, client.pd)
        BuildConstants.sdf().add_channel(ch)

        assert self.active_clients is not None
        self.active_clients.append(FwConnectionResource(
            queue=queue.map(self.pd, "rw"),
            capacity=dma_buffer_queue.capacity,
            ch=ch.pd_a_id,
        ))

        return FwConnectionResource(
            queue=queue.map(client.pd, "rw"),
            capacity=dma_buffer_queue.capacity,
            ch=ch.pd_b_id,
        )

    # Adds a free firewall client to the Tx virtualiser, allowing it to return
    # buffers to the correct component. Expects queue with the component, along
    # with data region.
    def add_free_fw_client(self,
                        queue: FwConnectionResource,
                        data: FirewallMemoryRegion,
                        interface_idx: int) -> None:

        # Ensure region does not already have a free queue
        assert data.mr.paddr not in (data_map.io_addr for data_map in self._data_regions.values())
        # Add data region to list
        assert interface_idx not in self._data_regions.keys()
        self._data_regions[interface_idx] = data.map_device(self.pd, "r")

        assert self.free_clients is not None
        self.free_clients.append(
            FwDataConnectionResource(
                conn=queue,
                data=self._data_regions[interface_idx],
            )
        )

    def finalise_config(self) -> None:
        assert self.data_regions is not None and len(self.data_regions) == 0
        for i in range(len(self._data_regions)):
            assert i in self._data_regions.keys()
            self.data_regions.append(self._data_regions[i])
