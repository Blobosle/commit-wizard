#include <commitwizard.h>

#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;

/*
 * Executes git command and returns buffer with result
 */
static std::string exec_git_cmd(fs::path &dir) {
    std::array<char, MAX_BUF_SIZE> buffer{};
    std::string cmd = "git -C \"" + dir.string() + "\" " + GIT_CMD;
    std::string ret;

    FILE *pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        throw std::runtime_error("failed to run git command");
    }

    while (fgets(buffer.data(), buffer.size(), pipe)) {
        ret += buffer.data();
    }

    pclose(pipe);
    pipe = NULL;

    return ret;
}

std::vector<commit_t> fetch_commits(fs::path dir) {
    if (!(fs::exists(dir / ".git") && (fs::is_directory(dir / ".git")))) {
        throw std::invalid_argument("not a valid git directory");
    }

    std::cout << exec_git_cmd(dir) << std::endl;

    return {};
}
