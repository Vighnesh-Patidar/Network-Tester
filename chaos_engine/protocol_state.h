#pragma once

#include <string>

namespace nch {

// OSPF neighbor FSM states, in the order they progress through. Only Full means
// the adjacency is usable for SPF; everything below it is transient or stuck.
enum class OspfNeighborState {
    Down,
    Attempt,
    Init,
    TwoWay,
    ExStart,
    Exchange,
    Loading,
    Full,
    Unknown,
};

// BGP peer FSM states. Only Established carries routes.
enum class BgpPeerState {
    Idle,
    Connect,
    Active,
    OpenSent,
    OpenConfirm,
    Established,
    Unknown,
};

OspfNeighborState ospf_state_from_string(const std::string& value);
BgpPeerState bgp_state_from_string(const std::string& value);

std::string to_string(OspfNeighborState state);
std::string to_string(BgpPeerState state);

bool is_full(OspfNeighborState state);
bool is_established(BgpPeerState state);

}  // namespace nch
