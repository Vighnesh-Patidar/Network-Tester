#include <gtest/gtest.h>

#include "chaos_engine/topology_parser.h"
#include "common/address_plan.h"

namespace {

nch::Topology make_topology() {
    const char* doc = R"({
      "nodes": [
        {"id":"r1","protocols":["ospf"]},
        {"id":"r2","protocols":["ospf"]},
        {"id":"r3","protocols":["ospf"]}
      ],
      "links": [
        {"id":"l12","a":"r1","b":"r2","metric":10},
        {"id":"l23","a":"r2","b":"r3","metric":10}
      ]
    })";
    nch::TopologyParser parser;
    return parser.parse_string(doc);
}

}  // namespace

TEST(AddressPlan, AssignsLoopbacksByNodeOrder) {
    const nch::Topology topology = make_topology();
    const nch::AddressPlan plan(topology);
    EXPECT_EQ(plan.loopback_address("r1"), std::string("10.255.0.1"));
    EXPECT_EQ(plan.loopback_address("r3"), std::string("10.255.0.3"));
    EXPECT_EQ(plan.loopback_prefix("r2"), std::string("10.255.0.2/32"));
}

TEST(AddressPlan, AssignsLinkEndpointAddresses) {
    const nch::Topology topology = make_topology();
    const nch::AddressPlan plan(topology);
    EXPECT_EQ(plan.interface_address("l12", "r1"), std::string("10.0.1.1"));
    EXPECT_EQ(plan.interface_address("l12", "r2"), std::string("10.0.1.2"));
    EXPECT_EQ(plan.interface_address("l23", "r2"), std::string("10.0.2.1"));
}

TEST(AddressPlan, NamesInterfacesSequentiallyPerNode) {
    const nch::Topology topology = make_topology();
    const nch::AddressPlan plan(topology);
    EXPECT_EQ(plan.interface_name("l12", "r1"), std::string("r1-eth0"));
    EXPECT_EQ(plan.interface_name("l12", "r2"), std::string("r2-eth0"));
    EXPECT_EQ(plan.interface_name("l23", "r2"), std::string("r2-eth1"));
}

TEST(AddressPlan, ResolvesAddressBackToOwningNode) {
    const nch::Topology topology = make_topology();
    const nch::AddressPlan plan(topology);
    EXPECT_EQ(plan.node_for_address("10.0.1.2"), std::string("r2"));
    EXPECT_EQ(plan.node_for_address("10.255.0.3"), std::string("r3"));
    EXPECT_TRUE(plan.node_for_address("203.0.113.9").empty());
}
