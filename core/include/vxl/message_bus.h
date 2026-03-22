#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "vxl/export.h"
#include "vxl/types.h"

namespace vxl {

// ===========================================================================
// Message base
// ===========================================================================

struct VXL_EXPORT Message {
    std::string topic;
    int64_t     timestamp_ns = 0;

    virtual ~Message() = default;
};

// ===========================================================================
// Predefined messages
// ===========================================================================

struct VXL_EXPORT FrameCaptured : public Message {
    Image       image;
    std::string camera_id;

    FrameCaptured() { topic = "frame_captured"; }
};

struct VXL_EXPORT ReconstructDone : public Message {
    HeightMap   height_map;
    PointCloud  point_cloud;

    ReconstructDone() { topic = "reconstruct_done"; }
};

struct VXL_EXPORT InspectionDone : public Message {
    InspectionResult result;

    InspectionDone() { topic = "inspection_done"; }
};

struct VXL_EXPORT ParamChanged : public Message {
    std::string key;
    std::string value;

    ParamChanged() { topic = "param_changed"; }
};

struct VXL_EXPORT AlarmTriggered : public Message {
    std::string alarm_id;
    std::string description;

    AlarmTriggered() { topic = "alarm_triggered"; }
};

// ===========================================================================
// DispatchMode
// ===========================================================================

enum class DispatchMode : int {
    SYNC = 0   // default: dispatch on publisher's thread
};

// ===========================================================================
// MessageBus
// ===========================================================================

class VXL_EXPORT MessageBus {
public:
    using Handler = std::function<void(const std::shared_ptr<Message>&)>;

    MessageBus();
    ~MessageBus();

    MessageBus(const MessageBus&) = delete;
    MessageBus& operator=(const MessageBus&) = delete;

    void     publish(const std::shared_ptr<Message>& msg);
    uint64_t subscribe(const std::string& topic, Handler handler);
    void     unsubscribe(uint64_t id);

private:
    struct Subscriber {
        uint64_t id;
        Handler  handler;
    };

    struct Impl;
    std::unique_ptr<Impl> impl_;

    Impl& get_impl();
};

// Global accessor
VXL_EXPORT MessageBus& message_bus();

} // namespace vxl
