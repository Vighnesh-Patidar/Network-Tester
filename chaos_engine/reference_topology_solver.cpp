#include "reference_topology_solver.h"

#include <algorithm>
#include <limits>
#include <queue>
#include <set>
#include <unordered_map>

namespace nch {

const std::map<std::string, Route> RoutingTable::kEmpty{};

void RoutingTable::set(const std::string& source, const Route& route) {
    entries_[source][route.destination] = route;
}

const Route* RoutingTable::lookup(const std::string& source,
                                  const std::string& destination) const {
    auto src_it = entries_.find(source);
    if (src_it == entries_.end()) {
        return nullptr;
    }
    auto dst_it = src_it->second.find(destination);
    if (dst_it == src_it->second.end()) {
        return nullptr;
    }
    return &dst_it->second;
}

const std::map<std::string, Route>& RoutingTable::routes_from(
    const std::string& source) const {
    auto it = entries_.find(source);
    return it == entries_.end() ? kEmpty : it->second;
}

bool RoutingTable::has_source(const std::string& source) const {
    return entries_.find(source) != entries_.end();
}

namespace {

struct Edge {
    std::string to;
    int weight;
};

// Adjacency list restricted to OSPF participants, excluding any failed links.
std::unordered_map<std::string, std::vector<Edge>> build_ospf_graph(
    const Topology& topology, const std::vector<std::string>& down_links) {
    std::set<std::string> down(down_links.begin(), down_links.end());
    std::unordered_map<std::string, std::vector<Edge>> graph;

    for (const auto& node : topology.nodes) {
        if (node.runs(Protocol::Ospf)) {
            graph[node.id];  // ensure isolated OSPF nodes still appear
        }
    }

    for (const auto& link : topology.links) {
        if (down.count(link.id)) {
            continue;
        }
        const Node* a = topology.find_node(link.a);
        const Node* b = topology.find_node(link.b);
        if (a == nullptr || b == nullptr) {
            continue;
        }
        if (!a->runs(Protocol::Ospf) || !b->runs(Protocol::Ospf)) {
            continue;
        }
        graph[link.a].push_back({link.b, link.metric});
        graph[link.b].push_back({link.a, link.metric});
    }

    return graph;
}

}  // namespace

RoutingTable ReferenceTopologySolver::compute_expected_ospf_state(
    const Topology& topology, const std::vector<std::string>& down_links) const {
    const auto graph = build_ospf_graph(topology, down_links);
    RoutingTable table;

    for (const auto& entry : graph) {
        const std::string& source = entry.first;

        std::unordered_map<std::string, int> dist;
        std::unordered_map<std::string, std::string> prev;
        for (const auto& node_entry : graph) {
            dist[node_entry.first] = std::numeric_limits<int>::max();
        }
        dist[source] = 0;

        using QItem = std::pair<int, std::string>;  // (distance, node)
        std::priority_queue<QItem, std::vector<QItem>, std::greater<QItem>> pq;
        pq.push({0, source});

        while (!pq.empty()) {
            auto [d, u] = pq.top();
            pq.pop();
            if (d > dist[u]) {
                continue;
            }
            auto adj_it = graph.find(u);
            if (adj_it == graph.end()) {
                continue;
            }
            for (const auto& edge : adj_it->second) {
                const int nd = d + edge.weight;
                if (nd < dist[edge.to]) {
                    dist[edge.to] = nd;
                    prev[edge.to] = u;
                    pq.push({nd, edge.to});
                }
            }
        }

        for (const auto& node_entry : graph) {
            const std::string& dest = node_entry.first;
            if (dest == source) {
                continue;
            }
            if (dist[dest] == std::numeric_limits<int>::max()) {
                continue;  // unreachable after the modeled failures
            }

            std::vector<std::string> path;
            for (std::string at = dest; at != source; at = prev[at]) {
                path.push_back(at);
            }
            path.push_back(source);
            std::reverse(path.begin(), path.end());

            Route route;
            route.destination = dest;
            route.cost = dist[dest];
            route.path = path;
            route.next_hop = path.size() >= 2 ? path[1] : dest;
            table.set(source, route);
        }
    }

    return table;
}

bool ReferenceTopologySolver::verify_bgp_policy_consistency(
    const Topology& topology, const RoutingTable& observed) const {
    // Build the undirected adjacency set once so each observed hop can be checked
    // against a real link.
    std::set<std::pair<std::string, std::string>> adjacency;
    for (const auto& link : topology.links) {
        adjacency.insert({link.a, link.b});
        adjacency.insert({link.b, link.a});
    }

    for (const auto& node : topology.nodes) {
        if (!node.runs(Protocol::Bgp)) {
            continue;
        }
        if (!observed.has_source(node.id)) {
            continue;
        }
        for (const auto& dest_route : observed.routes_from(node.id)) {
            const Route& route = dest_route.second;

            std::set<std::string> seen;
            for (const auto& hop : route.path) {
                if (!seen.insert(hop).second) {
                    return false;  // a node repeats: the path contains a loop
                }
            }

            for (std::size_t i = 1; i < route.path.size(); ++i) {
                const auto pair = std::make_pair(route.path[i - 1], route.path[i]);
                if (adjacency.find(pair) == adjacency.end()) {
                    return false;  // hop does not correspond to a real adjacency
                }
            }
        }
    }

    return true;
}

}  // namespace nch
