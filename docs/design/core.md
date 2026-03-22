# Core 设计

`libvxl_core` — 独立的 C++ 动态库，不依赖任何 GUI 框架。

**不管上层是 Demo(PyQt)、VxlApp(Dear PyGui)、Studio(Tauri) 还是命令行，Core 都不变。**

Core 是纯库，不管理线程策略。线程模型见 [threading.md](threading.md)。

## 模块划分

```
libvxl_core.so / vxl_core.dll
│
├── vxl::Camera          # 相机采集（结构光 + 2D 工业相机）
├── vxl::CameraManager   # 多相机管理（多设备采集 + 结果聚合）
├── vxl::Reconstruct     # 结构光重建（核心护城河）+ IDepthProvider
├── vxl::Calibration     # 标定（相机、投影器、手眼）
├── vxl::HeightMap       # 高度图处理（基准面、ROI、滤波、插值）
├── vxl::PointCloud      # 点云操作（配准、滤波、分割）
├── vxl::Inspector3D     # 3D 检测算子
├── vxl::Inspector2D     # 2D 检测算子
├── vxl::Inference       # 模型推理（ONNX Runtime）
├── vxl::Compute         # GPU 抽象（CPU/CUDA/Metal/OpenCL）
├── vxl::Pipeline        # 检测管线（JSON 定义 + 线性执行）
├── vxl::Audit           # 用户权限 + 审计日志（SQLite）
├── vxl::Plugin          # 插件系统（C ABI 动态加载）
├── vxl::VLM             # 视觉语言模型辅助（Copilot）
├── vxl::Transport       # 远程通信（JSON-over-TCP）
├── vxl::Guidance        # 机器人引导（抓取位姿计算）
├── vxl::Recipe          # 检测方案（参数集 + 算子组合 + 模型引用）
├── vxl::Result          # 检测结果（OK/NG、缺陷列表、测量值）
├── vxl::IO              # PLC / 数字 IO / 串口
└── vxl::Log             # 日志、图像保存、追溯
```

## 统一数据对象

Core 公开 API 使用自有类型，提供与第三方库的**零拷贝互转**：

```cpp
namespace vxl {
    struct Image;              // 2D 图像
    struct DepthMap;           // 深度图
    struct HeightMap;          // 高度图
    struct PointCloud;         // 点云
    struct Mesh;               // 网格
    struct ROI;                // 感兴趣区域
    struct Pose6D;             // 六自由度位姿
    struct DefectRegion;       // 缺陷区域
    struct InspectionResult;   // 检测结果
}
```

**互转接口（types.h）：**

```cpp
// C++
cv::Mat       vxl::Image::to_cv_mat() const;         // 零拷贝
vxl::Image    vxl::Image::from_cv_mat(cv::Mat&);
open3d::geometry::PointCloud  vxl::PointCloud::to_o3d() const;
vxl::PointCloud               vxl::PointCloud::from_o3d(...);
```

```python
# Python
np_array = img.to_numpy()           # numpy, OpenCV 直接可用
o3d_cloud = cloud.to_open3d()       # open3d.geometry.PointCloud
```

内存模型详见 [threading.md](threading.md) 中的引用计数章节。

## 统一错误处理

C++ 内部用错误码，Python 绑定转为异常：

```cpp
namespace vxl {
    enum class ErrorCode {
        OK = 0,
        DEVICE_NOT_FOUND, DEVICE_OPEN_FAILED, DEVICE_TIMEOUT, DEVICE_DISCONNECTED,
        CALIB_INSUFFICIENT_DATA, CALIB_CONVERGENCE_FAILED,
        RECONSTRUCT_LOW_MODULATION, RECONSTRUCT_PHASE_UNWRAP_FAILED,
        INSPECT_NO_REFERENCE, INSPECT_ROI_OUT_OF_BOUNDS,
        MODEL_LOAD_FAILED, MODEL_INPUT_MISMATCH,
        IO_CONNECTION_FAILED, IO_WRITE_FAILED,
        INVALID_PARAMETER, FILE_NOT_FOUND, OUT_OF_MEMORY, INTERNAL_ERROR,
    };

    template<typename T>
    struct Result {
        ErrorCode code;
        std::string message;
        T value;
        bool ok() const;
        operator bool() const;
    };

    using ErrorCallback = std::function<void(ErrorCode, const std::string&, const std::string& context)>;
    void set_error_callback(ErrorCallback cb);
}
```

Python 侧：`vxl.VxlError` → `vxl.DeviceError` / `vxl.CalibrationError` / ...

## 统一日志系统

```cpp
namespace vxl::log {
    enum class Level { TRACE, DEBUG, INFO, WARN, ERROR, FATAL };

    void set_level(Level level);
    void trace/debug/info/warn/error(const std::string& msg);

    void add_console_sink();
    void add_file_sink(const std::string& path);       // 自动轮转
    void add_callback_sink(LogCallback cb);             // GUI 接收

    void log_event(const std::string& category, const json& data);
    void save_image(const Image& img, const std::string& tag);
    void save_height_map(const HeightMap& hmap, const std::string& tag);
    void save_result(const InspectionResult& result);

    void set_log_dir(const std::string& dir);
    void set_max_days(int days);
    void set_max_size_mb(int mb);
}
```

## 设备抽象接口

**只包装硬件接口，不包装算法库。**

```cpp
namespace vxl {
    class ICamera2D;       // 2D 工业相机
    class ICamera3D;       // 结构光/3D 相机
    class IProjector;      // 投影器
    class ITrigger;        // 触发源
    class IIO;             // 数字 IO
    class IRobot;          // 机器人（预留）
    class IPLC;            // PLC 通信（预留）
}
```

## 第三方库策略

**包装硬件，开放算法。**

| 类别 | 策略 | 理由 |
|------|------|------|
| 硬件/设备接口 | 完整包装 | 各家 SDK 不同，包装后可换硬件 |
| 结构光重建 | 完整自研 | 开源空白，护城河 |
| 我们的检测算子 | 自有 API | 产品价值 |
| 通用图像/点云处理 | 不包装 | OpenCV/Open3D 已是行业标准 |
| 模型推理 | 薄包装 | 统一加载 ONNX 模型 |

用户在 Python 层 `import vxl` + `import cv2` + `import open3d` 混合使用，通过 `to_numpy()` / `to_open3d()` 零拷贝互转。

## 结构光重建管线（核心护城河）

```
投影条纹序列 → 相机采集
     ↓
相位计算（N-step PSP / 多频外差 / 互补格雷码）
     ↓
相位展开（时间展开 / 空间展开 / 质量引导）
     ↓
三维坐标计算（标定参数 → 世界坐标）
     ↓
点云后处理（去噪、去飞点、补洞）
     ↓
高度图生成（投影到基准平面、插值、分辨率控制）
```

| 难点 | 说明 |
|------|------|
| 反光/高亮 | HDR 多曝光融合 |
| 暗区/低调制 | 可靠性掩码 |
| 标定精度 | 相机-投影器标定直接决定最终精度 |
| 速度 | GPU 加速相位计算 |
| 材质适应性 | 金属反光、黑色塑料、透明件 |

## 3D 检测算子

| 算子 | 输入 | 输出 | 复用性 | 阶段 |
|------|------|------|--------|------|
| `ref_plane_fit` | 高度图 + 板面 ROI | 基准平面参数 | 通用 | Phase 1 |
| `height_measure` | 高度图 + ROI | max/min/avg/体积 | 通用 | Phase 1 |
| `flatness` | 高度图 + ROI | 平面度值 | 通用 | Phase 1 |
| `height_threshold` | 高度图 + 阈值 | 二值化区域 | 通用 | Phase 1 |
| `defect_cluster` | 二值图 | 缺陷列表 | 通用 | Phase 1 |
| `coplanarity` | 高度图 + 多 ROI | 共面性偏差 | PCB/连接器 | Phase 3 |
| `template_compare` | 当前 + 参考高度图 | 偏差图 + NG | 通用 | Phase 3 |

## IDepthProvider（多源 3D 采集）

`reconstruct.h` 中实现的可插拔深度数据源抽象：

```cpp
class IDepthProvider {
    virtual std::string type() const = 0;
    virtual Result<ReconstructOutput> process(
        const std::vector<Image>&, const CalibrationParams&,
        const ReconstructParams&) = 0;
};
```

内置入口：

| 方法 | 说明 | 阶段 |
|------|------|------|
| `Reconstruct::from_fringe` | 结构光条纹 → 点云 | Phase 1 |
| `Reconstruct::from_stereo` | 双目立体匹配 → 深度 | Phase 1 |
| `Reconstruct::from_depth` | 深度图 → 点云（直通） | Phase 1 |
| `Reconstruct::process` | 按 type 字符串分发（含自定义） | Phase 1 |
| `Reconstruct::register_provider` | 注册第三方 IDepthProvider | Phase 1 |

## 2D 检测算子

`inspector_2d.h` 中实现，与 `Inspector3D` 对称的 2D 检测引擎。

| 算子 | 基于 | 说明 | 阶段 |
|------|------|------|------|
| `template_match` | OpenCV | 定位 + 角度矫正 | Phase 2 |
| `blob_analysis` | OpenCV | 面积、形状、颜色异常 | Phase 2 |
| `edge_detect` | OpenCV | 缺口、裂纹 | Phase 2 |
| `ocr` | OpenCV / ONNX | 丝印识别 | Phase 3 |
| `anomaly_detect` | ONNX Runtime + Anomalib | 无监督异常检测 | Phase 3 |

## 模型推理（Inference）

`inference.h` 提供 ONNX Runtime 薄包装。当前为 stub 实现，待集成 ONNX Runtime 后填充。

```cpp
class Inference {
    static Result<Inference> load(const std::string& onnx_path, const InferenceParams& params);
    Result<std::vector<float>> run(const Image& input) const;
    std::string model_path() const;
    std::vector<int> input_shape() const;
    std::vector<int> output_shape() const;
};
```

支持 CPU 和 CUDA（通过 InferenceParams::device 选择）。Phase 2 完成 API 定义与 stub，Phase 3 接入 ONNX Runtime。

## GPU 抽象（Compute）

`compute.h` 提供后端无关的 GPU 计算接口，加速相位计算和相位展开等密集操作。

```cpp
enum class ComputeBackend { CPU, CUDA, METAL, OPENCL };

class IComputeEngine {
    virtual ComputeBackend type() const = 0;
    virtual std::string name() const = 0;
    virtual bool is_available() const = 0;
    virtual Result<std::pair<cv::Mat, cv::Mat>> compute_phase(
        const std::vector<cv::Mat>& frames, int steps) = 0;
    virtual Result<cv::Mat> unwrap_phase(
        const std::vector<cv::Mat>& wrapped_phases,
        const std::vector<int>& frequencies,
        const std::vector<cv::Mat>& modulations,
        float min_modulation) = 0;
};

Result<void> set_compute_backend(ComputeBackend backend);
ComputeBackend get_compute_backend();
std::vector<ComputeBackend> available_backends();
IComputeEngine& compute_engine();
```

CPU 后端已完整实现。CUDA 和 Metal 后端为编译期可选 stub。

## 检测管线（Pipeline）

`pipeline.h` 定义线性执行管线，支持 JSON 配置和编程式构建。

```cpp
enum class StepType { CAPTURE, RECONSTRUCT, INSPECT_3D, INSPECT_2D, OUTPUT, CUSTOM };

struct PipelineStep {
    StepType type;
    std::string name;
    std::unordered_map<std::string, std::string> params;
};

struct PipelineContext {
    std::vector<Image> frames;
    HeightMap height_map;
    PointCloud point_cloud;
    InspectionResult result;
    std::unordered_map<std::string, std::any> custom_data;
};

class Pipeline {
    static Result<Pipeline> load(const std::string& path);
    void add_step(const PipelineStep& step);
    void set_custom_callback(const std::string& step_name, CustomStepCallback cb);
    Result<PipelineContext> run_once();
    void start(std::function<void(const PipelineContext&)> on_complete);
    void stop();
    Result<void> save(const std::string& path) const;
};
```

## 用户权限与审计（Audit）

`audit.h` 提供用户管理和审计日志，使用 SQLite 存储，SHA-256 防篡改。

```cpp
enum class Role : int { OPERATOR = 0, ENGINEER = 1, ADMIN = 2 };

class UserManager {
    Result<void> init(const std::string& db_path);
    Result<void> create_user(const std::string& username, const std::string& password, Role role);
    Result<User> authenticate(const std::string& username, const std::string& password);
    bool has_permission(Role required) const;
    // ... CRUD, session management
};

class AuditLog {
    Result<void> init(const std::string& db_path);
    Result<void> log_event(const std::string& action, const std::string& details = "");
    Result<std::vector<AuditEntry>> query(int64_t from, int64_t to, ...);
    Result<void> export_csv(const std::string& path, ...);
};

UserManager& user_manager();  // global singleton
AuditLog& audit_log();        // global singleton
```

## 多相机管理（CameraManager）

`camera_manager.h` 管理多台 3D 相机，支持统一采集和结果聚合。

```cpp
class CameraManager {
    Result<void> add_camera(const std::string& device_id, const CalibrationParams& calib);
    Result<void> remove_camera(const std::string& device_id);
    std::vector<std::string> camera_ids() const;
    ICamera3D* get_camera(const std::string& device_id);
    Result<std::unordered_map<std::string, std::vector<Image>>> capture_all();
    static InspectionResult aggregate_results(
        const std::vector<std::pair<std::string, InspectionResult>>& per_camera_results);
};
```

## 插件系统（Plugin）

`plugin_api.h` 定义 C ABI 插件接口，支持动态加载 .so/.dylib/.dll。

```cpp
struct PluginInfo {
    std::string name, version, author;
    std::string type;   // "depth_provider", "inspector", "device_driver", "io_driver"
    std::string description;
};

// 每个插件 .so 必须导出的 C 函数：
// vxl_plugin_name(), vxl_plugin_version(), vxl_plugin_type(),
// vxl_plugin_create(), vxl_plugin_destroy()

class PluginManager {
    Result<PluginInfo> load(const std::string& path);
    Result<int> load_directory(const std::string& dir_path);
    Result<void> unload(const std::string& name);
    std::vector<PluginInfo> loaded_plugins() const;
    IDepthProvider* get_depth_provider(const std::string& name);
};

PluginManager& plugin_manager();  // global singleton
```

## VLM 辅助（VLM）

`vlm.h` 提供视觉语言模型集成，Copilot 角色（离线辅助，不参与实时检测）。

```cpp
struct VLMConfig {
    std::string provider;   // "openai", "anthropic", "ollama", "custom"
    std::string api_key, model, base_url;
};

class VLMAssistant {
    Result<void> configure(const VLMConfig& config);
    void set_http_caller(HttpCaller caller);
    Result<VLMResponse> describe_defect(const Image& image, const DefectRegion& defect);
    Result<VLMResponse> suggest_parameters(const Image& sample, const std::string& context = "");
    Result<VLMResponse> generate_report(const std::vector<InspectionResult>& results, ...);
    Result<VLMResponse> query(const std::string& prompt, const Image* image = nullptr);
    Result<std::vector<std::pair<std::string, float>>> suggest_labels(
        const Image& image, const std::vector<std::string>& candidate_labels);
};

VLMAssistant& vlm_assistant();  // global singleton
```

## 远程通信（Transport）

`transport.h` 提供 JSON-over-TCP 远程通信，支持请求/响应和事件广播。

```cpp
struct TransportMessage {
    std::string type;     // "request", "response", "event"
    std::string method;   // e.g., "inspect", "get_status"
    std::string payload;  // JSON
    int64_t id;
};

class TransportServer {
    Result<void> start(const std::string& address, int port);
    void stop();
    void on_request(const std::string& method, RequestHandler handler);
    Result<void> broadcast_event(const TransportMessage& event);
};

class TransportClient {
    Result<void> connect(const std::string& address, int port);
    void disconnect();
    Result<TransportMessage> request(const std::string& method,
                                      const std::string& payload_json, int timeout_ms = 5000);
    void on_event(EventCallback callback);
};
```

## 机器人引导（Guidance）

`guidance.h` 提供抓取位姿计算，支持 3D 和 2.5D 两种模式。

```cpp
struct GraspPose {
    Pose6D pose;
    float score, width_mm;
    std::string label;
};

struct GuidanceParams {
    std::string strategy;        // "top_down", "side", "custom"
    float approach_distance_mm;
    float min_score;
    int max_results;
};

class GuidanceEngine {
    Result<std::vector<GraspPose>> compute_grasp(const PointCloud& cloud, const GuidanceParams& params);
    Result<std::vector<GraspPose>> compute_grasp_2d(const HeightMap& hmap, const GuidanceParams& params);
    static Result<GraspPose> find_pick_point(const HeightMap& hmap, const ROI& roi);
    static Result<Pose6D> camera_to_robot(const Pose6D& camera_pose, const Pose6D& hand_eye_transform);
};
```

## Python API

```python
import vxl

cam = vxl.Camera.open("SL-001")
frames = cam.capture_sequence()
cloud = vxl.Reconstruct.from_fringe(frames, cam.calib)
hmap = cloud.to_height_map(resolution=0.05)

inspector = vxl.Inspector3D()
inspector.set_reference(ref_hmap)
inspector.add_roi("pad_area", rect)
result = inspector.run(hmap)

# Recipe 驱动
recipe = vxl.Recipe.load("pcb_model_a.json")
io = vxl.IO.open("COM3")
while True:
    io.wait_trigger()
    frames = cam.capture()
    hmap = vxl.reconstruct(frames, cam.calib)
    result = recipe.inspect(hmap)
    io.set_output("ok", result.ok)
    vxl.log.save(result)
```

## 头文件清单（26 个）

| 头文件 | 模块 | 状态 |
|--------|------|------|
| `core.h` | 总入口 | 已实现 |
| `export.h` | 导出宏 | 已实现 |
| `error.h` | 错误处理 | 已实现 |
| `types.h` | 统一数据对象 | 已实现 |
| `log.h` | 日志系统 | 已实现 |
| `message_bus.h` | 消息总线 | 已实现 |
| `camera.h` | 相机采集 | 已实现 |
| `camera_manager.h` | 多相机管理 | 已实现 |
| `reconstruct.h` | 结构光重建 + IDepthProvider | 已实现 |
| `calibration.h` | 标定 | 已实现 |
| `height_map.h` | 高度图处理 | 已实现 |
| `point_cloud.h` | 点云操作 | 已实现 |
| `inspector_3d.h` | 3D 检测算子 | 已实现 |
| `inspector_2d.h` | 2D 检测算子 | 接口已定义，算法 stub |
| `inference.h` | ONNX Runtime 推理 | 接口已定义，stub |
| `result.h` | 检测结果 | 已实现 |
| `recipe.h` | 检测方案 | 已实现 |
| `device.h` | 设备抽象 | 已实现 |
| `io.h` | PLC / 数字 IO | 已实现 |
| `compute.h` | GPU 抽象 | CPU 已实现，CUDA/Metal stub |
| `pipeline.h` | 检测管线 | 已实现 |
| `audit.h` | 用户权限 + 审计 | 已实现 |
| `plugin_api.h` | 插件系统 | 已实现 |
| `vlm.h` | VLM 辅助 | 已实现 |
| `transport.h` | 远程通信 | 已实现 |
| `guidance.h` | 机器人引导 | 已实现 |
