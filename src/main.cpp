#include <commitwizard.h>

#include <print>
#include <filesystem>

std::vector<git_entry_t> g_entries = {};

static int args_checker(int, char **);

int main(int argc, char **argv) {
    if (args_checker(argc, argv)) {
        return 0;
    }

    fetch_commits(static_cast<std::filesystem::path>(argv[1]));

    return 0;
}

static int args_checker(int argc, char **argv) {
    if (argc <= 1) {
        std::print(stderr, "Missing argument: {} [root directory]\n", argv[0]);
        return 1;
    }

    return 0;
}
