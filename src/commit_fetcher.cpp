#include <commitwizard.h>

#include <array>
#include <filesystem>
#include <print>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace fs = std::filesystem;

void fetch_batch(fs::path);
git_entry_t fetch_commits(fs::path);
long fetch_commit_count(fs::path);
static std::string exec_git_cmd(fs::path&, std::string);
static void prune_commits(git_entry_t&);

/*
 * Executes git command and returns buffer with result.
 */
static std::string exec_git_cmd(fs::path &dir, std::string cmd) {
    std::array<char, MAX_BUF_SIZE> buffer{};
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

/*
 * Obtains every subdirectory that is a git instance and runs the commit
 * fetcher to then push them into the global git entry vector.
 */
void fetch_batch(fs::path dir) {
    if ((!fs::exists(dir)) || (!fs::is_directory(dir))) {
        return;
    }

    for (auto i : fs::directory_iterator(dir)) {
        if (!i.is_directory()) {
            continue;
        }

        fs::path subdir = i.path();

        if ((!fs::exists(subdir / ".git")) || (!fs::is_directory(subdir / ".git"))) {
            continue;
        }

        g_entries.push_back(fetch_commits(subdir));
    }
}

/*
 * Obtains the number of commits for a repository through the same
 * command execution path used by the fetcher.
 */
long fetch_commit_count(fs::path dir) {
    if (!(fs::exists(dir / ".git") && fs::is_directory(dir / ".git"))) {
        throw std::invalid_argument("not a valid git directory");
    }

    std::string cmd_out = exec_git_cmd(dir, "git -C \"" + dir.string() + "\" rev-list --count HEAD");

    if (cmd_out.empty()) {
        return 0;
    }

    while (!cmd_out.empty() && (cmd_out.back() == '\n' || cmd_out.back() == '\r')) {
        cmd_out.pop_back();
    }

    return cmd_out.empty() ? 0 : std::stol(cmd_out);
}

/*
 * Given a directory name it will parse the git commit information
 * and return the entry related to it.
 */
git_entry_t fetch_commits(fs::path dir) {
    if (!(fs::exists(dir / ".git") && (fs::is_directory(dir / ".git")))) {
        throw std::invalid_argument("not a valid git directory");
    }

    fs::path resolved_dir = fs::weakly_canonical(dir);

    std::string cmd_out = exec_git_cmd(dir, "git -C \"" + dir.string() + "\" " + GIT_CMD);

    std::istringstream cmd_iss(cmd_out);
    std::vector<std::vector<std::string>> parsed_blocks;
    std::vector<std::string> cur_block;

    /* Parsing out individual commits into blocks */
    for (std::string line;;) {
        if (!std::getline(cmd_iss, line)) {
            if (!cur_block.empty()) {
                parsed_blocks.push_back(cur_block);
            }
            break;
        }

        if (line.empty()) {
            if (!cur_block.empty()) {
                parsed_blocks.push_back(cur_block);
            }
            cur_block = {};
            continue;
        }

        cur_block.push_back(line);
    }

    git_entry_t new_git_entry = { .entry_name = resolved_dir.filename().string(),
        .num_commits = static_cast<long>(parsed_blocks.size()) };

    /* Parsing blocks into their file changes */
    for (const auto& block : parsed_blocks) {
        commit_t new_commit = {};

        if (block.empty()) {
            continue;
        }

        size_t pos = block[0].find("|");
        new_commit.commit_hash = block[0].substr(0, pos);
        new_commit.time = std::stol(block[0].substr(pos + 1));

        for (int i = 1; i < block.size(); i++) {
            std::istringstream block_iss(block[i]);
            files_t new_file = {};

            std::string first_diff;
            std::getline(block_iss, first_diff, '\t');

            if (first_diff == "-") {
                new_file.is_active = false;
            }
            else {
                new_file.is_active = true;
                new_file.line_diff.first = std::stol(first_diff);
            }

            std::string second_diff;
            std::getline(block_iss, second_diff, '\t');

            if (second_diff != "-") {
                new_file.line_diff.second = std::stol(second_diff);
            }

            std::getline(block_iss, new_file.file_name);

            new_commit.files.push_back(new_file);
        }

        new_git_entry.commits.push_back(new_commit);
    }

    prune_commits(new_git_entry);

    return new_git_entry;
}

/*
 * Find missing files in between commits and propagates them.
 */
static void prune_commits(git_entry_t& repo) {
    if (repo.commits.empty()) {
        return;
    }

    std::unordered_map<std::string, files_t> seen_files;

    for (auto& f : repo.commits.back().files) {
        seen_files[f.file_name] = f;
    }

    commit_t prev_commit = repo.commits.back();

    for (int i = static_cast<int>(repo.commits.size()) - 2; i >= 0; --i) {
        commit_t& commit = repo.commits[i];

        commit.seconds_since_prev = commit.time - prev_commit.time;

        std::unordered_set<std::string> commit_files;
        for (auto& f : commit.files) {
            commit_files.insert(f.file_name);

            if (f.is_active) {
                seen_files[f.file_name] = f;
            }
            else {
                seen_files.erase(f.file_name);
            }
        }

        for (auto& [name, file] : seen_files) {
            if (!commit_files.contains(name)) {
                commit.files.push_back(file);
            }
        }

        prev_commit = commit;
    }
}
