#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace nch {

// Routing protocols a node may run. The substrate maps each of these onto a
// concrete FRR daemon (ospfd, bgpd) during Tier 3 bring-up.
enum class Protocol {
    Ospf,
    Bgp,
};

std::string to_string(Protocol protocol);
std::optional<Protocol> protocol_from_string(const std::string& value);

// A configured BGP adjacency. Peers reference another node by id; the substrate
// resolves the peer's interface address from the link that connects the two.
struct BgpPeer {
    std::string peer;          // remote node id
    std::uint32_t remote_as = 0;
};

struct Node {
    std::string id;
    std::vector<Protocol> protocols;
    std::string ospf_area = "0.0.0.0";
    std::optional<std::uint32_t> bgp_as;
    std::vector<BgpPeer> bgp_peers;

    bool runs(Protocol protocol) const;
};

// A bidirectional link between two nodes. The capability fields (§1.1) are the
// declared profile the LinkShaper enforces with tc; they are part of the single
// source of truth rather than an assumption baked into test code.
struct Link {
    std::string id;
    std::string a;
    std::string b;
    int metric = 10;
    int capacity_mbps = 0;     // 0 means "unshaped" (no tbf applied)
    double latency_ms = 0.0;
    double jitter_ms = 0.0;
    double loss_pct = 0.0;

    // Returns the endpoint opposite to node_id, or empty if node_id is not an
    // endpoint of this link.
    std::string other_endpoint(const std::string& node_id) const;
    bool touches(const std::string& node_id) const;
};

struct Topology {
    std::vector<Node> nodes;
    std::vector<Link> links;

    const Node* find_node(const std::string& id) const;
    const Link* find_link(const std::string& id) const;
    std::vector<const Link*> links_for(const std::string& node_id) const;

    // Throws std::runtime_error describing the first structural problem found
    // (duplicate ids, links referencing unknown nodes, missing BGP AS, ...).
    void validate() const;
};

// Free-function (de)serialization keeps the data contract in one place so the
// visualizer (writer) and chaos engine (reader) cannot drift apart.
Topology topology_from_json(const nlohmann::json& doc);
nlohmann::json topology_to_json(const Topology& topology);

}  // namespace nch
