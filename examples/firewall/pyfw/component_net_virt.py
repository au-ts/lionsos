# Copyright 2025, UNSW SPDX-License-Identifier: BSD-2-Clause

from sdfgen import SystemDescription
from pyfw.component_base import Component
from pyfw.config_structs import (
    DeviceRegionResource,
    FwMaxFwClients,
    FwMaxInterfaces,
    SddfNetMaxClients,
    FwConnectionResource,
    FwDataConnectionResource,
    FwNetVirtRxConfig,
    FwNetVirtTxConfig,
)
from pyfw.constants import (
    NetworkInterface,
    dma_buffer_queue,
    dma_buffer_queue_region,
)
import pyfw.constants
from pyfw.specs import FirewallMemoryRegion

SDF_Channel = SystemDescription.Channel

class NetVirtRx(Component, FwNetVirtRxConfig):
    def __init__(self,
                 net_interface: NetworkInterface,
                 priority: int
    ) -> None:
        # Initialise base component class
        super().__init__(
            f"net_virt_rx{net_interface.index}",
            f"firewall_network_virt_rx{net_interface.index}.elf",
            priority,
        )

        # Store the network interface so sDDF net clients can be added
        self._net_interface: NetworkInterface = net_interface

        # Initialise Rx virtualiser config class
        FwNetVirtRxConfig.__init__(
            self,
            net_interface.index,
            [],
            [],
            [],
        )

    def add_active_net_client(self,
                              client: Component,
                              ethtype: int,
                              subtype: int,
                              tx: bool = False
    ) -> None:

        # Add sDDF net client
        assert self._net_interface.net_system is not None
        self._net_interface.net_system.add_client_with_copier(client.pd, tx = tx)

        # CALLUM: I find this odd, shouldn't the C config be an array of structs?
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
        pyfw.constants.sdf.add_channel(ch)

        self.free_clients.append(FwConnectionResource(
            queue.map(self.pd, "rw"),
            dma_buffer_queue.capacity,
            ch.pd_a_id,
        ))

        return FwConnectionResource(
            queue.map(client.pd, "rw"),
            dma_buffer_queue.capacity,
            ch.pd_b_id,
        )

    def finalise_config(self) -> None:
        assert len(self.free_clients) > 0
        assert len(self.free_clients) <= FwMaxFwClients
        assert len(self.active_client_ethtypes) > 0
        assert len(self.active_client_ethtypes) == len(self.active_client_subtypes)
        assert len(self.active_client_ethtypes) <= SddfNetMaxClients


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

        # Store the network interface so sDDF net clients can be added
        self._net_interface: NetworkInterface = net_interface

        # Store data region as a dictionary to be sorted into list upon finalisation
        self._data_regions: dict[int, DeviceRegionResource] = {}

        # Initialise Tx virtualiser config class
        FwNetVirtTxConfig.__init__(
            self,
            net_interface.index,
            [],
            [],
            [],
        )

    def add_active_fw_client(self, client: Component) -> FwConnectionResource:
        # Create tx queue
        queue = FirewallMemoryRegion(
            "fw_queue_" + self.name + "_" + client.name,
            dma_buffer_queue_region.region_size,
        )

        # Create channel for notifying upon return
        ch = SDF_Channel(self.pd, client.pd)
        pyfw.constants.sdf.add_channel(ch)

        self.active_clients.append(FwConnectionResource(
            queue.map(self.pd, "rw"),
            dma_buffer_queue.capacity,
            ch.pd_a_id,
        ))

        return FwConnectionResource(
            queue.map(client.pd, "rw"),
            dma_buffer_queue.capacity,
            ch.pd_b_id,
        )

    # Adds a free firewall client to the Tx virtualiser, allowing it to return
    # buffers to the correct component. Expects queue with the component, along
    # with data region.
    def add_free_fw_client(self,
                        queue: FwConnectionResource,
                        data: FirewallMemoryRegion,
                        interface_idx: int) -> None:

        # Add data region to list
        # TODO: Check there are not duplicate data regions here?
        # CALLUM: doesn't the code below do that?
        assert interface_idx not in self._data_regions.keys()
        self._data_regions[interface_idx] = data.map_device(self.pd, "r")

        self.free_clients.append(
            FwDataConnectionResource(queue, self._data_regions[interface_idx])
        )

    def finalise_config(self) -> None:
        ordered_regions = []
        for i in range(len(self._data_regions)):
            assert i in self._data_regions.keys()
            ordered_regions.append(self._data_regions[i])

        # Rebuild the serialised ordering so repeated finalisation is stable.
        self.data_regions = ordered_regions
        assert len(self.active_clients) > 0
        assert len(self.active_clients) <= FwMaxFwClients
        assert len(self.free_clients) > 0
        assert len(self.free_clients) <= FwMaxFwClients
        assert len(self.data_regions) > 0
        # TODO: Likely will need to be updated when fixing Webserver Tx git issue
        assert len(self.data_regions) <= FwMaxInterfaces
