#include <commitwizard.h>

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
 * Given a directory name it will parse the git commit information
 * and return the entry related to it.
 */
git_entry_t fetch_commits(fs::path dir) {
    if (!(fs::exists(dir / ".git") && (fs::is_directory(dir / ".git")))) {
        throw std::invalid_argument("not a valid git directory");
    }

    std::string cmd_out = exec_git_cmd(dir, "git -C \"" + dir.string() + "\" " + GIT_CMD);

    std::istringstream cmd_iss(cmd_out);
    std::vector<std::vector<std::string>> parsed_blocks;
    std::vector<std::string> cur_block;

    /* Parsing out individual commits into blocks */
    for (std::string line;;) {
        if (!std::getline(cmd_iss, line)) {
            parsed_blocks.push_back(cur_block);
            break;
        }

        if (line.empty()) {
            parsed_blocks.push_back(cur_block);
            cur_block = {};
            continue;
        }

        cur_block.push_back(line);
    }

    git_entry_t new_git_entry = { .entry_name = dir.string(),
        .num_commits = static_cast<long>(parsed_blocks.size()) };

    /* Parsing blocks into their file changes */
    for (auto block : parsed_blocks) {
        commit_t new_commit = {};

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
                new_file.line_diff.first = std::stol(second_diff);
            }

            std::getline(block_iss, new_file.file_name);

            new_commit.files.push_back(new_file);
        }

        new_git_entry.commits.push_back(new_commit);
    }

    // prune_commits(new_git_entry);

    for (const auto& commit : new_git_entry.commits) {
        std::print("    commit_t {{\n");
        std::print("      commit_hash: {}\n", commit.commit_hash);
        std::print("      time: {}\n", commit.time);
        std::print("      seconds_since_prev: {}\n", commit.seconds_since_prev);
        std::print("      files: [\n");

        for (const auto& file : commit.files) {
            std::print("        files_t {{ file_name: {}, is_active: {}, line_diff: ({}, {}) }}\n",
                    file.file_name,
                    file.is_active,
                    file.line_diff.first,
                    file.line_diff.second);
        }

        std::print("      ]\n");
        std::print("    }}\n");
    }

    std::print("  ]\n");
    std::print("}}\n");

    return {};
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
