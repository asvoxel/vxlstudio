#include <gtest/gtest.h>

#include "vxl/visual_ai.h"
#include "vxl/types.h"

// ===========================================================================
// ModelRegistry tests
// ===========================================================================

// Helper: a trivial IAnomalyDetector for registry tests (no ONNX needed)
class DummyAnomalyDetector : public vxl::IAnomalyDetector {
public:
    std::string name() const override { return "DummyAnomaly"; }
    vxl::Result<vxl::AnomalyResult> detect(const vxl::Image& /*image*/, float threshold) override {
        vxl::AnomalyResult r;
        r.score = 0.42f;
        r.is_anomalous = (r.score > threshold);
        return vxl::Result<vxl::AnomalyResult>::success(std::move(r));
    }
};

class DummyClassifier : public vxl::IClassifier {
public:
    std::string name() const override { return "DummyClassifier"; }
    vxl::Result<vxl::ClassifyResult> classify(const vxl::Image& /*image*/) override {
        vxl::ClassifyResult r;
        r.class_id = 0;
        r.class_name = "ok";
        r.confidence = 0.99f;
        return vxl::Result<vxl::ClassifyResult>::success(std::move(r));
    }
    std::vector<std::string> class_names() const override { return {"ok", "ng"}; }
};

class DummyDetector : public vxl::IDetector {
public:
    std::string name() const override { return "DummyDetector"; }
    vxl::Result<vxl::DetectionResult> detect(const vxl::Image& /*image*/, float /*conf*/, float /*iou*/) override {
        vxl::DetectionResult r;
        r.total_count = 0;
        return vxl::Result<vxl::DetectionResult>::success(std::move(r));
    }
    std::vector<std::string> class_names() const override { return {"defect"}; }
};

class DummySegmenter : public vxl::ISegmenter {
public:
    std::string name() const override { return "DummySegmenter"; }
    vxl::Result<vxl::SegmentResult> segment(const vxl::Image& /*image*/, float /*conf*/) override {
        vxl::SegmentResult r;
        r.num_instances = 0;
        return vxl::Result<vxl::SegmentResult>::success(std::move(r));
    }
    std::vector<std::string> class_names() const override { return {"crack"}; }
};

// ---------------------------------------------------------------------------
// Register and retrieve all 4 types
// ---------------------------------------------------------------------------
TEST(ModelRegistry, RegisterAndRetrieveAll) {
    vxl::ModelRegistry reg;

    auto ad = std::make_unique<DummyAnomalyDetector>();
    auto cl = std::make_unique<DummyClassifier>();
    auto dt = std::make_unique<DummyDetector>();
    auto sg = std::make_unique<DummySegmenter>();

    EXPECT_TRUE(reg.register_anomaly_detector("ad1", std::move(ad)).ok());
    EXPECT_TRUE(reg.register_classifier("cl1", std::move(cl)).ok());
    EXPECT_TRUE(reg.register_detector("dt1", std::move(dt)).ok());
    EXPECT_TRUE(reg.register_segmenter("sg1", std::move(sg)).ok());

    EXPECT_NE(reg.get_anomaly_detector("ad1"), nullptr);
    EXPECT_NE(reg.get_classifier("cl1"), nullptr);
    EXPECT_NE(reg.get_detector("dt1"), nullptr);
    EXPECT_NE(reg.get_segmenter("sg1"), nullptr);

    EXPECT_EQ(reg.get_anomaly_detector("ad1")->name(), "DummyAnomaly");
    EXPECT_EQ(reg.get_classifier("cl1")->name(), "DummyClassifier");
    EXPECT_EQ(reg.get_detector("dt1")->name(), "DummyDetector");
    EXPECT_EQ(reg.get_segmenter("sg1")->name(), "DummySegmenter");
}

// ---------------------------------------------------------------------------
// List models
// ---------------------------------------------------------------------------
TEST(ModelRegistry, ListModels) {
    vxl::ModelRegistry reg;

    EXPECT_TRUE(reg.list_anomaly_detectors().empty());
    EXPECT_TRUE(reg.list_classifiers().empty());
    EXPECT_TRUE(reg.list_detectors().empty());
    EXPECT_TRUE(reg.list_segmenters().empty());

    reg.register_anomaly_detector("a", std::make_unique<DummyAnomalyDetector>());
    reg.register_anomaly_detector("b", std::make_unique<DummyAnomalyDetector>());
    reg.register_classifier("c1", std::make_unique<DummyClassifier>());

    auto ad_list = reg.list_anomaly_detectors();
    EXPECT_EQ(ad_list.size(), 2u);

    auto cl_list = reg.list_classifiers();
    EXPECT_EQ(cl_list.size(), 1u);
    EXPECT_EQ(cl_list[0], "c1");
}

// ---------------------------------------------------------------------------
// Get nonexistent returns nullptr
// ---------------------------------------------------------------------------
TEST(ModelRegistry, GetNonexistent) {
    vxl::ModelRegistry reg;
    EXPECT_EQ(reg.get_anomaly_detector("nope"), nullptr);
    EXPECT_EQ(reg.get_classifier("nope"), nullptr);
    EXPECT_EQ(reg.get_detector("nope"), nullptr);
    EXPECT_EQ(reg.get_segmenter("nope"), nullptr);
}

// ---------------------------------------------------------------------------
// Register null model fails
// ---------------------------------------------------------------------------
TEST(ModelRegistry, RegisterNullFails) {
    vxl::ModelRegistry reg;
    auto r = reg.register_anomaly_detector("x", nullptr);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.code, vxl::ErrorCode::INVALID_PARAMETER);
}

// ---------------------------------------------------------------------------
// Global model_registry() returns the same instance
// ---------------------------------------------------------------------------
TEST(ModelRegistry, GlobalSingleton) {
    auto& r1 = vxl::model_registry();
    auto& r2 = vxl::model_registry();
    EXPECT_EQ(&r1, &r2);
}

// ---------------------------------------------------------------------------
// DummyAnomalyDetector produces expected results
// ---------------------------------------------------------------------------
TEST(ModelRegistry, DummyAnomalyDetectorWorks) {
    DummyAnomalyDetector det;
    vxl::Image img = vxl::Image::create(64, 64, vxl::PixelFormat::RGB8);
    auto res = det.detect(img, 0.5f);
    EXPECT_TRUE(res.ok());
    EXPECT_FLOAT_EQ(res.value.score, 0.42f);
    EXPECT_FALSE(res.value.is_anomalous);  // 0.42 < 0.5

    auto res2 = det.detect(img, 0.3f);
    EXPECT_TRUE(res2.ok());
    EXPECT_TRUE(res2.value.is_anomalous);  // 0.42 > 0.3
}

// ===========================================================================
// Factory error-path tests (nonexistent model files)
// ===========================================================================

TEST(VisualAI, AnomalibCreateNonexistent) {
    auto r = vxl::AnomalibDetector::create("/tmp/nonexistent_anomalib_12345.onnx");
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.code, vxl::ErrorCode::FILE_NOT_FOUND);
}

TEST(VisualAI, AnomalibCreateEmptyPath) {
    auto r = vxl::AnomalibDetector::create("");
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.code, vxl::ErrorCode::INVALID_PARAMETER);
}

TEST(VisualAI, YoloClassifierCreateNonexistent) {
    auto r = vxl::YoloClassifier::create("/tmp/nonexistent_yolo_cls_12345.onnx");
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.code, vxl::ErrorCode::FILE_NOT_FOUND);
}

TEST(VisualAI, YoloDetectorCreateNonexistent) {
    auto r = vxl::YoloDetector::create("/tmp/nonexistent_yolo_det_12345.onnx");
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.code, vxl::ErrorCode::FILE_NOT_FOUND);
}

TEST(VisualAI, YoloSegmenterCreateNonexistent) {
    auto r = vxl::YoloSegmenter::create("/tmp/nonexistent_yolo_seg_12345.onnx");
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.code, vxl::ErrorCode::FILE_NOT_FOUND);
}

TEST(VisualAI, YoloDetectorCreateEmptyPath) {
    auto r = vxl::YoloDetector::create("");
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.code, vxl::ErrorCode::INVALID_PARAMETER);
}

TEST(VisualAI, YoloSegmenterCreateEmptyPath) {
    auto r = vxl::YoloSegmenter::create("");
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.code, vxl::ErrorCode::INVALID_PARAMETER);
}

TEST(VisualAI, YoloClassifierCreateEmptyPath) {
    auto r = vxl::YoloClassifier::create("");
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.code, vxl::ErrorCode::INVALID_PARAMETER);
}
