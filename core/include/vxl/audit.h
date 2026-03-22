#pragma once

#include "vxl/export.h"
#include "vxl/error.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace vxl {

// User roles -- ordinal comparison: OPERATOR < ENGINEER < ADMIN
enum class Role : int { OPERATOR = 0, ENGINEER = 1, ADMIN = 2 };

struct VXL_EXPORT User {
    int         id       = 0;
    std::string username;
    Role        role     = Role::OPERATOR;
    bool        active   = true;
};

// Audit log entry
struct VXL_EXPORT AuditEntry {
    int64_t     id            = 0;
    int64_t     timestamp_ms  = 0;   // epoch milliseconds
    std::string username;
    std::string action;              // e.g. "recipe_change", "login", "io_write"
    std::string details;             // JSON or free-form text
    std::string checksum;            // SHA-256 for tamper detection
};

// ---------------------------------------------------------------------------
// UserManager
// ---------------------------------------------------------------------------
class VXL_EXPORT UserManager {
public:
    UserManager();
    ~UserManager();

    // Initialise with an SQLite database path
    Result<void> init(const std::string& db_path);

    // CRUD -------------------------------------------------------------------
    Result<void> create_user(const std::string& username,
                             const std::string& password,
                             Role role);
    Result<void> delete_user(const std::string& username);
    Result<void> change_password(const std::string& username,
                                 const std::string& new_password);
    Result<void> set_role(const std::string& username, Role role);
    Result<std::vector<User>> list_users() const;

    // Authentication ---------------------------------------------------------
    Result<User> authenticate(const std::string& username,
                              const std::string& password);

    // Session ----------------------------------------------------------------
    void        set_current_user(const User& user);
    const User& current_user() const;
    bool        is_logged_in() const;
    void        logout();

    // Permissions ------------------------------------------------------------
    bool         has_permission(Role required) const;
    Result<void> require_permission(Role required) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ---------------------------------------------------------------------------
// AuditLog
// ---------------------------------------------------------------------------
class VXL_EXPORT AuditLog {
public:
    AuditLog();
    ~AuditLog();

    Result<void> init(const std::string& db_path);

    // Record an event (username taken from UserManager::current_user)
    Result<void> log_event(const std::string& action,
                           const std::string& details = "");

    // Query with optional filters
    Result<std::vector<AuditEntry>> query(
        int64_t from_timestamp_ms  = 0,
        int64_t to_timestamp_ms    = 0,      // 0 => now
        const std::string& username = "",
        const std::string& action   = "",
        int limit                   = 1000) const;

    // Export to CSV
    Result<void> export_csv(const std::string& path,
                            int64_t from_timestamp_ms = 0,
                            int64_t to_timestamp_ms   = 0) const;

    // Maintenance
    Result<void> cleanup(int max_days = 90);
    int64_t      entry_count() const;

    // Link to UserManager for obtaining the current username
    void set_user_manager(UserManager* um);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Global singletons
VXL_EXPORT UserManager& user_manager();
VXL_EXPORT AuditLog&    audit_log();

} // namespace vxl
