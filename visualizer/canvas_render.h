#pragma once

#include "graph_state.h"

#include "imgui.h"

namespace nch {

// Affine camera mapping world (graph) coordinates to screen space. The transform
// is screen = canvas_origin + world * zoom + pan, which gives uniform pan/zoom and
// an effectively infinite canvas (§2, Tier 1).
struct Camera {
    ImVec2 pan{0.0f, 0.0f};
    float zoom = 1.0f;

    ImVec2 world_to_screen(const ImVec2& origin, const ImVec2& world) const {
        return ImVec2(origin.x + world.x * zoom + pan.x,
                      origin.y + world.y * zoom + pan.y);
    }

    ImVec2 screen_to_world(const ImVec2& origin, const ImVec2& screen) const {
        return ImVec2((screen.x - origin.x - pan.x) / zoom,
                      (screen.y - origin.y - pan.y) / zoom);
    }
};

// Renders and edits the graph on an ImDrawList-backed canvas: infinite-panning
// background grid, links, draggable nodes, selection, and node creation.
class CanvasRenderer {
public:
    // Draws the interactive canvas filling the current ImGui window content area.
    void draw(GraphStateManager& graph);

    Uuid selected_node() const { return selected_node_; }
    const Camera& camera() const { return camera_; }

private:
    Camera camera_;
    Uuid selected_node_ = 0;
    Uuid dragging_node_ = 0;
    Uuid pending_link_source_ = 0;
    int node_counter_ = 0;

    void handle_pan_zoom(const ImVec2& canvas_origin, const ImVec2& canvas_size);
    void draw_grid(ImDrawList* draw_list, const ImVec2& origin, const ImVec2& size) const;
};

}  // namespace nch
