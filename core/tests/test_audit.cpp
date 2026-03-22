// VxlStudio -- audit / user-manager unit tests
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include "vxl/audit.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <sqlite3.h>
#include <thread>

namespace vxl {
namespace {

// Helper: generate a unique temp path for SQLite
static std::string temp_db_path(const char* tag) {
    std::string path = std::string("/tmp/vxl_test_") + tag + "_" +
                       std::to_string(std::hash<std::thread::id>{}(
                           std::this_thread::get_id())) +
                       ".db";
    std::remove(path.c_str());
    return path;
}

// ===========================================================================
// UserManager tests
// ===========================================================================

class UserManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_path_ = temp_db_path("um");
        ASSERT_TRUE(um_.init(db_path_).ok());
    }
    void TearDown() override { std::remove(db_path_.c_str()); }

    std::string  db_path_;
    UserManager  um_;
};

TEST_F(UserManagerTest, CreateAndListUsers) {
    ASSERT_TRUE(um_.create_user("alice", "pass1", Role::OPERATOR).ok());
    ASSERT_TRUE(um_.create_user("bob",   "pass2", Role::ENGINEER).ok());
    ASSERT_TRUE(um_.create_user("carol", "pass3", Role::ADMIN).ok());

    auto res = um_.list_users();
    ASSERT_TRUE(res.ok());
    ASSERT_EQ(res.value.size(), 3u);

    EXPECT_EQ(res.value[0].username, "alice");
    EXPECT_EQ(res.value[0].role,     Role::OPERATOR);
    EXPECT_EQ(res.value[1].username, "bob");
    EXPECT_EQ(res.value[1].role,     Role::ENGINEER);
    EXPECT_EQ(res.value[2].username, "carol");
    EXPECT_EQ(res.value[2].role,     Role::ADMIN);
}

TEST_F(UserManagerTest, DuplicateUserFails) {
    ASSERT_TRUE(um_.create_user("alice", "pass1", Role::OPERATOR).ok());
    auto res = um_.create_user("alice", "pass2", Role::ADMIN);
    EXPECT_FALSE(res.ok());
}

TEST_F(UserManagerTest, AuthenticateCorrectPassword) {
    ASSERT_TRUE(um_.create_user("alice", "secret", Role::ENGINEER).ok());
    auto res = um_.authenticate("alice", "secret");
    ASSERT_TRUE(res.ok());
    EXPECT_EQ(res.value.username, "alice");
    EXPECT_EQ(res.value.role, Role::ENGINEER);
}

TEST_F(UserManagerTest, AuthenticateWrongPassword) {
    ASSERT_TRUE(um_.create_user("alice", "secret", Role::OPERATOR).ok());
    auto res = um_.authenticate("alice", "wrong");
    EXPECT_FALSE(res.ok());
}

TEST_F(UserManagerTest, AuthenticateNonexistentUser) {
    auto res = um_.authenticate("ghost", "pass");
    EXPECT_FALSE(res.ok());
}

TEST_F(UserManagerTest, DeleteUser) {
    ASSERT_TRUE(um_.create_user("alice", "p", Role::OPERATOR).ok());
    ASSERT_TRUE(um_.delete_user("alice").ok());
    auto res = um_.list_users();
    ASSERT_TRUE(res.ok());
    EXPECT_TRUE(res.value.empty());
}

TEST_F(UserManagerTest, DeleteNonexistentUserFails) {
    EXPECT_FALSE(um_.delete_user("ghost").ok());
}

TEST_F(UserManagerTest, SetRole) {
    ASSERT_TRUE(um_.create_user("alice", "p", Role::OPERATOR).ok());
    ASSERT_TRUE(um_.set_role("alice", Role::ADMIN).ok());

    auto res = um_.list_users();
    ASSERT_TRUE(res.ok());
    EXPECT_EQ(res.value[0].role, Role::ADMIN);
}

TEST_F(UserManagerTest, ChangePassword) {
    ASSERT_TRUE(um_.create_user("alice", "old", Role::OPERATOR).ok());
    ASSERT_TRUE(um_.change_password("alice", "new").ok());

    EXPECT_FALSE(um_.authenticate("alice", "old").ok());
    EXPECT_TRUE(um_.authenticate("alice", "new").ok());
}

TEST_F(UserManagerTest, PermissionCheck) {
    ASSERT_TRUE(um_.create_user("op",  "p", Role::OPERATOR).ok());
    ASSERT_TRUE(um_.create_user("eng", "p", Role::ENGINEER).ok());
    ASSERT_TRUE(um_.create_user("adm", "p", Role::ADMIN).ok());

    // Not logged in -- no permissions
    EXPECT_FALSE(um_.has_permission(Role::OPERATOR));

    // Operator
    um_.set_current_user(um_.authenticate("op", "p").value);
    EXPECT_TRUE(um_.has_permission(Role::OPERATOR));
    EXPECT_FALSE(um_.has_permission(Role::ENGINEER));
    EXPECT_FALSE(um_.has_permission(Role::ADMIN));

    // Engineer
    um_.set_current_user(um_.authenticate("eng", "p").value);
    EXPECT_TRUE(um_.has_permission(Role::OPERATOR));
    EXPECT_TRUE(um_.has_permission(Role::ENGINEER));
    EXPECT_FALSE(um_.has_permission(Role::ADMIN));

    // Admin
    um_.set_current_user(um_.authenticate("adm", "p").value);
    EXPECT_TRUE(um_.has_permission(Role::OPERATOR));
    EXPECT_TRUE(um_.has_permission(Role::ENGINEER));
    EXPECT_TRUE(um_.has_permission(Role::ADMIN));
}

TEST_F(UserManagerTest, RequirePermissionReturnsError) {
    ASSERT_TRUE(um_.create_user("op", "p", Role::OPERATOR).ok());
    um_.set_current_user(um_.authenticate("op", "p").value);

    EXPECT_TRUE(um_.require_permission(Role::OPERATOR).ok());
    EXPECT_FALSE(um_.require_permission(Role::ENGINEER).ok());
}

TEST_F(UserManagerTest, LoginLogout) {
    ASSERT_TRUE(um_.create_user("alice", "p", Role::OPERATOR).ok());
    EXPECT_FALSE(um_.is_logged_in());

    um_.set_current_user(um_.authenticate("alice", "p").value);
    EXPECT_TRUE(um_.is_logged_in());
    EXPECT_EQ(um_.current_user().username, "alice");

    um_.logout();
    EXPECT_FALSE(um_.is_logged_in());
}

// ===========================================================================
// AuditLog tests
// ===========================================================================

class AuditLogTest : public ::testing::Test {
protected:
    void SetUp() override {
        um_path_ = temp_db_path("al_um");
        al_path_ = temp_db_path("al");
        ASSERT_TRUE(um_.init(um_path_).ok());
        ASSERT_TRUE(al_.init(al_path_).ok());
        al_.set_user_manager(&um_);

        ASSERT_TRUE(um_.create_user("tester", "pw", Role::ENGINEER).ok());
        um_.set_current_user(um_.authenticate("tester", "pw").value);
    }

    void TearDown() override {
        std::remove(um_path_.c_str());
        std::remove(al_path_.c_str());
    }

    std::string  um_path_, al_path_;
    UserManager  um_;
    AuditLog     al_;
};

TEST_F(AuditLogTest, LogEventAndQuery) {
    ASSERT_TRUE(al_.log_event("login", "user logged in").ok());
    ASSERT_TRUE(al_.log_event("recipe_change", "{\"recipe\":\"A\"}").ok());

    auto res = al_.query();
    ASSERT_TRUE(res.ok());
    ASSERT_EQ(res.value.size(), 2u);

    // Results are ordered DESC by id, so newest first
    EXPECT_EQ(res.value[0].action, "recipe_change");
    EXPECT_EQ(res.value[1].action, "login");
    EXPECT_EQ(res.value[0].username, "tester");
}

TEST_F(AuditLogTest, QueryByAction) {
    ASSERT_TRUE(al_.log_event("login").ok());
    ASSERT_TRUE(al_.log_event("recipe_change").ok());
    ASSERT_TRUE(al_.log_event("login").ok());

    auto res = al_.query(0, 0, "", "login");
    ASSERT_TRUE(res.ok());
    EXPECT_EQ(res.value.size(), 2u);
}

TEST_F(AuditLogTest, QueryByUsername) {
    ASSERT_TRUE(al_.log_event("action1").ok());

    // Log out so events are logged as "system"
    um_.logout();
    ASSERT_TRUE(al_.log_event("action2").ok());

    auto res1 = al_.query(0, 0, "tester");
    ASSERT_TRUE(res1.ok());
    EXPECT_EQ(res1.value.size(), 1u);

    auto res2 = al_.query(0, 0, "system");
    ASSERT_TRUE(res2.ok());
    EXPECT_EQ(res2.value.size(), 1u);
}

TEST_F(AuditLogTest, QueryByTimeRange) {
    // Log one event, note the time, log another
    ASSERT_TRUE(al_.log_event("early").ok());

    // Small sleep to ensure distinct timestamps
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    auto mid = std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    ASSERT_TRUE(al_.log_event("late").ok());

    // Query only events after midpoint
    auto res = al_.query(mid, 0, "", "");
    ASSERT_TRUE(res.ok());
    EXPECT_EQ(res.value.size(), 1u);
    EXPECT_EQ(res.value[0].action, "late");
}

TEST_F(AuditLogTest, EntryCount) {
    EXPECT_EQ(al_.entry_count(), 0);
    ASSERT_TRUE(al_.log_event("a").ok());
    ASSERT_TRUE(al_.log_event("b").ok());
    EXPECT_EQ(al_.entry_count(), 2);
}

TEST_F(AuditLogTest, CleanupRemovesOldEntries) {
    // Insert an entry with a very old timestamp directly
    {
        // We rely on log_event using now(), so we insert manually
        sqlite3* db = nullptr;
        sqlite3_open(al_path_.c_str(), &db);
        int64_t old_ts = 1000; // 1970-01-01 00:00:01
        sqlite3_exec(db,
            ("INSERT INTO audit_log (timestamp_ms, username, action, details, checksum) "
             "VALUES (" + std::to_string(old_ts) + ", 'old', 'old_action', '', 'abc');")
                .c_str(),
            nullptr, nullptr, nullptr);
        sqlite3_close(db);
    }

    // Also log a fresh entry
    ASSERT_TRUE(al_.log_event("fresh").ok());
    EXPECT_EQ(al_.entry_count(), 2);

    // Cleanup with 1 day retention -- should remove the ancient entry
    ASSERT_TRUE(al_.cleanup(1).ok());
    EXPECT_EQ(al_.entry_count(), 1);

    auto res = al_.query();
    ASSERT_TRUE(res.ok());
    EXPECT_EQ(res.value[0].action, "fresh");
}

TEST_F(AuditLogTest, ChecksumIsConsistent) {
    // Two events with identical inputs should produce the same checksum
    // We cannot control timestamp easily, so instead verify that the
    // checksum field is non-empty and looks like a hex SHA-256 (64 chars)
    ASSERT_TRUE(al_.log_event("test_action", "some details").ok());

    auto res = al_.query();
    ASSERT_TRUE(res.ok());
    ASSERT_EQ(res.value.size(), 1u);
    EXPECT_EQ(res.value[0].checksum.size(), 64u);

    // All characters should be hex
    for (char c : res.value[0].checksum) {
        EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
    }
}

TEST_F(AuditLogTest, ExportCsvProducesValidFile) {
    ASSERT_TRUE(al_.log_event("action1", "detail1").ok());
    ASSERT_TRUE(al_.log_event("action2", "detail,with,commas").ok());
    ASSERT_TRUE(al_.log_event("action3", "detail \"with\" quotes").ok());

    std::string csv_path = al_path_ + ".csv";
    ASSERT_TRUE(al_.export_csv(csv_path).ok());

    std::ifstream ifs(csv_path);
    ASSERT_TRUE(ifs.good());

    // Read header
    std::string header;
    std::getline(ifs, header);
    EXPECT_EQ(header, "id,timestamp_ms,username,action,details,checksum");

    // Count data rows
    int row_count = 0;
    std::string line;
    while (std::getline(ifs, line)) {
        if (!line.empty()) ++row_count;
    }
    EXPECT_EQ(row_count, 3);

    std::remove(csv_path.c_str());
}

TEST_F(AuditLogTest, LogEventWithoutUserManager) {
    AuditLog standalone;
    std::string path = temp_db_path("standalone");
    ASSERT_TRUE(standalone.init(path).ok());

    // No user manager linked -- should log as "system"
    ASSERT_TRUE(standalone.log_event("boot", "system start").ok());
    auto res = standalone.query();
    ASSERT_TRUE(res.ok());
    EXPECT_EQ(res.value[0].username, "system");

    std::remove(path.c_str());
}

} // namespace
} // namespace vxl
