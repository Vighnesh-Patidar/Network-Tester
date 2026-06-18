#include <cstdlib>
#include <iostream>
#include <string>

#include "chaos_engine/system_execution_engine.h"
#include "link_shaper.h"

namespace {

void usage(const char* argv0) {
    std::cerr << "usage: " << argv0
              << " --iface <name> [--netns <ns>] [--capacity-mbps N]"
                 " [--latency-ms F] [--jitter-ms F] [--loss-pct F] [--update] [--dry-run]\n";
}

}  // namespace

// Thin CLI wrapper so the substrate bring-up (mininet_topology.py) can drive the
// C++ LinkShaper per link rather than reimplementing the tc chain in Python.
int main(int argc, char** argv) {
    nch::Link link;
    std::string iface;
    std::string netns;
    bool update = false;
    bool dry_run = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto value = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << name << " requires a value\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (arg == "--iface") {
            iface = value("--iface");
        } else if (arg == "--netns") {
            netns = value("--netns");
        } else if (arg == "--capacity-mbps") {
            link.capacity_mbps = std::stoi(value("--capacity-mbps"));
        } else if (arg == "--latency-ms") {
            link.latency_ms = std::stod(value("--latency-ms"));
        } else if (arg == "--jitter-ms") {
            link.jitter_ms = std::stod(value("--jitter-ms"));
        } else if (arg == "--loss-pct") {
            link.loss_pct = std::stod(value("--loss-pct"));
        } else if (arg == "--update") {
            update = true;
        } else if (arg == "--dry-run") {
            dry_run = true;
        } else if (arg == "--help" || arg == "-h") {
            usage(argv[0]);
            return 0;
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    if (iface.empty()) {
        usage(argv[0]);
        return 2;
    }

    link.id = iface;
    nch::SystemExecutionEngine engine(dry_run);
    nch::LinkShaper shaper(engine);
    const bool ok = update ? shaper.update(link, iface, netns)
                           : shaper.apply(link, iface, netns);
    return ok ? 0 : 1;
}
