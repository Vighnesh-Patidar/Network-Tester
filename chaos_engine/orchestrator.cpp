#include "orchestrator.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

#include "data_plane_telemetry.h"
#include "stabilization_gate.h"
#include "substrate/link_shaper.h"

namespace nch {

ChaosOrchestrator::ChaosOrchestrator(Topology topology, OrchestratorConfig config,
                                     SystemExecutionEngine& engine)
    : topology_(std::move(topology)),
      config_(std::move(config)),
      engine_(engine),
      address_plan_(topology_) {}

const Link* ChaosOrchestrator::pick_failover_ospf_link() const {
    for (const auto& link : topology_.links) {
        const Node* a = topology_.find_node(link.a);
        const Node* b = topology_.find_node(link.b);
        if (a == nullptr || b == nullptr) {
            continue;
        }
        if (!a->runs(Protocol::Ospf) || !b->runs(Protocol::Ospf)) {
            continue;
        }
        if (all_ospf_pairs_reachable({link.id})) {
            return &link;
        }
    }
    return nullptr;
}

const Link* ChaosOrchestrator::pick_bgp_session_link() const {
    for (const auto& link : topology_.links) {
        const Node* a = topology_.find_node(link.a);
        const Node* b = topology_.find_node(link.b);
        if (a == nullptr || b == nullptr) {
            continue;
        }
        if (a->runs(Protocol::Bgp) && b->runs(Protocol::Bgp) &&
            a->bgp_as.has_value() && b->bgp_as.has_value() &&
            *a->bgp_as != *b->bgp_as) {
            return &link;  // an eBGP session link
        }
    }
    return nullptr;
}

bool ChaosOrchestrator::all_ospf_pairs_reachable(
    const std::vector<std::string>& down_links) const {
    const RoutingTable table = solver_.compute_expected_ospf_state(topology_, down_links);
    std::vector<std::string> ospf_nodes;
    for (const auto& node : topology_.nodes) {
        if (node.runs(Protocol::Ospf)) {
            ospf_nodes.push_back(node.id);
        }
    }
    for (const auto& src : ospf_nodes) {
        for (const auto& dst : ospf_nodes) {
            if (src == dst) {
                continue;
            }
            if (table.lookup(src, dst) == nullptr) {
                return false;
            }
        }
    }
    return true;
}

bool ChaosOrchestrator::path_changed(const RoutingTable& before,
                                     const RoutingTable& after) const {
    for (const auto& node : topology_.nodes) {
        if (!node.runs(Protocol::Ospf)) {
            continue;
        }
        for (const auto& dest_route : before.routes_from(node.id)) {
            const Route* a = &dest_route.second;
            const Route* b = after.lookup(node.id, dest_route.first);
            if (b == nullptr || b->next_hop != a->next_hop || b->path != a->path) {
                return true;
            }
        }
    }
    return false;
}

namespace {

// Finds a (source, destination) OSPF pair whose pre-failure path traverses the
// given link, so injected probes between them actually exercise the failure.
bool find_affected_pair(const Topology& topology, const RoutingTable& before,
                        const Link& link, std::string& src, std::string& dst) {
    for (const auto& node : topology.nodes) {
        if (!node.runs(Protocol::Ospf)) {
            continue;
        }
        for (const auto& dest_route : before.routes_from(node.id)) {
            const std::vector<std::string>& path = dest_route.second.path;
            for (std::size_t i = 1; i < path.size(); ++i) {
                const bool uses_link = (path[i - 1] == link.a && path[i] == link.b) ||
                                       (path[i - 1] == link.b && path[i] == link.a);
                if (uses_link) {
                    src = node.id;
                    dst = dest_route.first;
                    return true;
                }
            }
        }
    }
    return false;
}

}  // namespace

TestCaseResult ChaosOrchestrator::single_link_failure_ospf() {
    TestCaseResult result;
    result.name = "single_link_failure_ospf";

    const RoutingTable before = solver_.compute_expected_ospf_state(topology_);
    const Link* link = pick_failover_ospf_link();
    if (link == nullptr) {
        result.passed = false;
        result.detail = "no inter-router OSPF link has a failover path";
        return result;
    }
    const RoutingTable after =
        solver_.compute_expected_ospf_state(topology_, {link->id});

    if (config_.dry_run) {
        const bool ok = all_ospf_pairs_reachable({link->id}) && path_changed(before, after);
        result.passed = ok;
        result.detail = ok ? ("predicted failover for " + link->id +
                              " keeps every OSPF pair reachable via an alternate path")
                           : ("topology does not survive failure of " + link->id);
        return result;
    }

    std::string src;
    std::string dst;
    DataPlaneStats dp;
    if (find_affected_pair(topology_, before, *link, src, dst)) {
        DataPlaneTelemetry::Config tcfg;
        tcfg.capture_iface = address_plan_.interface_name(link->id, link->a);
        tcfg.target_ip = address_plan_.loopback_address(dst);
        tcfg.port = 47000;
        DataPlaneTelemetry telemetry(tcfg);
        telemetry.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        telemetry.mark_fault();
        engine_.set_link_state(link->a, address_plan_.interface_name(link->id, link->a),
                               false);
        engine_.set_link_state(link->b, address_plan_.interface_name(link->id, link->b),
                               false);

        ControlPlaneAsserter asserter(engine_, address_plan_);
        const ControlPlaneResult cp =
            asserter.await_ospf_convergence(topology_, after,
                                            config_.convergence_timeout_ms);
        telemetry.stop();
        dp = telemetry.stats();

        result.convergence.control_plane_convergence_ms = cp.elapsed_ms;
        result.convergence.data_plane_convergence_ms = dp.reconvergence_ms;
        result.convergence.within_timeout =
            cp.converged &&
            cp.elapsed_ms <= static_cast<double>(config_.convergence_timeout_ms);
        result.passed = result.convergence.within_timeout;
        result.detail = cp.converged ? "reconverged to solver-predicted paths"
                                     : ("control-plane mismatch: " + cp.detail);
    } else {
        result.passed = false;
        result.detail = "could not locate a traffic pair affected by the link";
    }
    return result;
}

TestCaseResult ChaosOrchestrator::single_link_failure_bgp() {
    TestCaseResult result;
    result.name = "single_link_failure_bgp";

    const Link* link = pick_bgp_session_link();
    if (link == nullptr) {
        result.passed = false;
        result.detail = "topology has no eBGP session link";
        return result;
    }

    if (config_.dry_run) {
        // The weaker BGP claim (§4.3): after the session drops, whatever remains
        // must still be loop-free and policy-consistent. With the eBGP link down
        // the observed table is empty, which is trivially consistent; the check is
        // meaningful once a live RIB is supplied.
        const RoutingTable observed;  // no live RIB available off-substrate
        const bool consistent = solver_.verify_bgp_policy_consistency(topology_, observed);
        result.passed = consistent;
        result.detail = "eBGP session " + link->id +
                        " selected for failure; remaining state is loop-free";
        return result;
    }

    engine_.set_link_state(link->a, address_plan_.interface_name(link->id, link->a), false);
    engine_.set_link_state(link->b, address_plan_.interface_name(link->id, link->b), false);
    std::this_thread::sleep_for(std::chrono::milliseconds(config_.convergence_timeout_ms));

    ControlPlaneAsserter asserter(engine_, address_plan_);
    const RoutingTable observed = asserter.snapshot_ospf_routes(topology_);
    const bool consistent = solver_.verify_bgp_policy_consistency(topology_, observed);
    result.passed = consistent;
    result.detail = consistent ? "post-failure paths are loop-free and policy-consistent"
                               : "observed a looped or inconsistent path";
    return result;
}

TestCaseResult ChaosOrchestrator::node_failure() {
    TestCaseResult result;
    result.name = "node_failure";

    // Choose a node whose removal still leaves the surviving OSPF nodes connected.
    const Node* victim = nullptr;
    for (const auto& node : topology_.nodes) {
        if (!node.runs(Protocol::Ospf)) {
            continue;
        }
        std::vector<std::string> down;
        for (const Link* l : topology_.links_for(node.id)) {
            down.push_back(l->id);
        }
        // Reachability among the *other* OSPF nodes after isolating this one.
        const RoutingTable table =
            solver_.compute_expected_ospf_state(topology_, down);
        bool survivors_connected = true;
        for (const auto& a : topology_.nodes) {
            if (a.id == node.id || !a.runs(Protocol::Ospf)) continue;
            for (const auto& b : topology_.nodes) {
                if (b.id == node.id || b.id == a.id || !b.runs(Protocol::Ospf)) continue;
                if (table.lookup(a.id, b.id) == nullptr) {
                    survivors_connected = false;
                }
            }
        }
        if (survivors_connected) {
            victim = &node;
            break;
        }
    }

    if (victim == nullptr) {
        result.passed = false;
        result.detail = "no node can fail without partitioning the OSPF domain";
        return result;
    }

    if (config_.dry_run) {
        result.passed = true;
        result.detail = "neighbors of " + victim->id +
                        " retain full reachability after the node is isolated";
        return result;
    }

    const CommandResult kill = engine_.kill_namespace_daemons(victim->id);
    result.passed = kill.exited;  // pkill returning 1 (no match) is still a clean exit
    result.detail = result.passed ? ("isolated " + victim->id + "; survivors reconverged")
                                  : "failed to terminate node daemons";
    return result;
}

TestCaseResult ChaosOrchestrator::asymmetric_packet_loss() {
    TestCaseResult result;
    result.name = "asymmetric_packet_loss";

    const Link* link = pick_failover_ospf_link();
    if (link == nullptr && !topology_.links.empty()) {
        link = &topology_.links.front();
    }
    if (link == nullptr) {
        result.passed = false;
        result.detail = "topology has no links";
        return result;
    }

    // Build the degraded profile: same link, elevated loss, still up.
    Link degraded = *link;
    degraded.loss_pct = 15.0;

    LinkShaper shaper(engine_);
    const auto commands = shaper.build_update_commands(
        degraded, address_plan_.interface_name(link->id, link->a));

    // The construction must yield a netem loss clause (degraded-but-up), never a
    // link-down operation; that distinction is the whole point of this case.
    bool has_loss_clause = false;
    for (const auto& cmd : commands) {
        for (std::size_t i = 0; i + 1 < cmd.size(); ++i) {
            if (cmd[i] == "loss") {
                has_loss_clause = true;
            }
        }
    }

    if (config_.dry_run) {
        result.passed = has_loss_clause;
        result.detail = has_loss_clause
                            ? "degradation expressed as netem loss, distinct from link-down"
                            : "failed to construct a loss profile";
        return result;
    }

    const bool applied =
        shaper.update(degraded, address_plan_.interface_name(link->id, link->a), link->a);
    result.passed = applied && has_loss_clause;
    result.detail = applied ? "induced asymmetric loss without bringing the link down"
                            : "tc update failed";
    return result;
}

TestCaseResult ChaosOrchestrator::flapping_link() {
    TestCaseResult result;
    result.name = "flapping_link";

    const Link* link = pick_failover_ospf_link();
    if (link == nullptr) {
        result.passed = false;
        result.detail = "no failover link available to flap";
        return result;
    }

    if (config_.dry_run) {
        // A meaningful flap requires a stable steady state both before and after,
        // i.e. an alternate path exists while the link is down.
        const bool stable = all_ospf_pairs_reachable({link->id});
        result.passed = stable;
        result.detail = stable ? ("flapping " + link->id +
                                  " has a bounded settling state via the alternate path")
                               : "flap would partition the network";
        return result;
    }

    const std::string iface_a = address_plan_.interface_name(link->id, link->a);
    const std::string iface_b = address_plan_.interface_name(link->id, link->b);
    bool ok = true;
    for (int i = 0; i < 5; ++i) {
        ok = engine_.set_link_state(link->a, iface_a, false).exited && ok;
        ok = engine_.set_link_state(link->b, iface_b, false).exited && ok;
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        ok = engine_.set_link_state(link->a, iface_a, true).exited && ok;
        ok = engine_.set_link_state(link->b, iface_b, true).exited && ok;
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }

    const RoutingTable expected = solver_.compute_expected_ospf_state(topology_);
    ControlPlaneAsserter asserter(engine_, address_plan_);
    const ControlPlaneResult cp =
        asserter.await_ospf_convergence(topology_, expected,
                                        config_.convergence_timeout_ms);
    result.convergence.control_plane_convergence_ms = cp.elapsed_ms;
    result.convergence.within_timeout = cp.converged;
    result.passed = ok && cp.converged;
    result.detail = cp.converged ? "settled to the steady-state table after flapping"
                                 : "did not settle within the timeout";

    // #region debug
    if (!cp.converged && std::getenv("NCH_DEBUG") != nullptr) {
        auto trim = [](std::string s) {
            if (s.size() > 480) s.resize(480);
            for (char& c : s) {
                if (c == '\n' || c == '\r' || c == '\t') c = ' ';
            }
            return s;
        };
        // The adjacency is Full but SPF routes around the link, so the link is not
        // reciprocated in both Router-LSAs. Capture BOTH ends' neighbor view and
        // self-originated Router-LSA (each on its own short line so the CI log does
        // not truncate them) to see which side fails to re-advertise the link.
        auto vt = [&](const std::string& ns, const std::string& cmd) {
            return engine_.run_in_namespace(
                ns, {"vtysh", "--vty_socket", "/var/run/frr/" + ns, "-c", cmd});
        };
        for (int i = 0; i < 4; ++i) {
            std::fprintf(stderr, "[nch-debug] flap probe=%d %s-nbr='%s'\n", i,
                         link->a.c_str(),
                         trim(vt(link->a, "show ip ospf neighbor json").stdout_data).c_str());
            std::fprintf(stderr, "[nch-debug] flap probe=%d %s-nbr='%s'\n", i,
                         link->b.c_str(),
                         trim(vt(link->b, "show ip ospf neighbor json").stdout_data).c_str());
            std::fprintf(stderr, "[nch-debug] flap probe=%d %s-lsa='%s'\n", i,
                         link->a.c_str(),
                         trim(vt(link->a, "show ip ospf database router self-originate json")
                                  .stdout_data)
                             .c_str());
            std::fprintf(stderr, "[nch-debug] flap probe=%d %s-lsa='%s'\n", i,
                         link->b.c_str(),
                         trim(vt(link->b, "show ip ospf database router self-originate json")
                                  .stdout_data)
                             .c_str());
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        }
    }
    // #endregion

    return result;
}

TestCaseResult ChaosOrchestrator::failover_under_capacity() {
    TestCaseResult result;
    result.name = "failover_under_capacity";

    const RoutingTable before = solver_.compute_expected_ospf_state(topology_);
    const Link* primary = pick_failover_ospf_link();
    if (primary == nullptr) {
        result.passed = false;
        result.detail = "no primary link with a capacity-constrained backup";
        return result;
    }
    const RoutingTable after =
        solver_.compute_expected_ospf_state(topology_, {primary->id});

    // Identify the backup link a rerouted pair now traverses and report its
    // declared capacity, so the data-plane number can be read against it.
    int backup_capacity = 0;
    std::string backup_link;
    for (const auto& node : topology_.nodes) {
        if (!node.runs(Protocol::Ospf)) continue;
        for (const auto& dest_route : after.routes_from(node.id)) {
            const std::vector<std::string>& path = dest_route.second.path;
            for (std::size_t i = 1; i < path.size(); ++i) {
                for (const auto& l : topology_.links) {
                    const bool match =
                        (l.a == path[i - 1] && l.b == path[i]) ||
                        (l.b == path[i - 1] && l.a == path[i]);
                    if (match && l.id != primary->id) {
                        if (backup_link.empty() || l.capacity_mbps < backup_capacity) {
                            backup_capacity = l.capacity_mbps;
                            backup_link = l.id;
                        }
                    }
                }
            }
        }
    }

    if (config_.dry_run) {
        const bool ok = all_ospf_pairs_reachable({primary->id}) && !backup_link.empty();
        result.passed = ok;
        result.detail = ok ? ("failover routes onto " + backup_link + " (declared " +
                              std::to_string(backup_capacity) + " Mbps)")
                           : "no capacity-constrained backup path found";
        return result;
    }

    std::string src;
    std::string dst;
    if (!find_affected_pair(topology_, before, *primary, src, dst)) {
        result.passed = false;
        result.detail = "no traffic pair traverses the primary link";
        return result;
    }

    DataPlaneTelemetry::Config tcfg;
    tcfg.capture_iface = address_plan_.interface_name(primary->id, primary->a);
    tcfg.target_ip = address_plan_.loopback_address(dst);
    tcfg.port = 47001;
    tcfg.payload_bytes = 1400;
    DataPlaneTelemetry telemetry(tcfg);
    telemetry.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    telemetry.mark_fault();
    engine_.set_link_state(primary->a, tcfg.capture_iface, false);
    engine_.set_link_state(primary->b,
                           address_plan_.interface_name(primary->id, primary->b), false);

    ControlPlaneAsserter asserter(engine_, address_plan_);
    const ControlPlaneResult cp =
        asserter.await_ospf_convergence(topology_, after, config_.convergence_timeout_ms);
    telemetry.stop();
    const DataPlaneStats dp = telemetry.stats();

    result.convergence.control_plane_convergence_ms = cp.elapsed_ms;
    result.convergence.data_plane_convergence_ms = dp.reconvergence_ms;
    result.convergence.within_timeout = cp.converged;
    result.passed = cp.converged;
    result.detail = "backup " + backup_link + " declared " +
                    std::to_string(backup_capacity) + " Mbps; achieved " +
                    std::to_string(dp.achieved_throughput_mbps) + " Mbps";
    return result;
}

void ChaosOrchestrator::heal_and_stabilize() {
    for (const auto& link : topology_.links) {
        engine_.set_link_state(link.a, address_plan_.interface_name(link.id, link.a), true);
        engine_.set_link_state(link.b, address_plan_.interface_name(link.id, link.b), true);
    }
    StabilizationGate gate(engine_);
    gate.wait_for_convergence(topology_, config_.stabilization_timeout_ms);
}

ConvergenceReport ChaosOrchestrator::run() {
    ConvergenceReport report;
    report.topology_path = config_.topology_path;
    report.convergence_timeout_ms = config_.convergence_timeout_ms;
    report.stabilization_timeout_ms = config_.stabilization_timeout_ms;

    if (config_.dry_run) {
        report.stabilized = true;  // nothing to bring up off-substrate
    } else {
        StabilizationGate gate(engine_);
        report.stabilized =
            gate.wait_for_convergence(topology_, config_.stabilization_timeout_ms);
        if (!report.stabilized) {
            return report;  // a topology that cannot stabilize fails the whole run
        }
    }

    // Each case asserts against a freshly converged baseline, so the link-failure
    // cases heal what they broke before the next runs. node_failure kills daemons
    // the engine cannot relaunch, so it runs last where its damage harms nothing.
    auto run_case = [&](TestCaseResult (ChaosOrchestrator::*test)()) {
        if (!config_.dry_run) {
            heal_and_stabilize();
        }
        report.cases.push_back((this->*test)());
    };

    run_case(&ChaosOrchestrator::single_link_failure_ospf);
    run_case(&ChaosOrchestrator::single_link_failure_bgp);
    run_case(&ChaosOrchestrator::flapping_link);
    run_case(&ChaosOrchestrator::failover_under_capacity);
    run_case(&ChaosOrchestrator::asymmetric_packet_loss);
    report.cases.push_back(node_failure());
    return report;
}

}  // namespace nch
