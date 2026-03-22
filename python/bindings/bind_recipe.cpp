// VxlStudio Python bindings -- Recipe
// SPDX-License-Identifier: MIT

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "vxl/recipe.h"

namespace py = pybind11;

extern void throw_on_error(vxl::ErrorCode code, const std::string& msg);

void init_recipe(py::module_& m) {
    py::class_<vxl::Recipe>(m, "Recipe")
        .def(py::init<>())
        .def(py::init<const vxl::Recipe&>())
        .def_static("load", [](const std::string& path) {
            auto r = vxl::Recipe::load(path);
            throw_on_error(r.code, r.message);
            return r.value;
        }, py::arg("path"),
           "Load a recipe from a JSON file. Raises on error.")
        .def("save", [](const vxl::Recipe& self, const std::string& path) {
            auto r = self.save(path);
            throw_on_error(r.code, r.message);
        }, py::arg("path"),
           "Save this recipe to a JSON file.")
        .def("validate", [](const vxl::Recipe& self) {
            auto r = self.validate();
            throw_on_error(r.code, r.message);
        }, "Validate that all types, ROIs, and parameters are well-formed.")
        .def("inspect", [](const vxl::Recipe& self, const vxl::HeightMap& hmap) {
            auto r = self.inspect(hmap);
            throw_on_error(r.code, r.message);
            return r.value;
        }, py::arg("hmap"),
           "Run the recipe's configured 3D inspectors on the given height map.")
        .def("name", &vxl::Recipe::name,
             "Return the recipe name.")
        .def("type", &vxl::Recipe::type,
             "Return the recipe type.")
        .def("inspector_configs", &vxl::Recipe::inspector_configs,
             py::return_value_policy::reference_internal,
             "Return the list of InspectorConfig objects.")
        .def("__repr__", [](const vxl::Recipe& r) {
            return "Recipe(name=" + r.name() + ", type=" + r.type() + ")";
        });
}
