#pragma once

#include <map>
#include <string>
#include <vector>

#include "common/topology.h"

namespace nch {

// A single computed route entry from one source node to one destination node.
struct Route {
    std::string destination;          // destination node id
    std::string next_hop;             // first hop (adjacent node) toward destination
    int cost = 0;                     // summed link metric along the path
    std::vector<std::string> path;    // full node path, source ... destination
};

// Expected forwarding state for the whole topology: a per-source routing table.
class RoutingTable {
public:
    void set(const std::string& source, const Route& route);
    const Route* lookup(const std::string& source, const std::string& destination) const;
    const std::map<std::string, Route>& routes_from(const std::string& source) const;
    bool has_source(const std::string& source) const;
    std::size_t size() const { return entries_.size(); }

private:
    std::map<std::string, std::map<std::string, Route>> entries_;
    static const std::map<std::string, Route> kEmpty;
};

// Independently derives the expected converged state from the graph, so the
// Control-Plane Asserter checks FRR against a ground truth rather than against
// FRR's own computation (§4.3, §5.3).
class ReferenceTopologySolver {
public:
    // OSPF: shortest path over the link-state view, link metric as edge weight,
    // restricted to nodes/links that participate in OSPF. Links that are listed
    // in down_links are removed from the graph, which is how the solver predicts
    // the post-failure steady state for a given test case.
    RoutingTable compute_expected_ospf_state(
        const Topology& topology,
        const std::vector<std::string>& down_links = {}) const;

    // BGP: the weaker, policy-oriented claim (§4.3). Every observed route must be
    // loop-free (no node repeated) and every consecutive hop must correspond to a
    // real adjacency in the topology. Returns false on the first violation.
    bool verify_bgp_policy_consistency(const Topology& topology,
                                       const RoutingTable& observed) const;
};

}  // namespace nch
