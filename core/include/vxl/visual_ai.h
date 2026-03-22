#pragma once

#include "vxl/export.h"
#include "vxl/error.h"
#include "vxl/types.h"
#include "vxl/inference.h"

#include <memory>
#include <string>
#include <vector>

namespace vxl {

// ============================================================================
// Task 1: Anomaly Detection -- "Is this normal?"
// ============================================================================
struct VXL_EXPORT AnomalyResult {
    float score;           // 0-1, higher = more anomalous
    Image heatmap;         // spatial anomaly map (optional, may be empty)
    bool is_anomalous;     // score > threshold
};

class VXL_EXPORT IAnomalyDetector {
public:
    virtual ~IAnomalyDetector() = default;
    virtual std::string name() const = 0;
    virtual Result<AnomalyResult> detect(const Image& image, float threshold = 0.5f) = 0;
};

// Built-in: Anomalib-style models (PaDiM, PatchCore, etc.)
class VXL_EXPORT AnomalibDetector : public IAnomalyDetector {
public:
    static Result<std::unique_ptr<AnomalibDetector>> create(const std::string& onnx_path);
    std::string name() const override;
    Result<AnomalyResult> detect(const Image& image, float threshold = 0.5f) override;
    ~AnomalibDetector() override;
private:
    AnomalibDetector();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ============================================================================
// Task 2: Defect Classification -- "What type of defect?"
// ============================================================================
struct VXL_EXPORT ClassifyResult {
    int class_id;
    std::string class_name;
    float confidence;       // 0-1
    std::vector<std::pair<std::string, float>> top_k;  // top-K predictions
};

class VXL_EXPORT IClassifier {
public:
    virtual ~IClassifier() = default;
    virtual std::string name() const = 0;
    virtual Result<ClassifyResult> classify(const Image& image) = 0;
    virtual std::vector<std::string> class_names() const = 0;
};

// Built-in: YOLO26-cls
class VXL_EXPORT YoloClassifier : public IClassifier {
public:
    static Result<std::unique_ptr<YoloClassifier>> create(
        const std::string& onnx_path,
        const std::vector<std::string>& class_names = {});
    std::string name() const override;
    Result<ClassifyResult> classify(const Image& image) override;
    std::vector<std::string> class_names() const override;
    ~YoloClassifier() override;
private:
    YoloClassifier();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ============================================================================
// Task 3: Defect Detection -- "Where is the defect?" (bounding boxes)
// ============================================================================
struct VXL_EXPORT Detection {
    ROI bbox;
    int class_id;
    std::string class_name;
    float confidence;
};

struct VXL_EXPORT DetectionResult {
    std::vector<Detection> detections;
    int total_count;
};

class VXL_EXPORT IDetector {
public:
    virtual ~IDetector() = default;
    virtual std::string name() const = 0;
    virtual Result<DetectionResult> detect(const Image& image, float conf_threshold = 0.25f, float iou_threshold = 0.45f) = 0;
    virtual std::vector<std::string> class_names() const = 0;
};

// Built-in: YOLO26
class VXL_EXPORT YoloDetector : public IDetector {
public:
    static Result<std::unique_ptr<YoloDetector>> create(
        const std::string& onnx_path,
        const std::vector<std::string>& class_names = {},
        int input_size = 640);
    std::string name() const override;
    Result<DetectionResult> detect(const Image& image, float conf_threshold = 0.25f, float iou_threshold = 0.45f) override;
    std::vector<std::string> class_names() const override;
    ~YoloDetector() override;
private:
    YoloDetector();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ============================================================================
// Task 4: Defect Segmentation -- "Exact defect contour?" (pixel mask)
// ============================================================================
struct VXL_EXPORT SegmentResult {
    Image mask;            // per-pixel class labels (GRAY8) or instance mask
    std::vector<Detection> instances;  // per-instance info (bbox + class + confidence)
    int num_instances;
};

class VXL_EXPORT ISegmenter {
public:
    virtual ~ISegmenter() = default;
    virtual std::string name() const = 0;
    virtual Result<SegmentResult> segment(const Image& image, float conf_threshold = 0.25f) = 0;
    virtual std::vector<std::string> class_names() const = 0;
};

// Built-in: YOLO26-seg
class VXL_EXPORT YoloSegmenter : public ISegmenter {
public:
    static Result<std::unique_ptr<YoloSegmenter>> create(
        const std::string& onnx_path,
        const std::vector<std::string>& class_names = {},
        int input_size = 640);
    std::string name() const override;
    Result<SegmentResult> segment(const Image& image, float conf_threshold = 0.25f) override;
    std::vector<std::string> class_names() const override;
    ~YoloSegmenter() override;
private:
    YoloSegmenter();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ============================================================================
// Model Registry -- central place to manage loaded models
// ============================================================================
class VXL_EXPORT ModelRegistry {
public:
    ModelRegistry();
    ~ModelRegistry();

    // Register models by name
    Result<void> register_anomaly_detector(const std::string& name, std::unique_ptr<IAnomalyDetector> model);
    Result<void> register_classifier(const std::string& name, std::unique_ptr<IClassifier> model);
    Result<void> register_detector(const std::string& name, std::unique_ptr<IDetector> model);
    Result<void> register_segmenter(const std::string& name, std::unique_ptr<ISegmenter> model);

    // Retrieve by name
    IAnomalyDetector* get_anomaly_detector(const std::string& name);
    IClassifier* get_classifier(const std::string& name);
    IDetector* get_detector(const std::string& name);
    ISegmenter* get_segmenter(const std::string& name);

    // List loaded models
    std::vector<std::string> list_anomaly_detectors() const;
    std::vector<std::string> list_classifiers() const;
    std::vector<std::string> list_detectors() const;
    std::vector<std::string> list_segmenters() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

VXL_EXPORT ModelRegistry& model_registry();

} // namespace vxl
