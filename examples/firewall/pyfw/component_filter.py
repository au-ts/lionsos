# Copyright 2026, UNSW SPDX-License-Identifier: BSD-2-Clause

from sdfgen import SystemDescription
from pyfw.component_base import Component
from pyfw.constants import (
    BuildConstants,
    supported_protocols,
    supported_filter_actions,
    filter_instances_buffer,
    filter_instances_region,
    filter_rules_buffer,
    filter_rules_region,
    filter_rule_bitmap_region,
    dma_buffer_queue,
    dma_buffer_queue_region,
)
from pyfw.specs import FirewallMemoryRegion
from config_structs import (
    FwConnectionResource,
    FwFilterConfig,
    FwWebserverFilterConfig,
)

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

        initial_rules = BuildConstants.initial_rules()
        assert iface_index < len(initial_rules)
        # Initialise filter config class
        FwFilterConfig.__init__(
            self,
            interface=iface_index,
            router=None,
            internal_instances=self._local_instance_mr.map(self.pd, "rw"),
            external_instances=[],
            instances_capacity=filter_instances_buffer.capacity,
            webserver=FwWebserverFilterConfig(
                protocol=protocol,
                ch=None,
                rules=self._filter_rules_mr.map(self.pd, "rw"),
                rules_capacity=filter_rules_buffer.capacity,
                actions=supported_filter_actions[protocol],
            ),
            rule_id_bitmap=rule_id_bitmap_mr.map(self.pd, "rw"),
            icmp_module=None,
            initial_rules=initial_rules[iface_index][protocol],
        )

    def connect_webserver(self, webserver: Component) -> FwWebserverFilterConfig:
        # Map rules region into webserver
       web_rules_region = self._filter_rules_mr.map(webserver.pd, "r")

       # Create filter-webserver channel
       web_update_ch = SDF_Channel(webserver.pd, self.pd, pp_a=True)
       BuildConstants.sdf().add_channel(web_update_ch)

       # Update filter config
       assert self.webserver is not None
       self.webserver.ch = web_update_ch.pd_b_id

       # Return webserver config
       assert self.webserver.protocol is not None
       assert self.webserver.actions is not None
       return FwWebserverFilterConfig(
                protocol=self.webserver.protocol,
                ch=web_update_ch.pd_a_id,
                rules=web_rules_region,
                rules_capacity=filter_rules_buffer.capacity,
                actions=self.webserver.actions,
            )

    def connect_router(self, router: Component) -> FwConnectionResource:
        # Create tx queue with router
        router_queue_mr = FirewallMemoryRegion(
            "fw_queue_" + self.name + "_" + router.name,
            dma_buffer_queue_region.region_size,
        )

        # Create filter-router channel
        router_ch = SDF_Channel(self.pd, router.pd)
        BuildConstants.sdf().add_channel(router_ch)

        # Update filter config
        self.router = FwConnectionResource(
            queue=router_queue_mr.map(self.pd, "rw"),
            capacity=dma_buffer_queue.capacity,
            ch=router_ch.pd_a_id,

        )

        # Return router config
        return FwConnectionResource(
            queue=router_queue_mr.map(router.pd, "rw"),
            capacity=dma_buffer_queue.capacity,
            ch=router_ch.pd_b_id,

        )

    def finalise_config(self) -> None:
        # Create external instance mappings
        external_mrs = Filter.instance_regions[self.webserver.protocol]
        self.external_instances = [
            instance_mr.map(self.pd, "r") for instance_mr in external_mrs if instance_mr != self._local_instance_mr
        ]
        assert len(self.external_instances) == len(BuildConstants.interfaces()) - 1
