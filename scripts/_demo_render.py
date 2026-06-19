#!/usr/bin/env python3
"""Data-driven sections for the chaos-harness walkthrough.

Reads the same artifacts the engine uses (topology.json) and produces (the
JSON convergence report), then prints the topology summary, the per-case
matrix, and the final verdict. Keeping the formatting here means demo.sh stays
a thin driver and every displayed number traces back to a real file rather than
a hand-written string.
"""

import argparse
import json
import os
import sys

USE_COLOR = sys.stdout.isatty() or os.environ.get("DEMO_FORCE_COLOR") == "1"

SGR = {
    "reset": "0",
    "bold": "1",
    "dim": "2",
    "red": "31",
    "green": "32",
    "yellow": "33",
    "blue": "34",
    "magenta": "35",
    "cyan": "36",
    "white": "37",
    "gray": "90",
    "bgreen": "92",
    "byellow": "93",
    "bcyan": "96",
}


def c(text, *styles):
    if not USE_COLOR or not styles:
        return text
    codes = ";".join(SGR[s] for s in styles)
    return "\033[{}m{}\033[0m".format(codes, text)


def load(path):
    with open(path, "r", encoding="utf-8") as fh:
        return json.load(fh)


# Human-readable framing for each test case in the section-6 matrix. The harness
# reports machine fields (name/passed/detail); this maps the canonical case
# ids to what is injected, so the walkthrough reads like the matrix table.
INJECTION = {
    "single_link_failure_ospf": "ip link set dev down  (core OSPF link)",
    "single_link_failure_bgp": "ip link set dev down  (eBGP session link)",
    "node_failure": "kill FRR daemon set in one namespace",
    "asymmetric_packet_loss": "tc netem loss on a link (degraded, not down)",
    "flapping_link": "repeated up/down on a short interval",
    "failover_under_capacity": "primary down; backup near declared capacity",
}


def render_topology(topo_path):
    topo = load(topo_path)
    nodes = topo.get("nodes", [])
    links = topo.get("links", [])

    print(c("  NODES", "bold"))
    print(c("    id    protocols        attributes", "dim"))
    for n in nodes:
        protos = ",".join(n.get("protocols", []))
        attrs = []
        if "ospf_area" in n:
            attrs.append("area " + str(n["ospf_area"]))
        if "bgp_as" in n:
            attrs.append("AS" + str(n["bgp_as"]))
        peers = n.get("bgp_peers", [])
        if peers:
            attrs.append(
                "peers=" + ",".join(p["peer"] + "/AS" + str(p["remote_as"]) for p in peers)
            )
        line = "    {:<6}{:<17}{}".format(
            n.get("id", "?"), protos, "  ".join(attrs)
        )
        print(c(line, "cyan"))

    print()
    print(c("  LINKS", "bold"))
    print(c("    id          a -- b        metric   capacity   latency", "dim"))
    for l in links:
        line = "    {:<12}{:<14}{:<9}{:<11}{}".format(
            l.get("id", "?"),
            "{} -- {}".format(l.get("a", "?"), l.get("b", "?")),
            l.get("metric", "-"),
            "{} Mbps".format(l.get("capacity_mbps", "-")),
            "{} ms".format(l.get("latency_ms", "-")),
        )
        print(line)

    ospf = sum(1 for n in nodes if "ospf" in n.get("protocols", []))
    bgp = sum(1 for n in nodes if "bgp" in n.get("protocols", []))
    print()
    print(
        "    {} nodes  {} links  ({} OSPF, {} BGP speakers)".format(
            c(str(len(nodes)), "bold"),
            c(str(len(links)), "bold"),
            ospf,
            bgp,
        )
    )


def render_cases(report_path):
    report = load(report_path)
    cases = report.get("cases", [])
    width = 78
    print(c("    {:<28}{:<44}{}".format("CASE", "INJECTION", "RESULT"), "dim"))
    print(c("    " + "-" * width, "dim"))
    for case in cases:
        name = case.get("name", "?")
        passed = case.get("passed", False)
        injection = INJECTION.get(name, "")
        verdict = c(" PASS ", "bgreen", "bold") if passed else c(" FAIL ", "red", "bold")
        print("    {:<28}{:<44}{}".format(name, injection, verdict))
        detail = case.get("detail", "")
        if detail:
            print(c("      - " + detail, "gray"))
    print(c("    " + "-" * width, "dim"))


def render_verdict(report_path):
    report = load(report_path)
    cases = report.get("cases", [])
    passed = sum(1 for x in cases if x.get("passed"))
    total = len(cases)
    all_passed = report.get("all_passed", False)

    print(
        "    stabilized: {}    cases: {}/{} passed".format(
            c("yes", "green") if report.get("stabilized") else c("no", "red"),
            c(str(passed), "bold"),
            total,
        )
    )
    print(
        "    convergence budget: {} ms     stabilization budget: {} ms".format(
            report.get("convergence_timeout_ms", "-"),
            report.get("stabilization_timeout_ms", "-"),
        )
    )
    print()
    if all_passed:
        print(
            "    "
            + c(" GATE: PASS ", "bgreen", "bold")
            + "  exit 0  ->  "
            + c("merge allowed", "green")
        )
    else:
        print(
            "    "
            + c(" GATE: FAIL ", "red", "bold")
            + "  exit 1  ->  "
            + c("merge blocked", "red")
        )


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("section", choices=["topology", "cases", "verdict"])
    ap.add_argument("path")
    args = ap.parse_args()
    if args.section == "topology":
        render_topology(args.path)
    elif args.section == "cases":
        render_cases(args.path)
    else:
        render_verdict(args.path)


if __name__ == "__main__":
    main()
