#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace nch {

// Data-plane and control-plane reconvergence are distinct measurements and are
// intentionally never collapsed into one number (§4.4 / §5.4).
struct ConvergenceResult {
    double data_plane_convergence_ms = -1.0;     // libpcap-observed traffic resumption
    double control_plane_convergence_ms = -1.0;  // vtysh-observed routing-table update
    bool within_timeout = false;                 // both planes met convergence_timeout_ms
};

// Outcome of one row of the test matrix (§6).
struct TestCaseResult {
    std::string name;
    bool passed = false;
    std::string detail;
    ConvergenceResult convergence;

    nlohmann::json to_json() const;
};

// Aggregate report emitted by the orchestrator and consumed by CI.
struct ConvergenceReport {
    std::string topology_path;
    std::uint32_t convergence_timeout_ms = 0;
    std::uint32_t stabilization_timeout_ms = 0;
    bool stabilized = false;
    std::vector<TestCaseResult> cases;

    bool all_passed() const;
    nlohmann::json to_json() const;
};

}  // namespace nch
