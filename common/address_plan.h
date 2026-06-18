#pragma once

#include <map>
#include <string>
#include <utility>

#include "topology.h"

namespace nch {

// Deterministic IP/interface assignment derived purely from topology.json.
//
// The same scheme is reproduced by substrate/mininet_topology.py; keeping it a
// pure function of link/node ordering is what lets the Control-Plane Asserter map
// an observed next-hop IP back to a node id without any out-of-band state.
//
// Conventions:
//   loopback(node)            -> 10.255.0.<node_index+1>/32  (advertised prefix)
//   link subnet (link_index k)-> 10.0.<k+1>.0/30
//     endpoint a              -> 10.0.<k+1>.1
//     endpoint b              -> 10.0.<k+1>.2
//   interface name            -> <node_id>-eth<per_node_sequence>
class AddressPlan {
public:
    explicit AddressPlan(const Topology& topology);

    std::string loopback_address(const std::string& node_id) const;   // dotted, no mask
    std::string loopback_prefix(const std::string& node_id) const;    // a.b.c.d/32

    std::string interface_address(const std::string& link_id,
                                  const std::string& node_id) const;
    std::string interface_name(const std::string& link_id,
                               const std::string& node_id) const;

    // Reverse lookup used by the asserter: which node owns this interface IP.
    std::string node_for_address(const std::string& ip) const;

private:
    std::map<std::string, std::string> loopback_;                        // node -> ip
    std::map<std::pair<std::string, std::string>, std::string> iface_ip_;   // (link,node)->ip
    std::map<std::pair<std::string, std::string>, std::string> iface_name_;  // (link,node)->name
    std::map<std::string, std::string> ip_owner_;                        // ip -> node
};

}  // namespace nch
