#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "vxl/export.h"

namespace cv {
class Mat;
}

namespace vxl {

// ---------------------------------------------------------------------------
// SharedBuffer -- RAII buffer with atomic reference counting (zero-copy)
// ---------------------------------------------------------------------------
class VXL_EXPORT SharedBuffer {
public:
    SharedBuffer() = default;
    ~SharedBuffer();

    SharedBuffer(const SharedBuffer& other);
    SharedBuffer& operator=(const SharedBuffer& other);

    SharedBuffer(SharedBuffer&& other) noexcept;
    SharedBuffer& operator=(SharedBuffer&& other) noexcept;

    static SharedBuffer allocate(size_t size_bytes);

    uint8_t* data() const { return data_; }
    size_t   size() const { return size_; }
    int      ref_count() const;

private:
    struct ControlBlock {
        std::atomic<int> refs{1};
    };

    uint8_t*      data_ = nullptr;
    size_t        size_ = 0;
    ControlBlock* ctrl_ = nullptr;

    void retain();
    void release();
};

// ---------------------------------------------------------------------------
// PixelFormat
// ---------------------------------------------------------------------------
enum class PixelFormat : int {
    GRAY8   = 0,
    GRAY16  = 1,
    RGB8    = 2,
    BGR8    = 3,
    FLOAT32 = 4
};

// ---------------------------------------------------------------------------
// Point2f
// ---------------------------------------------------------------------------
struct Point2f {
    float x = 0.0f;
    float y = 0.0f;
};

// ---------------------------------------------------------------------------
// Image
// ---------------------------------------------------------------------------
struct VXL_EXPORT Image {
    SharedBuffer buffer;
    int          width  = 0;
    int          height = 0;
    PixelFormat  format = PixelFormat::GRAY8;
    int          stride = 0;   // bytes per row

    static Image create(int w, int h, PixelFormat fmt);

    cv::Mat to_cv_mat() const;
    static Image from_cv_mat(const cv::Mat& mat);
};

// ---------------------------------------------------------------------------
// HeightMap
// ---------------------------------------------------------------------------
struct VXL_EXPORT HeightMap {
    SharedBuffer buffer;      // float* data
    int          width  = 0;
    int          height = 0;
    float        resolution_mm = 1.0f;
    float        origin_x = 0.0f;
    float        origin_y = 0.0f;

    static HeightMap create(int w, int h, float resolution);

    cv::Mat to_cv_mat() const;   // returns CV_32F
};

// ---------------------------------------------------------------------------
// PointFormat / PointCloud
// ---------------------------------------------------------------------------
enum class PointFormat : int {
    XYZ_FLOAT    = 0,   // 3 floats per point (12 bytes)
    XYZRGB_FLOAT = 1    // 3 floats + 3 uint8 packed (16 bytes padded)
};

struct VXL_EXPORT PointCloud {
    SharedBuffer buffer;
    size_t       point_count = 0;
    PointFormat  format = PointFormat::XYZ_FLOAT;
};

// ---------------------------------------------------------------------------
// Mesh (placeholder)
// ---------------------------------------------------------------------------
struct VXL_EXPORT Mesh {
    // Placeholder for future mesh support
};

// ---------------------------------------------------------------------------
// ROI
// ---------------------------------------------------------------------------
struct VXL_EXPORT ROI {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    bool contains(int px, int py) const;
    ROI  intersect(const ROI& other) const;
    int  area() const;
};

// ---------------------------------------------------------------------------
// Pose6D
// ---------------------------------------------------------------------------
struct Pose6D {
    double translation[3] = {0.0, 0.0, 0.0};
    double rotation[9]    = {1, 0, 0,
                             0, 1, 0,
                             0, 0, 1};  // 3x3 identity
};

// ---------------------------------------------------------------------------
// DefectRegion
// ---------------------------------------------------------------------------
struct DefectRegion {
    ROI         bounding_box;
    float       area_mm2   = 0.0f;
    float       max_height = 0.0f;
    float       avg_height = 0.0f;
    Point2f     centroid;
    std::string type;
};

// ---------------------------------------------------------------------------
// MeasureResult
// ---------------------------------------------------------------------------
struct MeasureResult {
    float min_height = 0.0f;
    float max_height = 0.0f;
    float avg_height = 0.0f;
    float std_height = 0.0f;
    float volume     = 0.0f;
};

// ---------------------------------------------------------------------------
// InspectionResult
// ---------------------------------------------------------------------------
struct VXL_EXPORT InspectionResult {
    bool                        ok = true;
    std::vector<DefectRegion>   defects;
    std::vector<MeasureResult>  measures;
    std::string                 timestamp;
    std::string                 recipe_name;

    std::string to_json() const;
    static InspectionResult from_json(const std::string& json_str);
};

} // namespace vxl
