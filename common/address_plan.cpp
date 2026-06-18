#include "address_plan.h"

#include <map>
#include <stdexcept>

namespace nch {

AddressPlan::AddressPlan(const Topology& topology) {
    for (std::size_t i = 0; i < topology.nodes.size(); ++i) {
        const std::string ip = "10.255.0." + std::to_string(i + 1);
        loopback_[topology.nodes[i].id] = ip;
        ip_owner_[ip] = topology.nodes[i].id;
    }

    std::map<std::string, int> per_node_seq;
    for (std::size_t k = 0; k < topology.links.size(); ++k) {
        const Link& link = topology.links[k];
        const std::string subnet = "10.0." + std::to_string(k + 1) + ".";
        const std::string ip_a = subnet + "1";
        const std::string ip_b = subnet + "2";

        iface_ip_[{link.id, link.a}] = ip_a;
        iface_ip_[{link.id, link.b}] = ip_b;
        ip_owner_[ip_a] = link.a;
        ip_owner_[ip_b] = link.b;

        const int seq_a = per_node_seq[link.a]++;
        const int seq_b = per_node_seq[link.b]++;
        iface_name_[{link.id, link.a}] = link.a + "-eth" + std::to_string(seq_a);
        iface_name_[{link.id, link.b}] = link.b + "-eth" + std::to_string(seq_b);
    }
}

std::string AddressPlan::loopback_address(const std::string& node_id) const {
    auto it = loopback_.find(node_id);
    if (it == loopback_.end()) {
        throw std::runtime_error("no loopback for node '" + node_id + "'");
    }
    return it->second;
}

std::string AddressPlan::loopback_prefix(const std::string& node_id) const {
    return loopback_address(node_id) + "/32";
}

std::string AddressPlan::interface_address(const std::string& link_id,
                                           const std::string& node_id) const {
    auto it = iface_ip_.find({link_id, node_id});
    if (it == iface_ip_.end()) {
        throw std::runtime_error("node '" + node_id + "' is not on link '" + link_id +
                                 "'");
    }
    return it->second;
}

std::string AddressPlan::interface_name(const std::string& link_id,
                                        const std::string& node_id) const {
    auto it = iface_name_.find({link_id, node_id});
    if (it == iface_name_.end()) {
        throw std::runtime_error("node '" + node_id + "' is not on link '" + link_id +
                                 "'");
    }
    return it->second;
}

std::string AddressPlan::node_for_address(const std::string& ip) const {
    auto it = ip_owner_.find(ip);
    return it == ip_owner_.end() ? std::string() : it->second;
}

}  // namespace nch
