#include "protocol_state.h"

#include <algorithm>

namespace nch {

namespace {

std::string strip_options(const std::string& value) {
    // vtysh reports states like "Full/DR" or "Full/Backup"; only the FSM token
    // before the slash is meaningful for adjacency checks.
    const auto slash = value.find('/');
    return slash == std::string::npos ? value : value.substr(0, slash);
}

}  // namespace

OspfNeighborState ospf_state_from_string(const std::string& value) {
    const std::string token = strip_options(value);
    if (token == "Down") return OspfNeighborState::Down;
    if (token == "Attempt") return OspfNeighborState::Attempt;
    if (token == "Init") return OspfNeighborState::Init;
    if (token == "2-Way" || token == "Two-Way" || token == "TwoWay")
        return OspfNeighborState::TwoWay;
    if (token == "ExStart") return OspfNeighborState::ExStart;
    if (token == "Exchange") return OspfNeighborState::Exchange;
    if (token == "Loading") return OspfNeighborState::Loading;
    if (token == "Full") return OspfNeighborState::Full;
    return OspfNeighborState::Unknown;
}

BgpPeerState bgp_state_from_string(const std::string& value) {
    if (value == "Idle") return BgpPeerState::Idle;
    if (value == "Connect") return BgpPeerState::Connect;
    if (value == "Active") return BgpPeerState::Active;
    if (value == "OpenSent") return BgpPeerState::OpenSent;
    if (value == "OpenConfirm") return BgpPeerState::OpenConfirm;
    if (value == "Established") return BgpPeerState::Established;
    return BgpPeerState::Unknown;
}

std::string to_string(OspfNeighborState state) {
    switch (state) {
        case OspfNeighborState::Down: return "Down";
        case OspfNeighborState::Attempt: return "Attempt";
        case OspfNeighborState::Init: return "Init";
        case OspfNeighborState::TwoWay: return "2-Way";
        case OspfNeighborState::ExStart: return "ExStart";
        case OspfNeighborState::Exchange: return "Exchange";
        case OspfNeighborState::Loading: return "Loading";
        case OspfNeighborState::Full: return "Full";
        case OspfNeighborState::Unknown: return "Unknown";
    }
    return "Unknown";
}

std::string to_string(BgpPeerState state) {
    switch (state) {
        case BgpPeerState::Idle: return "Idle";
        case BgpPeerState::Connect: return "Connect";
        case BgpPeerState::Active: return "Active";
        case BgpPeerState::OpenSent: return "OpenSent";
        case BgpPeerState::OpenConfirm: return "OpenConfirm";
        case BgpPeerState::Established: return "Established";
        case BgpPeerState::Unknown: return "Unknown";
    }
    return "Unknown";
}

bool is_full(OspfNeighborState state) { return state == OspfNeighborState::Full; }

bool is_established(BgpPeerState state) { return state == BgpPeerState::Established; }

}  // namespace nch
