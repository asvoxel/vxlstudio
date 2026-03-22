#include "vxl/visual_ai.h"

#include <map>
#include <mutex>

namespace vxl {

// ---------------------------------------------------------------------------
// ModelRegistry::Impl
// ---------------------------------------------------------------------------
struct ModelRegistry::Impl {
    std::mutex mtx;
    std::map<std::string, std::unique_ptr<IAnomalyDetector>> anomaly_detectors;
    std::map<std::string, std::unique_ptr<IClassifier>>      classifiers;
    std::map<std::string, std::unique_ptr<IDetector>>        detectors;
    std::map<std::string, std::unique_ptr<ISegmenter>>       segmenters;
};

ModelRegistry::ModelRegistry() : impl_(std::make_unique<Impl>()) {}
ModelRegistry::~ModelRegistry() = default;

// ---------------------------------------------------------------------------
// Register
// ---------------------------------------------------------------------------
Result<void> ModelRegistry::register_anomaly_detector(
    const std::string& name, std::unique_ptr<IAnomalyDetector> model) {
    if (!model) {
        return Result<void>::failure(ErrorCode::INVALID_PARAMETER,
                                    "Cannot register null anomaly detector");
    }
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->anomaly_detectors[name] = std::move(model);
    return Result<void>::success();
}

Result<void> ModelRegistry::register_classifier(
    const std::string& name, std::unique_ptr<IClassifier> model) {
    if (!model) {
        return Result<void>::failure(ErrorCode::INVALID_PARAMETER,
                                    "Cannot register null classifier");
    }
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->classifiers[name] = std::move(model);
    return Result<void>::success();
}

Result<void> ModelRegistry::register_detector(
    const std::string& name, std::unique_ptr<IDetector> model) {
    if (!model) {
        return Result<void>::failure(ErrorCode::INVALID_PARAMETER,
                                    "Cannot register null detector");
    }
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->detectors[name] = std::move(model);
    return Result<void>::success();
}

Result<void> ModelRegistry::register_segmenter(
    const std::string& name, std::unique_ptr<ISegmenter> model) {
    if (!model) {
        return Result<void>::failure(ErrorCode::INVALID_PARAMETER,
                                    "Cannot register null segmenter");
    }
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->segmenters[name] = std::move(model);
    return Result<void>::success();
}

// ---------------------------------------------------------------------------
// Retrieve
// ---------------------------------------------------------------------------
IAnomalyDetector* ModelRegistry::get_anomaly_detector(const std::string& name) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->anomaly_detectors.find(name);
    return (it != impl_->anomaly_detectors.end()) ? it->second.get() : nullptr;
}

IClassifier* ModelRegistry::get_classifier(const std::string& name) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->classifiers.find(name);
    return (it != impl_->classifiers.end()) ? it->second.get() : nullptr;
}

IDetector* ModelRegistry::get_detector(const std::string& name) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->detectors.find(name);
    return (it != impl_->detectors.end()) ? it->second.get() : nullptr;
}

ISegmenter* ModelRegistry::get_segmenter(const std::string& name) {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    auto it = impl_->segmenters.find(name);
    return (it != impl_->segmenters.end()) ? it->second.get() : nullptr;
}

// ---------------------------------------------------------------------------
// List
// ---------------------------------------------------------------------------
std::vector<std::string> ModelRegistry::list_anomaly_detectors() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::vector<std::string> names;
    names.reserve(impl_->anomaly_detectors.size());
    for (const auto& kv : impl_->anomaly_detectors) {
        names.push_back(kv.first);
    }
    return names;
}

std::vector<std::string> ModelRegistry::list_classifiers() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::vector<std::string> names;
    names.reserve(impl_->classifiers.size());
    for (const auto& kv : impl_->classifiers) {
        names.push_back(kv.first);
    }
    return names;
}

std::vector<std::string> ModelRegistry::list_detectors() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::vector<std::string> names;
    names.reserve(impl_->detectors.size());
    for (const auto& kv : impl_->detectors) {
        names.push_back(kv.first);
    }
    return names;
}

std::vector<std::string> ModelRegistry::list_segmenters() const {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    std::vector<std::string> names;
    names.reserve(impl_->segmenters.size());
    for (const auto& kv : impl_->segmenters) {
        names.push_back(kv.first);
    }
    return names;
}

// ---------------------------------------------------------------------------
// Global singleton
// ---------------------------------------------------------------------------
ModelRegistry& model_registry() {
    static ModelRegistry instance;
    return instance;
}

} // namespace vxl
