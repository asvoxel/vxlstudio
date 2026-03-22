// VxlStudio -- AuditLog implementation (SQLite + SHA-256 checksums)
// SPDX-License-Identifier: MIT

#include "vxl/audit.h"

#include <sqlite3.h>

#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>

#ifdef __APPLE__
#include <CommonCrypto/CommonDigest.h>
#else
#include <openssl/sha.h>
#endif

namespace vxl {

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

static std::string compute_checksum(int64_t timestamp_ms,
                                    const std::string& username,
                                    const std::string& action,
                                    const std::string& details) {
    std::string input = std::to_string(timestamp_ms) + "|" +
                        username + "|" + action + "|" + details;

    unsigned char hash[32];
#ifdef __APPLE__
    CC_SHA256(input.data(),
              static_cast<CC_LONG>(input.size()),
              hash);
#else
    SHA256(reinterpret_cast<const unsigned char*>(input.data()),
           input.size(),
           hash);
#endif

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (int i = 0; i < 32; ++i)
        oss << std::setw(2) << static_cast<int>(hash[i]);
    return oss.str();
}

static int64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(
               system_clock::now().time_since_epoch())
        .count();
}

// Escape a field for CSV (RFC 4180)
static std::string csv_escape(const std::string& field) {
    if (field.find_first_of(",\"\r\n") == std::string::npos)
        return field;
    std::string escaped = "\"";
    for (char c : field) {
        if (c == '"') escaped += "\"\"";
        else          escaped += c;
    }
    escaped += '"';
    return escaped;
}

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------

struct AuditLog::Impl {
    sqlite3*     db = nullptr;
    UserManager* um = nullptr;
    mutable std::mutex mtx;

    ~Impl() {
        if (db) sqlite3_close(db);
    }

    Result<void> exec(const char* sql) {
        char* err = nullptr;
        int rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            std::string msg = err ? err : "sqlite error";
            sqlite3_free(err);
            return Result<void>::failure(ErrorCode::INTERNAL_ERROR, msg);
        }
        return Result<void>::success();
    }
};

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

AuditLog::AuditLog() : impl_(std::make_unique<Impl>()) {}
AuditLog::~AuditLog() = default;

// ---------------------------------------------------------------------------
// init
// ---------------------------------------------------------------------------

Result<void> AuditLog::init(const std::string& db_path) {
    std::lock_guard<std::mutex> lk(impl_->mtx);

    if (impl_->db) {
        sqlite3_close(impl_->db);
        impl_->db = nullptr;
    }

    int rc = sqlite3_open(db_path.c_str(), &impl_->db);
    if (rc != SQLITE_OK) {
        std::string msg = sqlite3_errmsg(impl_->db);
        sqlite3_close(impl_->db);
        impl_->db = nullptr;
        return Result<void>::failure(ErrorCode::FILE_NOT_FOUND, msg);
    }

    return impl_->exec(
        "CREATE TABLE IF NOT EXISTS audit_log ("
        "  id           INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  timestamp_ms INTEGER NOT NULL,"
        "  username     TEXT NOT NULL,"
        "  action       TEXT NOT NULL,"
        "  details      TEXT,"
        "  checksum     TEXT NOT NULL"
        ");");
}

// ---------------------------------------------------------------------------
// log_event
// ---------------------------------------------------------------------------

Result<void> AuditLog::log_event(const std::string& action,
                                  const std::string& details) {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    if (!impl_->db)
        return Result<void>::failure(ErrorCode::INTERNAL_ERROR,
                                     "AuditLog not initialised");

    int64_t ts = now_ms();

    std::string username = "system";
    if (impl_->um && impl_->um->is_logged_in())
        username = impl_->um->current_user().username;

    std::string checksum = compute_checksum(ts, username, action, details);

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db,
        "INSERT INTO audit_log (timestamp_ms, username, action, details, checksum) "
        "VALUES (?, ?, ?, ?, ?);",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK)
        return Result<void>::failure(ErrorCode::INTERNAL_ERROR,
                                     sqlite3_errmsg(impl_->db));

    sqlite3_bind_int64(stmt, 1, ts);
    sqlite3_bind_text(stmt, 2, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, action.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, details.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, checksum.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE)
        return Result<void>::failure(ErrorCode::INTERNAL_ERROR,
                                     sqlite3_errmsg(impl_->db));

    return Result<void>::success();
}

// ---------------------------------------------------------------------------
// query
// ---------------------------------------------------------------------------

Result<std::vector<AuditEntry>> AuditLog::query(
    int64_t from_timestamp_ms,
    int64_t to_timestamp_ms,
    const std::string& username,
    const std::string& action,
    int limit) const {

    std::lock_guard<std::mutex> lk(impl_->mtx);
    if (!impl_->db)
        return Result<std::vector<AuditEntry>>::failure(
            ErrorCode::INTERNAL_ERROR, "AuditLog not initialised");

    // Build query dynamically
    std::string sql = "SELECT id, timestamp_ms, username, action, details, checksum "
                      "FROM audit_log WHERE 1=1";
    std::vector<std::pair<int, std::string>> text_binds;
    std::vector<std::pair<int, int64_t>>     int_binds;
    int bind_idx = 1;

    if (from_timestamp_ms > 0) {
        sql += " AND timestamp_ms >= ?";
        int_binds.push_back({bind_idx++, from_timestamp_ms});
    }
    if (to_timestamp_ms > 0) {
        sql += " AND timestamp_ms <= ?";
        int_binds.push_back({bind_idx++, to_timestamp_ms});
    }
    if (!username.empty()) {
        sql += " AND username = ?";
        text_binds.push_back({bind_idx++, username});
    }
    if (!action.empty()) {
        sql += " AND action = ?";
        text_binds.push_back({bind_idx++, action});
    }

    sql += " ORDER BY id DESC LIMIT ?";
    int_binds.push_back({bind_idx++, static_cast<int64_t>(limit)});

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK)
        return Result<std::vector<AuditEntry>>::failure(
            ErrorCode::INTERNAL_ERROR, sqlite3_errmsg(impl_->db));

    for (auto& [idx, val] : int_binds)
        sqlite3_bind_int64(stmt, idx, val);
    for (auto& [idx, val] : text_binds)
        sqlite3_bind_text(stmt, idx, val.c_str(), -1, SQLITE_TRANSIENT);

    std::vector<AuditEntry> entries;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        AuditEntry e;
        e.id           = sqlite3_column_int64(stmt, 0);
        e.timestamp_ms = sqlite3_column_int64(stmt, 1);
        e.username     = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        e.action       = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        auto det       = sqlite3_column_text(stmt, 4);
        e.details      = det ? reinterpret_cast<const char*>(det) : "";
        e.checksum     = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        entries.push_back(std::move(e));
    }
    sqlite3_finalize(stmt);

    return Result<std::vector<AuditEntry>>::success(std::move(entries));
}

// ---------------------------------------------------------------------------
// export_csv
// ---------------------------------------------------------------------------

Result<void> AuditLog::export_csv(const std::string& path,
                                   int64_t from_timestamp_ms,
                                   int64_t to_timestamp_ms) const {
    auto res = query(from_timestamp_ms, to_timestamp_ms, "", "", 1000000);
    if (!res.ok())
        return Result<void>::failure(res.code, res.message);

    std::ofstream ofs(path);
    if (!ofs)
        return Result<void>::failure(ErrorCode::FILE_NOT_FOUND,
                                     "Cannot open file: " + path);

    ofs << "id,timestamp_ms,username,action,details,checksum\n";
    for (auto& e : res.value) {
        ofs << e.id << ","
            << e.timestamp_ms << ","
            << csv_escape(e.username) << ","
            << csv_escape(e.action) << ","
            << csv_escape(e.details) << ","
            << csv_escape(e.checksum) << "\n";
    }

    return Result<void>::success();
}

// ---------------------------------------------------------------------------
// cleanup
// ---------------------------------------------------------------------------

Result<void> AuditLog::cleanup(int max_days) {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    if (!impl_->db)
        return Result<void>::failure(ErrorCode::INTERNAL_ERROR,
                                     "AuditLog not initialised");

    int64_t cutoff = now_ms() - static_cast<int64_t>(max_days) * 86400000LL;

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(impl_->db,
        "DELETE FROM audit_log WHERE timestamp_ms < ?;",
        -1, &stmt, nullptr);
    sqlite3_bind_int64(stmt, 1, cutoff);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE)
        return Result<void>::failure(ErrorCode::INTERNAL_ERROR,
                                     sqlite3_errmsg(impl_->db));

    return Result<void>::success();
}

// ---------------------------------------------------------------------------
// entry_count
// ---------------------------------------------------------------------------

int64_t AuditLog::entry_count() const {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    if (!impl_->db) return 0;

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(impl_->db,
        "SELECT COUNT(*) FROM audit_log;", -1, &stmt, nullptr);

    int64_t count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        count = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    return count;
}

// ---------------------------------------------------------------------------
// set_user_manager
// ---------------------------------------------------------------------------

void AuditLog::set_user_manager(UserManager* um) {
    impl_->um = um;
}

// ---------------------------------------------------------------------------
// Global singleton
// ---------------------------------------------------------------------------

AuditLog& audit_log() {
    static AuditLog instance;
    return instance;
}

} // namespace vxl
