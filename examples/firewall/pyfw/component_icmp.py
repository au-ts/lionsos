# Copyright 2025, UNSW SPDX-License-Identifier: BSD-2-Clause

from typing import List, Optional

from sdfgen import SystemDescription

from pyfw.component_base import Component
from pyfw.config_structs import FwConnectionResource, FwIcmpModuleConfig, RegionResource


class IcmpModule(Component):
    def __init__(
        self,
        sdf: SystemDescription,
        priority: int = 100,
        budget: int = 20000,
    ) -> None:
        super().__init__("icmp_module", "icmp_module.elf", sdf, priority, budget=budget)
        self._ips: List[int] = []
        self._router_conn: Optional[FwConnectionResource] = None

    def add_ip(self, ip: int) -> None:
        self._ips.append(ip)

    def set_router_connection(self, queue: RegionResource, capacity: int, ch: int) -> None:
        self._router_conn = FwConnectionResource(queue=queue, capacity=capacity, ch=ch)

    def finalize_config(self) -> FwIcmpModuleConfig:
        assert self._router_conn is not None
        self.config = FwIcmpModuleConfig(
            ips=self._ips,
            router=self._router_conn,
            num_interfaces=len(self._ips),
        )
        return self.config
