#include <commitwizard.h>
#include <sqlite3.h>

#include <vector>

#define SQL_ERROR(x) do { \
    std::string msg = err_msg ? err_msg : (x); \
    sqlite3_free(err_msg); \
    sqlite3_close(db); \
    throw std::runtime_error(msg); \
} while(0)

namespace fs = std::filesystem;

static sqlite3* open_db(fs::path);
static std::vector<fs::path> collect_repo_dirs(fs::path);
static long fetch_db_commit_count(sqlite3*, const std::string&);
static git_entry_t load_git_entry(sqlite3*, const std::string&);
static void replace_git_entry(sqlite3*, const git_entry_t&);
static void delete_git_entry(sqlite3*, const std::string&);

/*
 * Creates the database schema when the backing file is initialized.
 */
void init_db(fs::path dir) {
    sqlite3 *db = open_db(dir);

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
    int rc = SQLITE_OK;

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

    sqlite3_close(db);
    db = NULL;
}

/*
 * Synchronizes repository commit data with the local sqlite database.
 */
void sync_db(fs::path db_dir, fs::path root_dir) {
    sqlite3 *db = open_db(db_dir);
    g_entries.clear();

    for (const auto& repo_dir : collect_repo_dirs(root_dir)) {
        const std::string entry_name = fs::weakly_canonical(repo_dir).filename().string();
        long local_commit_count = fetch_commit_count(repo_dir);
        long db_commit_count = fetch_db_commit_count(db, entry_name);

        if (db_commit_count >= local_commit_count && db_commit_count > 0) {
            g_entries.push_back(load_git_entry(db, entry_name));
            continue;
        }

        git_entry_t entry = fetch_commits(repo_dir);
        g_entries.push_back(entry);
        replace_git_entry(db, entry);
    }

    sqlite3_close(db);
}

/*
 * Opens the sqlite database and enables foreign key support.
 */
static sqlite3* open_db(fs::path dir) {
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

    rc = sqlite3_exec(db, "PRAGMA foreign_keys = ON;", nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        sqlite3_close(db);
        throw std::runtime_error("failed to enable foreign keys");
    }

    return db;
}

static std::vector<fs::path> collect_repo_dirs(fs::path root_dir) {
    std::vector<fs::path> repos;

    if (!fs::exists(root_dir) || !fs::is_directory(root_dir)) {
        return repos;
    }

    if (fs::exists(root_dir / ".git") && fs::is_directory(root_dir / ".git")) {
        repos.push_back(root_dir);
        return repos;
    }

    for (const auto& entry : fs::directory_iterator(root_dir)) {
        if (!entry.is_directory()) {
            continue;
        }

        fs::path subdir = entry.path();
        if (fs::exists(subdir / ".git") && fs::is_directory(subdir / ".git")) {
            repos.push_back(subdir);
        }
    }

    return repos;
}

/*
 * Fetches the stored commit count for a repository entry.
 */
static long fetch_db_commit_count(sqlite3 *db, const std::string& entry_name) {
    sqlite3_stmt *stmt = nullptr;
    const char *sql = R"(
        SELECT num_commits
        FROM git_entries
        WHERE entry_name = ?
        ORDER BY id DESC
        LIMIT 1;
    )";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(sqlite3_errmsg(db));
    }

    sqlite3_bind_text(stmt, 1, entry_name.c_str(), -1, SQLITE_TRANSIENT);

    long commit_count = 0;
    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        commit_count = static_cast<long>(sqlite3_column_int64(stmt, 0));
    }
    else if (rc != SQLITE_DONE) {
        std::string msg = sqlite3_errmsg(db);
        sqlite3_finalize(stmt);
        throw std::runtime_error(msg);
    }

    sqlite3_finalize(stmt);
    return commit_count;
}

/*
 * Loads a repository entry and its commit data from the database.
 */
static git_entry_t load_git_entry(sqlite3 *db, const std::string& entry_name) {
    sqlite3_stmt *entry_stmt = nullptr;
    const char *entry_sql = R"(
        SELECT id, entry_name, num_commits
        FROM git_entries
        WHERE entry_name = ?
        ORDER BY id DESC
        LIMIT 1;
    )";

    if (sqlite3_prepare_v2(db, entry_sql, -1, &entry_stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(sqlite3_errmsg(db));
    }

    sqlite3_bind_text(entry_stmt, 1, entry_name.c_str(), -1, SQLITE_TRANSIENT);

    git_entry_t git_entry = {};
    int rc = sqlite3_step(entry_stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(entry_stmt);
        throw std::runtime_error("missing git entry in database");
    }

    const long git_entry_id = static_cast<long>(sqlite3_column_int64(entry_stmt, 0));
    git_entry.entry_name = reinterpret_cast<const char*>(sqlite3_column_text(entry_stmt, 1));
    git_entry.num_commits = static_cast<long>(sqlite3_column_int64(entry_stmt, 2));
    sqlite3_finalize(entry_stmt);

    sqlite3_stmt *commit_stmt = nullptr;
    const char *commit_sql = R"(
        SELECT id, commit_hash, time, seconds_since_prev
        FROM commits
        WHERE git_entry_id = ?
        ORDER BY time DESC, id DESC;
    )";

    if (sqlite3_prepare_v2(db, commit_sql, -1, &commit_stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(sqlite3_errmsg(db));
    }

    sqlite3_bind_int64(commit_stmt, 1, git_entry_id);

    while ((rc = sqlite3_step(commit_stmt)) == SQLITE_ROW) {
        const long commit_id = static_cast<long>(sqlite3_column_int64(commit_stmt, 0));
        commit_t commit = {};
        commit.commit_hash = reinterpret_cast<const char*>(sqlite3_column_text(commit_stmt, 1));
        commit.time = static_cast<long>(sqlite3_column_int64(commit_stmt, 2));
        commit.seconds_since_prev = static_cast<long>(sqlite3_column_int64(commit_stmt, 3));

        sqlite3_stmt *file_stmt = nullptr;
        const char *file_sql = R"(
            SELECT file_name, is_active, line_diff_added, line_diff_removed
            FROM files
            WHERE commit_id = ?
            ORDER BY id ASC;
        )";

        if (sqlite3_prepare_v2(db, file_sql, -1, &file_stmt, nullptr) != SQLITE_OK) {
            sqlite3_finalize(commit_stmt);
            throw std::runtime_error(sqlite3_errmsg(db));
        }

        sqlite3_bind_int64(file_stmt, 1, commit_id);

        int file_rc = SQLITE_OK;
        while ((file_rc = sqlite3_step(file_stmt)) == SQLITE_ROW) {
            files_t file = {};
            file.file_name = reinterpret_cast<const char*>(sqlite3_column_text(file_stmt, 0));
            file.is_active = sqlite3_column_int(file_stmt, 1) != 0;
            file.line_diff.first = static_cast<long>(sqlite3_column_int64(file_stmt, 2));
            file.line_diff.second = static_cast<long>(sqlite3_column_int64(file_stmt, 3));
            commit.files.push_back(file);
        }

        if (file_rc != SQLITE_DONE) {
            std::string msg = sqlite3_errmsg(db);
            sqlite3_finalize(file_stmt);
            sqlite3_finalize(commit_stmt);
            throw std::runtime_error(msg);
        }

        sqlite3_finalize(file_stmt);
        git_entry.commits.push_back(commit);
    }

    if (rc != SQLITE_DONE) {
        std::string msg = sqlite3_errmsg(db);
        sqlite3_finalize(commit_stmt);
        throw std::runtime_error(msg);
    }

    sqlite3_finalize(commit_stmt);
    return git_entry;
}

/*
 * Replaces a repository entry and all nested records in one transaction.
 */
static void replace_git_entry(sqlite3 *db, const git_entry_t& git_entry) {
    char *err_msg = nullptr;
    int rc = sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        std::string msg = err_msg ? err_msg : "failed to begin transaction";
        sqlite3_free(err_msg);
        throw std::runtime_error(msg);
    }

    try {
        delete_git_entry(db, git_entry.entry_name);
    }
    catch (...) {
        sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw;
    }

    sqlite3_stmt *entry_stmt = nullptr;
    const char *entry_sql = R"(
        INSERT INTO git_entries (entry_name, num_commits)
        VALUES (?, ?);
    )";

    if (sqlite3_prepare_v2(db, entry_sql, -1, &entry_stmt, nullptr) != SQLITE_OK) {
        sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw std::runtime_error(sqlite3_errmsg(db));
    }

    sqlite3_bind_text(entry_stmt, 1, git_entry.entry_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(entry_stmt, 2, git_entry.num_commits);

    rc = sqlite3_step(entry_stmt);
    if (rc != SQLITE_DONE) {
        std::string msg = sqlite3_errmsg(db);
        sqlite3_finalize(entry_stmt);
        sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw std::runtime_error(msg);
    }

    sqlite3_finalize(entry_stmt);
    const sqlite3_int64 git_entry_id = sqlite3_last_insert_rowid(db);

    sqlite3_stmt *commit_stmt = nullptr;
    const char *commit_sql = R"(
        INSERT INTO commits (git_entry_id, commit_hash, time, seconds_since_prev)
        VALUES (?, ?, ?, ?);
    )";

    if (sqlite3_prepare_v2(db, commit_sql, -1, &commit_stmt, nullptr) != SQLITE_OK) {
        sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw std::runtime_error(sqlite3_errmsg(db));
    }

    sqlite3_stmt *file_stmt = nullptr;
    const char *file_sql = R"(
        INSERT INTO files (commit_id, file_name, is_active, line_diff_added, line_diff_removed)
        VALUES (?, ?, ?, ?, ?);
    )";

    if (sqlite3_prepare_v2(db, file_sql, -1, &file_stmt, nullptr) != SQLITE_OK) {
        sqlite3_finalize(commit_stmt);
        sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw std::runtime_error(sqlite3_errmsg(db));
    }

    for (const auto& commit : git_entry.commits) {
        sqlite3_reset(commit_stmt);
        sqlite3_clear_bindings(commit_stmt);
        sqlite3_bind_int64(commit_stmt, 1, git_entry_id);
        sqlite3_bind_text(commit_stmt, 2, commit.commit_hash.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(commit_stmt, 3, commit.time);
        sqlite3_bind_int64(commit_stmt, 4, commit.seconds_since_prev);

        rc = sqlite3_step(commit_stmt);
        if (rc != SQLITE_DONE) {
            std::string msg = sqlite3_errmsg(db);
            sqlite3_finalize(file_stmt);
            sqlite3_finalize(commit_stmt);
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            throw std::runtime_error(msg);
        }

        const sqlite3_int64 commit_id = sqlite3_last_insert_rowid(db);

        for (const auto& file : commit.files) {
            sqlite3_reset(file_stmt);
            sqlite3_clear_bindings(file_stmt);
            sqlite3_bind_int64(file_stmt, 1, commit_id);
            sqlite3_bind_text(file_stmt, 2, file.file_name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(file_stmt, 3, file.is_active ? 1 : 0);
            sqlite3_bind_int64(file_stmt, 4, file.line_diff.first);
            sqlite3_bind_int64(file_stmt, 5, file.line_diff.second);

            rc = sqlite3_step(file_stmt);
            if (rc != SQLITE_DONE) {
                std::string msg = sqlite3_errmsg(db);
                sqlite3_finalize(file_stmt);
                sqlite3_finalize(commit_stmt);
                sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
                throw std::runtime_error(msg);
            }
        }
    }

    sqlite3_finalize(file_stmt);
    sqlite3_finalize(commit_stmt);

    rc = sqlite3_exec(db, "COMMIT;", nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        std::string msg = err_msg ? err_msg : "failed to commit transaction";
        sqlite3_free(err_msg);
        sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw std::runtime_error(msg);
    }
}

/*
 * Deletes a repository entry and its dependent records from the database.
 */
static void delete_git_entry(sqlite3 *db, const std::string& entry_name) {
    const char *delete_files_sql = R"(
        DELETE FROM files
        WHERE commit_id IN (
            SELECT id
            FROM commits
            WHERE git_entry_id IN (
                SELECT id
                FROM git_entries
                WHERE entry_name = ?
            )
        );
    )";
    const char *delete_commits_sql = R"(
        DELETE FROM commits
        WHERE git_entry_id IN (
            SELECT id
            FROM git_entries
            WHERE entry_name = ?
        );
    )";
    const char *delete_entry_sql = R"(
        DELETE FROM git_entries
        WHERE entry_name = ?;
    )";

    const char *sqls[] = {delete_files_sql, delete_commits_sql, delete_entry_sql};

    for (const char *sql : sqls) {
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            throw std::runtime_error(sqlite3_errmsg(db));
        }

        sqlite3_bind_text(stmt, 1, entry_name.c_str(), -1, SQLITE_TRANSIENT);

        int rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            std::string msg = sqlite3_errmsg(db);
            sqlite3_finalize(stmt);
            throw std::runtime_error(msg);
        }

        sqlite3_finalize(stmt);
    }
}
