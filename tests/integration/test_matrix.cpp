#include <gtest/gtest.h>

#include <iostream>
#include <map>
#include <string>

#include "chaos_engine/orchestrator.h"
#include "chaos_engine/system_execution_engine.h"
#include "chaos_engine/topology_parser.h"

namespace {

// The sample topology from the repository root. The dry-run path exercises the
// parser, the reference solver and every §6 test case's pre/post-failure analysis
// without touching the kernel, so it is safe to run as an ordinary unit-scope
// integration check. The full kernel-backed matrix runs in CI under root with
// Mininet and FRR present (see tests/integration/README.md).
nch::ConvergenceReport run_dry_run(const std::string& topology_path) {
    nch::TopologyParser parser;
    nch::Topology topology = parser.parse_file(topology_path);

    nch::OrchestratorConfig config;
    config.topology_path = topology_path;
    config.dry_run = true;

    nch::SystemExecutionEngine engine(true);
    nch::ChaosOrchestrator orchestrator(topology, config, engine);
    return orchestrator.run();
}

}  // namespace

TEST(TestMatrix, SampleTopologyPassesEveryCaseInDryRun) {
    const nch::ConvergenceReport report = run_dry_run(NCH_TOPOLOGY_PATH);
    EXPECT_TRUE(report.stabilized);
    ASSERT_EQ(report.cases.size(), 6u);
    for (const auto& test_case : report.cases) {
        if (!test_case.passed) {
            std::cerr << "case failed: " << test_case.name << ": " << test_case.detail
                      << "\n";
        }
        EXPECT_TRUE(test_case.passed);
    }
    EXPECT_TRUE(report.all_passed());
}

TEST(TestMatrix, ReportSerializesAllCases) {
    const nch::ConvergenceReport report = run_dry_run(NCH_TOPOLOGY_PATH);
    const nlohmann::json doc = report.to_json();
    ASSERT_TRUE(doc.contains("cases"));
    EXPECT_EQ(doc.at("cases").size(), 6u);

    std::map<std::string, bool> seen;
    for (const auto& c : doc.at("cases")) {
        seen[c.at("name").get<std::string>()] = true;
    }
    EXPECT_TRUE(seen.count("single_link_failure_ospf") == 1);
    EXPECT_TRUE(seen.count("single_link_failure_bgp") == 1);
    EXPECT_TRUE(seen.count("node_failure") == 1);
    EXPECT_TRUE(seen.count("asymmetric_packet_loss") == 1);
    EXPECT_TRUE(seen.count("flapping_link") == 1);
    EXPECT_TRUE(seen.count("failover_under_capacity") == 1);
}
