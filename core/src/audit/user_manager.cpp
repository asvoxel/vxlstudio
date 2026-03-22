// VxlStudio -- UserManager implementation (SQLite + CommonCrypto)
// SPDX-License-Identifier: MIT

#include "vxl/audit.h"

#include <sqlite3.h>

#include <cstring>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <stdexcept>

// SHA-256 via CommonCrypto on macOS, OpenSSL elsewhere
#ifdef __APPLE__
#include <CommonCrypto/CommonDigest.h>
#else
#include <openssl/sha.h>
#endif

// Random salt generation
#ifdef __APPLE__
#include <stdlib.h>   // arc4random_buf
#else
#include <fstream>
#endif

namespace vxl {

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

static constexpr int SALT_BYTES = 16;

static std::string bytes_to_hex(const unsigned char* data, size_t len) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (size_t i = 0; i < len; ++i)
        oss << std::setw(2) << static_cast<int>(data[i]);
    return oss.str();
}

static std::vector<unsigned char> hex_to_bytes(const std::string& hex) {
    std::vector<unsigned char> out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        unsigned int byte = 0;
        std::istringstream iss(hex.substr(i, 2));
        iss >> std::hex >> byte;
        out.push_back(static_cast<unsigned char>(byte));
    }
    return out;
}

static void generate_salt(unsigned char* buf, size_t len) {
#ifdef __APPLE__
    arc4random_buf(buf, len);
#else
    // /dev/urandom fallback for Linux / other POSIX
    std::ifstream urandom("/dev/urandom", std::ios::binary);
    if (urandom.good()) {
        urandom.read(reinterpret_cast<char*>(buf), static_cast<std::streamsize>(len));
    } else {
        // Last resort: use std::random_device (not ideal but better than nothing)
        std::srand(static_cast<unsigned>(std::time(nullptr)));
        for (size_t i = 0; i < len; ++i)
            buf[i] = static_cast<unsigned char>(std::rand() & 0xFF);
    }
#endif
}

static std::string sha256_hex(const std::string& input) {
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

    return bytes_to_hex(hash, 32);
}

// Hash a password with a given salt: SHA-256(salt_bytes + password_bytes)
static std::string salted_hash(const unsigned char* salt, size_t salt_len,
                               const std::string& password) {
    std::string combined;
    combined.append(reinterpret_cast<const char*>(salt), salt_len);
    combined.append(password);
    return sha256_hex(combined);
}

// Generate a new salted password hash in the format "salt_hex:hash_hex"
static std::string make_password_hash(const std::string& password) {
    unsigned char salt[SALT_BYTES];
    generate_salt(salt, SALT_BYTES);
    std::string salt_hex = bytes_to_hex(salt, SALT_BYTES);
    std::string hash_hex = salted_hash(salt, SALT_BYTES, password);
    return salt_hex + ":" + hash_hex;
}

// Verify a password against a stored "salt_hex:hash_hex" value.
// Also handles legacy unsalted hashes (no colon separator) for migration.
static bool verify_password_hash(const std::string& stored,
                                 const std::string& password) {
    auto colon = stored.find(':');
    if (colon == std::string::npos) {
        // Legacy unsalted hash: direct SHA-256 comparison
        return stored == sha256_hex(password);
    }
    std::string salt_hex = stored.substr(0, colon);
    std::string hash_hex = stored.substr(colon + 1);
    auto salt_bytes = hex_to_bytes(salt_hex);
    std::string computed = salted_hash(salt_bytes.data(), salt_bytes.size(), password);
    return computed == hash_hex;
}

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------

struct UserManager::Impl {
    sqlite3*   db        = nullptr;
    User       current;
    bool       logged_in = false;
    std::mutex mtx;

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

UserManager::UserManager() : impl_(std::make_unique<Impl>()) {}
UserManager::~UserManager() = default;

// ---------------------------------------------------------------------------
// init
// ---------------------------------------------------------------------------

Result<void> UserManager::init(const std::string& db_path) {
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
        "CREATE TABLE IF NOT EXISTS users ("
        "  id        INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  username  TEXT UNIQUE NOT NULL,"
        "  password_hash TEXT NOT NULL,"
        "  role      INTEGER NOT NULL DEFAULT 0,"
        "  active    INTEGER NOT NULL DEFAULT 1"
        ");");
}

// ---------------------------------------------------------------------------
// CRUD
// ---------------------------------------------------------------------------

Result<void> UserManager::create_user(const std::string& username,
                                      const std::string& password,
                                      Role role) {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    if (!impl_->db)
        return Result<void>::failure(ErrorCode::INTERNAL_ERROR, "UserManager not initialised");

    std::string hash = make_password_hash(password);

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db,
        "INSERT INTO users (username, password_hash, role) VALUES (?, ?, ?);",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK)
        return Result<void>::failure(ErrorCode::INTERNAL_ERROR, sqlite3_errmsg(impl_->db));

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, static_cast<int>(role));

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return Result<void>::failure(ErrorCode::INVALID_PARAMETER,
                                     "User already exists or DB error: " +
                                     std::string(sqlite3_errmsg(impl_->db)));
    }

    return Result<void>::success();
}

Result<void> UserManager::delete_user(const std::string& username) {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    if (!impl_->db)
        return Result<void>::failure(ErrorCode::INTERNAL_ERROR, "UserManager not initialised");

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(impl_->db,
        "DELETE FROM users WHERE username = ?;", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE)
        return Result<void>::failure(ErrorCode::INTERNAL_ERROR, sqlite3_errmsg(impl_->db));

    if (sqlite3_changes(impl_->db) == 0)
        return Result<void>::failure(ErrorCode::INVALID_PARAMETER, "User not found");

    return Result<void>::success();
}

Result<void> UserManager::change_password(const std::string& username,
                                           const std::string& new_password) {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    if (!impl_->db)
        return Result<void>::failure(ErrorCode::INTERNAL_ERROR, "UserManager not initialised");

    std::string hash = make_password_hash(new_password);

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(impl_->db,
        "UPDATE users SET password_hash = ? WHERE username = ?;",
        -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, username.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE)
        return Result<void>::failure(ErrorCode::INTERNAL_ERROR, sqlite3_errmsg(impl_->db));

    if (sqlite3_changes(impl_->db) == 0)
        return Result<void>::failure(ErrorCode::INVALID_PARAMETER, "User not found");

    return Result<void>::success();
}

Result<void> UserManager::set_role(const std::string& username, Role role) {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    if (!impl_->db)
        return Result<void>::failure(ErrorCode::INTERNAL_ERROR, "UserManager not initialised");

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(impl_->db,
        "UPDATE users SET role = ? WHERE username = ?;",
        -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, static_cast<int>(role));
    sqlite3_bind_text(stmt, 2, username.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE)
        return Result<void>::failure(ErrorCode::INTERNAL_ERROR, sqlite3_errmsg(impl_->db));

    if (sqlite3_changes(impl_->db) == 0)
        return Result<void>::failure(ErrorCode::INVALID_PARAMETER, "User not found");

    return Result<void>::success();
}

Result<std::vector<User>> UserManager::list_users() const {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    if (!impl_->db)
        return Result<std::vector<User>>::failure(ErrorCode::INTERNAL_ERROR,
                                                   "UserManager not initialised");

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db,
        "SELECT id, username, role, active FROM users ORDER BY id;",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK)
        return Result<std::vector<User>>::failure(ErrorCode::INTERNAL_ERROR,
                                                   sqlite3_errmsg(impl_->db));

    std::vector<User> users;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        User u;
        u.id       = sqlite3_column_int(stmt, 0);
        u.username = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        u.role     = static_cast<Role>(sqlite3_column_int(stmt, 2));
        u.active   = sqlite3_column_int(stmt, 3) != 0;
        users.push_back(std::move(u));
    }
    sqlite3_finalize(stmt);

    return Result<std::vector<User>>::success(std::move(users));
}

// ---------------------------------------------------------------------------
// Authentication
// ---------------------------------------------------------------------------

Result<User> UserManager::authenticate(const std::string& username,
                                        const std::string& password) {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    if (!impl_->db)
        return Result<User>::failure(ErrorCode::INTERNAL_ERROR,
                                     "UserManager not initialised");

    // Retrieve the stored password hash for this user, then verify locally.
    // This avoids embedding the hash in the SQL query (salted hashes
    // cannot be compared with '=' in SQL).
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(impl_->db,
        "SELECT id, username, password_hash, role, active FROM users "
        "WHERE username = ? AND active = 1;",
        -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);

    User user;
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        std::string stored_hash = reinterpret_cast<const char*>(
            sqlite3_column_text(stmt, 2));
        if (verify_password_hash(stored_hash, password)) {
            user.id       = sqlite3_column_int(stmt, 0);
            user.username = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            user.role     = static_cast<Role>(sqlite3_column_int(stmt, 3));
            user.active   = sqlite3_column_int(stmt, 4) != 0;
            found = true;
        }
    }
    sqlite3_finalize(stmt);

    if (!found)
        return Result<User>::failure(ErrorCode::INVALID_PARAMETER,
                                     "Invalid username or password");

    return Result<User>::success(std::move(user));
}

// ---------------------------------------------------------------------------
// Session
// ---------------------------------------------------------------------------

void UserManager::set_current_user(const User& user) {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    impl_->current   = user;
    impl_->logged_in = true;
}

const User& UserManager::current_user() const {
    return impl_->current;
}

bool UserManager::is_logged_in() const {
    return impl_->logged_in;
}

void UserManager::logout() {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    impl_->current   = User{};
    impl_->logged_in = false;
}

// ---------------------------------------------------------------------------
// Permissions
// ---------------------------------------------------------------------------

bool UserManager::has_permission(Role required) const {
    if (!impl_->logged_in) return false;
    return static_cast<int>(impl_->current.role) >= static_cast<int>(required);
}

Result<void> UserManager::require_permission(Role required) const {
    if (!has_permission(required))
        return Result<void>::failure(ErrorCode::INVALID_PARAMETER,
                                     "Insufficient permissions");
    return Result<void>::success();
}

// ---------------------------------------------------------------------------
// Global singleton
// ---------------------------------------------------------------------------

UserManager& user_manager() {
    static UserManager instance;
    return instance;
}

} // namespace vxl
