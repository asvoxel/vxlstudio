// VxlStudio Python bindings -- Plugin system
// SPDX-License-Identifier: MIT

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "vxl/plugin_api.h"

namespace py = pybind11;

// Helper declared in bind_error.cpp
extern void throw_on_error(vxl::ErrorCode code, const std::string& msg);

void init_plugin(py::module_& m) {
    // ---- PluginInfo -----------------------------------------------------------
    py::class_<vxl::PluginInfo>(m, "PluginInfo")
        .def(py::init<>())
        .def_readwrite("name",        &vxl::PluginInfo::name)
        .def_readwrite("version",     &vxl::PluginInfo::version)
        .def_readwrite("author",      &vxl::PluginInfo::author)
        .def_readwrite("type",        &vxl::PluginInfo::type)
        .def_readwrite("description", &vxl::PluginInfo::description)
        .def("__repr__", [](const vxl::PluginInfo& pi) {
            return "PluginInfo(name='" + pi.name + "', version='" + pi.version +
                   "', type='" + pi.type + "')";
        });

    // ---- PluginManager --------------------------------------------------------
    py::class_<vxl::PluginManager>(m, "PluginManager")
        .def("load", [](vxl::PluginManager& pm, const std::string& path) {
            auto r = pm.load(path);
            throw_on_error(r.code, r.message);
            return r.value;
        }, py::arg("path"),
           "Load a plugin from a shared-library path. Returns PluginInfo.")
        .def("load_directory", [](vxl::PluginManager& pm, const std::string& dir) {
            auto r = pm.load_directory(dir);
            throw_on_error(r.code, r.message);
            return r.value;
        }, py::arg("dir_path"),
           "Load all plugins from a directory. Returns number of plugins loaded.")
        .def("unload", [](vxl::PluginManager& pm, const std::string& name) {
            auto r = pm.unload(name);
            throw_on_error(r.code, r.message);
        }, py::arg("name"),
           "Unload a plugin by name.")
        .def("loaded_plugins", &vxl::PluginManager::loaded_plugins,
             "Return a list of PluginInfo for all loaded plugins.")
        .def("is_loaded", &vxl::PluginManager::is_loaded,
             py::arg("name"),
             "Return True if a plugin with the given name is loaded.");

    // ---- Global singleton -----------------------------------------------------
    m.def("plugin_manager", &vxl::plugin_manager,
          py::return_value_policy::reference,
          "Return the global PluginManager singleton.");
}
