# Copyright 2025, UNSW SPDX-License-Identifier: BSD-2-Clause

from sdfgen import SystemDescription
from pyfw.component_base import Component
from pyfw.config_structs import (
    FwConnectionResource,
    FwFilterConfig,
    FwMaxInitialFilterRules,
    FwMaxInterfaces,
    FwWebserverFilterConfig,
)
from pyfw.constants import (
    interfaces,
    supported_protocols,
    initial_rules,
    filter_instances_buffer,
    filter_instances_region,
    filter_rules_buffer,
    filter_rules_region,
    filter_rule_bitmap_region,
    dma_buffer_queue,
    dma_buffer_queue_region,
)
import pyfw.constants
from pyfw.specs import FirewallMemoryRegion

SDF_Channel = SystemDescription.Channel

class Filter(Component, FwFilterConfig):
    """Per-interface protocol filter."""
    instance_regions: dict[int, list[FirewallMemoryRegion]] = {}

    def __init__(
        self,
        iface_index: int,
        protocol: int,
        priority: int,
        budget: int = 20000,
    ) -> None:
        # Ensure protocol is supported
        if protocol not in supported_protocols:
            supported = ", ".join(hex(p) for p in sorted(supported_protocols))
            raise ValueError(
                f"Unsupported protocol {protocol:#x}; supported protocol numbers: {supported}"
            )
        proto_name = supported_protocols[protocol]

        # Initialise base component class
        super().__init__(
            f"{proto_name}_filter{iface_index}",
            f"{proto_name}_filter{iface_index}.elf",
            priority,
            budget,
        )

        # Create local instances region
        self._local_instance_mr = FirewallMemoryRegion(
            "instances_" + self.name,
            filter_instances_region.region_size,
        )

        # Store region to map into filters of the same protocol later
        if protocol in Filter.instance_regions.keys():
            Filter.instance_regions[protocol].append(self._local_instance_mr)
        else:
            Filter.instance_regions[protocol] = [self._local_instance_mr]

        # Create filter rule region
        self._filter_rules_mr = FirewallMemoryRegion(
            "filter_rules_" + self.name,
            filter_rules_region.region_size,
        )

        # Create rule id bitmap region
        rule_id_bitmap_mr = FirewallMemoryRegion(
            "rule_bitmap_" + self.name,
            filter_rule_bitmap_region.region_size,
        )

        # Initialise filter config class
        FwFilterConfig.__init__(
            self,
            iface_index,
            None,
            self._local_instance_mr.map(self.pd, "rw"),
            [],
            filter_instances_buffer.capacity,
            FwWebserverFilterConfig(
                protocol,
                None,
                self._filter_rules_mr.map(self.pd, "rw"),
                filter_rules_buffer.capacity,
            ),
            rule_id_bitmap_mr.map(self.pd, "rw"),
            initial_rules[iface_index][protocol]
        )

    def connect_webserver(self, webserver: Component) -> FwWebserverFilterConfig:
        # Map rules region into webserver
       web_rules_region = self._filter_rules_mr.map(webserver.pd, "r")

       # Create filter-webserver channel
       web_update_ch = SDF_Channel(webserver.pd, self.pd, pp_a=True)
       pyfw.constants.sdf.add_channel(web_update_ch)

       # Update filter config
       assert self.webserver is not None
       self.webserver.ch = web_update_ch.pd_b_id

       # Return webserver config
       assert self.webserver.protocol is not None
       return FwWebserverFilterConfig(
                self.webserver.protocol,
                web_update_ch.pd_a_id,
                web_rules_region,
                filter_rules_buffer.capacity,
            )

    def connect_router(self, router: Component) -> FwConnectionResource:
        # Create tx queue with router
        router_queue_mr = FirewallMemoryRegion(
            "fw_queue_" + self.name + "_" + router.name,
            dma_buffer_queue_region.region_size,
        )

        # Create filter-router channel
        router_ch = SDF_Channel(self.pd, router.pd)
        pyfw.constants.sdf.add_channel(router_ch)

        # Update filter config
        self.router = FwConnectionResource(
            router_queue_mr.map(self.pd, "rw"),
            dma_buffer_queue.capacity,
            router_ch.pd_a_id,

        )

        # Return router config
        return FwConnectionResource(
            router_queue_mr.map(router.pd, "rw"),
            dma_buffer_queue.capacity,
            router_ch.pd_b_id,

        )

    def finalise_config(self) -> None:
        assert self.router is not None
        assert self.webserver is not None
        assert self.internal_instances is not None
        webserver_config = self.webserver
        assert len(self.initial_rules) > 0
        assert len(self.initial_rules) <= FwMaxInitialFilterRules
        assert webserver_config.ch is not None
        assert webserver_config.protocol is not None
        protocol_regions = Filter.instance_regions[webserver_config.protocol]
        assert len(interfaces) == len(protocol_regions)
        assert len(protocol_regions) <= FwMaxInterfaces

        # Build external instance mappings
        self.external_instances = [
            instance.map(self.pd, "r") for instance in protocol_regions if instance != self._local_instance_mr
        ]
        assert len(self.external_instances) == len(protocol_regions) - 1
        assert len(self.external_instances) <= FwMaxInterfaces
