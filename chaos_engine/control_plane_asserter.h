#pragma once

#include <cstdint>
#include <string>

#include "common/address_plan.h"
#include "common/topology.h"
#include "reference_topology_solver.h"
#include "system_execution_engine.h"

namespace nch {

struct ControlPlaneResult {
    bool converged = false;
    double elapsed_ms = -1.0;
    std::string detail;
};

// Polls FRR's RIB through vtysh and asserts the observed forwarding state against
// the ReferenceTopologySolver's expectation (§4.3). Convergence is the moment the
// observed next-hop for every destination matches the independently computed one.
class ControlPlaneAsserter {
public:
    ControlPlaneAsserter(const SystemExecutionEngine& engine,
                         const AddressPlan& address_plan,
                         std::uint32_t poll_interval_ms = 200);

    ControlPlaneResult await_ospf_convergence(const Topology& topology,
                                              const RoutingTable& expected,
                                              std::uint32_t convergence_timeout_ms) const;

    // One snapshot of the observed OSPF forwarding state, expressed in node ids
    // (next-hop IPs resolved back through the AddressPlan).
    RoutingTable snapshot_ospf_routes(const Topology& topology) const;

    // Empty string when observed satisfies expected; otherwise a human-readable
    // description of the first divergence.
    std::string compare(const Topology& topology, const RoutingTable& expected,
                        const RoutingTable& observed) const;

private:
    const SystemExecutionEngine& engine_;
    const AddressPlan& address_plan_;
    std::uint32_t poll_interval_ms_;

    RoutingTable parse_node_routes(const std::string& source,
                                   const Topology& topology,
                                   const std::string& json_text) const;
};

}  // namespace nch
