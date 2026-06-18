#pragma once

#include <cstdint>
#include <string>

#include "common/topology.h"
#include "system_execution_engine.h"

namespace nch {

// Summary of one protocol's adjacencies on a single node at one poll.
struct AdjacencyStatus {
    int expected = 0;
    int converged = 0;   // Full (OSPF) or Established (BGP)
    bool query_ok = false;

    bool satisfied() const { return query_ok && converged >= expected; }
};

// Blocks the pipeline until the topology has reached its initial converged state
// (§5.1 / §4.6). Polling daemon adjacency status replaces any fixed sleep, so the
// attack phase never starts against a half-formed network.
class StabilizationGate {
public:
    explicit StabilizationGate(const SystemExecutionEngine& engine,
                               std::uint32_t poll_interval_ms = 500);

    // Returns true once every expected OSPF neighbor is Full and every expected
    // BGP peer is Established, or false if stabilization_timeout_ms elapses first
    // (which is itself a meaningful failure: the topology cannot reach baseline).
    bool wait_for_convergence(const Topology& topology,
                              std::uint32_t stabilization_timeout_ms) const;

    AdjacencyStatus poll_ospf_neighbors(const std::string& node_ns,
                                        int expected_neighbors) const;
    AdjacencyStatus poll_bgp_peers(const std::string& node_ns,
                                   int expected_peers) const;

private:
    const SystemExecutionEngine& engine_;
    std::uint32_t poll_interval_ms_;

    static int expected_ospf_neighbors(const Topology& topology, const Node& node);
};

}  // namespace nch
