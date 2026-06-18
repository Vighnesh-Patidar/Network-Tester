# GitOps-Driven Automated Network Test Harness
## Architecture & Design Document v0.2

> **Repo:** `network-chaos-harness` (working name)
> **Language:** C++17 core (Chaos Engine + Visualizer), Bash/YAML for CI
> **Substrate:** Mininet + Open vSwitch + FRRouting
> **Status:** Pre-implementation design document, post-review

---

## 0. Core Lifecycle

```
Design → Commit → Emulate → Stabilize → Attack → Assert → Report
```

This is a full-stack infrastructure validation pipeline that treats network topologies as verifiable code. It spans three execution domains: a local visual authoring tool, a cloud-based CI/CD pipeline, and a Linux kernel network-namespace emulation substrate driven by a C++ chaos engine.

A `Stabilize` phase has been added between `Emulate` and `Attack` relative to the original design — see §6.1 for why this is not optional.

---

## 1. The Universal Data Contract

The entire system rotates around a single source of truth: `topology.json`.

- The visualizer's only job is to write this file.
- The CI pipeline's only job is to trigger when this file changes.
- The C++ orchestrator's only job is to read this file to map its attack vectors and to derive the expected steady-state for assertions (§5.3).

This strict decoupling ensures frontend GUI logic never bleeds into backend execution logic.

`topology.json` encodes only the **physical and logical graph** — nodes, links, per-node protocol configuration (OSPF area, BGP AS/peers), and per-link capability constraints (§1.1). It does **not** encode the expected converged routing state. That state is *derived* at test time by the `ReferenceTopologySolver` (§5.3), not hand-authored, so the graph remains the only thing a human edits.

```json
{
  "nodes": [
    { "id": "r1", "protocols": ["ospf"], "ospf_area": "0.0.0.0" },
    { "id": "r2", "protocols": ["ospf", "bgp"], "bgp_as": 65001 }
  ],
  "links": [
    { "id": "l1", "a": "r1", "b": "r2", "metric": 10,
      "capacity_mbps": 100, "latency_ms": 5, "jitter_ms": 1, "loss_pct": 0.0 }
  ]
}
```

### 1.1 Link capability fields — why these exist

A bare `veth` pair has no realistic notion of bandwidth, latency, or loss — it behaves like an idealized, instantaneous wire, limited only by whatever the CI runner's kernel and CPU happen to provide at the moment, which is neither user-controlled nor representative of a real network link. Without explicit capability constraints, every test in §6 runs against an unconstrained substrate, so "convergence under failure" is only ever tested in isolation from "convergence under realistic load."

The added fields — `capacity_mbps`, `latency_ms`, `jitter_ms`, `loss_pct` — are user-specified per link and are enforced at the kernel level by the `LinkShaper` component (§5.6) using Linux traffic control. This makes the topology's *capacity profile* part of the same single source of truth as its *graph structure*, so a test author can express scenarios like "this backup path is only 10 Mbps and already carrying background traffic" as data, not as a side-channel assumption baked into test code.

---

## 2. Component Architecture

### Tier 1 — Authoring Plane (ImGui Network Visualizer)

A lightweight, immediate-mode C++ desktop application used to design test topologies.

- **Graph State Manager** — a contiguous `std::vector` of `Node`/`Link`, addressed by UUID, to avoid dangling pointers under graph edits.
- **Canvas Rendering** — graph data projected through an affine camera matrix onto an interactive, infinite-panning `ImDrawList` canvas.
- **Git Integrator** — serialises graph state to `topology.json` via `nlohmann/json` and commits/pushes it. See §6.2 for the revised push mechanism.

### Tier 2 — Orchestration Plane (CI/CD GitOps Pipeline)

- **Environment Provisioner** — GitHub Actions runner, Ubuntu base image.
- **Dependency Manager** — installs Open vSwitch, Mininet, compiles FRRouting.
- **Execution Trigger** — builds the C++ Chaos Engine and runs it against `topology.json`. Privilege scope is explicit, not implied — see §6.3.

### Tier 3 — Emulation Substrate (Mininet & FRR)

- **Kernel Isolation** — Linux network namespaces (`netns`) partition the runner into independent routing entities.
- **Virtual Wiring** — `veth` pairs bridged through Open vSwitch.
- **Distributed State Machines** — each namespace runs its own FRR daemon set (`zebra`, `ospfd`, `bgpd`).
- **Link Shaper** — new component, §5.6. Applies each link's user-specified `capacity_mbps`/`latency_ms`/`jitter_ms`/`loss_pct` (§1.1) to the corresponding `veth` interface via `tc qdisc`, so the substrate's link behaviour matches the topology's declared capability profile rather than the runner's incidental defaults.
- **Stabilization Gate** — new component, §6.1. Blocks the pipeline from proceeding to Tier 4 until the network has reached an initial converged state, verified by polling daemon adjacency status rather than a fixed sleep.

### Tier 4 — Validation Engine (C++ Chaos Orchestrator)

Operates entirely in user-space, launching attacks against the kernel namespaces.

- **Topology Parser** — ingests `topology.json`, maps the target environment.
- **Reference Topology Solver** — new component, §5.3. Independently computes the expected post-convergence routing state from the graph, so assertions check against a ground truth that isn't just "FRR agrees with itself."
- **System Execution Engine** — `fork`/`exec` wrapping `ip link set dev down` and similar, fired into specific namespaces to simulate failures.
- **Data-Plane Telemetry** — `libpcap`-based background threads timestamping injected ICMP/UDP traffic to measure when forwarding actually resumes (§6.4 — kept distinct from control-plane convergence).
- **Control-Plane Asserter** — polls FRR's `vtysh` for structured routing-table JSON, asserts convergence against the Reference Topology Solver's output within `convergence_timeout_ms` (§6.5 — renamed from "TTL window").

---

## 3. Exit Criteria & Reporting

The Chaos Engine iterates the full test matrix, emits a structured pass/fail report (including both data-plane and control-plane convergence numbers per §6.4), and exits with a POSIX status code. CI uses that code as the merge/reject gate.

---

## 4. Issues Identified in Design Review (v0.1 → v0.2)

This section exists so the reasoning behind each change is visible, not just the change itself.

### 4.1 Privilege scope for the Chaos Engine was unspecified

v0.1 said the binary runs "with elevated privileges" and left it there. Mininet's `netns`/`veth`/OVS operations genuinely need root or `CAP_NET_ADMIN`-class capabilities, but "elevated privileges" as a one-line spec is a gap. **Resolution:** the Chaos Engine runs as root inside the ephemeral, disposable GitHub Actions runner. This is standard practice for Mininet-based CI and is acceptable specifically because the runner is single-use and isolated — this justification is now explicit in the design rather than assumed.

### 4.2 Git push via raw `system()` had no error path

Shelling out to `git push` with no return-code handling means a failed push (no upstream, auth expiry, merge conflict, no network) fails silently from the GUI's perspective — exactly wrong for a tool whose entire premise is "this is the trusted trigger for a validation pipeline." **Resolution (§5.2):** the Git Integrator captures the `system()` return code and renders an explicit success/failure state in the UI. `libgit2` is noted as a future upgrade path if richer error introspection becomes necessary, but isn't required for v0.1.

### 4.3 "Mathematically correct backup paths" had no reference computation behind it

The original Control-Plane Asserter description implied an independently verified expected result, but nothing in the system computed one — it was effectively just checking that FRR agreed with itself, which proves nothing about correctness. **Resolution (§5.3):** a dedicated `ReferenceTopologySolver` computes expected shortest paths from the graph (Dijkstra over the link-state view, since the topology is fully known at test time) independent of FRR's own computation. For BGP, where path selection is policy-driven rather than pure shortest-path, the claim is downgraded to "converged to a valid, loop-free path consistent with configured policy" rather than a single "correct" path — this distinction is now stated rather than implied.

### 4.4 Data-plane and control-plane reconvergence are different measurements that were being conflated

`libpcap` timestamps measure when traffic resumes flowing. `vtysh` polling measures when the routing table itself reflects convergence. These can and do disagree (caching, summarization timing, FIB update lag). **Resolution (§5.4):** both numbers are measured and reported separately, explicitly labelled `data_plane_convergence_ms` and `control_plane_convergence_ms`, with no implied equivalence between them.

### 4.5 "TTL window" was an ambiguous, overloaded name

Could be misread as IP TTL (hop count) rather than a test timeout. **Resolution:** renamed to `convergence_timeout_ms` throughout the design and the eventual config schema.

### 4.6 No stabilization phase between environment startup and attack injection

FRR daemons have startup-order dependencies (`zebra` before `ospfd`/`bgpd`) and need a settling period to form initial adjacencies. Firing chaos before the network has even finished its *first* convergence contaminates every subsequent measurement with an unrelated race condition. **Resolution (§5.1):** an explicit Stabilization Gate, polling `vtysh -c "show ip ospf neighbor"` (and the BGP equivalent) until all expected adjacencies report `Full`/`Established`, replacing any fixed `sleep N`.

### 4.7 Link capability was unconstrained — every test ran against an idealized substrate

`veth` pairs have no inherent bandwidth, latency, or loss characteristics; left unconstrained, every convergence test in the original design was implicitly testing "failover on an instantaneous, lossless wire," which is not representative of any real network and not something a test author could control or vary. There was no way to express "this backup path is capacity-limited" as part of the topology. **Resolution (§1.1, §5.6):** link capability fields (`capacity_mbps`, `latency_ms`, `jitter_ms`, `loss_pct`) are now part of `topology.json`, enforced on the corresponding interface by a new `LinkShaper` component using `tc qdisc` (`tbf` for bandwidth, `netem` for latency/jitter/loss). This also unlocks a genuinely more realistic test case — convergence behaviour when the failover path is already near its capacity limit (§6).

---

## 5. Revised Component Specifications

### 5.1 Stabilization Gate (Tier 3)

```cpp
class StabilizationGate {
public:
    // Blocks until all expected adjacencies are Full/Established,
    // or until stabilization_timeout_ms elapses (hard failure).
    bool wait_for_convergence(const Topology& topo,
                              uint32_t stabilization_timeout_ms);

private:
    // Polls vtysh per-node, parses JSON adjacency state.
    AdjacencyStatus poll_ospf_neighbors(const std::string& node_ns);
    AdjacencyStatus poll_bgp_peers(const std::string& node_ns);
};
```

Runs once, immediately after Tier 3 environment bring-up, before any test case in the matrix executes. A stabilization timeout is itself a test failure — it means the topology as authored cannot even reach a baseline converged state, which is useful information in its own right.

### 5.2 Git Integrator — explicit push result (Tier 1)

```cpp
struct PushResult {
    bool success;
    int exit_code;
    std::string stderr_capture;
};

class GitIntegrator {
public:
    PushResult commit_and_push(const Topology& topo,
                               const std::string& commit_message);
    // UI binds to PushResult, not a fire-and-forget call.
};
```

### 5.3 Reference Topology Solver (Tier 4)

```cpp
class ReferenceTopologySolver {
public:
    // OSPF: pure shortest-path, computed independently of FRR.
    RoutingTable compute_expected_ospf_state(const Topology& topo);

    // BGP: weaker claim by design — see §4.3. Verifies loop-freedom
    // and policy consistency, not a single "correct" path.
    bool verify_bgp_policy_consistency(const Topology& topo,
                                       const RoutingTable& observed);
};
```

### 5.4 Convergence measurement — both planes, reported separately

```cpp
struct ConvergenceResult {
    double data_plane_convergence_ms;     // libpcap-observed traffic resumption
    double control_plane_convergence_ms;  // vtysh-observed routing table update
    bool   within_timeout;                // both must satisfy convergence_timeout_ms
};
```

### 5.5 Naming — `convergence_timeout_ms` replaces "TTL window"

Applied consistently across the config schema, the CLI, and the report output.

### 5.6 Link Shaper (Tier 3)

```cpp
class LinkShaper {
public:
    // Applies the link's declared capability profile to its veth
    // interface. Called once per link during Tier 3 bring-up, before
    // the Stabilization Gate runs — so daemons converge against the
    // *real* link characteristics, not an unconstrained default.
    bool apply(const Link& link, const std::string& interface_name);

    // Used by the flapping-link and packet-loss test cases (§6) to
    // change a link's profile mid-test without tearing down the
    // interface.
    bool update(const Link& link, const std::string& interface_name);

private:
    // tbf for capacity_mbps, netem for latency_ms/jitter_ms/loss_pct.
    // Both are applied as a single qdisc chain per interface so they
    // compose correctly (tbf as the outer/root qdisc, netem nested
    // beneath it) rather than fighting over the same interface.
    std::string build_tc_command(const Link& link,
                                 const std::string& interface_name);
};
```

`apply()` runs for every link during Tier 3 bring-up, before the Stabilization Gate (§5.1) — this ordering matters, since OSPF/BGP cost and timer behaviour can be sensitive to link latency, so daemons should form their initial adjacencies against the link's *real* declared characteristics, not against an unconstrained default that then changes underneath them.

---

## 6. Test Matrix (initial scope)

| Test case | What's injected | What's asserted |
|---|---|---|
| Single link failure (OSPF) | `ip link set dev <X> down` on one inter-router link | Data-plane + control-plane reconvergence within timeout; new path matches `ReferenceTopologySolver` output |
| Single link failure (BGP) | Same, on an eBGP session link | Reconvergence within timeout; resulting path is loop-free and policy-consistent |
| Node failure | Kill all FRR daemons in one namespace | Neighbors detect failure, reconverge around the dead node |
| Asymmetric packet loss | `tc qdisc` induced loss on a link, not full down | Data-plane telemetry correctly distinguishes degraded-but-up from down |
| Flapping link | Repeated up/down on a short interval | No route oscillation beyond a bounded settling period (dampening behaviour, if configured, is honoured) |
| Failover under capacity constraint | Primary link down; backup link's declared `capacity_mbps` is already near saturation from background traffic generated during the test | Reconvergence still completes within timeout; data-plane telemetry reports actual achieved throughput on the backup path against its declared capacity, not just binary up/down |

---

## 7. Repository Structure

```
network-chaos-harness/
├── CMakeLists.txt
├── ARCHITECTURE.md
├── topology.json                 ← single source of truth
│
├── visualizer/                   ← Tier 1
│   ├── graph_state.{h,cpp}
│   ├── canvas_render.{h,cpp}
│   └── git_integrator.{h,cpp}
│
├── chaos_engine/                 ← Tier 4
│   ├── topology_parser.{h,cpp}
│   ├── reference_topology_solver.{h,cpp}
│   ├── system_execution_engine.{h,cpp}
│   ├── data_plane_telemetry.{h,cpp}
│   ├── control_plane_asserter.{h,cpp}
│   └── stabilization_gate.{h,cpp}
│
├── substrate/                    ← Tier 3 setup scripts
│   ├── mininet_topology.py
│   ├── link_shaper.{h,cpp}
│   └── frr_daemon_config/
│
├── .github/workflows/
│   └── network_validation.yml    ← Tier 2
│
└── tests/
    ├── unit/
    └── integration/
```

---

## 8. Roadmap

### v0.1 — Core loop, single protocol
- [ ] `topology.json` schema + parser (incl. link capability fields, §1.1)
- [ ] Mininet + FRR substrate bring-up (OSPF only)
- [ ] `LinkShaper` — `tc qdisc` enforcement of declared link capabilities
- [ ] Stabilization Gate
- [ ] Single link-failure test case, both convergence measurements
- [ ] CI wiring, exit-code gating

### v0.2 — BGP + reference solver
- [ ] `ReferenceTopologySolver` (OSPF shortest-path + BGP policy check)
- [ ] BGP test cases
- [ ] Node-failure and flapping-link test cases

### v0.3 — Visualizer
- [ ] ImGui authoring tool
- [ ] Git Integrator with explicit push-result UI

### v1.0 — Stable
- [ ] Full test matrix (§6) passing in CI
- [ ] Structured JSON report output
- [ ] Documentation of the data-plane vs control-plane distinction for anyone extending the test matrix

---

*Document version: 0.3.0 — added user-specified link capability profile (§1.1), `LinkShaper` component (§5.6) enforcing bandwidth/latency/jitter/loss via tc qdisc, capacity-constrained failover test case (§6)*
*Document version: 0.2.0 — design review pass: privilege scope, push error handling, reference-computed assertions, dual-plane convergence measurement, stabilization gate, timeout naming*
*Authors: Vighnesh Patidar*
