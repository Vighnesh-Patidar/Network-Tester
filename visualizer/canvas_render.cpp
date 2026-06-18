#include "canvas_render.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace nch {

namespace {

constexpr float kNodeRadius = 22.0f;

ImU32 protocol_color(const Node& node) {
    const bool ospf = node.runs(Protocol::Ospf);
    const bool bgp = node.runs(Protocol::Bgp);
    if (ospf && bgp) {
        return IM_COL32(173, 127, 222, 255);
    }
    if (bgp) {
        return IM_COL32(222, 158, 102, 255);
    }
    return IM_COL32(102, 170, 222, 255);
}

}  // namespace

void CanvasRenderer::handle_pan_zoom(const ImVec2& canvas_origin,
                                     const ImVec2& canvas_size) {
    ImGuiIO& io = ImGui::GetIO();
    const bool hovered = ImGui::IsItemHovered();

    // Pan with a right- or middle-drag anywhere on the canvas.
    if (hovered && (ImGui::IsMouseDragging(ImGuiMouseButton_Right) ||
                    ImGui::IsMouseDragging(ImGuiMouseButton_Middle))) {
        camera_.pan.x += io.MouseDelta.x;
        camera_.pan.y += io.MouseDelta.y;
    }

    // Zoom toward the cursor so the point under the mouse stays put.
    if (hovered && io.MouseWheel != 0.0f) {
        const ImVec2 mouse = io.MousePos;
        const ImVec2 world_before = camera_.screen_to_world(canvas_origin, mouse);
        const float factor = std::pow(1.1f, io.MouseWheel);
        camera_.zoom = std::clamp(camera_.zoom * factor, 0.1f, 6.0f);
        const ImVec2 world_after = camera_.screen_to_world(canvas_origin, mouse);
        camera_.pan.x += (world_after.x - world_before.x) * camera_.zoom;
        camera_.pan.y += (world_after.y - world_before.y) * camera_.zoom;
    }

    (void)canvas_size;
}

void CanvasRenderer::draw_grid(ImDrawList* draw_list, const ImVec2& origin,
                               const ImVec2& size) const {
    const ImU32 grid_color = IM_COL32(60, 60, 70, 255);
    const float spacing = 64.0f * camera_.zoom;
    if (spacing <= 1.0f) {
        return;
    }
    const float start_x = std::fmod(camera_.pan.x, spacing);
    const float start_y = std::fmod(camera_.pan.y, spacing);
    for (float x = start_x; x < size.x; x += spacing) {
        draw_list->AddLine(ImVec2(origin.x + x, origin.y),
                           ImVec2(origin.x + x, origin.y + size.y), grid_color);
    }
    for (float y = start_y; y < size.y; y += spacing) {
        draw_list->AddLine(ImVec2(origin.x, origin.y + y),
                           ImVec2(origin.x + size.x, origin.y + y), grid_color);
    }
}

void CanvasRenderer::draw(GraphStateManager& graph) {
    const ImVec2 canvas_origin = ImGui::GetCursorScreenPos();
    ImVec2 canvas_size = ImGui::GetContentRegionAvail();
    if (canvas_size.x < 64.0f) canvas_size.x = 64.0f;
    if (canvas_size.y < 64.0f) canvas_size.y = 64.0f;

    // An invisible button captures mouse interaction over the whole canvas.
    ImGui::InvisibleButton("canvas", canvas_size,
                           ImGuiButtonFlags_MouseButtonLeft |
                               ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);
    handle_pan_zoom(canvas_origin, canvas_size);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->PushClipRect(canvas_origin,
                            ImVec2(canvas_origin.x + canvas_size.x,
                                   canvas_origin.y + canvas_size.y),
                            true);
    draw_list->AddRectFilled(canvas_origin,
                             ImVec2(canvas_origin.x + canvas_size.x,
                                    canvas_origin.y + canvas_size.y),
                             IM_COL32(34, 34, 40, 255));
    draw_grid(draw_list, canvas_origin, canvas_size);

    for (const auto& link : graph.links()) {
        const VisualNode* a = graph.node(link.endpoint_a);
        const VisualNode* b = graph.node(link.endpoint_b);
        if (a == nullptr || b == nullptr) {
            continue;
        }
        const ImVec2 pa = camera_.world_to_screen(canvas_origin,
                                                  ImVec2(a->position.x, a->position.y));
        const ImVec2 pb = camera_.world_to_screen(canvas_origin,
                                                  ImVec2(b->position.x, b->position.y));
        draw_list->AddLine(pa, pb, IM_COL32(150, 150, 160, 255), 2.0f);
        const ImVec2 mid((pa.x + pb.x) * 0.5f, (pa.y + pb.y) * 0.5f);
        const std::string label = std::to_string(link.model.metric);
        draw_list->AddText(mid, IM_COL32(200, 200, 210, 255), label.c_str());
    }

    ImGuiIO& io = ImGui::GetIO();

    for (auto& node : graph.nodes()) {
        const ImVec2 center = camera_.world_to_screen(
            canvas_origin, ImVec2(node.position.x, node.position.y));
        const float radius = kNodeRadius * camera_.zoom;

        const float dx = io.MousePos.x - center.x;
        const float dy = io.MousePos.y - center.y;
        const bool over = (dx * dx + dy * dy) <= radius * radius;

        if (over && ImGui::IsItemActive() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            dragging_node_ = node.uuid;
            selected_node_ = node.uuid;
        }
        if (dragging_node_ == node.uuid &&
            ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            node.position.x += io.MouseDelta.x / camera_.zoom;
            node.position.y += io.MouseDelta.y / camera_.zoom;
        }

        const ImU32 fill = protocol_color(node.model);
        draw_list->AddCircleFilled(center, radius, fill);
        if (node.uuid == selected_node_) {
            draw_list->AddCircle(center, radius + 3.0f, IM_COL32(255, 255, 255, 255), 0,
                                 2.0f);
        }
        draw_list->AddText(ImVec2(center.x - radius, center.y + radius + 2.0f),
                           IM_COL32(230, 230, 235, 255), node.model.id.c_str());
    }

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        dragging_node_ = 0;
    }

    // Double-click on empty canvas creates a node at the world position.
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
        dragging_node_ == 0) {
        const ImVec2 world = camera_.screen_to_world(canvas_origin, io.MousePos);
        const std::string name = "r" + std::to_string(++node_counter_);
        graph.add_node(name, Vec2{world.x, world.y});
    }

    draw_list->PopClipRect();
}

}  // namespace nch
