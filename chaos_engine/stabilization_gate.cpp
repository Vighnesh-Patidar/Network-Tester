#include "stabilization_gate.h"

#include <chrono>
#include <thread>

#include <nlohmann/json.hpp>

#include "protocol_state.h"

namespace nch {

namespace {

// FRR's JSON schema has shifted across releases, so the counters tolerate both
// "state"/"nbrState" (OSPF) and the per-peer "state"/"bgpState" (BGP) spellings.
int count_full_ospf_neighbors(const nlohmann::json& doc) {
    int full = 0;
    if (!doc.contains("neighbors")) {
        return 0;
    }
    const nlohmann::json& neighbors = doc.at("neighbors");
    for (const auto& entry : neighbors.items()) {
        const nlohmann::json& value = entry.value();
        // Each router id maps to an array of adjacency records.
        if (value.is_array()) {
            for (const auto& record : value) {
                std::string state = record.value("nbrState", record.value("state", ""));
                if (is_full(ospf_state_from_string(state))) {
                    ++full;
                }
            }
        } else if (value.is_object()) {
            std::string state = value.value("nbrState", value.value("state", ""));
            if (is_full(ospf_state_from_string(state))) {
                ++full;
            }
        }
    }
    return full;
}

int count_established_bgp_peers(const nlohmann::json& doc) {
    int established = 0;
    if (!doc.contains("peers")) {
        return 0;
    }
    for (const auto& entry : doc.at("peers").items()) {
        const nlohmann::json& peer = entry.value();
        std::string state = peer.value("state", peer.value("bgpState", ""));
        if (is_established(bgp_state_from_string(state))) {
            ++established;
        }
    }
    return established;
}

}  // namespace

StabilizationGate::StabilizationGate(const SystemExecutionEngine& engine,
                                     std::uint32_t poll_interval_ms)
    : engine_(engine), poll_interval_ms_(poll_interval_ms) {}

int StabilizationGate::expected_ospf_neighbors(const Topology& topology,
                                               const Node& node) {
    int count = 0;
    for (const Link* link : topology.links_for(node.id)) {
        const std::string other = link->other_endpoint(node.id);
        const Node* peer = topology.find_node(other);
        if (peer != nullptr && peer->runs(Protocol::Ospf) && node.runs(Protocol::Ospf)) {
            ++count;
        }
    }
    return count;
}

AdjacencyStatus StabilizationGate::poll_ospf_neighbors(const std::string& node_ns,
                                                       int expected_neighbors) const {
    AdjacencyStatus status;
    status.expected = expected_neighbors;

    const CommandResult result = engine_.run_in_namespace(
        node_ns, {"vtysh", "-c", "show ip ospf neighbor json"});
    if (!result.ok()) {
        return status;
    }

    try {
        const nlohmann::json doc = nlohmann::json::parse(result.stdout_data);
        status.converged = count_full_ospf_neighbors(doc);
        status.query_ok = true;
    } catch (const std::exception&) {
        status.query_ok = false;
    }
    return status;
}

AdjacencyStatus StabilizationGate::poll_bgp_peers(const std::string& node_ns,
                                                  int expected_peers) const {
    AdjacencyStatus status;
    status.expected = expected_peers;

    const CommandResult result =
        engine_.run_in_namespace(node_ns, {"vtysh", "-c", "show bgp summary json"});
    if (!result.ok()) {
        return status;
    }

    try {
        nlohmann::json doc = nlohmann::json::parse(result.stdout_data);
        // "show bgp summary json" nests address families; accept either the flat
        // form or the ipv4Unicast wrapper.
        if (doc.contains("ipv4Unicast")) {
            doc = doc.at("ipv4Unicast");
        }
        status.converged = count_established_bgp_peers(doc);
        status.query_ok = true;
    } catch (const std::exception&) {
        status.query_ok = false;
    }
    return status;
}

bool StabilizationGate::wait_for_convergence(
    const Topology& topology, std::uint32_t stabilization_timeout_ms) const {
    using clock = std::chrono::steady_clock;
    const auto deadline =
        clock::now() + std::chrono::milliseconds(stabilization_timeout_ms);

    while (true) {
        bool all_satisfied = true;

        for (const auto& node : topology.nodes) {
            if (node.runs(Protocol::Ospf)) {
                const int expected = expected_ospf_neighbors(topology, node);
                if (expected > 0) {
                    const AdjacencyStatus ospf = poll_ospf_neighbors(node.id, expected);
                    if (!ospf.satisfied()) {
                        all_satisfied = false;
                    }
                }
            }
            if (node.runs(Protocol::Bgp) && !node.bgp_peers.empty()) {
                const AdjacencyStatus bgp = poll_bgp_peers(
                    node.id, static_cast<int>(node.bgp_peers.size()));
                if (!bgp.satisfied()) {
                    all_satisfied = false;
                }
            }
        }

        if (all_satisfied) {
            return true;
        }
        if (clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval_ms_));
    }
}

}  // namespace nch
