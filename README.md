# network-chaos-harness

A GitOps-driven automated network test harness. It treats a network topology as
verifiable code: you author a topology, commit it, and a C++ chaos engine brings
up an emulated copy of the network, injects failures, and asserts that routing
reconverges correctly and within budget. The full pipeline is:

```
Design -> Commit -> Emulate -> Stabilize -> Attack -> Assert -> Report
```

The design rationale lives in [`ARCHITECTURE.md`](ARCHITECTURE.md); this README
covers building, running, and what each part does.

![Chaos engine walking the lifecycle from topology through the test matrix to the exit-code gate](media/demo.gif)

The topology below is rendered straight from [`topology.json`](topology.json):
node colour reflects the configured protocol set, and edge weight and colour
reflect each link's declared capacity.

![Topology rendered from topology.json](media/topology.png)

## The single source of truth

Everything rotates around [`topology.json`](topology.json). It encodes only the
physical and logical graph: nodes, their protocols (OSPF area, BGP AS/peers), and
per-link capability constraints (`capacity_mbps`, `latency_ms`, `jitter_ms`,
`loss_pct`). It deliberately does **not** encode the expected converged routing
state; that is derived at test time by the `ReferenceTopologySolver`, so the graph
stays the only thing a human edits.

## Repository layout

| Path | Tier | Purpose |
|---|---|---|
| `common/` | shared | `Node`/`Link`/`Topology` model, JSON (de)serialization, deterministic address plan |
| `chaos_engine/` | Tier 4 | Parser, reference solver, exec engine, telemetry, asserter, stabilization gate, orchestrator |
| `substrate/` | Tier 3 | Mininet builder, `LinkShaper` (tc), FRR config templates |
| `visualizer/` | Tier 1 | ImGui authoring tool: graph state, canvas, git integrator |
| `.github/workflows/` | Tier 2 | CI that builds, unit-tests, and runs the live matrix as the merge gate |
| `tests/` | - | Unit tests and the §6 integration matrix |
| `third_party/` | - | Vendored fallbacks (JSON, test harness) for offline builds |

## Prerequisites

Building and the test suite need only a C++17 toolchain and CMake >= 3.16.
Running the live substrate additionally needs Mininet, Open vSwitch, and
FRRouting, and must run as root.

Build and unit/integration tests (Debian/Ubuntu), matching the CI
`build-and-unit-test` job:

```bash
sudo apt-get install -y --no-install-recommends \
  build-essential cmake libpcap-dev nlohmann-json3-dev libgtest-dev
```

`nlohmann-json3-dev`, `libpcap-dev`, and `libgtest-dev` are optional: the build
falls back to vendored copies under `third_party/` (and to a bound-socket
capture path when libpcap is absent), so it still builds on an air-gapped host.

Live substrate (Debian/Ubuntu), matching the CI `validate-topology` job — these
are needed on top of the build packages above:

```bash
sudo apt-get install -y --no-install-recommends \
  openvswitch-switch openvswitch-common mininet frr frr-pythontools iproute2
```

## Building

CI configures Release; the substrate job also disables the optional visualizer:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_VISUALIZER=OFF
cmake --build build --parallel
```

A validation run only needs the `chaos_engine` and `link_shaper_cli` targets,
which can be built on their own:

```bash
cmake --build build --parallel --target chaos_engine link_shaper_cli
```

Dependency resolution:

- **nlohmann/json** — uses the system package if `find_package` locates it,
  otherwise the vendored header in `third_party/json`.
- **libpcap** — if present, the data-plane telemetry uses a libpcap capture
  backend; if not, it falls back to a bound-socket receiver (build define
  `HAVE_LIBPCAP`).
- **GoogleTest** — uses the system package if found, otherwise the vendored
  GoogleTest-compatible harness in `third_party/gtest`.

## Tests

`ctest` runs the unit suite and the integration matrix. Both run off-substrate —
the integration matrix drives the full orchestrator against `topology.json` in
dry-run mode — so neither needs root, Mininet, or FRR:

```bash
ctest --test-dir build --output-on-failure
```

## Running the chaos engine

The engine reads `topology.json`, derives the expected converged state, stabilizes
the substrate, walks the §6 test matrix, writes a structured JSON report, and exits
with a POSIX status code that CI gates on: `0` = all cases passed, `1` = the run
stabilized but at least one case failed, `2` = a usage or load error (bad topology,
unwritable report path, invalid argument).

Flags (`--help` for the list):

| Flag | Default | Purpose |
|---|---|---|
| `--topology <path>` | `topology.json` | topology to validate |
| `--report <path>` | stdout | write the JSON report here instead of stdout |
| `--convergence-timeout-ms N` | `5000` | per-case convergence budget |
| `--stabilization-timeout-ms N` | `30000` | initial (pre-matrix) convergence budget |
| `--dry-run` | off | parse/solve/validate without touching the kernel |

```bash
# Off-substrate planning run: parse, solve, and validate every test case's
# pre/post-failure analysis without touching the kernel. Safe to run anywhere.
./build/chaos_engine --topology topology.json --dry-run

# Full run against a live substrate (root + Mininet + FRR; see below). These are
# the timeouts CI uses.
sudo ./build/chaos_engine \
  --topology topology.json \
  --report convergence_report.json \
  --convergence-timeout-ms 8000 \
  --stabilization-timeout-ms 60000
```

### Interpreting the convergence report

The report is a single JSON object. Top-level fields:

- `topology_path`, `convergence_timeout_ms`, `stabilization_timeout_ms` — the run
  inputs, echoed back.
- `stabilized` — whether the substrate reached its initial converged baseline
  within the stabilization budget. If `false`, the matrix never ran.
- `all_passed` — `true` only if `stabilized` is `true` and every case passed; this
  is what the exit code mirrors.
- `cases[]` — one object per test case.

Each case object:

- `name` — the test case id (e.g. `single_link_failure_ospf`).
- `passed` — the case verdict.
- `within_timeout` — whether convergence was observed inside
  `convergence_timeout_ms` (set by the cases that take a timed measurement).
- `control_plane_convergence_ms` — `vtysh`-observed routing-table reconvergence.
- `data_plane_convergence_ms` — libpcap-observed traffic resumption.
- `detail` — a human-readable explanation of the verdict.

The two convergence numbers are distinct measurements (routing-table update vs.
traffic resumption) and are never collapsed into one. They are reported as `-1`
for the cases that do not take a timed measurement (and for every case on the
`--dry-run` path, which never touches the kernel).

## Demo and visuals

The images above are generated, not hand-drawn. `scripts/demo.sh` drives the
engine through its lifecycle on the dry-run path and narrates each phase, printing
the topology, the per-case matrix, and the exit-code gate; every number it shows is
read back from the engine's own JSON report. The substrate-only measurements
(`data_plane`/`control_plane_convergence_ms`) are reported as `-1` in dry-run, since
they require a live host.

```bash
# Run the narrated walkthrough in your terminal.
bash scripts/demo.sh

# Regenerate every asset under media/ (topology diagram, GIF, hero still, cast).
bash scripts/make_media.sh
```

| Asset | What it is |
|---|---|
| `media/demo.gif` | Scrolling terminal capture of the full lifecycle, ending on the pass/fail gate |
| `media/demo_hero.png` | The same walkthrough as a single still image |
| `media/demo.cast` | asciinema v2 cast (`asciinema play media/demo.cast`; convert with `agg`/`ffmpeg`) |
| `media/topology.{png,svg}` | Graphviz rendering of `topology.json` |

Regenerating the diagram needs Graphviz; the GIF and still need Python with Pillow.
A recording of the visualizer authoring a topology, and a capture of the live
Mininet/FRR matrix, are best produced on a desktop host with a display and root.

## Running the full matrix on a substrate (privileged host only)

The emulation substrate uses Linux network namespaces, veth pairs, Open vSwitch,
`tc`, and FRRouting. It requires root (or `CAP_NET_ADMIN`) and cannot run in an
unprivileged environment. The sequence below is what the CI `validate-topology`
job runs; it reproduces the same result locally.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_VISUALIZER=OFF
cmake --build build --parallel --target chaos_engine link_shaper_cli

# OVS provides the bridge backplane; make sure it is running.
sudo systemctl start openvswitch-switch || sudo service openvswitch-switch start

# Bring the substrate up in the background. It builds the namespaces, addresses
# and shapes every link, starts the FRR daemon set, then waits for SIGTERM.
# mininet_topology.py picks up the shaper from $LINK_SHAPER_CLI.
export LINK_SHAPER_CLI="$PWD/build/link_shaper_cli"
sudo -E python3 substrate/mininet_topology.py --topology topology.json \
  > build/substrate.log 2>&1 &
SUBSTRATE_PID=$!

# Give FRR time to build the namespaces and form initial adjacencies.
sleep 25

sudo -E ./build/chaos_engine \
  --topology topology.json \
  --report build/convergence_report.json \
  --convergence-timeout-ms 8000 \
  --stabilization-timeout-ms 60000
STATUS=$?

cat build/convergence_report.json
sudo kill "$SUBSTRATE_PID" 2>/dev/null
exit "$STATUS"
```

The engine's exit code is the gate (0 = every case passed). The Python builder
reproduces the exact addressing scheme from `common/address_plan.h`, so the
engine can map observed next-hop IPs back to node ids when asserting against the
solver.

To explore the substrate by hand instead, drop into the Mininet CLI with `--cli`
and tear it down with Ctrl-D:

```bash
export LINK_SHAPER_CLI="$PWD/build/link_shaper_cli"
sudo -E python3 substrate/mininet_topology.py --topology topology.json --cli
```

## The visualizer (optional)

The ImGui authoring tool is an optional target because it needs GLFW, OpenGL, and
a Dear ImGui checkout:

```bash
cmake -S . -B build \
  -DBUILD_VISUALIZER=ON \
  -DIMGUI_DIR=/path/to/imgui
cmake --build build --target visualizer
./build/visualizer .
```

Double-click the canvas to add routers, drag to arrange, pan/zoom the infinite
canvas, edit protocols in the side panel, then commit and push. The git
integrator captures the push exit code and surfaces an explicit success/failure
state, because that commit is the trusted trigger for the validation pipeline.

## What runs where

- **Unit tests and the dry-run integration matrix** run on any host via `ctest`.
- **The live test matrix** (kernel namespaces, FRR adjacencies, libpcap capture,
  `tc` shaping, link/node failure injection) requires a privileged host with
  Mininet, Open vSwitch, and FRRouting. The CI workflow automates this on an
  ephemeral, single-use runner, which is what makes running as root acceptable.
  See [`tests/integration/README.md`](tests/integration/README.md).
