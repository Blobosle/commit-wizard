#include <commitwizard.h>
#include <sqlite3.h>

#include <print>

#define SQL_ERROR(x) do { \
    std::string msg = err_msg ? err_msg : (x); \
    sqlite3_free(err_msg); \
    sqlite3_close(db); \
    throw std::runtime_error(msg); \
} while(0)

namespace fs = std::filesystem;

void init_db(fs::path dir) {
    sqlite3 *db = nullptr;
    fs::path db_path = dir / "cwiz.db";

    int rc = sqlite3_open(db_path.c_str(), &db);


    if (rc != SQLITE_OK) {
        std::string msg = db ? sqlite3_errmsg(db) : "failed to open database";

        if (db) {
            sqlite3_close(db);
        }

        throw std::runtime_error("sqlite3_open failed: " + msg);
    }

    std::string git_query = R"(
        CREATE TABLE IF NOT EXISTS git_entries (
            id INTEGER PRIMARY KEY,
            entry_name TEXT NOT NULL,
            num_commits INTEGER NOT NULL
        );
    )";

    std::string commit_query = R"(
        CREATE TABLE IF NOT EXISTS commits (
            id INTEGER PRIMARY KEY,
            git_entry_id INTEGER NOT NULL,
            commit_hash TEXT NOT NULL,
            time INTEGER NOT NULL,
            seconds_since_prev INTEGER NOT NULL,
            FOREIGN KEY (git_entry_id) REFERENCES git_entries(id)
        );
    )";

    std::string files_query = R"(
        CREATE TABLE IF NOT EXISTS files (
            id INTEGER PRIMARY KEY,
            commit_id INTEGER NOT NULL,
            file_name TEXT NOT NULL,
            is_active INTEGER NOT NULL,
            line_diff_added INTEGER NOT NULL,
            line_diff_removed INTEGER NOT NULL,
            FOREIGN KEY (commit_id) REFERENCES commits(id)
        );
    )";

    char* err_msg = nullptr;

    rc = sqlite3_exec(db, git_query.c_str(), nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        SQL_ERROR("failed to create git_entries");
    }

    rc = sqlite3_exec(db, commit_query.c_str(), nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        SQL_ERROR("failed to create commits");
    }

    rc = sqlite3_exec(db, files_query.c_str(), nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        SQL_ERROR("failed to create files");
    }

    rc = sqlite3_exec(db, "PRAGMA foreign_keys = ON;", nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        sqlite3_close(db);
        throw std::runtime_error("failed to enable foreign keys");
    }

    sqlite3_close(db);
    db = NULL;
}
