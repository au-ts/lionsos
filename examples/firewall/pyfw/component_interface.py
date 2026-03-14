from __future__ import annotations

from dataclasses import dataclass, field
from typing import TYPE_CHECKING, Dict, Iterator, List, Optional, Tuple

from sdfgen import SystemDescription

if TYPE_CHECKING:
    from pyfw.component_arp import ArpRequester, ArpResponder
    from pyfw.component_base import Component
    from pyfw.component_filter import Filter
    from pyfw.component_net_virt import NetVirtRx, NetVirtTx
    from pyfw.specs import FirewallMemoryRegion, TrackedNet

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
    ethernet_driver: Optional[SystemDescription.ProtectionDomain] = field(
        init=False, default=None, repr=False
    )
    rx_virtualiser: Optional[NetVirtRx] = field(init=False, default=None, repr=False)
    tx_virtualiser: Optional[NetVirtTx] = field(init=False, default=None, repr=False)
    arp_requester: Optional[ArpRequester] = field(init=False, default=None, repr=False)
    arp_responder: Optional[ArpResponder] = field(init=False, default=None, repr=False)
    filters: Dict[int, Filter] = field(init=False, default_factory=dict, repr=False)
    rx_dma_region: Optional[FirewallMemoryRegion] = field(
        init=False, default=None, repr=False
    )
    net_system: Optional[TrackedNet] = field(init=False, default=None, repr=False)

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

    @property
    def out_dir(self) -> str:
        import pyfw.constants

        return f"{pyfw.constants.output_dir}/net_data{self.index}"

    def all_components(self) -> Iterator[Component]:
        assert self.rx_virtualiser is not None
        assert self.tx_virtualiser is not None
        assert self.arp_requester is not None
        assert self.arp_responder is not None
        yield self.rx_virtualiser
        yield self.tx_virtualiser
        yield self.arp_requester
        yield self.arp_responder
        for ip_filter in self.filters.values():
            yield ip_filter
