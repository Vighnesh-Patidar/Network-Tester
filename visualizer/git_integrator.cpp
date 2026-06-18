#include "git_integrator.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include <sys/wait.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

namespace nch {

namespace {

std::string read_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string make_temp_path(const char* tag) {
    std::string path = "/tmp/nch_git_";
    path += tag;
    path += "_";
    path += std::to_string(static_cast<long>(::getpid()));
    return path;
}

}  // namespace

GitIntegrator::GitIntegrator(std::string repo_path, std::string topology_filename)
    : repo_path_(std::move(repo_path)), topology_filename_(std::move(topology_filename)) {}

GitIntegrator::ShellResult GitIntegrator::run(const std::string& command) const {
    const std::string stderr_path = make_temp_path("stderr");
    // Run inside the repo and divert stderr to a temp file so the captured text
    // can be surfaced in the UI. system() returns a wait(2) status word, not the
    // raw exit code, so it has to be decoded with the W* macros.
    std::ostringstream full;
    full << "cd " << repo_path_ << " && " << command << " 2> " << stderr_path;

    const int status = std::system(full.str().c_str());
    ShellResult result;
    if (status == -1) {
        result.exit_code = -1;
    } else if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    } else {
        result.exit_code = 128;  // killed by signal or otherwise abnormal
    }
    result.stderr_capture = read_file(stderr_path);
    std::remove(stderr_path.c_str());
    return result;
}

bool GitIntegrator::write_topology(const Topology& topology) {
    const std::string path = repo_path_ + "/" + topology_filename_;
    std::ofstream out(path);
    if (!out) {
        return false;
    }
    out << topology_to_json(topology).dump(2) << "\n";
    return static_cast<bool>(out);
}

PushResult GitIntegrator::commit_and_push(const Topology& topology,
                                          const std::string& commit_message,
                                          const std::string& remote,
                                          const std::string& branch) {
    PushResult result;

    if (!write_topology(topology)) {
        result.success = false;
        result.stage = "write";
        result.exit_code = -1;
        result.stderr_capture = "failed to write " + topology_filename_;
        return result;
    }

    ShellResult add = run("git add " + topology_filename_);
    if (add.exit_code != 0) {
        result.success = false;
        result.stage = "add";
        result.exit_code = add.exit_code;
        result.stderr_capture = add.stderr_capture;
        return result;
    }

    // Pass the message via a file to sidestep all shell quoting concerns.
    const std::string msg_path = make_temp_path("msg");
    {
        std::ofstream msg(msg_path);
        msg << commit_message;
    }
    ShellResult commit = run("git commit -F " + msg_path);
    std::remove(msg_path.c_str());
    if (commit.exit_code != 0) {
        result.success = false;
        result.stage = "commit";
        result.exit_code = commit.exit_code;
        result.stderr_capture = commit.stderr_capture;
        return result;
    }

    ShellResult push = run("git push " + remote + " " + branch);
    if (push.exit_code != 0) {
        result.success = false;
        result.stage = "push";
        result.exit_code = push.exit_code;
        result.stderr_capture = push.stderr_capture;
        return result;
    }

    result.success = true;
    result.exit_code = 0;
    result.stage = "push";
    return result;
}

}  // namespace nch
