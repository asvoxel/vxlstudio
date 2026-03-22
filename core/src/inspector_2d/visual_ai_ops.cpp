#include "vxl/inspector_2d.h"

namespace vxl {

Result<AnomalyResult> Inspector2D::anomaly_detect_ai(
    const Image& image, IAnomalyDetector* model, float threshold)
{
    if (!model) {
        return Result<AnomalyResult>::failure(
            ErrorCode::MODEL_LOAD_FAILED,
            "Anomaly detection requires a model (IAnomalyDetector is null)");
    }
    return model->detect(image, threshold);
}

Result<ClassifyResult> Inspector2D::classify(
    const Image& image, IClassifier* model)
{
    if (!model) {
        return Result<ClassifyResult>::failure(
            ErrorCode::MODEL_LOAD_FAILED,
            "Classification requires a model (IClassifier is null)");
    }
    return model->classify(image);
}

Result<DetectionResult> Inspector2D::detect_objects(
    const Image& image, IDetector* model, float conf, float iou)
{
    if (!model) {
        return Result<DetectionResult>::failure(
            ErrorCode::MODEL_LOAD_FAILED,
            "Object detection requires a model (IDetector is null)");
    }
    return model->detect(image, conf, iou);
}

Result<SegmentResult> Inspector2D::segment(
    const Image& image, ISegmenter* model, float conf)
{
    if (!model) {
        return Result<SegmentResult>::failure(
            ErrorCode::MODEL_LOAD_FAILED,
            "Segmentation requires a model (ISegmenter is null)");
    }
    return model->segment(image, conf);
}

} // namespace vxl
