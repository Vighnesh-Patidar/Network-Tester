#include <gtest/gtest.h>

#include "chaos_engine/topology_parser.h"

namespace {

const char* kSample = R"({
  "nodes": [
    { "id": "r1", "protocols": ["ospf"], "ospf_area": "0.0.0.0" },
    { "id": "r2", "protocols": ["ospf", "bgp"], "bgp_as": 65001,
      "bgp_peers": [{ "peer": "r3", "remote_as": 65002 }] },
    { "id": "r3", "protocols": ["bgp"], "bgp_as": 65002,
      "bgp_peers": [{ "peer": "r2", "remote_as": 65001 }] }
  ],
  "links": [
    { "id": "l1", "a": "r1", "b": "r2", "metric": 10, "capacity_mbps": 100,
      "latency_ms": 5.0, "jitter_ms": 1.0, "loss_pct": 0.0 },
    { "id": "l2", "a": "r2", "b": "r3", "metric": 20, "capacity_mbps": 50,
      "latency_ms": 8.0, "jitter_ms": 0.5, "loss_pct": 0.1 }
  ]
})";

}  // namespace

TEST(TopologyParser, ParsesNodesAndLinks) {
    nch::TopologyParser parser;
    const nch::Topology topology = parser.parse_string(kSample);

    ASSERT_EQ(topology.nodes.size(), 3u);
    ASSERT_EQ(topology.links.size(), 2u);

    const nch::Node* r2 = topology.find_node("r2");
    ASSERT_TRUE(r2 != nullptr);
    EXPECT_TRUE(r2->runs(nch::Protocol::Ospf));
    EXPECT_TRUE(r2->runs(nch::Protocol::Bgp));
    ASSERT_TRUE(r2->bgp_as.has_value());
    EXPECT_EQ(*r2->bgp_as, 65001u);
    ASSERT_EQ(r2->bgp_peers.size(), 1u);
    EXPECT_EQ(r2->bgp_peers.front().peer, std::string("r3"));
}

TEST(TopologyParser, ParsesLinkCapabilityFields) {
    nch::TopologyParser parser;
    const nch::Topology topology = parser.parse_string(kSample);
    const nch::Link* l2 = topology.find_link("l2");
    ASSERT_TRUE(l2 != nullptr);
    EXPECT_EQ(l2->metric, 20);
    EXPECT_EQ(l2->capacity_mbps, 50);
    EXPECT_NEAR(l2->latency_ms, 8.0, 1e-9);
    EXPECT_NEAR(l2->jitter_ms, 0.5, 1e-9);
    EXPECT_NEAR(l2->loss_pct, 0.1, 1e-9);
}

TEST(TopologyParser, RejectsDuplicateNodeId) {
    nch::TopologyParser parser;
    const char* doc = R"({"nodes":[{"id":"r1"},{"id":"r1"}],"links":[]})";
    EXPECT_THROW(parser.parse_string(doc), std::exception);
}

TEST(TopologyParser, RejectsLinkToUnknownNode) {
    nch::TopologyParser parser;
    const char* doc =
        R"({"nodes":[{"id":"r1"}],"links":[{"id":"l1","a":"r1","b":"ghost","metric":10}]})";
    EXPECT_THROW(parser.parse_string(doc), std::exception);
}

TEST(TopologyParser, RejectsBgpNodeWithoutAs) {
    nch::TopologyParser parser;
    const char* doc = R"({"nodes":[{"id":"r1","protocols":["bgp"]}],"links":[]})";
    EXPECT_THROW(parser.parse_string(doc), std::exception);
}

TEST(TopologyParser, RoundTripsThroughSerialization) {
    nch::TopologyParser parser;
    const nch::Topology topology = parser.parse_string(kSample);
    const std::string serialized = nch::topology_to_json(topology).dump();
    const nch::Topology again = parser.parse_string(serialized);
    EXPECT_EQ(again.nodes.size(), topology.nodes.size());
    EXPECT_EQ(again.links.size(), topology.links.size());
    EXPECT_EQ(again.find_link("l2")->capacity_mbps, 50);
}
