#pragma once

#include <string>
#include <vector>

#include "chaos_engine/system_execution_engine.h"
#include "common/topology.h"

namespace nch {

// Enforces a link's declared capability profile (§1.1) on its veth interface via
// Linux traffic control (§5.6). The qdisc chain is composed so the two shapers
// do not fight over the interface: tbf is the outer/root qdisc (capacity) and
// netem is nested beneath it (latency/jitter/loss).
class LinkShaper {
public:
    explicit LinkShaper(const SystemExecutionEngine& engine);

    // Installs the qdisc chain. Called once per link during Tier 3 bring-up,
    // before the Stabilization Gate, so daemons converge against the real link
    // characteristics. Returns false if any tc invocation fails.
    bool apply(const Link& link, const std::string& interface_name,
               const std::string& netns = "") const;

    // Changes the profile in place without tearing the interface down. Used by the
    // flapping-link and packet-loss cases (§6) to mutate a link mid-test.
    bool update(const Link& link, const std::string& interface_name,
                const std::string& netns = "") const;

    // tc argument vectors for apply()/update(), exposed so the construction logic
    // is unit-testable without touching the kernel.
    std::vector<std::vector<std::string>> build_apply_commands(
        const Link& link, const std::string& interface_name) const;
    std::vector<std::vector<std::string>> build_update_commands(
        const Link& link, const std::string& interface_name) const;

private:
    const SystemExecutionEngine& engine_;

    std::vector<std::string> netem_options(const Link& link) const;
    bool run_chain(const std::vector<std::vector<std::string>>& commands,
                   const std::string& netns) const;
};

}  // namespace nch
