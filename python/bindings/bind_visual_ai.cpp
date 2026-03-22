// VxlStudio Python bindings -- Visual AI model abstraction layer
// SPDX-License-Identifier: MIT

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "vxl/visual_ai.h"

namespace py = pybind11;

extern void throw_on_error(vxl::ErrorCode code, const std::string& msg);

// ---------------------------------------------------------------------------
// Trampoline classes for Python subclassing
// ---------------------------------------------------------------------------
class PyIAnomalyDetector : public vxl::IAnomalyDetector {
public:
    using vxl::IAnomalyDetector::IAnomalyDetector;

    std::string name() const override {
        PYBIND11_OVERRIDE_PURE(std::string, vxl::IAnomalyDetector, name);
    }

    vxl::Result<vxl::AnomalyResult> detect(const vxl::Image& image, float threshold) override {
        PYBIND11_OVERRIDE_PURE(vxl::Result<vxl::AnomalyResult>, vxl::IAnomalyDetector, detect, image, threshold);
    }
};

class PyIClassifier : public vxl::IClassifier {
public:
    using vxl::IClassifier::IClassifier;

    std::string name() const override {
        PYBIND11_OVERRIDE_PURE(std::string, vxl::IClassifier, name);
    }

    vxl::Result<vxl::ClassifyResult> classify(const vxl::Image& image) override {
        PYBIND11_OVERRIDE_PURE(vxl::Result<vxl::ClassifyResult>, vxl::IClassifier, classify, image);
    }

    std::vector<std::string> class_names() const override {
        PYBIND11_OVERRIDE_PURE(std::vector<std::string>, vxl::IClassifier, class_names);
    }
};

class PyIDetector : public vxl::IDetector {
public:
    using vxl::IDetector::IDetector;

    std::string name() const override {
        PYBIND11_OVERRIDE_PURE(std::string, vxl::IDetector, name);
    }

    vxl::Result<vxl::DetectionResult> detect(const vxl::Image& image, float conf_threshold, float iou_threshold) override {
        PYBIND11_OVERRIDE_PURE(vxl::Result<vxl::DetectionResult>, vxl::IDetector, detect, image, conf_threshold, iou_threshold);
    }

    std::vector<std::string> class_names() const override {
        PYBIND11_OVERRIDE_PURE(std::vector<std::string>, vxl::IDetector, class_names);
    }
};

class PyISegmenter : public vxl::ISegmenter {
public:
    using vxl::ISegmenter::ISegmenter;

    std::string name() const override {
        PYBIND11_OVERRIDE_PURE(std::string, vxl::ISegmenter, name);
    }

    vxl::Result<vxl::SegmentResult> segment(const vxl::Image& image, float conf_threshold) override {
        PYBIND11_OVERRIDE_PURE(vxl::Result<vxl::SegmentResult>, vxl::ISegmenter, segment, image, conf_threshold);
    }

    std::vector<std::string> class_names() const override {
        PYBIND11_OVERRIDE_PURE(std::vector<std::string>, vxl::ISegmenter, class_names);
    }
};

// ---------------------------------------------------------------------------
// init_visual_ai
// ---------------------------------------------------------------------------
void init_visual_ai(py::module_& m) {
    // ---- AnomalyResult -----------------------------------------------------
    py::class_<vxl::AnomalyResult>(m, "AnomalyResult")
        .def(py::init<>())
        .def_readwrite("score",        &vxl::AnomalyResult::score)
        .def_readwrite("heatmap",      &vxl::AnomalyResult::heatmap)
        .def_readwrite("is_anomalous", &vxl::AnomalyResult::is_anomalous)
        .def("__repr__", [](const vxl::AnomalyResult& r) {
            return "AnomalyResult(score=" + std::to_string(r.score) +
                   ", is_anomalous=" + (r.is_anomalous ? "True" : "False") + ")";
        });

    // ---- ClassifyResult ----------------------------------------------------
    py::class_<vxl::ClassifyResult>(m, "ClassifyResult")
        .def(py::init<>())
        .def_readwrite("class_id",   &vxl::ClassifyResult::class_id)
        .def_readwrite("class_name", &vxl::ClassifyResult::class_name)
        .def_readwrite("confidence", &vxl::ClassifyResult::confidence)
        .def_readwrite("top_k",      &vxl::ClassifyResult::top_k)
        .def("__repr__", [](const vxl::ClassifyResult& r) {
            return "ClassifyResult(class_id=" + std::to_string(r.class_id) +
                   ", class_name='" + r.class_name +
                   "', confidence=" + std::to_string(r.confidence) + ")";
        });

    // ---- Detection ---------------------------------------------------------
    py::class_<vxl::Detection>(m, "Detection")
        .def(py::init<>())
        .def_readwrite("bbox",       &vxl::Detection::bbox)
        .def_readwrite("class_id",   &vxl::Detection::class_id)
        .def_readwrite("class_name", &vxl::Detection::class_name)
        .def_readwrite("confidence", &vxl::Detection::confidence)
        .def("__repr__", [](const vxl::Detection& d) {
            return "Detection(class_id=" + std::to_string(d.class_id) +
                   ", class_name='" + d.class_name +
                   "', confidence=" + std::to_string(d.confidence) + ")";
        });

    // ---- DetectionResult ---------------------------------------------------
    py::class_<vxl::DetectionResult>(m, "DetectionResult")
        .def(py::init<>())
        .def_readwrite("detections",  &vxl::DetectionResult::detections)
        .def_readwrite("total_count", &vxl::DetectionResult::total_count)
        .def("__repr__", [](const vxl::DetectionResult& r) {
            return "DetectionResult(total_count=" + std::to_string(r.total_count) + ")";
        });

    // ---- SegmentResult -----------------------------------------------------
    py::class_<vxl::SegmentResult>(m, "SegmentResult")
        .def(py::init<>())
        .def_readwrite("mask",          &vxl::SegmentResult::mask)
        .def_readwrite("instances",     &vxl::SegmentResult::instances)
        .def_readwrite("num_instances", &vxl::SegmentResult::num_instances)
        .def("__repr__", [](const vxl::SegmentResult& r) {
            return "SegmentResult(num_instances=" + std::to_string(r.num_instances) + ")";
        });

    // ---- IAnomalyDetector (abstract, with trampoline) ----------------------
    py::class_<vxl::IAnomalyDetector, PyIAnomalyDetector>(m, "IAnomalyDetector")
        .def(py::init<>())
        .def("name", &vxl::IAnomalyDetector::name)
        .def("detect",
            [](vxl::IAnomalyDetector& self, const vxl::Image& image, float threshold) {
                vxl::Result<vxl::AnomalyResult> r;
                {
                    py::gil_scoped_release release;
                    r = self.detect(image, threshold);
                }
                throw_on_error(r.code, r.message);
                return r.value;
            },
            py::arg("image"), py::arg("threshold") = 0.5f,
            "Detect anomalies in the image.");

    // ---- AnomalibDetector --------------------------------------------------
    py::class_<vxl::AnomalibDetector, vxl::IAnomalyDetector>(m, "AnomalibDetector")
        .def_static("create",
            [](const std::string& onnx_path) {
                auto r = vxl::AnomalibDetector::create(onnx_path);
                throw_on_error(r.code, r.message);
                return std::move(r.value);
            },
            py::arg("onnx_path"),
            "Create an Anomalib-style anomaly detector from an ONNX model.");

    // ---- IClassifier (abstract, with trampoline) ---------------------------
    py::class_<vxl::IClassifier, PyIClassifier>(m, "IClassifier")
        .def(py::init<>())
        .def("name", &vxl::IClassifier::name)
        .def("classify",
            [](vxl::IClassifier& self, const vxl::Image& image) {
                vxl::Result<vxl::ClassifyResult> r;
                {
                    py::gil_scoped_release release;
                    r = self.classify(image);
                }
                throw_on_error(r.code, r.message);
                return r.value;
            },
            py::arg("image"),
            "Classify the image.")
        .def("class_names", &vxl::IClassifier::class_names);

    // ---- YoloClassifier ----------------------------------------------------
    py::class_<vxl::YoloClassifier, vxl::IClassifier>(m, "YoloClassifier")
        .def_static("create",
            [](const std::string& onnx_path, const std::vector<std::string>& class_names) {
                auto r = vxl::YoloClassifier::create(onnx_path, class_names);
                throw_on_error(r.code, r.message);
                return std::move(r.value);
            },
            py::arg("onnx_path"),
            py::arg("class_names") = std::vector<std::string>{},
            "Create a YOLO classifier from an ONNX model.");

    // ---- IDetector (abstract, with trampoline) -----------------------------
    py::class_<vxl::IDetector, PyIDetector>(m, "IDetector")
        .def(py::init<>())
        .def("name", &vxl::IDetector::name)
        .def("detect",
            [](vxl::IDetector& self, const vxl::Image& image, float conf, float iou) {
                vxl::Result<vxl::DetectionResult> r;
                {
                    py::gil_scoped_release release;
                    r = self.detect(image, conf, iou);
                }
                throw_on_error(r.code, r.message);
                return r.value;
            },
            py::arg("image"),
            py::arg("conf_threshold") = 0.25f,
            py::arg("iou_threshold") = 0.45f,
            "Detect objects in the image.")
        .def("class_names", &vxl::IDetector::class_names);

    // ---- YoloDetector ------------------------------------------------------
    py::class_<vxl::YoloDetector, vxl::IDetector>(m, "YoloDetector")
        .def_static("create",
            [](const std::string& onnx_path, const std::vector<std::string>& class_names, int input_size) {
                auto r = vxl::YoloDetector::create(onnx_path, class_names, input_size);
                throw_on_error(r.code, r.message);
                return std::move(r.value);
            },
            py::arg("onnx_path"),
            py::arg("class_names") = std::vector<std::string>{},
            py::arg("input_size") = 640,
            "Create a YOLO detector from an ONNX model.");

    // ---- ISegmenter (abstract, with trampoline) ----------------------------
    py::class_<vxl::ISegmenter, PyISegmenter>(m, "ISegmenter")
        .def(py::init<>())
        .def("name", &vxl::ISegmenter::name)
        .def("segment",
            [](vxl::ISegmenter& self, const vxl::Image& image, float conf) {
                vxl::Result<vxl::SegmentResult> r;
                {
                    py::gil_scoped_release release;
                    r = self.segment(image, conf);
                }
                throw_on_error(r.code, r.message);
                return r.value;
            },
            py::arg("image"),
            py::arg("conf_threshold") = 0.25f,
            "Segment objects in the image.")
        .def("class_names", &vxl::ISegmenter::class_names);

    // ---- YoloSegmenter -----------------------------------------------------
    py::class_<vxl::YoloSegmenter, vxl::ISegmenter>(m, "YoloSegmenter")
        .def_static("create",
            [](const std::string& onnx_path, const std::vector<std::string>& class_names, int input_size) {
                auto r = vxl::YoloSegmenter::create(onnx_path, class_names, input_size);
                throw_on_error(r.code, r.message);
                return std::move(r.value);
            },
            py::arg("onnx_path"),
            py::arg("class_names") = std::vector<std::string>{},
            py::arg("input_size") = 640,
            "Create a YOLO segmenter from an ONNX model.");

    // ---- ModelRegistry -----------------------------------------------------
    py::class_<vxl::ModelRegistry>(m, "ModelRegistry")
        .def("register_anomaly_detector",
            [](vxl::ModelRegistry& self, const std::string& name,
               std::unique_ptr<vxl::IAnomalyDetector> model) {
                auto r = self.register_anomaly_detector(name, std::move(model));
                throw_on_error(r.code, r.message);
            },
            py::arg("name"), py::arg("model"),
            "Register an anomaly detector by name.")
        .def("register_classifier",
            [](vxl::ModelRegistry& self, const std::string& name,
               std::unique_ptr<vxl::IClassifier> model) {
                auto r = self.register_classifier(name, std::move(model));
                throw_on_error(r.code, r.message);
            },
            py::arg("name"), py::arg("model"),
            "Register a classifier by name.")
        .def("register_detector",
            [](vxl::ModelRegistry& self, const std::string& name,
               std::unique_ptr<vxl::IDetector> model) {
                auto r = self.register_detector(name, std::move(model));
                throw_on_error(r.code, r.message);
            },
            py::arg("name"), py::arg("model"),
            "Register a detector by name.")
        .def("register_segmenter",
            [](vxl::ModelRegistry& self, const std::string& name,
               std::unique_ptr<vxl::ISegmenter> model) {
                auto r = self.register_segmenter(name, std::move(model));
                throw_on_error(r.code, r.message);
            },
            py::arg("name"), py::arg("model"),
            "Register a segmenter by name.")
        .def("get_anomaly_detector", &vxl::ModelRegistry::get_anomaly_detector,
             py::arg("name"), py::return_value_policy::reference,
             "Get an anomaly detector by name (returns None if not found).")
        .def("get_classifier", &vxl::ModelRegistry::get_classifier,
             py::arg("name"), py::return_value_policy::reference,
             "Get a classifier by name (returns None if not found).")
        .def("get_detector", &vxl::ModelRegistry::get_detector,
             py::arg("name"), py::return_value_policy::reference,
             "Get a detector by name (returns None if not found).")
        .def("get_segmenter", &vxl::ModelRegistry::get_segmenter,
             py::arg("name"), py::return_value_policy::reference,
             "Get a segmenter by name (returns None if not found).")
        .def("list_anomaly_detectors", &vxl::ModelRegistry::list_anomaly_detectors)
        .def("list_classifiers",       &vxl::ModelRegistry::list_classifiers)
        .def("list_detectors",         &vxl::ModelRegistry::list_detectors)
        .def("list_segmenters",        &vxl::ModelRegistry::list_segmenters);

    // ---- Global model_registry() -------------------------------------------
    m.def("model_registry", &vxl::model_registry,
          py::return_value_policy::reference,
          "Get the global ModelRegistry singleton.");
}
