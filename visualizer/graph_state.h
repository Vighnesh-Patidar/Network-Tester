#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/topology.h"

namespace nch {

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

using Uuid = std::uint64_t;

// A node as it exists in the editor: the canonical model plus its canvas
// position. The model.id is the router name written to topology.json.
struct VisualNode {
    Uuid uuid = 0;
    Node model;
    Vec2 position;
};

// A link references its endpoints by UUID, never by pointer, so graph edits that
// reallocate the underlying vectors can never leave a dangling reference.
struct VisualLink {
    Uuid uuid = 0;
    Uuid endpoint_a = 0;
    Uuid endpoint_b = 0;
    Link model;
};

// Owns the editable graph. Storage is a contiguous std::vector for cache-friendly
// iteration during rendering; a UUID->index map keeps lookups O(1) and stable
// across insertions and swap-erase removals (§2, Tier 1).
class GraphStateManager {
public:
    Uuid add_node(const std::string& id, Vec2 position);
    Uuid add_link(Uuid endpoint_a, Uuid endpoint_b, const std::string& link_id);

    bool remove_node(Uuid uuid);  // also removes incident links
    bool remove_link(Uuid uuid);

    VisualNode* node(Uuid uuid);
    const VisualNode* node(Uuid uuid) const;
    VisualLink* link(Uuid uuid);
    const VisualLink* link(Uuid uuid) const;

    const std::vector<VisualNode>& nodes() const { return nodes_; }
    const std::vector<VisualLink>& links() const { return links_; }
    std::vector<VisualNode>& nodes() { return nodes_; }
    std::vector<VisualLink>& links() { return links_; }

    // Bridges the editor graph to the shared data contract.
    Topology to_topology() const;
    void load_topology(const Topology& topology);

    void clear();

private:
    std::vector<VisualNode> nodes_;
    std::vector<VisualLink> links_;
    std::unordered_map<Uuid, std::size_t> node_index_;
    std::unordered_map<Uuid, std::size_t> link_index_;
    Uuid next_uuid_ = 1;
};

}  // namespace nch
