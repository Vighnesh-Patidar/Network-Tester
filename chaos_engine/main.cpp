#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#include "convergence.h"
#include "orchestrator.h"
#include "system_execution_engine.h"
#include "topology_parser.h"

namespace {

void print_usage(const char* argv0) {
    std::cerr
        << "usage: " << argv0 << " [options]\n"
        << "  --topology <path>            topology.json to validate (default: topology.json)\n"
        << "  --report <path>              write JSON report here (default: stdout)\n"
        << "  --convergence-timeout-ms N   per-test convergence budget (default: 5000)\n"
        << "  --stabilization-timeout-ms N initial convergence budget (default: 30000)\n"
        << "  --dry-run                    parse/solve/validate without touching the kernel\n"
        << "  --help                       show this message\n";
}

bool parse_uint(const char* text, std::uint32_t& out) {
    try {
        out = static_cast<std::uint32_t>(std::stoul(text));
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

}  // namespace

int main(int argc, char** argv) {
    nch::OrchestratorConfig config;
    config.topology_path = "topology.json";
    std::string report_path;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto need_value = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::cerr << name << " requires a value\n";
                std::exit(2);
            }
            return argv[++i];
        };

        if (arg == "--topology") {
            config.topology_path = need_value("--topology");
        } else if (arg == "--report") {
            report_path = need_value("--report");
        } else if (arg == "--convergence-timeout-ms") {
            if (!parse_uint(need_value("--convergence-timeout-ms"),
                            config.convergence_timeout_ms)) {
                std::cerr << "invalid --convergence-timeout-ms\n";
                return 2;
            }
        } else if (arg == "--stabilization-timeout-ms") {
            if (!parse_uint(need_value("--stabilization-timeout-ms"),
                            config.stabilization_timeout_ms)) {
                std::cerr << "invalid --stabilization-timeout-ms\n";
                return 2;
            }
        } else if (arg == "--dry-run") {
            config.dry_run = true;
        } else if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else {
            std::cerr << "unknown argument: " << arg << "\n";
            print_usage(argv[0]);
            return 2;
        }
    }
    config.report_path = report_path;

    nch::Topology topology;
    try {
        nch::TopologyParser parser;
        topology = parser.parse_file(config.topology_path);
    } catch (const std::exception& ex) {
        std::cerr << "failed to load topology: " << ex.what() << "\n";
        return 2;
    }

    nch::SystemExecutionEngine engine(config.dry_run);
    nch::ChaosOrchestrator orchestrator(topology, config, engine);
    const nch::ConvergenceReport report = orchestrator.run();

    const std::string serialized = report.to_json().dump(2);
    if (report_path.empty()) {
        std::cout << serialized << "\n";
    } else {
        std::ofstream out(report_path);
        if (!out) {
            std::cerr << "cannot write report to " << report_path << "\n";
            return 2;
        }
        out << serialized << "\n";
    }

    // The POSIX exit code is the CI merge/reject gate (§3).
    return report.all_passed() ? 0 : 1;
}
