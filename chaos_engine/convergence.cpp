#include "convergence.h"

namespace nch {

nlohmann::json TestCaseResult::to_json() const {
    nlohmann::json j;
    j["name"] = name;
    j["passed"] = passed;
    j["detail"] = detail;
    j["data_plane_convergence_ms"] = convergence.data_plane_convergence_ms;
    j["control_plane_convergence_ms"] = convergence.control_plane_convergence_ms;
    j["within_timeout"] = convergence.within_timeout;
    return j;
}

bool ConvergenceReport::all_passed() const {
    if (!stabilized) {
        return false;
    }
    for (const auto& c : cases) {
        if (!c.passed) {
            return false;
        }
    }
    return true;
}

nlohmann::json ConvergenceReport::to_json() const {
    nlohmann::json j;
    j["topology_path"] = topology_path;
    j["convergence_timeout_ms"] = convergence_timeout_ms;
    j["stabilization_timeout_ms"] = stabilization_timeout_ms;
    j["stabilized"] = stabilized;
    j["all_passed"] = all_passed();

    nlohmann::json cases_json = nlohmann::json::array();
    for (const auto& c : cases) {
        cases_json.push_back(c.to_json());
    }
    j["cases"] = cases_json;
    return j;
}

}  // namespace nch
