# Copyright 2025, UNSW SPDX-License-Identifier: BSD-2-Clause

from typing import List, Optional

from sdfgen import SystemDescription

from pyfw.component_base import Component
from pyfw.config_structs import (
    FwConnectionResource,
    FwFilterConfig,
    FwRule,
    FwWebserverFilterConfig,
    RegionResource,
)


class Filter(Component):
    """Per-interface protocol filter (ICMP/TCP/UDP)."""

    PROTOCOL_NAMES = {0x01: "icmp", 0x06: "tcp", 0x11: "udp"}

    def __init__(
        self,
        iface_index: int,
        protocol: int,
        sdf: SystemDescription,
        priority: int,
        budget: int = 20000,
    ) -> None:
        if protocol not in self.PROTOCOL_NAMES:
            supported = ", ".join(hex(p) for p in sorted(self.PROTOCOL_NAMES))
            raise ValueError(
                f"Unsupported protocol {protocol:#x}; supported protocol numbers: {supported}"
            )
        proto_name = self.PROTOCOL_NAMES[protocol]
        super().__init__(
            f"{proto_name}_filter{iface_index}",
            f"{proto_name}_filter{iface_index}.elf",
            sdf,
            priority,
            budget=budget,
        )
        self.iface_index = iface_index
        self.protocol = protocol

        self._router_conn: Optional[FwConnectionResource] = None

        self._rules: Optional[RegionResource] = None
        self._rules_capacity = 0

        self._rule_bitmap: Optional[RegionResource] = None
        self._internal_instances: Optional[RegionResource] = None
        self._external_instances: List[RegionResource] = []
        self._instances_capacity = 0

        self._initial_rules: List[FwRule] = []

        self._webserver_ch: Optional[int] = None

    def set_router_connection(self, queue: RegionResource, capacity: int, ch: int) -> None:
        self._router_conn = FwConnectionResource(queue=queue, capacity=capacity, ch=ch)

    def set_rules_region(self, resource: RegionResource, capacity: int) -> None:
        self._rules = resource
        self._rules_capacity = capacity

    def set_rule_bitmap(self, resource: RegionResource) -> None:
        self._rule_bitmap = resource

    def set_instances(
        self,
        internal: RegionResource,
        external: RegionResource,
        capacity: int,
    ) -> None:
        self._internal_instances = internal
        self._external_instances.append(external)
        self._instances_capacity = capacity

    def set_instances_capacity(self, capacity: int) -> None:
        self._instances_capacity = capacity

    def add_initial_rule(
        self,
        *,
        action: int,
        src_ip: int = 0,
        dst_ip: int = 0,
        src_port: int = 0,
        dst_port: int = 0,
        src_subnet: int = 0,
        dst_subnet: int = 0,
        src_port_any: bool = False,
        dst_port_any: bool = False,
    ) -> None:
        self._initial_rules.append(
            FwRule(
                action=action,
                src_ip=src_ip,
                dst_ip=dst_ip,
                src_port=src_port,
                dst_port=dst_port,
                src_subnet=src_subnet,
                dst_subnet=dst_subnet,
                src_port_any=src_port_any,
                dst_port_any=dst_port_any,
                rule_id=0,
            )
        )

    def set_webserver_channel(self, ch: int) -> None:
        self._webserver_ch = ch

    def finalize_config(self) -> FwFilterConfig:
        assert self._router_conn is not None
        assert self._internal_instances is not None
        assert self._rule_bitmap is not None
        assert self._rules is not None
        assert self._rules_capacity > 0
        assert self._webserver_ch is not None

        self.config = FwFilterConfig(
            interface=self.iface_index,
            router=self._router_conn,
            internal_instances=self._internal_instances,
            external_instances=self._external_instances,
            num_interfaces=len(self._external_instances),
            instances_capacity=self._instances_capacity,
            webserver=FwWebserverFilterConfig(
                protocol=self.protocol,
                ch=self._webserver_ch,
                rules=self._rules,
                rules_capacity=self._rules_capacity,
            ),
            rule_id_bitmap=self._rule_bitmap,
            initial_rules=self._initial_rules,
        )
        return self.config
