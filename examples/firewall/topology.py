# Copyright 2025, UNSW SPDX-License-Identifier: BSD-2-Clause
from typing import Dict, Any

EDGE_STYLES = {
    "data": {"color": "#2196F3", "penwidth": "2.0"},
    "buffer_return": {"color": "#9E9E9E"},
    "arp": {"color": "#FF9800", "penwidth": "1.5"},
    "filter": {"color": "#F44336", "penwidth": "1.5"},
    "icmp": {"color": "#9C27B0", "penwidth": "1.5"},
    "sddf_net": {"color": "#607D8B"},
}

REGION_STYLES = {
    "arp_cache": {"fillcolor": "#FFF3E0"},
    "routing_table": {"fillcolor": "#E8F5E9"},
    "filter_rules": {"fillcolor": "#FFEBEE"},
    "filter_instances": {"fillcolor": "#FBE9E7"},
    "arp_packet_queue": {"fillcolor": "#FFF8E1"},
    "rule_bitmap": {"fillcolor": "#F3E5F5"},
    "dma_buffer": {"fillcolor": "#E0F7FA"},
}

SHOW_NODE_LABELS = True
SHOW_EDGE_LABELS = True
SHOW_EDGE_PERMS = False
SHOW_LEGEND = True

QUEUE_PERMS_LABEL = "rw"


def _collect_all_connections(builder) -> Dict[str, Any]:
    """Collect all ConnectionSpec/ArpConnectionSpec/DataConnectionSpec objects."""
    conns = dict(builder._global_connections)
    for iface in builder.interfaces:
        for name, spec in iface.interface_wiring.connections.items():
            conns[f"iface{iface.index}_{name}"] = spec
    return conns


def _collect_all_regions(builder) -> Dict[str, Any]:
    """Collect all SharedRegionSpec objects."""
    regions = dict(builder._global_regions)
    for iface in builder.interfaces:
        for name, spec in iface.interface_wiring.regions.items():
            regions[f"iface{iface.index}_{name}"] = spec
    return regions


def _collect_all_net_edges(builder):
    """Collect inferred sDDF Net edges from interface wiring."""
    edges = []
    for iface in builder.interfaces:
        if hasattr(iface.interface_wiring, "net_edges"):
            edges.extend(iface.interface_wiring.net_edges)
    return edges


def generate_topology_dot(builder) -> str:
    """Generate a graphviz DOT string from all Spec objects."""
    lines = ["digraph firewall_topology {"]
    lines.append("    rankdir=LR;")
    lines.append(
        '    graph [overlap=false, splines=true, nodesep=0.4, ranksep=0.6, size="11,8.5!", ratio=fill];'
    )
    lines.append('    node [shape=box, style=filled, fillcolor="#E3F2FD"];')
    lines.append("    edge [fontsize=9, labelfloat=true];")
    lines.append("")
    label_node_counter = 0

    def _emit_edge(
        src: str,
        dst: str,
        *,
        label: str = "",
        attr_str: str = "",
        dir_val: str = "",
        arrowhead: str = "",
        arrowtail: str = "",
    ) -> None:
        attrs = []
        if label:
            attrs.append(f'label="{label}"')
        if dir_val:
            attrs.append(f"dir={dir_val}")
        if arrowhead:
            attrs.append(f"arrowhead={arrowhead}")
        if arrowtail:
            attrs.append(f"arrowtail={arrowtail}")
        if attr_str:
            attrs.append(attr_str)
        lines.append(f'    "{src}" -> "{dst}" [{", ".join(attrs)}];')

    def _emit_edge_with_label_node(
        src: str,
        dst: str,
        label: str,
        *,
        attr_str: str = "",
        dir_val: str = "",
        arrowhead: str = "",
        arrowtail: str = "",
    ) -> None:
        nonlocal label_node_counter
        node_id = f"perm_label_{label_node_counter}"
        label_node_counter += 1
        lines.append(
            f'    "{node_id}" [shape=box, label="{label}", fontsize=9, margin="0.05,0.02", '
            f'width=0, height=0, style="filled", fillcolor="#FFFFFF", color="#FFFFFF"];'
        )
        if dir_val == "both" and not arrowhead and not arrowtail:
            _emit_edge(
                src,
                node_id,
                attr_str=attr_str,
                dir_val="both",
                arrowhead="none",
                arrowtail="normal",
            )
            _emit_edge(
                node_id,
                dst,
                attr_str=attr_str,
                dir_val="both",
                arrowhead="normal",
                arrowtail="none",
            )
            return

        if dir_val == "none":
            _emit_edge(
                src,
                node_id,
                attr_str=attr_str,
                dir_val="none",
                arrowhead="none",
                arrowtail="none",
            )
            _emit_edge(
                node_id,
                dst,
                attr_str=attr_str,
                dir_val="none",
                arrowhead="none",
                arrowtail="none",
            )
            return

        _emit_edge(
            src,
            node_id,
            attr_str=attr_str,
            dir_val=dir_val,
            arrowhead=arrowhead or "none",
            arrowtail=arrowtail,
        )
        _emit_edge(
            node_id,
            dst,
            attr_str=attr_str,
            dir_val=dir_val,
            arrowhead=arrowhead,
            arrowtail=arrowtail,
        )

    def _emit_connection_edge(src, dst, *, full_label, perms_label, **kwargs):
        if SHOW_EDGE_LABELS:
            _emit_edge_with_label_node(src, dst, full_label, **kwargs)
        elif SHOW_EDGE_PERMS:
            _emit_edge_with_label_node(src, dst, perms_label, **kwargs)
        else:
            _emit_edge(src, dst, **kwargs)

    # Subgraph clusters per interface
    for iface in builder.interfaces:
        lines.append(f"    subgraph cluster_iface{iface.index} {{")
        if SHOW_NODE_LABELS:
            lines.append(
                f'        label="Interface {iface.index}: {iface.name} ({iface.ip}/{iface.subnet_bits})";'
            )
        lines.append("        style=rounded;")
        lines.append('        color="#90CAF9";')
        for pd, role in iface.interface_wiring.all_pds():
            if SHOW_NODE_LABELS:
                lines.append(f'        "{pd.name}" [label="{role}\\n{pd.name}"];')
            else:
                lines.append(f'        "{pd.name}" [label=""];')
        lines.append("    }")
        lines.append("")

    # Global PDs outside clusters
    for pd, label in [
        (builder.router, "Router"),
        (builder.webserver, "Webserver"),
        (builder.icmp_module, "ICMP Module"),
    ]:
        if pd:
            if SHOW_NODE_LABELS:
                lines.append(
                    f'    "{pd.name}" [label="{label}\\n{pd.name}", fillcolor="#C8E6C9"];'
                )
            else:
                lines.append(f'    "{pd.name}" [label="", fillcolor="#C8E6C9"];')
    lines.append("")

    # Connection edges
    all_conns = _collect_all_connections(builder)
    for name, spec in all_conns.items():
        for edge in spec.topology_edges():
            style_attrs = EDGE_STYLES.get(edge.category, {})
            attr_str = ", ".join(f'{k}="{v}"' for k, v in style_attrs.items())

            parts = [name]
            if edge.channel_label:
                parts.append(edge.channel_label)
            parts.append(f"perm={edge.queue_perms}/{edge.queue_perms}")
            if edge.extra_perms:
                parts.append(f"dma={edge.extra_perms}")

            dir_kw = {"dir_val": "both"} if edge.bidirectional else {}
            _emit_connection_edge(
                edge.src_name,
                edge.dst_name,
                full_label=" ".join(parts),
                perms_label=QUEUE_PERMS_LABEL,
                attr_str=attr_str,
                **dir_kw,
            )

            if edge.extra_perms:
                _emit_connection_edge(
                    edge.src_name,
                    edge.dst_name,
                    full_label="DMA",
                    perms_label=edge.extra_perms,
                    attr_str='color="#BBDEFB"',
                    dir_val="none",
                    arrowhead="none",
                    arrowtail="none",
                )
    lines.append("")

    # sDDF Net edges (inferred)
    all_net_edges = _collect_all_net_edges(builder)
    for edge in all_net_edges:
        style_attrs = EDGE_STYLES.get("sddf_net", {})
        attr_str = ", ".join(f'{k}="{v}"' for k, v in style_attrs.items())
        label = ""
        if SHOW_EDGE_LABELS:
            label = edge.label if edge.label else "sddf_net"
            if edge.interface_index is not None:
                label = f"{label} (iface {edge.interface_index})"
        if edge.bidirectional:
            _emit_edge_with_label_node(
                edge.src_pd.name,
                edge.dst_pd.name,
                label,
                dir_val="both",
                attr_str=attr_str,
            )
        else:
            _emit_edge_with_label_node(
                edge.src_pd.name,
                edge.dst_pd.name,
                label,
                attr_str=attr_str,
            )
    lines.append("")

    # Region nodes (shared and private)
    all_regions = _collect_all_regions(builder)
    for name, spec in all_regions.items():
        region_style = REGION_STYLES.get(spec.category, {})
        fillcolor = region_style.get("fillcolor", "#FFFFFF")
        if SHOW_NODE_LABELS:
            lines.append(
                f'    "{name}" [shape=note, label="{name}\\n{spec.size} bytes", fillcolor="{fillcolor}"];'
            )
        else:
            lines.append(
                f'    "{name}" [shape=note, label="", fillcolor="{fillcolor}"];'
            )
        for pd_name, perms in spec.topology_mappings():
            _emit_connection_edge(
                pd_name,
                name,
                full_label=perms,
                perms_label=perms,
                dir_val="none",
                arrowhead="none",
                arrowtail="none",
            )
    lines.append("")

    # Legend
    if SHOW_LEGEND:
        lines.append("    subgraph cluster_legend {")
        lines.append('        label="Legend";')
        lines.append("        style=rounded;")
        lines.append('        color="#E0E0E0";')
        lines.append("        fontsize=10;")
        lines.append("        node [style=filled];")
        lines.append(
            '        legend_pd [shape=box, fillcolor="#E3F2FD", label="PD (Protection Domain)"];'
        )
        lines.append(
            '        legend_region [shape=note, fillcolor="#FFFFFF", label="Shared Region"];'
        )
        lines.append(
            '        legend_cluster [shape=plaintext, label="Interface cluster = per-NIC PDs"];'
        )

        # Edge style samples
        lines.append('        legend_data_src [shape=point, width=0.1, label=""];')
        lines.append('        legend_data_dst [shape=point, width=0.1, label=""];')
        lines.append(
            '        legend_data_src -> legend_data_dst [label="Data", color="#2196F3", penwidth="2.0"];'
        )

        lines.append('        legend_dma_src [shape=point, width=0.1, label=""];')
        lines.append('        legend_dma_dst [shape=point, width=0.1, label=""];')
        lines.append(
            '        legend_dma_src -> legend_dma_dst [label="DMA (data region)", color="#BBDEFB"];'
        )

        lines.append('        legend_buf_src [shape=point, width=0.1, label=""];')
        lines.append('        legend_buf_dst [shape=point, width=0.1, label=""];')
        lines.append(
            '        legend_buf_src -> legend_buf_dst [label="Buffer Return", color="#9E9E9E"];'
        )

        lines.append('        legend_arp_src [shape=point, width=0.1, label=""];')
        lines.append('        legend_arp_dst [shape=point, width=0.1, label=""];')
        lines.append(
            '        legend_arp_src -> legend_arp_dst [label="ARP (bidirectional)", dir=both, color="#FF9800", penwidth="1.5"];'
        )

        lines.append('        legend_filter_src [shape=point, width=0.1, label=""];')
        lines.append('        legend_filter_dst [shape=point, width=0.1, label=""];')
        lines.append(
            '        legend_filter_src -> legend_filter_dst [label="Filter", color="#F44336", penwidth="1.5"];'
        )

        lines.append('        legend_icmp_src [shape=point, width=0.1, label=""];')
        lines.append('        legend_icmp_dst [shape=point, width=0.1, label=""];')
        lines.append(
            '        legend_icmp_src -> legend_icmp_dst [label="ICMP", color="#9C27B0", penwidth="1.5"];'
        )

        lines.append('        legend_net_src [shape=point, width=0.1, label=""];')
        lines.append('        legend_net_dst [shape=point, width=0.1, label=""];')
        lines.append(
            '        legend_net_src -> legend_net_dst [label="sDDF Net (inferred)", color="#607D8B"];'
        )

        lines.append("    }")

    lines.append("}")
    return "\n".join(lines)
