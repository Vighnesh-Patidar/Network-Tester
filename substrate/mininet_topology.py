#!/usr/bin/env python3
"""Builds the Tier 3 emulation substrate from topology.json.

The script is the Python counterpart to the C++ data contract: it reads the same
single source of truth, creates a Mininet host per node (each in its own network
namespace), wires veth pairs through Open vSwitch, applies the declared per-link
capability profile via tc, and launches an FRR daemon set inside every namespace.

Addressing mirrors common/address_plan.h exactly so the C++ Control-Plane
Asserter can resolve observed next-hop IPs back to node ids:

    loopback(node_index)      -> 10.255.0.<index+1>/32
    link subnet (link_index)  -> 10.0.<index+1>.0/30   (a=.1, b=.2)
    interface name            -> <node_id>-eth<per-node sequence>

Requires root and a host with Mininet, Open vSwitch and FRRouting installed; it
cannot run in an unprivileged environment.
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile

try:
    from mininet.net import Mininet
    from mininet.node import Host, OVSSwitch
    from mininet.link import TCLink
    from mininet.log import setLogLevel, info
except ImportError:  # pragma: no cover - only available on a substrate host
    Mininet = None


FRR_DAEMONS = ("zebra", "ospfd", "bgpd")

NETNS_DIR = "/var/run/netns"
FRR_RUN_ROOT = "/var/run/frr"


def frr_run_dir(node_id):
    return "{}/{}".format(FRR_RUN_ROOT, node_id)


def loopback_ip(node_index):
    return "10.255.0.{}".format(node_index + 1)


def link_subnet(link_index):
    return "10.0.{}".format(link_index + 1)


def endpoint_ip(link_index, endpoint):
    octet = 1 if endpoint == "a" else 2
    return "{}.{}".format(link_subnet(link_index), octet)


def interface_name(node_id, sequence):
    return "{}-eth{}".format(node_id, sequence)


def load_topology(path):
    with open(path, "r", encoding="utf-8") as handle:
        return json.load(handle)


def assign_interfaces(topology):
    """Returns the per-node interface plan, matching AddressPlan ordering."""
    sequence = {}
    plan = []
    for link_index, link in enumerate(topology["links"]):
        a, b = link["a"], link["b"]
        seq_a = sequence.get(a, 0)
        seq_b = sequence.get(b, 0)
        sequence[a] = seq_a + 1
        sequence[b] = seq_b + 1
        plan.append(
            {
                "link": link,
                "index": link_index,
                "iface_a": interface_name(a, seq_a),
                "iface_b": interface_name(b, seq_b),
                "ip_a": endpoint_ip(link_index, "a"),
                "ip_b": endpoint_ip(link_index, "b"),
            }
        )
    return plan


def render_config(template_path, substitutions):
    with open(template_path, "r", encoding="utf-8") as handle:
        text = handle.read()
    for key, value in substitutions.items():
        text = text.replace("{{" + key + "}}", str(value))
    return text


def write_frr_configs(node, node_index, topology, config_root, run_dir, plan):
    """Writes per-node zebra/ospfd/bgpd configs from the templates."""
    node_dir = os.path.join(run_dir, node["id"])
    os.makedirs(node_dir, exist_ok=True)
    os.chmod(node_dir, 0o755)

    protocols = node.get("protocols", [])
    router_id = loopback_ip(node_index)
    node_protocols = {n["id"]: n.get("protocols", []) for n in topology["nodes"]}

    ospf_networks = []
    for link_index, link in enumerate(topology["links"]):
        if link["a"] == node["id"]:
            ospf_networks.append("{}.0/30".format(link_subnet(link_index)))
        elif link["b"] == node["id"]:
            ospf_networks.append("{}.0/30".format(link_subnet(link_index)))

    network_stanza = "\n".join(
        " network {} area {}".format(net, node.get("ospf_area", "0.0.0.0"))
        for net in ospf_networks
    )

    # Point-to-point interfaces (both ends speak OSPF) skip DR election entirely.
    ospf_interfaces = []
    for entry in plan:
        link = entry["link"]
        if link["a"] == node["id"]:
            iface, other = entry["iface_a"], link["b"]
        elif link["b"] == node["id"]:
            iface, other = entry["iface_b"], link["a"]
        else:
            continue
        if "ospf" in protocols and "ospf" in node_protocols.get(other, []):
            ospf_interfaces.append(iface)

    interface_stanza = "\n".join(
        "interface {}\n ip ospf network point-to-point\n"
        " ip ospf hello-interval 1\n ip ospf dead-interval 4\n!".format(iface)
        for iface in ospf_interfaces
    )

    zebra = render_config(
        os.path.join(config_root, "zebra.conf.tmpl"),
        {"NODE_ID": node["id"], "ROUTER_ID": router_id},
    )
    zebra_path = os.path.join(node_dir, "zebra.conf")
    with open(zebra_path, "w", encoding="utf-8") as handle:
        handle.write(zebra)
    os.chmod(zebra_path, 0o644)

    if "ospf" in protocols:
        ospfd = render_config(
            os.path.join(config_root, "ospfd.conf.tmpl"),
            {
                "NODE_ID": node["id"],
                "ROUTER_ID": router_id,
                "OSPF_NETWORKS": network_stanza,
                "OSPF_INTERFACES": interface_stanza,
            },
        )
        ospfd_path = os.path.join(node_dir, "ospfd.conf")
        with open(ospfd_path, "w", encoding="utf-8") as handle:
            handle.write(ospfd)
        os.chmod(ospfd_path, 0o644)

    if "bgp" in protocols:
        neighbor_lines = []
        for peer in node.get("bgp_peers", []):
            peer_id = peer["peer"]
            remote_as = peer["remote_as"]
            peer_ip = None
            for link_index, link in enumerate(topology["links"]):
                if link["a"] == node["id"] and link["b"] == peer_id:
                    peer_ip = endpoint_ip(link_index, "b")
                elif link["b"] == node["id"] and link["a"] == peer_id:
                    peer_ip = endpoint_ip(link_index, "a")
            if peer_ip is not None:
                neighbor_lines.append(
                    " neighbor {} remote-as {}".format(peer_ip, remote_as)
                )
        bgpd = render_config(
            os.path.join(config_root, "bgpd.conf.tmpl"),
            {
                "NODE_ID": node["id"],
                "LOCAL_AS": node.get("bgp_as", 0),
                "ROUTER_ID": router_id,
                "BGP_NEIGHBORS": "\n".join(neighbor_lines),
                "LOOPBACK": router_id,
            },
        )
        bgpd_path = os.path.join(node_dir, "bgpd.conf")
        with open(bgpd_path, "w", encoding="utf-8") as handle:
            handle.write(bgpd)
        os.chmod(bgpd_path, 0o644)

    return node_dir


def shape_link(host_a, host_b, plan_entry, shaper_cli):
    """Applies the link's capability profile to both endpoints."""
    link = plan_entry["link"]
    for host, iface in ((host_a, plan_entry["iface_a"]), (host_b, plan_entry["iface_b"])):
        if shaper_cli:
            cmd = [
                shaper_cli,
                "--iface", iface,
                "--capacity-mbps", str(link.get("capacity_mbps", 0)),
                "--latency-ms", str(link.get("latency_ms", 0.0)),
                "--jitter-ms", str(link.get("jitter_ms", 0.0)),
                "--loss-pct", str(link.get("loss_pct", 0.0)),
            ]
            host.cmd(" ".join(cmd))
        else:
            _apply_tc_inline(host, iface, link)


def _apply_tc_inline(host, iface, link):
    """Fallback shaping if the C++ link_shaper CLI was not built."""
    host.cmd("tc qdisc del dev {} root".format(iface))
    netem = "netem"
    if link.get("latency_ms", 0.0) > 0 or link.get("jitter_ms", 0.0) > 0:
        netem += " delay {}ms".format(link.get("latency_ms", 0.0))
        if link.get("jitter_ms", 0.0) > 0:
            netem += " {}ms".format(link["jitter_ms"])
    if link.get("loss_pct", 0.0) > 0:
        netem += " loss {}%".format(link["loss_pct"])
    capacity = link.get("capacity_mbps", 0)
    if capacity > 0:
        host.cmd(
            "tc qdisc add dev {} root handle 1: tbf rate {}mbit burst 32kbit "
            "latency 50ms".format(iface, capacity)
        )
        host.cmd("tc qdisc add dev {} parent 1: handle 10: {}".format(iface, netem))
    else:
        host.cmd("tc qdisc add dev {} root handle 10: {}".format(iface, netem))


def start_frr(host, node_dir):
    """Starts the daemon set in dependency order: zebra first.

    Each node gets a private FRR runtime directory so the per-daemon vty and
    zserv sockets do not collide: every host shares the root mount namespace, so
    a single /var/run/frr would let one node's sockets shadow the others'. The
    chaos engine resolves the same directory when it shells out to vtysh.
    """
    run_dir = frr_run_dir(host.name)
    zserv = "{}/zserv.api".format(run_dir)
    host.cmd("mkdir -p {}".format(run_dir))
    host.cmd("chown -R frr:frr {} || true".format(run_dir))
    for daemon in FRR_DAEMONS:
        config = os.path.join(node_dir, "{}.conf".format(daemon))
        if not os.path.exists(config):
            continue
        binary = shutil.which(daemon) or "/usr/lib/frr/{}".format(daemon)
        host.cmd(
            "{} -f {} -d -i {}/{}.pid -z {} --vty_socket {}".format(
                binary, config, run_dir, daemon, zserv, run_dir
            )
        )


def register_namespaces(hosts):
    """Exposes each Mininet host's network namespace under /var/run/netns.

    Mininet keeps hosts in anonymous namespaces, so `ip netns exec <id>` (used by
    the chaos engine to inject failures and poll vtysh) cannot find them. Linking
    /proc/<pid>/ns/net into /var/run/netns gives iproute2 a name to attach to.
    """
    os.makedirs(NETNS_DIR, exist_ok=True)
    for node_id, (host, _index) in hosts.items():
        link = os.path.join(NETNS_DIR, node_id)
        if os.path.lexists(link):
            os.remove(link)
        os.symlink("/proc/{}/ns/net".format(host.pid), link)


def cleanup_substrate(net):
    net.stop()
    for host in net.hosts:
        link = os.path.join(NETNS_DIR, host.name)
        try:
            os.remove(link)
        except OSError:
            pass


def build(topology_path, config_root, shaper_cli):
    if Mininet is None:
        raise SystemExit(
            "Mininet is not available; this script must run on a substrate host."
        )

    topology = load_topology(topology_path)
    plan = assign_interfaces(topology)
    run_dir = tempfile.mkdtemp(prefix="nch-substrate-")
    # The FRR daemons drop privileges to the frr user, so the config tree they read
    # must be traversable by that user; mkdtemp creates it 0700 and root-owned.
    os.chmod(run_dir, 0o755)

    net = Mininet(switch=OVSSwitch, link=TCLink, controller=None)

    hosts = {}
    for index, node in enumerate(topology["nodes"]):
        host = net.addHost(node["id"], cls=Host, ip=None)
        hosts[node["id"]] = (host, index)

    # One OVS bridge backplane carries every veth; routing is done by FRR in the
    # host namespaces, not by the switch.
    backplane = net.addSwitch("s1", failMode="standalone")

    for entry in plan:
        link = entry["link"]
        host_a, _ = hosts[link["a"]]
        host_b, _ = hosts[link["b"]]
        net.addLink(host_a, backplane, intfName1=entry["iface_a"])
        net.addLink(host_b, backplane, intfName1=entry["iface_b"])

    net.start()
    register_namespaces(hosts)

    for entry in plan:
        host_a, _ = hosts[entry["link"]["a"]]
        host_b, _ = hosts[entry["link"]["b"]]
        host_a.cmd("ip addr add {}/30 dev {}".format(entry["ip_a"], entry["iface_a"]))
        host_b.cmd("ip addr add {}/30 dev {}".format(entry["ip_b"], entry["iface_b"]))
        host_a.cmd("ip link set {} up".format(entry["iface_a"]))
        host_b.cmd("ip link set {} up".format(entry["iface_b"]))
        shape_link(host_a, host_b, entry, shaper_cli)

    for node_id, (host, index) in hosts.items():
        host.cmd("ip addr add {}/32 dev lo".format(loopback_ip(index)))
        host.cmd("sysctl -w net.ipv4.ip_forward=1")
        node = topology["nodes"][index]
        node_dir = write_frr_configs(node, index, topology, config_root, run_dir, plan)
        start_frr(host, node_dir)

    info("substrate is up; run the chaos engine, then press Ctrl-D to tear down\n")
    return net


def main():
    parser = argparse.ArgumentParser(description="Bring up the network-chaos substrate.")
    parser.add_argument("--topology", default="topology.json")
    parser.add_argument(
        "--frr-config-dir",
        default=os.path.join(os.path.dirname(__file__), "frr_daemon_config"),
    )
    parser.add_argument(
        "--link-shaper-cli",
        default=os.environ.get("LINK_SHAPER_CLI", ""),
        help="path to the compiled link_shaper CLI (optional; falls back to inline tc)",
    )
    parser.add_argument("--cli", action="store_true", help="drop into the Mininet CLI")
    args = parser.parse_args()

    setLogLevel("info")
    net = build(args.topology, args.frr_config_dir, args.link_shaper_cli or None)

    if args.cli:
        from mininet.cli import CLI

        CLI(net)
        cleanup_substrate(net)
    elif sys.stdin.isatty():
        try:
            sys.stdin.read()
        finally:
            cleanup_substrate(net)
    else:
        # Backgrounded (e.g. in CI) there is no terminal to read from, so wait
        # for a termination signal instead of stdin EOF. Reading stdin here would
        # return immediately and tear the substrate down before the chaos engine
        # ever runs against it.
        import signal

        def teardown(signum, frame):
            cleanup_substrate(net)
            sys.exit(0)

        signal.signal(signal.SIGTERM, teardown)
        signal.signal(signal.SIGINT, teardown)
        signal.pause()


if __name__ == "__main__":
    main()
