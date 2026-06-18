#include <gtest/gtest.h>

#include "chaos_engine/protocol_state.h"

TEST(ProtocolState, ParsesOspfStatesWithOptions) {
    EXPECT_TRUE(nch::is_full(nch::ospf_state_from_string("Full/DR")));
    EXPECT_TRUE(nch::is_full(nch::ospf_state_from_string("Full/Backup")));
    EXPECT_TRUE(nch::is_full(nch::ospf_state_from_string("Full")));
    EXPECT_FALSE(nch::is_full(nch::ospf_state_from_string("2-Way/DROther")));
    EXPECT_FALSE(nch::is_full(nch::ospf_state_from_string("Init")));
    EXPECT_EQ(nch::ospf_state_from_string("ExStart"), nch::OspfNeighborState::ExStart);
}

TEST(ProtocolState, ParsesBgpStates) {
    EXPECT_TRUE(nch::is_established(nch::bgp_state_from_string("Established")));
    EXPECT_FALSE(nch::is_established(nch::bgp_state_from_string("Active")));
    EXPECT_FALSE(nch::is_established(nch::bgp_state_from_string("Idle")));
    EXPECT_EQ(nch::bgp_state_from_string("OpenConfirm"), nch::BgpPeerState::OpenConfirm);
}

TEST(ProtocolState, UnknownStatesAreNotConverged) {
    EXPECT_EQ(nch::ospf_state_from_string("garbage"), nch::OspfNeighborState::Unknown);
    EXPECT_EQ(nch::bgp_state_from_string("garbage"), nch::BgpPeerState::Unknown);
    EXPECT_FALSE(nch::is_full(nch::OspfNeighborState::Unknown));
    EXPECT_FALSE(nch::is_established(nch::BgpPeerState::Unknown));
}

TEST(ProtocolState, RoundTripsThroughString) {
    EXPECT_EQ(nch::to_string(nch::OspfNeighborState::Full), std::string("Full"));
    EXPECT_EQ(nch::to_string(nch::BgpPeerState::Established), std::string("Established"));
}
