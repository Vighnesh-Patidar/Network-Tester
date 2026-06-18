#include "system_execution_engine.h"

#include <array>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <stdexcept>

#include <sys/wait.h>
#include <unistd.h>

namespace nch {

namespace {

// Drains a read end fully into out; returns false on a hard read error.
bool drain_pipe(int fd, std::string& out) {
    std::array<char, 4096> buffer{};
    while (true) {
        const ssize_t n = ::read(fd, buffer.data(), buffer.size());
        if (n > 0) {
            out.append(buffer.data(), static_cast<std::size_t>(n));
        } else if (n == 0) {
            return true;
        } else if (errno == EINTR) {
            continue;
        } else {
            return false;
        }
    }
}

}  // namespace

SystemExecutionEngine::SystemExecutionEngine(bool dry_run) : dry_run_(dry_run) {}

CommandResult SystemExecutionEngine::run(const std::vector<std::string>& argv) const {
    if (argv.empty()) {
        throw std::invalid_argument("SystemExecutionEngine::run requires a non-empty argv");
    }

    if (dry_run_) {
        CommandResult result;
        result.exited = true;
        result.exit_code = 0;
        std::string joined;
        for (const auto& arg : argv) {
            joined += arg;
            joined += ' ';
        }
        result.stdout_data = "[dry-run] " + joined + "\n";
        return result;
    }

    int out_pipe[2];
    int err_pipe[2];
    if (::pipe(out_pipe) != 0) {
        throw std::runtime_error(std::string("pipe() failed: ") + std::strerror(errno));
    }
    if (::pipe(err_pipe) != 0) {
        const int saved = errno;
        ::close(out_pipe[0]);
        ::close(out_pipe[1]);
        throw std::runtime_error(std::string("pipe() failed: ") + std::strerror(saved));
    }

    const pid_t pid = ::fork();
    if (pid < 0) {
        const int saved = errno;
        ::close(out_pipe[0]);
        ::close(out_pipe[1]);
        ::close(err_pipe[0]);
        ::close(err_pipe[1]);
        throw std::runtime_error(std::string("fork() failed: ") + std::strerror(saved));
    }

    if (pid == 0) {
        // Child: wire stdout/stderr to the write ends, then exec.
        ::dup2(out_pipe[1], STDOUT_FILENO);
        ::dup2(err_pipe[1], STDERR_FILENO);
        ::close(out_pipe[0]);
        ::close(out_pipe[1]);
        ::close(err_pipe[0]);
        ::close(err_pipe[1]);

        std::vector<char*> c_argv;
        c_argv.reserve(argv.size() + 1);
        for (const auto& arg : argv) {
            c_argv.push_back(const_cast<char*>(arg.c_str()));
        }
        c_argv.push_back(nullptr);

        ::execvp(c_argv[0], c_argv.data());
        // execvp only returns on failure; communicate it through the exit status.
        ::_exit(127);
    }

    // Parent: close write ends and drain both pipes.
    ::close(out_pipe[1]);
    ::close(err_pipe[1]);

    CommandResult result;
    drain_pipe(out_pipe[0], result.stdout_data);
    drain_pipe(err_pipe[0], result.stderr_data);
    ::close(out_pipe[0]);
    ::close(err_pipe[0]);

    int status = 0;
    while (::waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            throw std::runtime_error(std::string("waitpid() failed: ") +
                                     std::strerror(errno));
        }
    }

    if (WIFEXITED(status)) {
        result.exited = true;
        result.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.exited = false;
        result.term_signal = WTERMSIG(status);
    }
    return result;
}

CommandResult SystemExecutionEngine::run_in_namespace(
    const std::string& netns, const std::vector<std::string>& argv) const {
    std::vector<std::string> full = {"ip", "netns", "exec", netns};
    full.insert(full.end(), argv.begin(), argv.end());
    return run(full);
}

CommandResult SystemExecutionEngine::set_link_state(const std::string& netns,
                                                    const std::string& iface,
                                                    bool up) const {
    const std::vector<std::string> command = {
        "ip", "link", "set", "dev", iface, up ? "up" : "down"};
    if (netns.empty()) {
        return run(command);
    }
    return run_in_namespace(netns, command);
}

CommandResult SystemExecutionEngine::kill_namespace_daemons(
    const std::string& netns) const {
    // pkill returns non-zero (1) when no process matched, which for a node-down
    // injection is still a benign outcome; callers inspect ok()/exit_code. The
    // pattern is matched as an extended regex against the process name.
    return run_in_namespace(netns, {"pkill", "^(zebra|ospfd|bgpd)$"});
}

}  // namespace nch
