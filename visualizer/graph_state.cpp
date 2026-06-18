#include "graph_state.h"

#include <algorithm>

namespace nch {

Uuid GraphStateManager::add_node(const std::string& id, Vec2 position) {
    VisualNode node;
    node.uuid = next_uuid_++;
    node.model.id = id;
    node.model.protocols = {Protocol::Ospf};
    node.position = position;
    node_index_[node.uuid] = nodes_.size();
    nodes_.push_back(std::move(node));
    return nodes_.back().uuid;
}

Uuid GraphStateManager::add_link(Uuid endpoint_a, Uuid endpoint_b,
                                 const std::string& link_id) {
    const VisualNode* a = node(endpoint_a);
    const VisualNode* b = node(endpoint_b);
    if (a == nullptr || b == nullptr) {
        return 0;
    }
    VisualLink link;
    link.uuid = next_uuid_++;
    link.endpoint_a = endpoint_a;
    link.endpoint_b = endpoint_b;
    link.model.id = link_id;
    link.model.a = a->model.id;
    link.model.b = b->model.id;
    link_index_[link.uuid] = links_.size();
    links_.push_back(std::move(link));
    return links_.back().uuid;
}

bool GraphStateManager::remove_node(Uuid uuid) {
    auto it = node_index_.find(uuid);
    if (it == node_index_.end()) {
        return false;
    }

    // Drop incident links first so no VisualLink is left referencing a dead UUID.
    std::vector<Uuid> incident;
    for (const auto& link : links_) {
        if (link.endpoint_a == uuid || link.endpoint_b == uuid) {
            incident.push_back(link.uuid);
        }
    }
    for (Uuid link_uuid : incident) {
        remove_link(link_uuid);
    }

    const std::size_t idx = it->second;
    nodes_[idx] = std::move(nodes_.back());
    nodes_.pop_back();
    node_index_.erase(it);
    if (idx < nodes_.size()) {
        node_index_[nodes_[idx].uuid] = idx;
    }
    return true;
}

bool GraphStateManager::remove_link(Uuid uuid) {
    auto it = link_index_.find(uuid);
    if (it == link_index_.end()) {
        return false;
    }
    const std::size_t idx = it->second;
    links_[idx] = std::move(links_.back());
    links_.pop_back();
    link_index_.erase(it);
    if (idx < links_.size()) {
        link_index_[links_[idx].uuid] = idx;
    }
    return true;
}

VisualNode* GraphStateManager::node(Uuid uuid) {
    auto it = node_index_.find(uuid);
    return it == node_index_.end() ? nullptr : &nodes_[it->second];
}

const VisualNode* GraphStateManager::node(Uuid uuid) const {
    auto it = node_index_.find(uuid);
    return it == node_index_.end() ? nullptr : &nodes_[it->second];
}

VisualLink* GraphStateManager::link(Uuid uuid) {
    auto it = link_index_.find(uuid);
    return it == link_index_.end() ? nullptr : &links_[it->second];
}

const VisualLink* GraphStateManager::link(Uuid uuid) const {
    auto it = link_index_.find(uuid);
    return it == link_index_.end() ? nullptr : &links_[it->second];
}

Topology GraphStateManager::to_topology() const {
    Topology topology;
    topology.nodes.reserve(nodes_.size());
    for (const auto& visual : nodes_) {
        topology.nodes.push_back(visual.model);
    }
    topology.links.reserve(links_.size());
    for (const auto& visual : links_) {
        Link model = visual.model;
        // Endpoint names are authoritative from the referenced nodes, so a renamed
        // node stays consistent on its links.
        const VisualNode* a = node(visual.endpoint_a);
        const VisualNode* b = node(visual.endpoint_b);
        if (a != nullptr) model.a = a->model.id;
        if (b != nullptr) model.b = b->model.id;
        topology.links.push_back(model);
    }
    return topology;
}

void GraphStateManager::load_topology(const Topology& topology) {
    clear();
    std::unordered_map<std::string, Uuid> by_name;
    float x = 80.0f;
    for (const auto& node : topology.nodes) {
        const Uuid uuid = add_node(node.id, Vec2{x, 120.0f});
        VisualNode* visual = this->node(uuid);
        if (visual != nullptr) {
            visual->model = node;
        }
        by_name[node.id] = uuid;
        x += 160.0f;
    }
    for (const auto& link : topology.links) {
        auto a = by_name.find(link.a);
        auto b = by_name.find(link.b);
        if (a == by_name.end() || b == by_name.end()) {
            continue;
        }
        const Uuid uuid = add_link(a->second, b->second, link.id);
        VisualLink* visual = this->link(uuid);
        if (visual != nullptr) {
            visual->model = link;
        }
    }
}

void GraphStateManager::clear() {
    nodes_.clear();
    links_.clear();
    node_index_.clear();
    link_index_.clear();
    next_uuid_ = 1;
}

}  // namespace nch
