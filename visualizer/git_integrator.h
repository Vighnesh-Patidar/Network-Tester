#pragma once

#include <string>

#include "common/topology.h"

namespace nch {

// Outcome of a commit/push attempt. The UI binds to this rather than treating the
// operation as fire-and-forget (§4.2): a failed push (no upstream, expired auth,
// merge conflict, offline) must be visible, because this commit is the trusted
// trigger for the validation pipeline.
struct PushResult {
    bool success = false;
    int exit_code = 0;
    std::string stderr_capture;
    std::string stage;  // which step failed: "write", "add", "commit", "push"
};

// Serializes the editor graph to topology.json inside a working tree and commits
// and pushes it. Every shell-out is checked; the first failure short-circuits and
// is reported with its captured stderr.
class GitIntegrator {
public:
    GitIntegrator(std::string repo_path, std::string topology_filename = "topology.json");

    PushResult commit_and_push(const Topology& topology,
                               const std::string& commit_message,
                               const std::string& remote = "origin",
                               const std::string& branch = "main");

    // Just write the file (no VCS). Returns false on I/O error.
    bool write_topology(const Topology& topology);

private:
    std::string repo_path_;
    std::string topology_filename_;

    struct ShellResult {
        int exit_code;
        std::string stderr_capture;
    };

    // Runs a command with cwd = repo_path_, capturing stderr and the real exit
    // code from the system() status word.
    ShellResult run(const std::string& command) const;
};

}  // namespace nch
