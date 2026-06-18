#include "topology.h"

#include <algorithm>
#include <set>
#include <stdexcept>

namespace nch {

std::string to_string(Protocol protocol) {
    switch (protocol) {
        case Protocol::Ospf:
            return "ospf";
        case Protocol::Bgp:
            return "bgp";
    }
    return "unknown";
}

std::optional<Protocol> protocol_from_string(const std::string& value) {
    if (value == "ospf") {
        return Protocol::Ospf;
    }
    if (value == "bgp") {
        return Protocol::Bgp;
    }
    return std::nullopt;
}

bool Node::runs(Protocol protocol) const {
    return std::find(protocols.begin(), protocols.end(), protocol) != protocols.end();
}

std::string Link::other_endpoint(const std::string& node_id) const {
    if (a == node_id) {
        return b;
    }
    if (b == node_id) {
        return a;
    }
    return {};
}

bool Link::touches(const std::string& node_id) const {
    return a == node_id || b == node_id;
}

const Node* Topology::find_node(const std::string& id) const {
    for (const auto& node : nodes) {
        if (node.id == id) {
            return &node;
        }
    }
    return nullptr;
}

const Link* Topology::find_link(const std::string& id) const {
    for (const auto& link : links) {
        if (link.id == id) {
            return &link;
        }
    }
    return nullptr;
}

std::vector<const Link*> Topology::links_for(const std::string& node_id) const {
    std::vector<const Link*> result;
    for (const auto& link : links) {
        if (link.touches(node_id)) {
            result.push_back(&link);
        }
    }
    return result;
}

void Topology::validate() const {
    std::set<std::string> node_ids;
    for (const auto& node : nodes) {
        if (node.id.empty()) {
            throw std::runtime_error("topology contains a node with an empty id");
        }
        if (!node_ids.insert(node.id).second) {
            throw std::runtime_error("duplicate node id: " + node.id);
        }
        if (node.runs(Protocol::Bgp) && !node.bgp_as.has_value()) {
            throw std::runtime_error("node '" + node.id +
                                     "' runs bgp but declares no bgp_as");
        }
        for (const auto& peer : node.bgp_peers) {
            if (peer.peer.empty()) {
                throw std::runtime_error("node '" + node.id +
                                         "' has a bgp peer with an empty id");
            }
        }
    }

    std::set<std::string> link_ids;
    for (const auto& link : links) {
        if (link.id.empty()) {
            throw std::runtime_error("topology contains a link with an empty id");
        }
        if (!link_ids.insert(link.id).second) {
            throw std::runtime_error("duplicate link id: " + link.id);
        }
        if (node_ids.find(link.a) == node_ids.end()) {
            throw std::runtime_error("link '" + link.id + "' references unknown node '" +
                                     link.a + "'");
        }
        if (node_ids.find(link.b) == node_ids.end()) {
            throw std::runtime_error("link '" + link.id + "' references unknown node '" +
                                     link.b + "'");
        }
        if (link.a == link.b) {
            throw std::runtime_error("link '" + link.id + "' is a self-loop on '" +
                                     link.a + "'");
        }
        if (link.metric <= 0) {
            throw std::runtime_error("link '" + link.id +
                                     "' has a non-positive metric");
        }
        if (link.loss_pct < 0.0 || link.loss_pct > 100.0) {
            throw std::runtime_error("link '" + link.id +
                                     "' has loss_pct outside [0, 100]");
        }
    }

    for (const auto& node : nodes) {
        for (const auto& peer : node.bgp_peers) {
            if (node_ids.find(peer.peer) == node_ids.end()) {
                throw std::runtime_error("node '" + node.id +
                                         "' peers with unknown node '" + peer.peer + "'");
            }
        }
    }
}

Topology topology_from_json(const nlohmann::json& doc) {
    Topology topology;

    if (doc.contains("nodes")) {
        for (const auto& node_json : doc.at("nodes")) {
            Node node;
            node.id = node_json.at("id").get<std::string>();

            if (node_json.contains("protocols")) {
                for (const auto& proto_json : node_json.at("protocols")) {
                    const std::string proto = proto_json.get<std::string>();
                    auto parsed = protocol_from_string(proto);
                    if (!parsed.has_value()) {
                        throw std::runtime_error("node '" + node.id +
                                                 "' declares unknown protocol '" + proto +
                                                 "'");
                    }
                    node.protocols.push_back(*parsed);
                }
            }

            node.ospf_area = node_json.value("ospf_area", std::string("0.0.0.0"));

            if (node_json.contains("bgp_as")) {
                node.bgp_as = node_json.at("bgp_as").get<std::uint32_t>();
            }

            if (node_json.contains("bgp_peers")) {
                for (const auto& peer_json : node_json.at("bgp_peers")) {
                    BgpPeer peer;
                    peer.peer = peer_json.at("peer").get<std::string>();
                    peer.remote_as = peer_json.value("remote_as", 0u);
                    node.bgp_peers.push_back(peer);
                }
            }

            topology.nodes.push_back(std::move(node));
        }
    }

    if (doc.contains("links")) {
        for (const auto& link_json : doc.at("links")) {
            Link link;
            link.id = link_json.at("id").get<std::string>();
            link.a = link_json.at("a").get<std::string>();
            link.b = link_json.at("b").get<std::string>();
            link.metric = link_json.value("metric", 10);
            link.capacity_mbps = link_json.value("capacity_mbps", 0);
            link.latency_ms = link_json.value("latency_ms", 0.0);
            link.jitter_ms = link_json.value("jitter_ms", 0.0);
            link.loss_pct = link_json.value("loss_pct", 0.0);
            topology.links.push_back(std::move(link));
        }
    }

    return topology;
}

nlohmann::json topology_to_json(const Topology& topology) {
    nlohmann::json doc;
    doc["nodes"] = nlohmann::json::array();
    doc["links"] = nlohmann::json::array();

    for (const auto& node : topology.nodes) {
        nlohmann::json node_json;
        node_json["id"] = node.id;

        nlohmann::json protocols = nlohmann::json::array();
        for (const auto& proto : node.protocols) {
            protocols.push_back(to_string(proto));
        }
        node_json["protocols"] = protocols;
        node_json["ospf_area"] = node.ospf_area;

        if (node.bgp_as.has_value()) {
            node_json["bgp_as"] = *node.bgp_as;
        }

        if (!node.bgp_peers.empty()) {
            nlohmann::json peers = nlohmann::json::array();
            for (const auto& peer : node.bgp_peers) {
                nlohmann::json peer_json;
                peer_json["peer"] = peer.peer;
                peer_json["remote_as"] = peer.remote_as;
                peers.push_back(peer_json);
            }
            node_json["bgp_peers"] = peers;
        }

        doc["nodes"].push_back(node_json);
    }

    for (const auto& link : topology.links) {
        nlohmann::json link_json;
        link_json["id"] = link.id;
        link_json["a"] = link.a;
        link_json["b"] = link.b;
        link_json["metric"] = link.metric;
        link_json["capacity_mbps"] = link.capacity_mbps;
        link_json["latency_ms"] = link.latency_ms;
        link_json["jitter_ms"] = link.jitter_ms;
        link_json["loss_pct"] = link.loss_pct;
        doc["links"].push_back(link_json);
    }

    return doc;
}

}  // namespace nch
