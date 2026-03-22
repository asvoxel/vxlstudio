#pragma once

#include <string>
#include <vector>

#include "vxl/error.h"
#include "vxl/export.h"
#include "vxl/inference.h"
#include "vxl/types.h"
#include "vxl/visual_ai.h"

namespace vxl {

// ---------------------------------------------------------------------------
// MatchResult -- template matching output
// ---------------------------------------------------------------------------
struct VXL_EXPORT MatchResult {
    float x = 0.0f;
    float y = 0.0f;
    float angle = 0.0f;    // rotation angle (degrees)
    float score = 0.0f;    // match confidence [0, 1]
    bool  found = false;
};

// ---------------------------------------------------------------------------
// BlobResult -- blob analysis output
// ---------------------------------------------------------------------------
struct VXL_EXPORT BlobResult {
    ROI     bounding_box;
    float   area = 0.0f;          // pixels
    float   circularity = 0.0f;   // 0-1
    Point2f centroid;
};

// ---------------------------------------------------------------------------
// OcrResult -- OCR output
// ---------------------------------------------------------------------------
struct VXL_EXPORT OcrResult {
    std::string text;
    float       confidence = 0.0f;
    ROI         region;
};

// ---------------------------------------------------------------------------
// Inspector2D -- 2D image inspection operators
// ---------------------------------------------------------------------------
class VXL_EXPORT Inspector2D {
public:
    // Template matching: find template in image, return position/angle/score
    static Result<MatchResult> template_match(
        const Image& image, const Image& templ,
        float min_score = 0.5f);

    // Blob analysis: find connected regions by threshold
    static Result<std::vector<BlobResult>> blob_analysis(
        const Image& image, int threshold = 128,
        int min_area = 10, int max_area = 100000);

    // Edge detection: find edges, return edge image
    static Result<Image> edge_detect(
        const Image& image, double low_threshold = 50,
        double high_threshold = 150);

    // OCR: read text in ROI (stub -- needs inference module)
    static Result<OcrResult> ocr(
        const Image& image, const ROI& roi,
        const Inference* model = nullptr);

    // Anomaly detection: score how anomalous an image region is (needs inference)
    static Result<float> anomaly_detect(
        const Image& image, const Inference* model = nullptr,
        float threshold = 0.5f);

    // --- Visual AI convenience methods (delegate to model interfaces) ---

    // Anomaly detection via IAnomalyDetector interface
    static Result<AnomalyResult> anomaly_detect_ai(
        const Image& image, IAnomalyDetector* model,
        float threshold = 0.5f);

    // Classification via IClassifier interface
    static Result<ClassifyResult> classify(
        const Image& image, IClassifier* model);

    // Object detection via IDetector interface
    static Result<DetectionResult> detect_objects(
        const Image& image, IDetector* model,
        float conf = 0.25f, float iou = 0.45f);

    // Segmentation via ISegmenter interface
    static Result<SegmentResult> segment(
        const Image& image, ISegmenter* model,
        float conf = 0.25f);
};

} // namespace vxl
