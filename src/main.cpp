#include <commitwizard.h>

#include <print>
#include <filesystem>

std::vector<git_entry_t> g_entries = {};

static int args_checker(int, char **, int&, int&, bool&);

namespace fs = std::filesystem;

/*
 * cwiz [-s <path>] <root_directory>
 * cwiz batch [-s <path>] <root_directory>
 */
int main(int argc, char **argv) {
    bool s_flag = false;
    int root_dir = 0;
    int db_dir = 0;
    int args_ret = args_checker(argc, argv, root_dir, db_dir, s_flag);

    if (s_flag) {
        init_db(static_cast<fs::path>(argv[db_dir]));
        sync_db(static_cast<fs::path>(argv[db_dir]), static_cast<fs::path>(argv[root_dir]));
    }
    else if (args_ret == -1) {
        return 1;
    }
    else if (args_ret == 1) {
        fetch_batch(static_cast<fs::path>(argv[root_dir]));
    }
    else {
        g_entries.clear();
        g_entries.push_back(fetch_commits(argv[root_dir]));
    }

    /* TODO: Tui logic somewhere around here */
    return 0;
}

static int args_checker(int argc, char **argv, int &root_dir, int &db_dir, bool& s_flag) {
    if (argc <= 1) {
        std::print(stderr, "Missing argument: {} [root directory]\n", argv[0]);
        return -1;
    }

    int ret = 0;
    std::string arg(argv[1]);

    if (arg == "batch") {
        if (argc <= 2) {
            std::print(stderr, "Missing argument: {} batch"
                    " [overarching directory]\n", argv[0]);
            return -1;
        }

        ret = 1;
    }

    std::string flag_str(argv[ret + 1]);
    if (flag_str == "-s") {
        if (argc <= ret + 3) {
            std::print(stderr, "Missing argument: {} batch -s "
                    "[db directory] [overarching directory]\n", argv[0]);
            return -1;
        }

        s_flag = true;
        db_dir = ret + 2;
        root_dir = ret + 3;
    }
    else {
        root_dir = ret + 1;
    }

    return ret;
}
