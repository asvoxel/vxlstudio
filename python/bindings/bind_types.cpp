// VxlStudio Python bindings -- core types (Image, HeightMap, PointCloud, etc.)
// SPDX-License-Identifier: MIT

#include <cstdint>
#include <cstring>
#include <stdexcept>

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

#include "vxl/types.h"
#include "vxl/error.h"

namespace py = pybind11;

// Helper declared in bind_error.cpp
extern void throw_on_error(vxl::ErrorCode code, const std::string& msg);

// ---------------------------------------------------------------------------
// Pixel-format helpers
// ---------------------------------------------------------------------------
static int pixel_format_channels(vxl::PixelFormat fmt) {
    switch (fmt) {
        case vxl::PixelFormat::GRAY8:   return 1;
        case vxl::PixelFormat::GRAY16:  return 1;
        case vxl::PixelFormat::RGB8:    return 3;
        case vxl::PixelFormat::BGR8:    return 3;
        case vxl::PixelFormat::FLOAT32: return 1;
    }
    return 1;
}

static std::string pixel_format_dtype(vxl::PixelFormat fmt) {
    switch (fmt) {
        case vxl::PixelFormat::GRAY8:   return py::format_descriptor<uint8_t>::format();
        case vxl::PixelFormat::GRAY16:  return py::format_descriptor<uint16_t>::format();
        case vxl::PixelFormat::RGB8:    return py::format_descriptor<uint8_t>::format();
        case vxl::PixelFormat::BGR8:    return py::format_descriptor<uint8_t>::format();
        case vxl::PixelFormat::FLOAT32: return py::format_descriptor<float>::format();
    }
    return py::format_descriptor<uint8_t>::format();
}

static size_t pixel_format_elem_size(vxl::PixelFormat fmt) {
    switch (fmt) {
        case vxl::PixelFormat::GRAY8:   return 1;
        case vxl::PixelFormat::GRAY16:  return 2;
        case vxl::PixelFormat::RGB8:    return 1;
        case vxl::PixelFormat::BGR8:    return 1;
        case vxl::PixelFormat::FLOAT32: return 4;
    }
    return 1;
}

// ---------------------------------------------------------------------------
// init_types -- called from module.cpp
// ---------------------------------------------------------------------------
void init_types(py::module_& m) {
    // ---- SharedBuffer (minimal read-only exposure) -------------------------
    py::class_<vxl::SharedBuffer>(m, "SharedBuffer")
        .def(py::init<>())
        .def_property_readonly("size", &vxl::SharedBuffer::size,
                               "Size in bytes of the underlying allocation.")
        .def_property_readonly("ref_count", &vxl::SharedBuffer::ref_count,
                               "Current atomic reference count.")
        .def_static("allocate", &vxl::SharedBuffer::allocate,
                    py::arg("size_bytes"),
                    "Allocate a new SharedBuffer of the given size.");

    // ---- PixelFormat -------------------------------------------------------
    py::enum_<vxl::PixelFormat>(m, "PixelFormat")
        .value("GRAY8",   vxl::PixelFormat::GRAY8)
        .value("GRAY16",  vxl::PixelFormat::GRAY16)
        .value("RGB8",    vxl::PixelFormat::RGB8)
        .value("BGR8",    vxl::PixelFormat::BGR8)
        .value("FLOAT32", vxl::PixelFormat::FLOAT32)
        .export_values();

    // ---- PointFormat -------------------------------------------------------
    py::enum_<vxl::PointFormat>(m, "PointFormat")
        .value("XYZ_FLOAT",    vxl::PointFormat::XYZ_FLOAT)
        .value("XYZRGB_FLOAT", vxl::PointFormat::XYZRGB_FLOAT)
        .export_values();

    // ---- Point2f -----------------------------------------------------------
    py::class_<vxl::Point2f>(m, "Point2f")
        .def(py::init<>())
        .def(py::init([](float x, float y) {
            vxl::Point2f p; p.x = x; p.y = y; return p;
        }), py::arg("x") = 0.0f, py::arg("y") = 0.0f)
        .def_readwrite("x", &vxl::Point2f::x)
        .def_readwrite("y", &vxl::Point2f::y)
        .def("__repr__", [](const vxl::Point2f& p) {
            return "Point2f(x=" + std::to_string(p.x) + ", y=" + std::to_string(p.y) + ")";
        });

    // ---- Image (with buffer protocol) --------------------------------------
    py::class_<vxl::Image>(m, "Image", py::buffer_protocol())
        .def(py::init<>())
        .def_readwrite("width",  &vxl::Image::width)
        .def_readwrite("height", &vxl::Image::height)
        .def_readwrite("format", &vxl::Image::format)
        .def_readwrite("stride", &vxl::Image::stride)
        .def_readwrite("buffer", &vxl::Image::buffer)
        .def_static("create", &vxl::Image::create,
                    py::arg("width"), py::arg("height"), py::arg("format"),
                    "Allocate a new Image with the given dimensions and pixel format.")
        .def_buffer([](vxl::Image& img) -> py::buffer_info {
            int ch = pixel_format_channels(img.format);
            size_t elem = pixel_format_elem_size(img.format);
            std::string fmt = pixel_format_dtype(img.format);

            if (ch == 1) {
                return py::buffer_info(
                    img.buffer.data(),
                    static_cast<ssize_t>(elem),
                    fmt,
                    2,
                    {static_cast<ssize_t>(img.height), static_cast<ssize_t>(img.width)},
                    {static_cast<ssize_t>(img.stride), static_cast<ssize_t>(elem)}
                );
            } else {
                return py::buffer_info(
                    img.buffer.data(),
                    static_cast<ssize_t>(elem),
                    fmt,
                    3,
                    {static_cast<ssize_t>(img.height),
                     static_cast<ssize_t>(img.width),
                     static_cast<ssize_t>(ch)},
                    {static_cast<ssize_t>(img.stride),
                     static_cast<ssize_t>(ch * elem),
                     static_cast<ssize_t>(elem)}
                );
            }
        })
        .def("to_numpy", [](vxl::Image& img) {
            int ch = pixel_format_channels(img.format);
            size_t elem = pixel_format_elem_size(img.format);
            std::string fmt = pixel_format_dtype(img.format);

            py::buffer_info bi;
            if (ch == 1) {
                bi = py::buffer_info(
                    img.buffer.data(), static_cast<ssize_t>(elem), fmt, 2,
                    {static_cast<ssize_t>(img.height), static_cast<ssize_t>(img.width)},
                    {static_cast<ssize_t>(img.stride), static_cast<ssize_t>(elem)}
                );
            } else {
                bi = py::buffer_info(
                    img.buffer.data(), static_cast<ssize_t>(elem), fmt, 3,
                    {static_cast<ssize_t>(img.height),
                     static_cast<ssize_t>(img.width),
                     static_cast<ssize_t>(ch)},
                    {static_cast<ssize_t>(img.stride),
                     static_cast<ssize_t>(ch * elem),
                     static_cast<ssize_t>(elem)}
                );
            }
            // Return a numpy array that shares memory with the Image buffer.
            // The py::cast keeps the Image alive while the array exists.
            return py::array(bi, py::cast(img));
        }, "Return a numpy array sharing memory with this Image (zero-copy).")
        .def_static("from_numpy", [](py::array arr) {
            py::buffer_info bi = arr.request();

            vxl::PixelFormat fmt;
            int channels = 1;

            if (bi.ndim == 2) {
                channels = 1;
            } else if (bi.ndim == 3) {
                channels = static_cast<int>(bi.shape[2]);
            } else {
                throw std::invalid_argument("Expected 2D or 3D numpy array");
            }

            if (bi.format == py::format_descriptor<uint8_t>::format()) {
                fmt = (channels == 1) ? vxl::PixelFormat::GRAY8 : vxl::PixelFormat::RGB8;
            } else if (bi.format == py::format_descriptor<uint16_t>::format()) {
                fmt = vxl::PixelFormat::GRAY16;
            } else if (bi.format == py::format_descriptor<float>::format()) {
                fmt = vxl::PixelFormat::FLOAT32;
            } else {
                throw std::invalid_argument("Unsupported numpy dtype for Image");
            }

            int h = static_cast<int>(bi.shape[0]);
            int w = static_cast<int>(bi.shape[1]);

            vxl::Image img = vxl::Image::create(w, h, fmt);
            size_t row_bytes = static_cast<size_t>(w) * channels * bi.itemsize;
            for (int r = 0; r < h; ++r) {
                std::memcpy(img.buffer.data() + r * img.stride,
                            static_cast<uint8_t*>(bi.ptr) + r * bi.strides[0],
                            row_bytes);
            }
            return img;
        }, py::arg("array"),
           "Create an Image by copying data from a numpy array.")
        .def("__repr__", [](const vxl::Image& img) {
            return "Image(" + std::to_string(img.width) + "x" +
                   std::to_string(img.height) + ", format=" +
                   std::to_string(static_cast<int>(img.format)) + ")";
        });

    // ---- HeightMap (with buffer protocol) ----------------------------------
    py::class_<vxl::HeightMap>(m, "HeightMap", py::buffer_protocol())
        .def(py::init<>())
        .def_readwrite("width",  &vxl::HeightMap::width)
        .def_readwrite("height", &vxl::HeightMap::height)
        .def_readwrite("resolution_mm", &vxl::HeightMap::resolution_mm)
        .def_readwrite("origin_x", &vxl::HeightMap::origin_x)
        .def_readwrite("origin_y", &vxl::HeightMap::origin_y)
        .def_readwrite("buffer", &vxl::HeightMap::buffer)
        .def_static("create", &vxl::HeightMap::create,
                    py::arg("width"), py::arg("height"), py::arg("resolution"),
                    "Allocate a new HeightMap (float32) with given dimensions.")
        .def_buffer([](vxl::HeightMap& hm) -> py::buffer_info {
            return py::buffer_info(
                hm.buffer.data(),
                sizeof(float),
                py::format_descriptor<float>::format(),
                2,
                {static_cast<ssize_t>(hm.height), static_cast<ssize_t>(hm.width)},
                {static_cast<ssize_t>(hm.width * sizeof(float)),
                 static_cast<ssize_t>(sizeof(float))}
            );
        })
        .def("to_numpy", [](vxl::HeightMap& hm) {
            py::buffer_info bi(
                hm.buffer.data(),
                sizeof(float),
                py::format_descriptor<float>::format(),
                2,
                {static_cast<ssize_t>(hm.height), static_cast<ssize_t>(hm.width)},
                {static_cast<ssize_t>(hm.width * sizeof(float)),
                 static_cast<ssize_t>(sizeof(float))}
            );
            return py::array_t<float>(bi, py::cast(hm));
        }, "Return a 2D float32 numpy array sharing memory with this HeightMap.")
        .def_static("from_numpy", [](py::array_t<float> arr) {
            py::buffer_info bi = arr.request();
            if (bi.ndim != 2)
                throw std::invalid_argument("Expected 2D float32 numpy array");

            int h = static_cast<int>(bi.shape[0]);
            int w = static_cast<int>(bi.shape[1]);
            vxl::HeightMap hm = vxl::HeightMap::create(w, h, 1.0f);

            auto* src = static_cast<float*>(bi.ptr);
            auto* dst = reinterpret_cast<float*>(hm.buffer.data());
            for (int r = 0; r < h; ++r) {
                std::memcpy(dst + r * w,
                            reinterpret_cast<uint8_t*>(src) + r * bi.strides[0],
                            w * sizeof(float));
            }
            return hm;
        }, py::arg("array"),
           "Create a HeightMap by copying data from a 2D float32 numpy array.")
        .def("__repr__", [](const vxl::HeightMap& hm) {
            return "HeightMap(" + std::to_string(hm.width) + "x" +
                   std::to_string(hm.height) + ", res=" +
                   std::to_string(hm.resolution_mm) + " mm)";
        });

    // ---- PointCloud --------------------------------------------------------
    py::class_<vxl::PointCloud>(m, "PointCloud")
        .def(py::init<>())
        .def_readwrite("point_count", &vxl::PointCloud::point_count)
        .def_readwrite("format",      &vxl::PointCloud::format)
        .def_readwrite("buffer",      &vxl::PointCloud::buffer)
        .def("to_numpy", [](vxl::PointCloud& pc) {
            if (pc.point_count == 0)
                return py::array_t<float>();
            size_t cols = (pc.format == vxl::PointFormat::XYZ_FLOAT) ? 3 : 4;
            size_t stride_bytes = (pc.format == vxl::PointFormat::XYZ_FLOAT) ? 12 : 16;
            py::buffer_info bi(
                pc.buffer.data(),
                sizeof(float),
                py::format_descriptor<float>::format(),
                2,
                {static_cast<ssize_t>(pc.point_count), static_cast<ssize_t>(cols)},
                {static_cast<ssize_t>(stride_bytes), static_cast<ssize_t>(sizeof(float))}
            );
            return py::array_t<float>(bi, py::cast(pc));
        }, "Return an Nx3 (or Nx4) float32 numpy array sharing memory with this PointCloud.")
        .def("__repr__", [](const vxl::PointCloud& pc) {
            return "PointCloud(n=" + std::to_string(pc.point_count) + ")";
        });

    // ---- Mesh (placeholder) ------------------------------------------------
    py::class_<vxl::Mesh>(m, "Mesh")
        .def(py::init<>());

    // ---- ROI ---------------------------------------------------------------
    py::class_<vxl::ROI>(m, "ROI")
        .def(py::init<>())
        .def(py::init([](int x, int y, int w, int h) {
            vxl::ROI r; r.x = x; r.y = y; r.w = w; r.h = h; return r;
        }), py::arg("x") = 0, py::arg("y") = 0, py::arg("w") = 0, py::arg("h") = 0)
        .def_readwrite("x", &vxl::ROI::x)
        .def_readwrite("y", &vxl::ROI::y)
        .def_readwrite("w", &vxl::ROI::w)
        .def_readwrite("h", &vxl::ROI::h)
        .def("contains",  &vxl::ROI::contains,  py::arg("px"), py::arg("py"),
             "Return True if the point (px, py) lies inside this ROI.")
        .def("intersect", &vxl::ROI::intersect, py::arg("other"),
             "Return the intersection of this ROI with another.")
        .def("area",      &vxl::ROI::area,
             "Return the area (w * h) of this ROI.")
        .def("__repr__", [](const vxl::ROI& r) {
            return "ROI(x=" + std::to_string(r.x) + ", y=" + std::to_string(r.y) +
                   ", w=" + std::to_string(r.w) + ", h=" + std::to_string(r.h) + ")";
        });

    // ---- Pose6D ------------------------------------------------------------
    py::class_<vxl::Pose6D>(m, "Pose6D")
        .def(py::init<>())
        .def_property("translation",
            [](const vxl::Pose6D& p) {
                return py::array_t<double>({3}, {sizeof(double)},
                                           p.translation, py::cast(p));
            },
            [](vxl::Pose6D& p, py::array_t<double> arr) {
                auto r = arr.unchecked<1>();
                if (r.shape(0) != 3)
                    throw std::invalid_argument("translation must have 3 elements");
                for (int i = 0; i < 3; ++i) p.translation[i] = r(i);
            })
        .def_property("rotation",
            [](const vxl::Pose6D& p) {
                return py::array_t<double>({3, 3}, {3 * sizeof(double), sizeof(double)},
                                           p.rotation, py::cast(p));
            },
            [](vxl::Pose6D& p, py::array_t<double> arr) {
                auto r = arr.unchecked<2>();
                if (r.shape(0) != 3 || r.shape(1) != 3)
                    throw std::invalid_argument("rotation must be 3x3");
                for (int i = 0; i < 3; ++i)
                    for (int j = 0; j < 3; ++j)
                        p.rotation[i * 3 + j] = r(i, j);
            })
        .def("__repr__", [](const vxl::Pose6D& p) {
            return "Pose6D(t=[" +
                   std::to_string(p.translation[0]) + ", " +
                   std::to_string(p.translation[1]) + ", " +
                   std::to_string(p.translation[2]) + "])";
        });

    // ---- DefectRegion ------------------------------------------------------
    py::class_<vxl::DefectRegion>(m, "DefectRegion")
        .def(py::init<>())
        .def_readwrite("bounding_box", &vxl::DefectRegion::bounding_box)
        .def_readwrite("area_mm2",     &vxl::DefectRegion::area_mm2)
        .def_readwrite("max_height",   &vxl::DefectRegion::max_height)
        .def_readwrite("avg_height",   &vxl::DefectRegion::avg_height)
        .def_readwrite("centroid",     &vxl::DefectRegion::centroid)
        .def_readwrite("type",         &vxl::DefectRegion::type)
        .def("__repr__", [](const vxl::DefectRegion& d) {
            return "DefectRegion(type=" + d.type +
                   ", area=" + std::to_string(d.area_mm2) + " mm^2)";
        });

    // ---- MeasureResult -----------------------------------------------------
    py::class_<vxl::MeasureResult>(m, "MeasureResult")
        .def(py::init<>())
        .def_readwrite("min_height", &vxl::MeasureResult::min_height)
        .def_readwrite("max_height", &vxl::MeasureResult::max_height)
        .def_readwrite("avg_height", &vxl::MeasureResult::avg_height)
        .def_readwrite("std_height", &vxl::MeasureResult::std_height)
        .def_readwrite("volume",     &vxl::MeasureResult::volume)
        .def("__repr__", [](const vxl::MeasureResult& mr) {
            return "MeasureResult(min=" + std::to_string(mr.min_height) +
                   ", max=" + std::to_string(mr.max_height) +
                   ", avg=" + std::to_string(mr.avg_height) + ")";
        });

    // ---- InspectionResult --------------------------------------------------
    py::class_<vxl::InspectionResult>(m, "InspectionResult")
        .def(py::init<>())
        .def_readwrite("ok",          &vxl::InspectionResult::ok)
        .def_readwrite("defects",     &vxl::InspectionResult::defects)
        .def_readwrite("measures",    &vxl::InspectionResult::measures)
        .def_readwrite("timestamp",   &vxl::InspectionResult::timestamp)
        .def_readwrite("recipe_name", &vxl::InspectionResult::recipe_name)
        .def("to_json",    &vxl::InspectionResult::to_json,
             "Serialize this result to a JSON string.")
        .def_static("from_json", &vxl::InspectionResult::from_json,
                    py::arg("json_str"),
                    "Deserialize an InspectionResult from a JSON string.")
        .def("__repr__", [](const vxl::InspectionResult& ir) {
            return std::string("InspectionResult(ok=") +
                   (ir.ok ? "True" : "False") +
                   ", defects=" + std::to_string(ir.defects.size()) +
                   ", measures=" + std::to_string(ir.measures.size()) + ")";
        });
}
