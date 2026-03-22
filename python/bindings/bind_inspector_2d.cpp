// VxlStudio Python bindings -- Inspector2D, MatchResult, BlobResult, OcrResult
// SPDX-License-Identifier: MIT

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "vxl/inspector_2d.h"
#include "vxl/visual_ai.h"

namespace py = pybind11;

extern void throw_on_error(vxl::ErrorCode code, const std::string& msg);

void init_inspector_2d(py::module_& m) {
    // ---- MatchResult -------------------------------------------------------
    py::class_<vxl::MatchResult>(m, "MatchResult")
        .def(py::init<>())
        .def_readwrite("x",     &vxl::MatchResult::x)
        .def_readwrite("y",     &vxl::MatchResult::y)
        .def_readwrite("angle", &vxl::MatchResult::angle)
        .def_readwrite("score", &vxl::MatchResult::score)
        .def_readwrite("found", &vxl::MatchResult::found)
        .def("__repr__", [](const vxl::MatchResult& r) {
            return "MatchResult(x=" + std::to_string(r.x) +
                   ", y=" + std::to_string(r.y) +
                   ", score=" + std::to_string(r.score) +
                   ", found=" + (r.found ? "True" : "False") + ")";
        });

    // ---- BlobResult --------------------------------------------------------
    py::class_<vxl::BlobResult>(m, "BlobResult")
        .def(py::init<>())
        .def_readwrite("bounding_box", &vxl::BlobResult::bounding_box)
        .def_readwrite("area",         &vxl::BlobResult::area)
        .def_readwrite("circularity",  &vxl::BlobResult::circularity)
        .def_readwrite("centroid",     &vxl::BlobResult::centroid)
        .def("__repr__", [](const vxl::BlobResult& b) {
            return "BlobResult(area=" + std::to_string(b.area) +
                   ", circularity=" + std::to_string(b.circularity) + ")";
        });

    // ---- OcrResult ---------------------------------------------------------
    py::class_<vxl::OcrResult>(m, "OcrResult")
        .def(py::init<>())
        .def_readwrite("text",       &vxl::OcrResult::text)
        .def_readwrite("confidence", &vxl::OcrResult::confidence)
        .def_readwrite("region",     &vxl::OcrResult::region)
        .def("__repr__", [](const vxl::OcrResult& o) {
            return "OcrResult(text='" + o.text +
                   "', confidence=" + std::to_string(o.confidence) + ")";
        });

    // ---- Inspector2D -------------------------------------------------------
    py::class_<vxl::Inspector2D>(m, "Inspector2D")
        .def_static("template_match",
            [](const vxl::Image& image, const vxl::Image& templ,
               float min_score) {
                vxl::Result<vxl::MatchResult> r;
                {
                    py::gil_scoped_release release;
                    r = vxl::Inspector2D::template_match(image, templ, min_score);
                }
                throw_on_error(r.code, r.message);
                return r.value;
            },
            py::arg("image"), py::arg("templ"),
            py::arg("min_score") = 0.5f,
            "Find template in image, return MatchResult.")
        .def_static("blob_analysis",
            [](const vxl::Image& image, int threshold,
               int min_area, int max_area) {
                vxl::Result<std::vector<vxl::BlobResult>> r;
                {
                    py::gil_scoped_release release;
                    r = vxl::Inspector2D::blob_analysis(
                        image, threshold, min_area, max_area);
                }
                throw_on_error(r.code, r.message);
                return r.value;
            },
            py::arg("image"), py::arg("threshold") = 128,
            py::arg("min_area") = 10, py::arg("max_area") = 100000,
            "Find connected regions by threshold.")
        .def_static("edge_detect",
            [](const vxl::Image& image, double low_threshold,
               double high_threshold) {
                auto r = vxl::Inspector2D::edge_detect(
                    image, low_threshold, high_threshold);
                throw_on_error(r.code, r.message);
                return r.value;
            },
            py::arg("image"), py::arg("low_threshold") = 50.0,
            py::arg("high_threshold") = 150.0,
            "Detect edges using Canny. Returns GRAY8 edge image.")
        .def_static("ocr",
            [](const vxl::Image& image, const vxl::ROI& roi,
               const vxl::Inference* model) {
                auto r = vxl::Inspector2D::ocr(image, roi, model);
                throw_on_error(r.code, r.message);
                return r.value;
            },
            py::arg("image"), py::arg("roi"),
            py::arg("model") = nullptr,
            "Read text in ROI (stub -- needs inference model).")
        .def_static("anomaly_detect",
            [](const vxl::Image& image, const vxl::Inference* model,
               float threshold) {
                auto r = vxl::Inspector2D::anomaly_detect(
                    image, model, threshold);
                throw_on_error(r.code, r.message);
                return r.value;
            },
            py::arg("image"), py::arg("model") = nullptr,
            py::arg("threshold") = 0.5f,
            "Score how anomalous an image region is (stub -- needs inference).")
        .def_static("anomaly_detect_ai",
            [](const vxl::Image& image, vxl::IAnomalyDetector* model,
               float threshold) {
                vxl::Result<vxl::AnomalyResult> r;
                {
                    py::gil_scoped_release release;
                    r = vxl::Inspector2D::anomaly_detect_ai(image, model, threshold);
                }
                throw_on_error(r.code, r.message);
                return r.value;
            },
            py::arg("image"), py::arg("model"),
            py::arg("threshold") = 0.5f,
            "Detect anomalies using an IAnomalyDetector model.")
        .def_static("classify",
            [](const vxl::Image& image, vxl::IClassifier* model) {
                vxl::Result<vxl::ClassifyResult> r;
                {
                    py::gil_scoped_release release;
                    r = vxl::Inspector2D::classify(image, model);
                }
                throw_on_error(r.code, r.message);
                return r.value;
            },
            py::arg("image"), py::arg("model"),
            "Classify the image using an IClassifier model.")
        .def_static("detect_objects",
            [](const vxl::Image& image, vxl::IDetector* model,
               float conf, float iou) {
                vxl::Result<vxl::DetectionResult> r;
                {
                    py::gil_scoped_release release;
                    r = vxl::Inspector2D::detect_objects(image, model, conf, iou);
                }
                throw_on_error(r.code, r.message);
                return r.value;
            },
            py::arg("image"), py::arg("model"),
            py::arg("conf") = 0.25f, py::arg("iou") = 0.45f,
            "Detect objects using an IDetector model.")
        .def_static("segment",
            [](const vxl::Image& image, vxl::ISegmenter* model,
               float conf) {
                vxl::Result<vxl::SegmentResult> r;
                {
                    py::gil_scoped_release release;
                    r = vxl::Inspector2D::segment(image, model, conf);
                }
                throw_on_error(r.code, r.message);
                return r.value;
            },
            py::arg("image"), py::arg("model"),
            py::arg("conf") = 0.25f,
            "Segment objects using an ISegmenter model.");
}
