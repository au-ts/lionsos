# Copyright 2025, UNSW SPDX-License-Identifier: BSD-2-Clause
from dataclasses import dataclass, field
from typing import List, Optional, Dict, Any, Tuple
from sdfgen import SystemDescription, Sddf, DeviceTree
from config_structs import (
    RegionResource,
    DeviceRegionResource,
    FwConnectionResource,
    FwArpConnection,
    FwDataConnectionResource,
)

ProtectionDomain = SystemDescription.ProtectionDomain
MemoryRegion = SystemDescription.MemoryRegion
Map = SystemDescription.Map
Channel = SystemDescription.Channel


def fw_map(pd, mr, perms):
    """Map a memory region into a protection domain, return the Map."""
    pd_map = Map(mr, pd.get_map_vaddr(mr), perms=perms)
    pd.add_map(pd_map)
    return pd_map


def fw_resource(pd_map, size):
    """Create a RegionResource from a Map and size."""
    return RegionResource(pd_map.vaddr, size)


def fw_device_resource(pd_map, mr):
    """Create a DeviceRegionResource from a Map and its physical MR."""
    return DeviceRegionResource(fw_resource(pd_map, mr.size), mr.paddr)


@dataclass
class TopologyEdge:
    """Descriptor for one edge in the topology graph."""

    src_name: str
    dst_name: str
    category: str
    bidirectional: bool = False
    channel_label: str = ""  # e.g., "ch=3/4"
    queue_perms: str = "rw"
    extra_perms: str = ""  # e.g., "rw/r" for DMA


@dataclass
class ConnectionSpec:
    """A point-to-point connection (queue + channel) between two PDs."""

    name: str
    sdf: SystemDescription
    src_pd: ProtectionDomain
    dst_pd: ProtectionDomain
    capacity: int
    queue_region_size: int
    name_suffix: str = ""
    category: str = ""
    interface_index: Optional[int] = None
    description: str = ""
    _src_resource: Optional[Any] = field(default=None, repr=False)
    _dst_resource: Optional[Any] = field(default=None, repr=False)
    _channel_id_src: Optional[int] = field(default=None, repr=False)
    _channel_id_dst: Optional[int] = field(default=None, repr=False)

    def create(self) -> Tuple[Any, Any]:
        """Creates a queue MR, maps into both PDs, and creates a communication channel."""
        queue_name = (
            "fw_queue_" + self.src_pd.name + "_" + self.dst_pd.name + self.name_suffix
        )
        queue = MemoryRegion(self.sdf, queue_name, self.queue_region_size)
        self.sdf.add_mr(queue)

        src_map = fw_map(self.src_pd, queue, "rw")
        src_region = fw_resource(src_map, self.queue_region_size)

        dst_map = fw_map(self.dst_pd, queue, "rw")
        dst_region = fw_resource(dst_map, self.queue_region_size)

        ch = Channel(self.src_pd, self.dst_pd)
        self.sdf.add_channel(ch)

        self._channel_id_src = ch.pd_a_id
        self._channel_id_dst = ch.pd_b_id
        self._src_resource = FwConnectionResource(src_region, self.capacity, ch.pd_a_id)
        self._dst_resource = FwConnectionResource(dst_region, self.capacity, ch.pd_b_id)

        return (self._src_resource, self._dst_resource)

    @property
    def src_resource(self) -> Any:
        assert self._src_resource is not None, (
            f"ConnectionSpec '{self.name}' not yet created"
        )
        return self._src_resource

    @property
    def dst_resource(self) -> Any:
        assert self._dst_resource is not None, (
            f"ConnectionSpec '{self.name}' not yet created"
        )
        return self._dst_resource

    def label(self) -> str:
        return f"Connection({self.name}: {self.src_pd.name} -> {self.dst_pd.name}, cap={self.capacity})"

    def topology_edges(self) -> List[TopologyEdge]:
        ch = ""
        if self._channel_id_src is not None:
            ch = f"ch={self._channel_id_src}/{self._channel_id_dst}"
        return [
            TopologyEdge(
                src_name=self.src_pd.name,
                dst_name=self.dst_pd.name,
                category=self.category,
                channel_label=ch,
            )
        ]


@dataclass
class ArpConnectionSpec:
    """Bidirectional ARP request/response connection (two queues + one channel)."""

    name: str
    sdf: SystemDescription
    pd1: ProtectionDomain
    pd2: ProtectionDomain
    capacity: int
    queue_region_size: int
    category: str = "arp"
    interface_index: Optional[int] = None
    description: str = ""
    _pd1_resource: Optional[Any] = field(default=None, repr=False)
    _pd2_resource: Optional[Any] = field(default=None, repr=False)
    _channel_id_src: Optional[int] = field(default=None, repr=False)
    _channel_id_dst: Optional[int] = field(default=None, repr=False)

    def create(self) -> Tuple[Any, Any]:
        # Request queue
        req_queue_name = "fw_req_queue_" + self.pd1.name + "_" + self.pd2.name
        req_queue = MemoryRegion(self.sdf, req_queue_name, self.queue_region_size)
        self.sdf.add_mr(req_queue)
        pd1_req_map = fw_map(self.pd1, req_queue, "rw")
        pd1_req_region = fw_resource(pd1_req_map, self.queue_region_size)
        pd2_req_map = fw_map(self.pd2, req_queue, "rw")
        pd2_req_region = fw_resource(pd2_req_map, self.queue_region_size)

        # Response queue
        res_queue_name = "fw_res_queue_" + self.pd1.name + "_" + self.pd2.name
        res_queue = MemoryRegion(self.sdf, res_queue_name, self.queue_region_size)
        self.sdf.add_mr(res_queue)
        pd1_res_map = fw_map(self.pd1, res_queue, "rw")
        pd1_res_region = fw_resource(pd1_res_map, self.queue_region_size)
        pd2_res_map = fw_map(self.pd2, res_queue, "rw")
        pd2_res_region = fw_resource(pd2_res_map, self.queue_region_size)

        ch = Channel(self.pd1, self.pd2)
        self.sdf.add_channel(ch)

        self._channel_id_src = ch.pd_a_id
        self._channel_id_dst = ch.pd_b_id
        self._pd1_resource = FwArpConnection(
            pd1_req_region, pd1_res_region, self.capacity, ch.pd_a_id
        )
        self._pd2_resource = FwArpConnection(
            pd2_req_region, pd2_res_region, self.capacity, ch.pd_b_id
        )
        return (self._pd1_resource, self._pd2_resource)

    @property
    def pd1_resource(self) -> Any:
        assert self._pd1_resource is not None, (
            f"ArpConnectionSpec '{self.name}' not yet created"
        )
        return self._pd1_resource

    @property
    def pd2_resource(self) -> Any:
        assert self._pd2_resource is not None, (
            f"ArpConnectionSpec '{self.name}' not yet created"
        )
        return self._pd2_resource

    def label(self) -> str:
        return f"ArpConnection({self.name}: {self.pd1.name} <-> {self.pd2.name})"

    def topology_edges(self) -> List[TopologyEdge]:
        ch = ""
        if self._channel_id_src is not None:
            ch = f"ch={self._channel_id_src}/{self._channel_id_dst}"
        return [
            TopologyEdge(
                src_name=self.pd1.name,
                dst_name=self.pd2.name,
                category=self.category,
                bidirectional=True,
                channel_label=ch,
            )
        ]


@dataclass
class DataConnectionSpec:
    """Data connection: queue + channel + DMA region mapping."""

    name: str
    sdf: SystemDescription
    src_pd: ProtectionDomain
    dst_pd: ProtectionDomain
    capacity: int
    queue_size: int
    data_mr: Any  # DMA MemoryRegion
    src_data_perms: str
    dst_data_perms: str
    name_suffix: str = ""
    category: str = "data"
    interface_index: Optional[int] = None
    description: str = ""
    _src_resource: Optional[Any] = field(default=None, repr=False)
    _dst_resource: Optional[Any] = field(default=None, repr=False)
    _conn_spec: Optional[ConnectionSpec] = field(default=None, repr=False)

    def create(self) -> Tuple[Any, Any]:
        self._conn_spec = ConnectionSpec(
            name=self.name + "_queue",
            sdf=self.sdf,
            src_pd=self.src_pd,
            dst_pd=self.dst_pd,
            capacity=self.capacity,
            queue_region_size=self.queue_size,
            name_suffix=self.name_suffix,
            category=self.category,
            interface_index=self.interface_index,
        )
        connection = self._conn_spec.create()
        src_data_map = fw_map(self.src_pd, self.data_mr, self.src_data_perms)
        data_region1 = fw_device_resource(src_data_map, self.data_mr)
        dst_data_map = fw_map(self.dst_pd, self.data_mr, self.dst_data_perms)
        data_region2 = fw_device_resource(dst_data_map, self.data_mr)
        self._src_resource = FwDataConnectionResource(connection[0], data_region1)
        self._dst_resource = FwDataConnectionResource(connection[1], data_region2)
        return (self._src_resource, self._dst_resource)

    @property
    def src_resource(self) -> Any:
        assert self._src_resource is not None, (
            f"DataConnectionSpec '{self.name}' not yet created"
        )
        return self._src_resource

    @property
    def dst_resource(self) -> Any:
        assert self._dst_resource is not None, (
            f"DataConnectionSpec '{self.name}' not yet created"
        )
        return self._dst_resource

    def label(self) -> str:
        return f"DataConnection({self.name}: {self.src_pd.name} -> {self.dst_pd.name})"

    def topology_edges(self) -> List[TopologyEdge]:
        ch = ""
        if self._conn_spec and self._conn_spec._channel_id_src is not None:
            ch = f"ch={self._conn_spec._channel_id_src}/{self._conn_spec._channel_id_dst}"
        return [
            TopologyEdge(
                src_name=self.src_pd.name,
                dst_name=self.dst_pd.name,
                category=self.category,
                channel_label=ch,
                extra_perms=f"{self.src_data_perms}/{self.dst_data_perms}",
            )
        ]


@dataclass
class SharedRegionSpec:
    """Shared memory region between two PDs with different permissions."""

    name: str
    sdf: SystemDescription
    size: int
    owner_pd: ProtectionDomain
    peer_pd: ProtectionDomain
    owner_perms: str = "rw"
    peer_perms: str = "r"
    category: str = ""
    interface_index: Optional[int] = None
    description: str = ""
    _owner_view: Optional[Any] = field(default=None, repr=False)
    _peer_view: Optional[Any] = field(default=None, repr=False)

    def map(self) -> Tuple[Any, Any]:
        region_name = self.name + "_" + self.owner_pd.name + "_" + self.peer_pd.name
        mr = MemoryRegion(self.sdf, region_name, self.size)
        self.sdf.add_mr(mr)
        owner_map = fw_map(self.owner_pd, mr, self.owner_perms)
        self._owner_view = fw_resource(owner_map, self.size)
        peer_map = fw_map(self.peer_pd, mr, self.peer_perms)
        self._peer_view = fw_resource(peer_map, self.size)
        return (self._owner_view, self._peer_view)

    @property
    def owner_view(self) -> Any:
        assert self._owner_view is not None, (
            f"SharedRegionSpec '{self.name}' not yet mapped"
        )
        return self._owner_view

    @property
    def peer_view(self) -> Any:
        assert self._peer_view is not None, (
            f"SharedRegionSpec '{self.name}' not yet mapped"
        )
        return self._peer_view

    def label(self) -> str:
        return f"SharedRegion({self.name}: {self.owner_pd.name}[{self.owner_perms}] <-> {self.peer_pd.name}[{self.peer_perms}])"

    def topology_mappings(self) -> List[Tuple[str, str]]:
        return [
            (self.owner_pd.name, self.owner_perms),
            (self.peer_pd.name, self.peer_perms),
        ]


@dataclass
class PrivateRegionSpec:
    """A private memory region owned by a single PD."""

    name: str
    sdf: SystemDescription
    size: int
    pd: ProtectionDomain
    perms: str = "rw"
    category: str = ""
    interface_index: Optional[int] = None
    description: str = ""
    _resource: Optional[Any] = field(default=None, repr=False)

    def create(self) -> Any:
        region_name = self.name + "_" + self.pd.name
        mr = MemoryRegion(self.sdf, region_name, self.size)
        self.sdf.add_mr(mr)
        pd_map = fw_map(self.pd, mr, self.perms)
        self._resource = fw_resource(pd_map, self.size)
        return self._resource

    @property
    def resource(self) -> Any:
        assert self._resource is not None, (
            f"PrivateRegionSpec '{self.name}' not yet created"
        )
        return self._resource

    def label(self) -> str:
        return f"PrivateRegion({self.name}: {self.pd.name}[{self.perms}])"

    def topology_mappings(self) -> List[Tuple[str, str]]:
        return [(self.pd.name, self.perms)]


@dataclass
class MappedRegionSpec:
    """An existing memory region mapped into a PD."""

    name: str
    mr: Any  # existing MemoryRegion
    pd: ProtectionDomain
    perms: str = "rw"
    size: int = 0  # override size (if different from mr.size)
    category: str = ""
    interface_index: Optional[int] = None
    description: str = ""
    _resource: Optional[Any] = field(default=None, repr=False)

    def map(self) -> Any:
        region_size = self.size if self.size else self.mr.size
        pd_map = fw_map(self.pd, self.mr, self.perms)
        self._resource = fw_resource(pd_map, region_size)
        return self._resource

    @property
    def resource(self) -> Any:
        assert self._resource is not None, (
            f"MappedRegionSpec '{self.name}' not yet mapped"
        )
        return self._resource

    def label(self) -> str:
        return f"MappedRegion({self.name}: {self.pd.name}[{self.perms}])"


@dataclass
class TxActiveClient:
    """Connection carrying routed packets into this interface's TX virtualiser."""

    src_interface_index: int
    resource: FwDataConnectionResource


@dataclass
class TxFreeClient:
    """Buffer return path from this interface's TX virtualiser back to a source interface's RX virtualiser."""

    src_interface_index: int
    resource: FwDataConnectionResource


@dataclass
class OutboundConnection:
    """Router's outbound data path from this interface toward a destination interface's TX virtualiser."""

    dest_interface_index: int
    resource: FwDataConnectionResource


@dataclass
class NetEdge:
    """Lightweight representation of sDDF Net wiring for topology tracking."""

    src_pd: ProtectionDomain
    dst_pd: ProtectionDomain
    label: str = ""
    bidirectional: bool = True
    interface_index: Optional[int] = None


@dataclass
class InterfaceWiring:
    """Named wiring for a single network interface."""

    interface: "NetworkInterface"

    # Named config slots
    rx_virt_config: Optional[Any] = None
    tx_virt_config: Optional[Any] = None
    arp_requester_config: Optional[Any] = None
    arp_responder_config: Optional[Any] = None
    filter_configs: Dict[int, Any] = field(default_factory=dict)  # protocol -> config

    # Named Spec objects (ConnectionSpec, ArpConnectionSpec, DataConnectionSpec, SharedRegionSpec)
    connections: Dict[str, Any] = field(default_factory=dict)
    regions: Dict[str, Any] = field(default_factory=dict)

    outbound_connections: List[OutboundConnection] = field(default_factory=list)
    tx_active_clients: List[TxActiveClient] = field(default_factory=list)
    tx_free_clients: List[TxFreeClient] = field(default_factory=list)
    net_edges: List[NetEdge] = field(default_factory=list)

    def configs_iter(self):
        """Yield (pd, config) for serialization."""
        if self.rx_virt_config:
            yield (self.interface.rx_virt, self.rx_virt_config)
        if self.tx_virt_config:
            yield (self.interface.tx_virt, self.tx_virt_config)
        if self.arp_requester_config:
            yield (self.interface.arp_requester, self.arp_requester_config)
        if self.arp_responder_config:
            yield (self.interface.arp_responder, self.arp_responder_config)
        for proto, cfg in self.filter_configs.items():
            yield (self.interface.filters[proto], cfg)

    def all_pds(self) -> List[Tuple[ProtectionDomain, str]]:
        """Return all (pd, role_label) pairs for graph node rendering."""
        pds = []
        if getattr(self.interface, "router", None):
            pds.append((self.interface.router, "Router"))
        if self.interface.driver:
            pds.append((self.interface.driver, "Driver"))
        if self.interface.rx_virt:
            pds.append((self.interface.rx_virt, "RX Virt"))
        if self.interface.tx_virt:
            pds.append((self.interface.tx_virt, "TX Virt"))
        if self.interface.arp_requester:
            pds.append((self.interface.arp_requester, "ARP Req"))
        if self.interface.arp_responder:
            pds.append((self.interface.arp_responder, "ARP Resp"))
        proto_names = {0x01: "ICMP", 0x06: "TCP", 0x11: "UDP"}
        for proto, pd in self.interface.filters.items():
            pds.append((pd, f"{proto_names.get(proto, hex(proto))} Filter"))
        return pds


class TrackedNet:
    """Wrapper around Sddf.Net to record net wiring edges for topology tracking."""

    def __init__(
        self,
        sdf_obj: SystemDescription,
        ethernet_node: DeviceTree.Node,
        driver: ProtectionDomain,
        virt_tx: ProtectionDomain,
        virt_rx: ProtectionDomain,
        rx_dma_region: Optional[MemoryRegion],
        *,
        interface_index: int,
        wiring: Optional[InterfaceWiring] = None,
    ):
        self._net = Sddf.Net(
            sdf_obj, ethernet_node, driver, virt_tx, virt_rx, rx_dma_region
        )
        self._obj = self._net._obj
        self._driver = driver
        self._virt_tx = virt_tx
        self._virt_rx = virt_rx
        self._interface_index = interface_index
        self._edge_sink = wiring.net_edges if wiring else None
        self._edge_keys = set()

        self._add_edge(self._driver, self._virt_rx, "driver<->virt_rx")
        self._add_edge(self._driver, self._virt_tx, "driver<->virt_tx")

    def _edge_key(
        self,
        src: ProtectionDomain,
        dst: ProtectionDomain,
        label: str,
        bidirectional: bool,
    ) -> tuple:
        if bidirectional:
            names = tuple(sorted((src.name, dst.name)))
        else:
            names = (src.name, dst.name)
        return (names, label, bidirectional, self._interface_index)

    def _add_edge(
        self,
        src: ProtectionDomain,
        dst: ProtectionDomain,
        label: str,
        *,
        bidirectional: bool = True,
    ) -> None:
        if self._edge_sink is None:
            return
        key = self._edge_key(src, dst, label, bidirectional)
        if key in self._edge_keys:
            return
        self._edge_keys.add(key)
        self._edge_sink.append(
            NetEdge(
                src_pd=src,
                dst_pd=dst,
                label=label,
                bidirectional=bidirectional,
                interface_index=self._interface_index,
            )
        )

    def add_client_with_copier(
        self,
        client: ProtectionDomain,
        copier: Optional[ProtectionDomain] = None,
        *,
        mac_addr: Optional[str] = None,
        rx: Optional[bool] = None,
        tx: Optional[bool] = None,
    ) -> None:
        rx_enabled = True if rx is None else bool(rx)
        tx_enabled = True if tx is None else bool(tx)

        if rx_enabled:
            if copier is None:
                self._add_edge(self._virt_rx, client, "virt_rx<->client")
            else:
                self._add_edge(self._virt_rx, copier, "virt_rx<->copier")
                self._add_edge(copier, client, "copier<->client")

        if tx_enabled:
            self._add_edge(self._virt_tx, client, "virt_tx<->client")

        self._net.add_client_with_copier(
            client, copier, mac_addr=mac_addr, rx=rx, tx=tx
        )

    def connect(self) -> bool:
        return self._net.connect()

    def serialise_config(self, output_dir: str) -> bool:
        return self._net.serialise_config(output_dir)
