#include <gtest/gtest.h>

#include "chaos_engine/reference_topology_solver.h"
#include "chaos_engine/topology_parser.h"

namespace {

// A square with one diagonal: r1-r2-r3-r4-r1 plus r2-r4.
//   r1-r2 = 10, r2-r3 = 10, r3-r4 = 10, r1-r4 = 30, r2-r4 = 20
nch::Topology make_grid() {
    const char* doc = R"({
      "nodes": [
        {"id":"r1","protocols":["ospf"]},
        {"id":"r2","protocols":["ospf"]},
        {"id":"r3","protocols":["ospf"]},
        {"id":"r4","protocols":["ospf"]}
      ],
      "links": [
        {"id":"l12","a":"r1","b":"r2","metric":10},
        {"id":"l23","a":"r2","b":"r3","metric":10},
        {"id":"l34","a":"r3","b":"r4","metric":10},
        {"id":"l14","a":"r1","b":"r4","metric":30},
        {"id":"l24","a":"r2","b":"r4","metric":20}
      ]
    })";
    nch::TopologyParser parser;
    return parser.parse_string(doc);
}

}  // namespace

TEST(ReferenceSolver, ComputesShortestPathNextHop) {
    const nch::Topology topology = make_grid();
    nch::ReferenceTopologySolver solver;
    const nch::RoutingTable table = solver.compute_expected_ospf_state(topology);

    // r1 -> r4: direct l14 costs 30; via r2 (10) then r2-r4 (20) also 30; via
    // r2,r3,r4 costs 30. The tie is broken deterministically, but every option
    // costs 30, so assert the cost rather than a particular next hop.
    const nch::Route* r1_to_r4 = table.lookup("r1", "r4");
    ASSERT_TRUE(r1_to_r4 != nullptr);
    EXPECT_EQ(r1_to_r4->cost, 30);

    // r1 -> r3: r1-r2-r3 = 20, strictly better than r1-r4-r3 = 40.
    const nch::Route* r1_to_r3 = table.lookup("r1", "r3");
    ASSERT_TRUE(r1_to_r3 != nullptr);
    EXPECT_EQ(r1_to_r3->cost, 20);
    EXPECT_EQ(r1_to_r3->next_hop, std::string("r2"));
}

TEST(ReferenceSolver, ReroutesAroundFailedLink) {
    const nch::Topology topology = make_grid();
    nch::ReferenceTopologySolver solver;

    const nch::Route* before = solver.compute_expected_ospf_state(topology).lookup("r1", "r3");
    ASSERT_TRUE(before != nullptr);
    EXPECT_EQ(before->next_hop, std::string("r2"));

    // Removing r2-r3 forces r1 -> r3 onto r1-r2-r4-r3 (40) or r1-r4-r3 (40).
    const nch::RoutingTable after =
        solver.compute_expected_ospf_state(topology, {"l23"});
    const nch::Route* rerouted = after.lookup("r1", "r3");
    ASSERT_TRUE(rerouted != nullptr);
    EXPECT_EQ(rerouted->cost, 40);
}

TEST(ReferenceSolver, MarksUnreachableAfterPartition) {
    const nch::Topology topology = make_grid();
    nch::ReferenceTopologySolver solver;
    // Isolate r4 by removing all three of its links.
    const nch::RoutingTable table =
        solver.compute_expected_ospf_state(topology, {"l34", "l14", "l24"});
    EXPECT_TRUE(table.lookup("r1", "r4") == nullptr);
    // The rest of the mesh stays reachable.
    EXPECT_TRUE(table.lookup("r1", "r3") != nullptr);
}

TEST(ReferenceSolver, BgpRejectsLoopedPath) {
    const char* doc = R"({
      "nodes": [
        {"id":"a","protocols":["bgp"],"bgp_as":1},
        {"id":"b","protocols":["bgp"],"bgp_as":2}
      ],
      "links": [{"id":"l","a":"a","b":"b","metric":10}]
    })";
    nch::TopologyParser parser;
    const nch::Topology topology = parser.parse_string(doc);

    nch::ReferenceTopologySolver solver;

    nch::RoutingTable looped;
    nch::Route bad;
    bad.destination = "b";
    bad.next_hop = "b";
    bad.path = {"a", "b", "a"};  // a repeats: a loop
    looped.set("a", bad);
    EXPECT_FALSE(solver.verify_bgp_policy_consistency(topology, looped));

    nch::RoutingTable clean;
    nch::Route good;
    good.destination = "b";
    good.next_hop = "b";
    good.path = {"a", "b"};
    clean.set("a", good);
    EXPECT_TRUE(solver.verify_bgp_policy_consistency(topology, clean));
}
