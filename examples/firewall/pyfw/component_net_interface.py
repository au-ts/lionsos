# Copyright 2026, UNSW SPDX-License-Identifier: BSD-2-Clause

from dataclasses import dataclass, field
from typing import List, Tuple

@dataclass
class InterfacePriorities:
    ethernet_driver: int = 101
    tx_virtualiser: int = 100
    rx_virtualiser: int = 99
    arp_requester: int = 98
    arp_responder: int = 95
    filters: dict[str, int] = field(
        default_factory=lambda: {
            "icmp": 90,
            "udp": 91,
            "tcp": 92,
        }
    )

@dataclass
class NetworkInterface:
    index: int
    name: str
    board_ethernet: str
    mac: Tuple[int, ...]
    ip: str
    subnet_bits: int
    priorities: InterfacePriorities = field(default_factory=InterfacePriorities)

    @property
    def ip_int(self) -> int:
        import ipaddress

        ip_split = self.ip.split(".")
        ip_split.reverse()
        reversed_ip = ".".join(ip_split)
        return int(ipaddress.IPv4Address(reversed_ip))

    @property
    def mac_list(self) -> List[int]:
        return list(self.mac)
