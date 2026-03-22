#include "vxl/types.h"

#include <algorithm>
#include <cstdlib>
#include <new>
#include <stdexcept>

#include <opencv2/core.hpp>
#include <nlohmann/json.hpp>

namespace vxl {

// ===========================================================================
// SharedBuffer
// ===========================================================================

SharedBuffer SharedBuffer::allocate(size_t size_bytes) {
    SharedBuffer buf;
    buf.ctrl_ = new ControlBlock();
    buf.data_ = static_cast<uint8_t*>(std::malloc(size_bytes));
    if (!buf.data_) {
        delete buf.ctrl_;
        throw std::bad_alloc();
    }
    buf.size_ = size_bytes;
    std::memset(buf.data_, 0, size_bytes);
    return buf;
}

SharedBuffer::~SharedBuffer() {
    release();
}

SharedBuffer::SharedBuffer(const SharedBuffer& other)
    : data_(other.data_), size_(other.size_), ctrl_(other.ctrl_) {
    retain();
}

SharedBuffer& SharedBuffer::operator=(const SharedBuffer& other) {
    if (this != &other) {
        release();
        data_ = other.data_;
        size_ = other.size_;
        ctrl_ = other.ctrl_;
        retain();
    }
    return *this;
}

SharedBuffer::SharedBuffer(SharedBuffer&& other) noexcept
    : data_(other.data_), size_(other.size_), ctrl_(other.ctrl_) {
    other.data_ = nullptr;
    other.size_ = 0;
    other.ctrl_ = nullptr;
}

SharedBuffer& SharedBuffer::operator=(SharedBuffer&& other) noexcept {
    if (this != &other) {
        release();
        data_ = other.data_;
        size_ = other.size_;
        ctrl_ = other.ctrl_;
        other.data_ = nullptr;
        other.size_ = 0;
        other.ctrl_ = nullptr;
    }
    return *this;
}

int SharedBuffer::ref_count() const {
    return ctrl_ ? ctrl_->refs.load(std::memory_order_relaxed) : 0;
}

void SharedBuffer::retain() {
    if (ctrl_) {
        ctrl_->refs.fetch_add(1, std::memory_order_relaxed);
    }
}

void SharedBuffer::release() {
    if (ctrl_) {
        if (ctrl_->refs.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            std::free(data_);
            delete ctrl_;
        }
    }
    data_ = nullptr;
    size_ = 0;
    ctrl_ = nullptr;
}

// ===========================================================================
// Image helpers
// ===========================================================================

static int pixel_size(PixelFormat fmt) {
    switch (fmt) {
        case PixelFormat::GRAY8:   return 1;
        case PixelFormat::GRAY16:  return 2;
        case PixelFormat::RGB8:    return 3;
        case PixelFormat::BGR8:    return 3;
        case PixelFormat::FLOAT32: return 4;
    }
    return 1;
}

static int to_cv_type(PixelFormat fmt) {
    switch (fmt) {
        case PixelFormat::GRAY8:   return CV_8UC1;
        case PixelFormat::GRAY16:  return CV_16UC1;
        case PixelFormat::RGB8:    return CV_8UC3;
        case PixelFormat::BGR8:    return CV_8UC3;
        case PixelFormat::FLOAT32: return CV_32FC1;
    }
    return CV_8UC1;
}

static PixelFormat from_cv_type(int cv_type) {
    switch (cv_type) {
        case CV_8UC1:  return PixelFormat::GRAY8;
        case CV_16UC1: return PixelFormat::GRAY16;
        case CV_8UC3:  return PixelFormat::BGR8;   // OpenCV default is BGR
        case CV_32FC1: return PixelFormat::FLOAT32;
        default:       return PixelFormat::GRAY8;
    }
}

Image Image::create(int w, int h, PixelFormat fmt) {
    Image img;
    img.width  = w;
    img.height = h;
    img.format = fmt;
    img.stride = w * pixel_size(fmt);
    img.buffer = SharedBuffer::allocate(static_cast<size_t>(img.stride) * h);
    return img;
}

cv::Mat Image::to_cv_mat() const {
    // Zero-copy: cv::Mat wraps external data
    return cv::Mat(height, width, to_cv_type(format), buffer.data(),
                   static_cast<size_t>(stride));
}

Image Image::from_cv_mat(const cv::Mat& mat) {
    Image img;
    img.width  = mat.cols;
    img.height = mat.rows;
    img.format = from_cv_type(mat.type());
    img.stride = static_cast<int>(mat.step[0]);

    size_t total = static_cast<size_t>(img.stride) * img.height;
    img.buffer = SharedBuffer::allocate(total);
    std::memcpy(img.buffer.data(), mat.data, total);
    return img;
}

// ===========================================================================
// HeightMap
// ===========================================================================

HeightMap HeightMap::create(int w, int h, float resolution) {
    HeightMap hm;
    hm.width         = w;
    hm.height        = h;
    hm.resolution_mm = resolution;
    hm.origin_x      = 0.0f;
    hm.origin_y      = 0.0f;
    hm.buffer = SharedBuffer::allocate(
        static_cast<size_t>(w) * h * sizeof(float));
    return hm;
}

cv::Mat HeightMap::to_cv_mat() const {
    return cv::Mat(height, width, CV_32FC1, buffer.data(),
                   static_cast<size_t>(width) * sizeof(float));
}

// ===========================================================================
// ROI
// ===========================================================================

bool ROI::contains(int px, int py) const {
    return px >= x && px < x + w && py >= y && py < y + h;
}

ROI ROI::intersect(const ROI& other) const {
    int x0 = std::max(x, other.x);
    int y0 = std::max(y, other.y);
    int x1 = std::min(x + w, other.x + other.w);
    int y1 = std::min(y + h, other.y + other.h);

    if (x1 <= x0 || y1 <= y0) {
        return ROI{0, 0, 0, 0};
    }
    return ROI{x0, y0, x1 - x0, y1 - y0};
}

int ROI::area() const {
    return w * h;
}

// ===========================================================================
// InspectionResult JSON
// ===========================================================================

using json = nlohmann::json;

std::string InspectionResult::to_json() const {
    json j;
    j["ok"]          = ok;
    j["timestamp"]   = timestamp;
    j["recipe_name"] = recipe_name;

    json defect_arr = json::array();
    for (const auto& d : defects) {
        json dj;
        dj["bounding_box"] = {
            {"x", d.bounding_box.x}, {"y", d.bounding_box.y},
            {"w", d.bounding_box.w}, {"h", d.bounding_box.h}
        };
        dj["area_mm2"]   = d.area_mm2;
        dj["max_height"] = d.max_height;
        dj["avg_height"] = d.avg_height;
        dj["centroid"]   = {{"x", d.centroid.x}, {"y", d.centroid.y}};
        dj["type"]       = d.type;
        defect_arr.push_back(dj);
    }
    j["defects"] = defect_arr;

    json measure_arr = json::array();
    for (const auto& m : measures) {
        json mj;
        mj["min_height"] = m.min_height;
        mj["max_height"] = m.max_height;
        mj["avg_height"] = m.avg_height;
        mj["std_height"] = m.std_height;
        mj["volume"]     = m.volume;
        measure_arr.push_back(mj);
    }
    j["measures"] = measure_arr;

    return j.dump(2);
}

InspectionResult InspectionResult::from_json(const std::string& json_str) {
    json j = json::parse(json_str);
    InspectionResult result;
    result.ok          = j.value("ok", true);
    result.timestamp   = j.value("timestamp", "");
    result.recipe_name = j.value("recipe_name", "");

    if (j.contains("defects")) {
        for (const auto& dj : j["defects"]) {
            DefectRegion d;
            if (dj.contains("bounding_box")) {
                const auto& bb = dj["bounding_box"];
                d.bounding_box.x = bb.value("x", 0);
                d.bounding_box.y = bb.value("y", 0);
                d.bounding_box.w = bb.value("w", 0);
                d.bounding_box.h = bb.value("h", 0);
            }
            d.area_mm2   = dj.value("area_mm2", 0.0f);
            d.max_height = dj.value("max_height", 0.0f);
            d.avg_height = dj.value("avg_height", 0.0f);
            if (dj.contains("centroid")) {
                d.centroid.x = dj["centroid"].value("x", 0.0f);
                d.centroid.y = dj["centroid"].value("y", 0.0f);
            }
            d.type = dj.value("type", "");
            result.defects.push_back(d);
        }
    }

    if (j.contains("measures")) {
        for (const auto& mj : j["measures"]) {
            MeasureResult m;
            m.min_height = mj.value("min_height", 0.0f);
            m.max_height = mj.value("max_height", 0.0f);
            m.avg_height = mj.value("avg_height", 0.0f);
            m.std_height = mj.value("std_height", 0.0f);
            m.volume     = mj.value("volume", 0.0f);
            result.measures.push_back(m);
        }
    }

    return result;
}

} // namespace vxl
