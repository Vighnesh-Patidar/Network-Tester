#pragma once

#include <string>
#include <vector>

namespace nch {

// Result of a single subprocess invocation. exit_code is the process exit status
// when exited == true; if the child was killed by a signal, exited is false and
// term_signal carries the signal number.
struct CommandResult {
    int exit_code = -1;
    int term_signal = 0;
    bool exited = false;
    std::string stdout_data;
    std::string stderr_data;

    bool ok() const { return exited && exit_code == 0; }
};

// fork/exec wrapper used to drive the kernel substrate: bringing links down,
// entering network namespaces, querying FRR, and so on. Argument vectors are
// passed through to execvp without a shell, so there is no quoting/word-splitting
// surface to get wrong.
class SystemExecutionEngine {
public:
    explicit SystemExecutionEngine(bool dry_run = false);

    // Runs argv[0] with the given arguments, capturing stdout and stderr.
    CommandResult run(const std::vector<std::string>& argv) const;

    // Equivalent to: ip netns exec <ns> <argv...>
    CommandResult run_in_namespace(const std::string& netns,
                                   const std::vector<std::string>& argv) const;

    // ip [netns exec <ns>] link set dev <iface> up|down
    CommandResult set_link_state(const std::string& netns, const std::string& iface,
                                 bool up) const;

    // Terminates every FRR daemon running inside a namespace (node failure case).
    CommandResult kill_namespace_daemons(const std::string& netns) const;

    bool dry_run() const { return dry_run_; }

private:
    bool dry_run_;
};

}  // namespace nch
