#include "control_plane_asserter.h"

#include <chrono>
#include <thread>

#include <nlohmann/json.hpp>

namespace nch {

ControlPlaneAsserter::ControlPlaneAsserter(const SystemExecutionEngine& engine,
                                           const AddressPlan& address_plan,
                                           std::uint32_t poll_interval_ms)
    : engine_(engine), address_plan_(address_plan), poll_interval_ms_(poll_interval_ms) {}

RoutingTable ControlPlaneAsserter::parse_node_routes(const std::string& source,
                                                     const Topology& topology,
                                                     const std::string& json_text) const {
    RoutingTable table;
    const nlohmann::json doc = nlohmann::json::parse(json_text);

    // "show ip route json" is an object keyed by prefix; each value is an array of
    // candidate routes. We only care about destination loopback /32s.
    for (const auto& entry : doc.items()) {
        const std::string& prefix = entry.key();
        std::string dest;
        for (const auto& node : topology.nodes) {
            if (address_plan_.loopback_prefix(node.id) == prefix) {
                dest = node.id;
                break;
            }
        }
        if (dest.empty() || dest == source) {
            continue;
        }

        const nlohmann::json& candidates = entry.value();
        if (!candidates.is_array() || candidates.empty()) {
            continue;
        }

        // FRR lists the selected route first; take its first nexthop.
        const nlohmann::json& best = candidates.at(0);
        if (!best.contains("nexthops")) {
            continue;
        }
        for (const auto& nh : best.at("nexthops")) {
            const std::string ip = nh.value("ip", "");
            const std::string nh_node = address_plan_.node_for_address(ip);
            if (!nh_node.empty()) {
                Route route;
                route.destination = dest;
                route.next_hop = nh_node;
                route.cost = best.value("metric", 0);
                table.set(source, route);
                break;
            }
        }
    }

    return table;
}

RoutingTable ControlPlaneAsserter::snapshot_ospf_routes(const Topology& topology) const {
    RoutingTable observed;
    for (const auto& node : topology.nodes) {
        if (!node.runs(Protocol::Ospf)) {
            continue;
        }
        const CommandResult result = engine_.run_in_namespace(
            node.id, {"vtysh", "--vty_socket", "/var/run/frr/" + node.id, "-c",
                      "show ip route json"});
        if (!result.ok()) {
            continue;
        }
        try {
            const RoutingTable node_table =
                parse_node_routes(node.id, topology, result.stdout_data);
            for (const auto& dest_route : node_table.routes_from(node.id)) {
                observed.set(node.id, dest_route.second);
            }
        } catch (const std::exception&) {
            // A malformed snapshot is treated as "not yet converged"; the polling
            // loop will retry until the timeout.
        }
    }
    return observed;
}

std::string ControlPlaneAsserter::compare(const Topology& topology,
                                          const RoutingTable& expected,
                                          const RoutingTable& observed) const {
    for (const auto& node : topology.nodes) {
        if (!node.runs(Protocol::Ospf)) {
            continue;
        }
        for (const auto& dest_route : expected.routes_from(node.id)) {
            const std::string& dest = dest_route.first;
            const Route& want = dest_route.second;
            const Route* got = observed.lookup(node.id, dest);
            if (got == nullptr) {
                return node.id + " has no route to " + dest + " (expected next-hop " +
                       want.next_hop + ")";
            }
            if (got->next_hop != want.next_hop) {
                return node.id + " routes to " + dest + " via " + got->next_hop +
                       ", expected " + want.next_hop;
            }
        }
    }
    return {};
}

ControlPlaneResult ControlPlaneAsserter::await_ospf_convergence(
    const Topology& topology, const RoutingTable& expected,
    std::uint32_t convergence_timeout_ms) const {
    using clock = std::chrono::steady_clock;
    const auto start = clock::now();
    const auto deadline = start + std::chrono::milliseconds(convergence_timeout_ms);

    ControlPlaneResult result;
    while (true) {
        const RoutingTable observed = snapshot_ospf_routes(topology);
        const std::string mismatch = compare(topology, expected, observed);
        const auto now = clock::now();
        if (mismatch.empty()) {
            result.converged = true;
            result.elapsed_ms =
                std::chrono::duration<double, std::milli>(now - start).count();
            return result;
        }
        if (now >= deadline) {
            result.converged = false;
            result.elapsed_ms =
                std::chrono::duration<double, std::milli>(now - start).count();
            result.detail = mismatch;
            return result;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval_ms_));
    }
}

}  // namespace nch
