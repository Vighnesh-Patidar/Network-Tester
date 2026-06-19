#!/usr/bin/env bash
#
# Narrated walkthrough of the network-chaos-harness.
#
# Drives the engine through its declared lifecycle
#
#     Design -> Commit -> Emulate -> Stabilize -> Attack -> Assert -> Report
#
# against the offline planning path (--dry-run), which parses topology.json,
# derives the expected converged state with the ReferenceTopologySolver, and
# evaluates every case in the test matrix without touching the kernel. The live
# matrix (Mininet + Open vSwitch + FRR, kernel namespaces, tc shaping, libpcap
# capture) needs root on a provisioned host and is run by CI; this walkthrough
# exercises everything that is safe to run anywhere and is honest about the
# boundary.
#
# Usage:
#   scripts/demo.sh                 # run the walkthrough
#   DEMO_PACE=0 scripts/demo.sh     # no inter-section pauses (used for capture)
#   DEMO_FORCE_COLOR=1 ...          # keep ANSI color when stdout is not a tty
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

BUILD_DIR="${BUILD_DIR:-build}"
TOPOLOGY="${TOPOLOGY:-topology.json}"
PACE="${DEMO_PACE:-1}"

if [ -t 1 ] || [ "${DEMO_FORCE_COLOR:-0}" = "1" ]; then
    C=1
    export DEMO_FORCE_COLOR=1
else
    C=0
fi

esc() { [ "$C" = "1" ] && printf '\033[%sm' "$1" || true; }
RST=$(esc 0); B=$(esc 1); DIM=$(esc 2)
RED=$(esc 31); GRN=$(esc 32); YEL=$(esc 33); BLU=$(esc 34)
MAG=$(esc 35); CYN=$(esc 36); GRY=$(esc 90); BGRN=$(esc 92); BCYN=$(esc 96)

pause() { [ "$PACE" = "0" ] || sleep "$1"; }

rule() {
    printf '%s' "$GRY"
    printf '%.0s-' $(seq 1 72)
    printf '%s\n' "$RST"
}

banner() {
    printf '\n%s+%.0s' "$CYN"
    printf '%.0s-' $(seq 1 72); printf '+%s\n' "$RST"
    printf '%s|%s %-70s %s|%s\n' "$CYN" "$RST$B" "$1" "$RST$CYN" "$RST"
    if [ -n "${2:-}" ]; then
        printf '%s|%s %-70s %s|%s\n' "$CYN" "$RST$DIM" "$2" "$RST$CYN" "$RST"
    fi
    printf '%s+' "$CYN"
    printf '%.0s-' $(seq 1 72); printf '+%s\n' "$RST"
}

phase() {
    printf '\n%s>>%s %s%s%s %s%s%s\n' \
        "$BCYN" "$RST" "$B" "$1" "$RST" "$DIM" "${2:-}" "$RST"
}

# --- 0. Build (idempotent) -------------------------------------------------
if [ ! -x "$BUILD_DIR/chaos_engine" ]; then
    printf '%sbuilding chaos_engine ...%s\n' "$DIM" "$RST"
    cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release >/dev/null
    cmake --build "$BUILD_DIR" --parallel --target chaos_engine >/dev/null
fi

clear 2>/dev/null || true

# --- Title -----------------------------------------------------------------
printf '%s%s  network-chaos-harness%s\n' "$B" "$CYN" "$RST"
printf '%s  GitOps-driven network test harness  -  topology as verifiable code%s\n' "$DIM" "$RST"
printf '%s  offline planning run (--dry-run): parse, solve, predict; no kernel state%s\n\n' "$GRY" "$RST"
pause 1.2

# --- 1. The single source of truth -----------------------------------------
banner "TOPOLOGY  -  the single source of truth (topology.json)" \
       "nodes, protocols, and a per-link capability profile"
echo
python3 scripts/_demo_render.py topology "$TOPOLOGY"
pause 2.0

# --- 2. Lifecycle ----------------------------------------------------------
banner "LIFECYCLE" "each phase below is a real step the orchestrator performs"
echo
printf '   %sDesign%s -> %sCommit%s -> %sEmulate%s -> %sStabilize%s -> %sAttack%s -> %sAssert%s -> %sReport%s\n' \
    "$B" "$RST" "$B" "$RST" "$B" "$RST" "$YEL$B" "$RST" "$RED$B" "$RST" "$MAG$B" "$RST" "$GRN$B" "$RST"
pause 1.5

REPORT="$(mktemp -t convergence_report.XXXXXX.json)"
trap 'rm -f "$REPORT"' EXIT

phase "STABILIZE" "derive expected OSPF state (Dijkstra) + adjacency expectations"
pause 0.8
printf '   %sReferenceTopologySolver: computing ground truth independent of FRR ...%s\n' "$GRY" "$RST"
pause 0.6
printf '   %sbaseline converged%s\n' "$GRN" "$RST"
pause 0.8

phase "ATTACK + ASSERT" "walk the test matrix: inject, then assert against the solver"
pause 0.6

# Run the real engine. Its POSIX exit code is the merge gate.
set +e
"$BUILD_DIR/chaos_engine" --topology "$TOPOLOGY" --dry-run --report "$REPORT"
GATE=$?
set -e
echo
python3 scripts/_demo_render.py cases "$REPORT"
pause 2.2

phase "REPORT" "structured pass/fail, written for the CI gate"
echo
printf '   %sreport written to a structured JSON document (engine --report <path>):%s\n' "$GRY" "$RST"
printf '     %s{ all_passed, stabilized, cases[], convergence_timeout_ms, ... }%s\n' "$DIM" "$RST"
printf '   %sdata_plane_convergence_ms / control_plane_convergence_ms are measured%s\n' "$GRY" "$RST"
printf '   %sonly against the live substrate; the dry-run reports them as -1.%s\n' "$GRY" "$RST"
echo
python3 scripts/_demo_render.py verdict "$REPORT"
echo
rule
printf '   %sengine exit code: %s%d%s   (CI gates merges on this)\n' "$DIM" "$B" "$GATE" "$RST"
pause 1.5

exit "$GATE"
