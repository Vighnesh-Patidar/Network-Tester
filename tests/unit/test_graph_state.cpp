#include <gtest/gtest.h>

#include "visualizer/graph_state.h"

TEST(GraphState, AddsNodesAndLinks) {
    nch::GraphStateManager graph;
    const nch::Uuid a = graph.add_node("r1", {0.0f, 0.0f});
    const nch::Uuid b = graph.add_node("r2", {10.0f, 0.0f});
    const nch::Uuid link = graph.add_link(a, b, "l1");

    EXPECT_EQ(graph.nodes().size(), 2u);
    EXPECT_EQ(graph.links().size(), 1u);
    ASSERT_TRUE(graph.link(link) != nullptr);
    EXPECT_EQ(graph.link(link)->model.a, std::string("r1"));
    EXPECT_EQ(graph.link(link)->model.b, std::string("r2"));
}

TEST(GraphState, RemovingNodeDropsIncidentLinks) {
    nch::GraphStateManager graph;
    const nch::Uuid a = graph.add_node("r1", {0.0f, 0.0f});
    const nch::Uuid b = graph.add_node("r2", {1.0f, 0.0f});
    const nch::Uuid c = graph.add_node("r3", {2.0f, 0.0f});
    graph.add_link(a, b, "l1");
    graph.add_link(b, c, "l2");

    ASSERT_EQ(graph.links().size(), 2u);
    EXPECT_TRUE(graph.remove_node(b));
    // Both links touched r2, so both must be gone.
    EXPECT_EQ(graph.links().size(), 0u);
    EXPECT_EQ(graph.nodes().size(), 2u);
}

TEST(GraphState, LookupStaysValidAfterSwapErase) {
    nch::GraphStateManager graph;
    const nch::Uuid a = graph.add_node("r1", {0.0f, 0.0f});
    const nch::Uuid b = graph.add_node("r2", {1.0f, 0.0f});
    const nch::Uuid c = graph.add_node("r3", {2.0f, 0.0f});

    // Removing the middle node triggers a swap-erase; surviving UUIDs must still
    // resolve to the correct elements.
    EXPECT_TRUE(graph.remove_node(b));
    ASSERT_TRUE(graph.node(a) != nullptr);
    ASSERT_TRUE(graph.node(c) != nullptr);
    EXPECT_EQ(graph.node(a)->model.id, std::string("r1"));
    EXPECT_EQ(graph.node(c)->model.id, std::string("r3"));
    EXPECT_TRUE(graph.node(b) == nullptr);
}

TEST(GraphState, ConvertsToTopologyAndBack) {
    nch::GraphStateManager graph;
    const nch::Uuid a = graph.add_node("r1", {0.0f, 0.0f});
    const nch::Uuid b = graph.add_node("r2", {1.0f, 0.0f});
    graph.add_link(a, b, "l1");

    const nch::Topology topology = graph.to_topology();
    EXPECT_EQ(topology.nodes.size(), 2u);
    EXPECT_EQ(topology.links.size(), 1u);

    nch::GraphStateManager reloaded;
    reloaded.load_topology(topology);
    EXPECT_EQ(reloaded.nodes().size(), 2u);
    EXPECT_EQ(reloaded.links().size(), 1u);
}
