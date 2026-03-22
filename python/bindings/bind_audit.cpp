// VxlStudio Python bindings -- audit logging & user permissions
// SPDX-License-Identifier: MIT

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "vxl/audit.h"

namespace py = pybind11;

void init_audit(py::module_& m) {
    // -----------------------------------------------------------------------
    // Role enum
    // -----------------------------------------------------------------------
    py::enum_<vxl::Role>(m, "Role")
        .value("OPERATOR", vxl::Role::OPERATOR)
        .value("ENGINEER", vxl::Role::ENGINEER)
        .value("ADMIN",    vxl::Role::ADMIN)
        .export_values();

    // -----------------------------------------------------------------------
    // User struct
    // -----------------------------------------------------------------------
    py::class_<vxl::User>(m, "User")
        .def(py::init<>())
        .def_readwrite("id",       &vxl::User::id)
        .def_readwrite("username", &vxl::User::username)
        .def_readwrite("role",     &vxl::User::role)
        .def_readwrite("active",   &vxl::User::active)
        .def("__repr__", [](const vxl::User& u) {
            return "<User '" + u.username + "' role=" +
                   std::to_string(static_cast<int>(u.role)) + ">";
        });

    // -----------------------------------------------------------------------
    // AuditEntry struct
    // -----------------------------------------------------------------------
    py::class_<vxl::AuditEntry>(m, "AuditEntry")
        .def(py::init<>())
        .def_readwrite("id",           &vxl::AuditEntry::id)
        .def_readwrite("timestamp_ms", &vxl::AuditEntry::timestamp_ms)
        .def_readwrite("username",     &vxl::AuditEntry::username)
        .def_readwrite("action",       &vxl::AuditEntry::action)
        .def_readwrite("details",      &vxl::AuditEntry::details)
        .def_readwrite("checksum",     &vxl::AuditEntry::checksum)
        .def("__repr__", [](const vxl::AuditEntry& e) {
            return "<AuditEntry id=" + std::to_string(e.id) +
                   " action='" + e.action + "' user='" + e.username + "'>";
        });

    // -----------------------------------------------------------------------
    // UserManager
    // -----------------------------------------------------------------------
    py::class_<vxl::UserManager>(m, "UserManager")
        .def(py::init<>())
        .def("init", &vxl::UserManager::init, py::arg("db_path"),
             "Initialise the user database at the given SQLite path.")
        .def("create_user", &vxl::UserManager::create_user,
             py::arg("username"), py::arg("password"), py::arg("role"),
             "Create a new user.")
        .def("delete_user", &vxl::UserManager::delete_user,
             py::arg("username"), "Delete a user by username.")
        .def("change_password", &vxl::UserManager::change_password,
             py::arg("username"), py::arg("new_password"),
             "Change a user's password.")
        .def("set_role", &vxl::UserManager::set_role,
             py::arg("username"), py::arg("role"),
             "Change a user's role.")
        .def("list_users", &vxl::UserManager::list_users,
             "List all users.")
        .def("authenticate",
             [](vxl::UserManager& self,
                const std::string& username,
                const std::string& password) {
                 py::gil_scoped_release release;
                 return self.authenticate(username, password);
             },
             py::arg("username"), py::arg("password"),
             "Authenticate and return a User on success.")
        .def("set_current_user", &vxl::UserManager::set_current_user,
             py::arg("user"), "Set the current session user.")
        .def("current_user", &vxl::UserManager::current_user,
             py::return_value_policy::reference_internal,
             "Return the current session user.")
        .def("is_logged_in", &vxl::UserManager::is_logged_in,
             "Check if any user is logged in.")
        .def("logout", &vxl::UserManager::logout,
             "Log out the current user.")
        .def("has_permission", &vxl::UserManager::has_permission,
             py::arg("required"), "Check if current user meets the role requirement.")
        .def("require_permission", &vxl::UserManager::require_permission,
             py::arg("required"),
             "Return a Result that fails if the current user lacks the role.");

    // -----------------------------------------------------------------------
    // AuditLog
    // -----------------------------------------------------------------------
    py::class_<vxl::AuditLog>(m, "AuditLog")
        .def(py::init<>())
        .def("init", &vxl::AuditLog::init, py::arg("db_path"),
             "Initialise the audit log database at the given SQLite path.")
        .def("log_event", &vxl::AuditLog::log_event,
             py::arg("action"), py::arg("details") = "",
             "Record an audit event.")
        .def("query",
             [](const vxl::AuditLog& self,
                int64_t from_ts, int64_t to_ts,
                const std::string& username,
                const std::string& action,
                int limit) {
                 py::gil_scoped_release release;
                 return self.query(from_ts, to_ts, username, action, limit);
             },
             py::arg("from_timestamp_ms") = 0,
             py::arg("to_timestamp_ms")   = 0,
             py::arg("username")          = "",
             py::arg("action")            = "",
             py::arg("limit")             = 1000,
             "Query audit entries with optional filters.")
        .def("export_csv", &vxl::AuditLog::export_csv,
             py::arg("path"),
             py::arg("from_timestamp_ms") = 0,
             py::arg("to_timestamp_ms")   = 0,
             "Export audit entries to a CSV file.")
        .def("cleanup", &vxl::AuditLog::cleanup,
             py::arg("max_days") = 90,
             "Remove entries older than max_days.")
        .def("entry_count", &vxl::AuditLog::entry_count,
             "Return the total number of audit entries.")
        .def("set_user_manager", &vxl::AuditLog::set_user_manager,
             py::arg("um"), py::keep_alive<1, 2>(),
             "Link a UserManager for automatic username lookup.");

    // -----------------------------------------------------------------------
    // Global accessors
    // -----------------------------------------------------------------------
    m.def("user_manager", &vxl::user_manager,
          py::return_value_policy::reference,
          "Return the global UserManager singleton.");
    m.def("audit_log", &vxl::audit_log,
          py::return_value_policy::reference,
          "Return the global AuditLog singleton.");
}
