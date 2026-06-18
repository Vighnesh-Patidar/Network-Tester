# Integration tests

Two layers of integration coverage exist, split by what they require from the host.

## Dry-run matrix (`test_matrix.cpp`)

Runs as part of `ctest`. It drives the full `ChaosOrchestrator` against the
repository's `topology.json` in dry-run mode: the parser, the
`ReferenceTopologySolver`, and every §6 test case's pre/post-failure reachability
analysis execute, but no network namespace, veth, OVS bridge, tc qdisc, or FRR
daemon is touched. This validates the engine's decision logic and report schema on
any host, including unprivileged CI and developer machines.

## Kernel-backed matrix (privileged host only)

The live test matrix cannot run here. It requires a host with:

- root (or `CAP_NET_ADMIN`) for network namespaces, veth pairs, OVS, and `tc`
- Open vSwitch
- Mininet
- FRRouting (`zebra`, `ospfd`, `bgpd`, `vtysh`)
- `libpcap` for data-plane telemetry capture

To run it:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target chaos_engine link_shaper_cli

export LINK_SHAPER_CLI="$PWD/build/link_shaper_cli"
sudo -E python3 substrate/mininet_topology.py --topology topology.json &
sleep 25
sudo -E ./build/chaos_engine --topology topology.json --report report.json
echo "exit code: $?"   # 0 = all cases passed, non-zero = CI should reject
```

This is exactly what the `validate-topology` job in
`.github/workflows/network_validation.yml` automates on an ephemeral, single-use
runner, which is what makes running as root acceptable (§4.1).
