#pragma once

#include <string>
#include <vector>
#include <filesystem>

#define MAX_BUF_SIZE    (4096)
#define GIT_CMD         ("log --pretty=format:\"%H|%ct\" --numstat")

struct files_t {
    std::string file_name;
    bool is_active;
    std::pair<long, long> line_diff;
};

struct commit_t {
    std::string commit_hash;
    std::vector<files_t> files;
    long time;
    long seconds_since_prev;
};

struct git_entry_t {
    std::string entry_name;
    std::vector<commit_t> commits;
};

extern std::vector<git_entry_t> g_entries;

git_entry_t fetch_commits(std::filesystem::path);
