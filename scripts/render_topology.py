#!/usr/bin/env python3
"""Render topology.json to a Graphviz diagram (SVG + PNG).

The diagram is generated from the same single source of truth the engine reads,
so it never drifts from what is actually tested. Node colour encodes the
protocol set; edge thickness and colour encode the declared link capacity, so
the capacity-limited backup paths (the ones the capacity-constrained failover
case leans on) are visible at a glance.
"""

import json
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

BG = "#0d1117"
FG = "#c9d1d9"
GRID = "#30363d"
OSPF = "#1f6feb"
BGP = "#d29922"
BOTH = "#2ea043"


def node_color(protocols):
    has_ospf = "ospf" in protocols
    has_bgp = "bgp" in protocols
    if has_ospf and has_bgp:
        return BOTH
    if has_bgp:
        return BGP
    return OSPF


def node_label(node):
    nid = node.get("id", "?")
    bits = []
    if "bgp_as" in node:
        bits.append("AS" + str(node["bgp_as"]))
    if "ospf_area" in node and "bgp_as" not in node:
        bits.append("OSPF")
    sub = "\\n".join(bits)
    return nid if not sub else "{}\\n{}".format(nid, sub)


def edge_style(link):
    cap = float(link.get("capacity_mbps", 0) or 0)
    if cap >= 1000:
        return {"penwidth": "3.0", "color": "#3fb950"}
    if cap >= 100:
        return {"penwidth": "1.4", "color": "#8b949e", "style": "dashed"}
    return {"penwidth": "1.0", "color": "#f85149", "style": "dashed"}


def build_dot(topo):
    lines = []
    lines.append("graph topology {")
    lines.append('  bgcolor="{}";'.format(BG))
    lines.append("  layout=neato; overlap=false; splines=true; sep=\"+18\";")
    lines.append('  fontname="DejaVu Sans"; fontcolor="{}"; fontsize=20;'.format(FG))
    lines.append(
        '  label="network-chaos-harness  -  sample topology\\n'
        'solid green = 1 Gbps core    dashed grey = 100 Mbps    dashed red = capacity-limited backup";'
    )
    lines.append("  labelloc=t;")
    lines.append(
        '  node [shape=circle, style="filled", fixedsize=true, width=0.95, '
        'fontname="DejaVu Sans Bold", fontsize=13, fontcolor="white", '
        'penwidth=2, color="{}"];'.format(BG)
    )
    lines.append(
        '  edge [fontname="DejaVu Sans Mono", fontsize=10, fontcolor="{}"];'.format(FG)
    )

    for node in topo.get("nodes", []):
        lines.append(
            '  "{}" [label="{}", fillcolor="{}"];'.format(
                node.get("id"), node_label(node), node_color(node.get("protocols", []))
            )
        )

    for link in topo.get("links", []):
        st = edge_style(link)
        attrs = ['label="m{}\\n{}Mb"'.format(link.get("metric", "?"), link.get("capacity_mbps", "?"))]
        for k, v in st.items():
            attrs.append('{}="{}"'.format(k, v))
        lines.append(
            '  "{}" -- "{}" [{}];'.format(link.get("a"), link.get("b"), ", ".join(attrs))
        )

    lines.append("}")
    return "\n".join(lines)


def main():
    topo_path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "topology.json")
    out_dir = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, "media")
    os.makedirs(out_dir, exist_ok=True)

    with open(topo_path, "r", encoding="utf-8") as fh:
        topo = json.load(fh)

    dot = build_dot(topo)
    dot_path = os.path.join(out_dir, "topology.dot")
    with open(dot_path, "w", encoding="utf-8") as fh:
        fh.write(dot)

    engine = shutil.which("neato") or shutil.which("dot")
    if engine is None:
        sys.stderr.write("graphviz not found; wrote {} only\n".format(dot_path))
        return 1

    for fmt in ("svg", "png"):
        out = os.path.join(out_dir, "topology." + fmt)
        subprocess.run([engine, "-T" + fmt, dot_path, "-o", out], check=True)
        sys.stderr.write("wrote {}\n".format(out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
