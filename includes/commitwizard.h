#pragma once

#include <string>
#include <vector>

struct commit_t {
    std::string commit_hash;
    long time;
    long seconds_since_prev;
    long lines_added;
};

struct git_entry_t {
    std::string name;
    std::vector<commit_t> commits;
};

extern std::vector<git_entry_t> g_entries;

std::vector<commit_t> fetch_commits(std::string);
