#include "link_shaper.h"

#include <cmath>
#include <sstream>

namespace nch {

namespace {

// tc accepts fractional milliseconds; print the value compactly without a
// trailing ".000" so the generated commands read the way an operator would type
// them.
std::string format_ms(double value) {
    std::ostringstream os;
    os.precision(3);
    os << std::fixed << value;
    std::string s = os.str();
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') {
            s.pop_back();
        }
        if (!s.empty() && s.back() == '.') {
            s.pop_back();
        }
    }
    return s + "ms";
}

std::string format_pct(double value) {
    std::ostringstream os;
    os.precision(3);
    os << std::fixed << value;
    std::string s = os.str();
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') {
            s.pop_back();
        }
        if (!s.empty() && s.back() == '.') {
            s.pop_back();
        }
    }
    return s + "%";
}

// tbf burst must cover at least one timer tick worth of traffic at the configured
// rate, with a sane floor for low-rate links.
std::string burst_bytes(int capacity_mbps) {
    const double rate_bytes_per_sec = static_cast<double>(capacity_mbps) * 1e6 / 8.0;
    double burst = rate_bytes_per_sec / 250.0;  // ~one 250 Hz tick
    if (burst < 1540.0) {
        burst = 1540.0;  // floor at roughly one MTU
    }
    return std::to_string(static_cast<long long>(std::ceil(burst)));
}

}  // namespace

LinkShaper::LinkShaper(const SystemExecutionEngine& engine) : engine_(engine) {}

std::vector<std::string> LinkShaper::netem_options(const Link& link) const {
    std::vector<std::string> opts = {"netem"};
    if (link.latency_ms > 0.0 || link.jitter_ms > 0.0) {
        opts.push_back("delay");
        opts.push_back(format_ms(link.latency_ms));
        if (link.jitter_ms > 0.0) {
            opts.push_back(format_ms(link.jitter_ms));
        }
    }
    if (link.loss_pct > 0.0) {
        opts.push_back("loss");
        opts.push_back(format_pct(link.loss_pct));
    }
    return opts;
}

std::vector<std::vector<std::string>> LinkShaper::build_apply_commands(
    const Link& link, const std::string& iface) const {
    std::vector<std::vector<std::string>> commands;

    // Start from a clean slate; a stale root qdisc would otherwise reject the add.
    commands.push_back({"tc", "qdisc", "del", "dev", iface, "root"});

    const std::vector<std::string> netem = netem_options(link);

    if (link.capacity_mbps > 0) {
        std::vector<std::string> tbf = {
            "tc",  "qdisc", "add", "dev", iface, "root", "handle", "1:", "tbf",
            "rate", std::to_string(link.capacity_mbps) + "mbit",
            "burst", burst_bytes(link.capacity_mbps), "latency", "50ms"};
        commands.push_back(std::move(tbf));

        std::vector<std::string> child = {"tc",     "qdisc", "add", "dev",
                                          iface,    "parent", "1:",  "handle",
                                          "10:"};
        child.insert(child.end(), netem.begin(), netem.end());
        commands.push_back(std::move(child));
    } else {
        // No capacity constraint: netem becomes the root qdisc.
        std::vector<std::string> root = {"tc",    "qdisc", "add", "dev",
                                         iface,   "root",  "handle", "10:"};
        root.insert(root.end(), netem.begin(), netem.end());
        commands.push_back(std::move(root));
    }

    return commands;
}

std::vector<std::vector<std::string>> LinkShaper::build_update_commands(
    const Link& link, const std::string& iface) const {
    std::vector<std::vector<std::string>> commands;
    const std::vector<std::string> netem = netem_options(link);

    if (link.capacity_mbps > 0) {
        commands.push_back({"tc",   "qdisc", "replace", "dev", iface, "root",
                            "handle", "1:", "tbf", "rate",
                            std::to_string(link.capacity_mbps) + "mbit", "burst",
                            burst_bytes(link.capacity_mbps), "latency", "50ms"});
        std::vector<std::string> child = {"tc",   "qdisc", "replace", "dev",
                                          iface,  "parent", "1:",      "handle",
                                          "10:"};
        child.insert(child.end(), netem.begin(), netem.end());
        commands.push_back(std::move(child));
    } else {
        std::vector<std::string> root = {"tc",   "qdisc", "replace", "dev",
                                         iface,  "root",  "handle",  "10:"};
        root.insert(root.end(), netem.begin(), netem.end());
        commands.push_back(std::move(root));
    }

    return commands;
}

bool LinkShaper::run_chain(const std::vector<std::vector<std::string>>& commands,
                           const std::string& netns) const {
    bool ok = true;
    for (std::size_t i = 0; i < commands.size(); ++i) {
        const CommandResult result =
            netns.empty() ? engine_.run(commands[i])
                          : engine_.run_in_namespace(netns, commands[i]);
        // The leading "qdisc del root" is best-effort: a fresh interface has no
        // root qdisc to remove, so its failure is not propagated.
        const bool is_initial_del =
            i == 0 && commands[i].size() >= 3 && commands[i][2] == "del";
        if (!result.ok() && !is_initial_del) {
            ok = false;
        }
    }
    return ok;
}

bool LinkShaper::apply(const Link& link, const std::string& iface,
                       const std::string& netns) const {
    return run_chain(build_apply_commands(link, iface), netns);
}

bool LinkShaper::update(const Link& link, const std::string& iface,
                        const std::string& netns) const {
    return run_chain(build_update_commands(link, iface), netns);
}

}  // namespace nch
