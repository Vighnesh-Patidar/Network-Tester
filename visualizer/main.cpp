#include <cstdio>
#include <string>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#include "canvas_render.h"
#include "chaos_engine/topology_parser.h"
#include "git_integrator.h"
#include "graph_state.h"

namespace {

void glfw_error_callback(int error, const char* description) {
    std::fprintf(stderr, "glfw error %d: %s\n", error, description);
}

// Side panel: editor controls plus the explicit push-result surface (§5.2).
void draw_side_panel(nch::GraphStateManager& graph, nch::CanvasRenderer& canvas,
                     const std::string& repo_path, nch::PushResult& last_push) {
    ImGui::Begin("Topology");
    ImGui::TextUnformatted("Double-click the canvas to add a router.");
    ImGui::TextUnformatted("Right- or middle-drag to pan, wheel to zoom.");
    ImGui::Separator();

    if (nch::VisualNode* node = graph.node(canvas.selected_node())) {
        ImGui::Text("Selected: %s", node->model.id.c_str());

        bool ospf = node->model.runs(nch::Protocol::Ospf);
        bool bgp = node->model.runs(nch::Protocol::Bgp);
        if (ImGui::Checkbox("OSPF", &ospf) | ImGui::Checkbox("BGP", &bgp)) {
            node->model.protocols.clear();
            if (ospf) node->model.protocols.push_back(nch::Protocol::Ospf);
            if (bgp) node->model.protocols.push_back(nch::Protocol::Bgp);
        }
        if (bgp) {
            int as = node->model.bgp_as.has_value()
                         ? static_cast<int>(*node->model.bgp_as)
                         : 65000;
            if (ImGui::InputInt("BGP AS", &as)) {
                node->model.bgp_as = static_cast<std::uint32_t>(as < 0 ? 0 : as);
            }
        }
    } else {
        ImGui::TextUnformatted("No node selected.");
    }

    ImGui::Separator();
    static char message[256] = "Update topology";
    ImGui::InputText("Commit message", message, sizeof(message));
    if (ImGui::Button("Commit and Push")) {
        nch::GitIntegrator integrator(repo_path);
        last_push = integrator.commit_and_push(graph.to_topology(), message);
    }

    if (last_push.exit_code != 0 || last_push.success) {
        if (last_push.success) {
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "Push succeeded");
        } else {
            ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1.0f),
                               "Push failed at '%s' (exit %d)",
                               last_push.stage.c_str(), last_push.exit_code);
            if (!last_push.stderr_capture.empty()) {
                ImGui::TextWrapped("%s", last_push.stderr_capture.c_str());
            }
        }
    }
    ImGui::End();
}

}  // namespace

int main(int argc, char** argv) {
    const std::string repo_path = argc > 1 ? argv[1] : ".";

    glfwSetErrorCallback(glfw_error_callback);
    if (glfwInit() == 0) {
        std::fprintf(stderr, "failed to initialize GLFW\n");
        return 1;
    }

    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    GLFWwindow* window =
        glfwCreateWindow(1280, 800, "network-chaos-harness visualizer", nullptr, nullptr);
    if (window == nullptr) {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    nch::GraphStateManager graph;
    try {
        nch::TopologyParser parser;
        graph.load_topology(parser.parse_file(repo_path + "/topology.json"));
    } catch (const std::exception&) {
        // Starting from an empty canvas is fine when no topology exists yet.
    }

    nch::CanvasRenderer canvas;
    nch::PushResult last_push;

    while (glfwWindowShouldClose(window) == 0) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        draw_side_panel(graph, canvas, repo_path, last_push);

        ImGui::Begin("Canvas");
        canvas.draw(graph);
        ImGui::End();

        ImGui::Render();
        int display_w = 0;
        int display_h = 0;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
