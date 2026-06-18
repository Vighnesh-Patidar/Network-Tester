#pragma once

#include <cstdint>
#include <string>

#include "common/address_plan.h"
#include "common/topology.h"
#include "control_plane_asserter.h"
#include "convergence.h"
#include "reference_topology_solver.h"
#include "system_execution_engine.h"

namespace nch {

struct OrchestratorConfig {
    std::string topology_path;
    std::string report_path;
    std::uint32_t convergence_timeout_ms = 5000;
    std::uint32_t stabilization_timeout_ms = 30000;
    // When true the engine performs no kernel mutation: it exercises the parser,
    // solver and assertion logic and reports the predicted post-failure state.
    // This is what runs on a host without root/Mininet/FRR.
    bool dry_run = false;
};

// Top-level Tier 4 driver. Stabilizes the substrate, walks the §6 matrix, and
// produces a ConvergenceReport whose all_passed() drives the process exit code
// that CI gates on (§3).
class ChaosOrchestrator {
public:
    ChaosOrchestrator(Topology topology, OrchestratorConfig config,
                      SystemExecutionEngine& engine);

    ConvergenceReport run();

private:
    Topology topology_;
    OrchestratorConfig config_;
    SystemExecutionEngine& engine_;
    AddressPlan address_plan_;
    ReferenceTopologySolver solver_;

    // Picks the first inter-router OSPF link whose removal leaves the OSPF graph
    // fully connected (i.e. a genuine failover path exists). Empty if none.
    const Link* pick_failover_ospf_link() const;
    const Link* pick_bgp_session_link() const;

    bool all_ospf_pairs_reachable(const std::vector<std::string>& down_links) const;
    bool path_changed(const RoutingTable& before, const RoutingTable& after) const;

    TestCaseResult single_link_failure_ospf();
    TestCaseResult single_link_failure_bgp();
    TestCaseResult node_failure();
    TestCaseResult asymmetric_packet_loss();
    TestCaseResult flapping_link();
    TestCaseResult failover_under_capacity();
};

}  // namespace nch
