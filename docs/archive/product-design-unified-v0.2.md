# VxlStudio 产品设计 v0.2（统一版）

> **状态：当前执行方案**
> 策略：老架构定方向，新架构定节奏。Core 是不变的核心，demo 快速落地场景。

---

## 1. 设计原则

1. **长期架构用老方案**：四层（Core → Script/Flow → Studio → VxlApp），面向平台化
2. **短期执行用新思路**：Phase 1-2 聚焦 Core，用 PyQt demo 验证场景和 API
3. **Core 接口按老架构设计**：统一数据对象、设备抽象、插件接口，一步到位
4. **Demo 是 Core 的消费者**：不是临时应用，是持续维护的 Core 使用示范
5. **结构光重建是护城河**：优先级最高，比任何上层框架都重要

---

## 2. 产品定位

> **面向制造业质检的 3D+2D 缺陷检测平台，原生适配自有结构光相机。**

### 精度定位

| 精度范围 | 适合场景 | 竞争对手 |
|---------|---------|---------|
| 0.01-0.05mm | 晶圆、精密电子 | ZEISS、基恩士、海克斯康 |
| **0.05-0.2mm** | **PCB、塑料件、金属件、焊点** | **中低端结构光、2D AOI 升级** |
| 0.2-1mm | 大型铸件、汽车钣金 | 低成本 3D / TOF |

### 差异化

1. 3D 检测开箱即用 — 买相机就能检测
2. 软硬一体标定 — 标定精度和稳定性最优
3. 行业模板 — PCB、平面度等模板开箱即用
4. Python 可定制 — 比基恩士灵活，比客户自研省时间
5. 平台化架构 — Studio 设计 + VxlApp 执行，可扩展

### 场景优先级

| 优先级 | 场景 | 阶段 |
|--------|------|------|
| **第一** | PCB / SMT 检测 | Phase 1-2 |
| **第二** | 塑料件 / 金属件表面 | Phase 3 |
| **暂缓** | 晶圆（精度不够）、分拣码垛（不同方向） | Phase 4+ |

---

## 3. 总体架构（四层）

```
┌──────────────────────────────────────────────────────────────┐
│  Studio (Tauri 2 + WebView + TS/React)          [Phase 3-4]  │
│  项目设计器 | 流程编排 | 测试 | 打包 | 部署                    │
│                         ↓ 导出 .vxap                         │
├──────────────────────────────────────────────────────────────┤
│  VxlApp (C++ 宿主 + Python/Dear PyGui)          [Phase 3-4]  │
│  加载 .vxap | 执行流程 | 操作 UI | 设备通信 | 日志            │
├──────────────────────────────────────────────────────────────┤
│  Script / Flow (Python + JSON/YAML)             [Phase 2-3]  │
│  脚本节点 | 流程描述 | 参数模板 | 规则表达式 | UI 脚本         │
├──────────────────────────────────────────────────────────────┤
│  Core (C++ 动态库)                              [Phase 1]    │
│  采集 | 重建 | 算法 | 推理 | 标定 | 调度 | 通信 | 插件 | 日志  │
└──────────────────────────────────────────────────────────────┘

  Demo (PyQt)  ←── Core 的使用示范                [Phase 1-2]
```

**Phase 1-2 只实现 Core + Demo。** Studio、VxlApp、Script/Flow 保留目录结构和接口设计，Phase 3-4 实施。

---

## 4. Core 设计（Phase 1 重点）

### 4.1 Core 是什么

`libvxl_core` — 独立的 C++ 动态库，不依赖任何 GUI 框架。

**不管上层是 Demo(PyQt)、VxlApp(Dear PyGui)、Studio(Tauri) 还是命令行，Core 都不变。**

### 4.2 模块划分

```
libvxl_core.so / vxl_core.dll
│
├── vxl::Camera          # 相机采集（结构光 + 2D 工业相机）
├── vxl::Reconstruct     # 结构光重建（核心护城河）
├── vxl::Calibration     # 标定（相机、投影器、手眼）
├── vxl::HeightMap       # 高度图处理（基准面、ROI、滤波、插值）
├── vxl::PointCloud      # 点云操作（配准、滤波、分割）
├── vxl::Inspector3D     # 3D 检测算子
├── vxl::Inspector2D     # 2D 检测算子
├── vxl::Inference       # 模型推理（ONNX Runtime）
├── vxl::Recipe          # 检测方案（参数集 + 算子组合 + 模型引用）
├── vxl::Result          # 检测结果（OK/NG、缺陷列表、测量值）
├── vxl::IO              # PLC / 数字 IO / 串口
└── vxl::Log             # 日志、图像保存、追溯
```

### 4.3 统一数据对象

Core 公开 API 使用自有类型，但**提供与第三方库的零拷贝互转**：

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

**互转接口（在 types.h 中提供）：**

```cpp
// C++ 侧
cv::Mat       vxl::Image::to_cv_mat() const;        // 零拷贝，共享内存
vxl::Image    vxl::Image::from_cv_mat(cv::Mat&);     // 零拷贝
open3d::geometry::PointCloud  vxl::PointCloud::to_o3d() const;
vxl::PointCloud               vxl::PointCloud::from_o3d(...);
```

```python
# Python 侧
img = cam.capture()
cv_mat = img.to_numpy()          # numpy array，OpenCV 直接可用
o3d_cloud = cloud.to_open3d()    # open3d.geometry.PointCloud
```

这样 vxl:: 类型是 Core API 的通用语言，但用户可以随时转出去用 OpenCV/Open3D 的全部能力，再转回来继续走 Core 流程。

### 4.4 统一错误处理

Core 采用**错误码 + 异常双模式**，内部用错误码，Python 绑定转为异常：

```cpp
namespace vxl {
    // 错误码枚举
    enum class ErrorCode {
        OK = 0,
        // 设备相关
        DEVICE_NOT_FOUND,
        DEVICE_OPEN_FAILED,
        DEVICE_TIMEOUT,
        DEVICE_DISCONNECTED,
        // 标定相关
        CALIB_INSUFFICIENT_DATA,
        CALIB_CONVERGENCE_FAILED,
        // 重建相关
        RECONSTRUCT_LOW_MODULATION,
        RECONSTRUCT_PHASE_UNWRAP_FAILED,
        // 检测相关
        INSPECT_NO_REFERENCE,
        INSPECT_ROI_OUT_OF_BOUNDS,
        // 推理相关
        MODEL_LOAD_FAILED,
        MODEL_INPUT_MISMATCH,
        // IO
        IO_CONNECTION_FAILED,
        IO_WRITE_FAILED,
        // 通用
        INVALID_PARAMETER,
        FILE_NOT_FOUND,
        OUT_OF_MEMORY,
        INTERNAL_ERROR,
    };

    // 结果类型（C++ 侧推荐使用）
    template<typename T>
    struct Result {
        ErrorCode code;
        std::string message;     // 人可读描述
        T value;                 // 成功时有值
        bool ok() const;
        operator bool() const;
    };

    // 全局错误回调（可选）
    using ErrorCallback = std::function<void(ErrorCode, const std::string&, const std::string& context)>;
    void set_error_callback(ErrorCallback cb);
}
```

**Python 侧自动转为异常：**

```python
try:
    cam = vxl.Camera.open("SL-001")
    frames = cam.capture()
except vxl.DeviceError as e:       # 设备类错误
    print(f"设备错误: {e}")
except vxl.CalibrationError as e:  # 标定类错误
    print(f"标定错误: {e}")
except vxl.VxlError as e:          # 所有 vxl 错误的基类
    print(f"错误 [{e.code}]: {e}")
```

**设计原则：**
- C++ 内部不抛异常，用 `vxl::Result<T>` 返回
- pybind11 绑定层检查 ErrorCode，非 OK 时转为对应 Python 异常
- 每个 ErrorCode 有对应的人可读消息，支持国际化
- 错误上下文（哪个函数、哪个参数）自动附带

### 4.5 统一日志系统

```cpp
namespace vxl::log {
    enum class Level { TRACE, DEBUG, INFO, WARN, ERROR, FATAL };

    // 基本日志
    void set_level(Level level);
    void trace(const std::string& msg);
    void debug(const std::string& msg);
    void info(const std::string& msg);
    void warn(const std::string& msg);
    void error(const std::string& msg);

    // 日志输出目标（可叠加）
    void add_console_sink();                           // 控制台
    void add_file_sink(const std::string& path);       // 文件（自动轮转）
    void add_callback_sink(LogCallback cb);            // 自定义回调（给 GUI 用）

    // 结构化日志（检测结果、设备事件等）
    void log_event(const std::string& category, const json& data);

    // 图像/数据保存（NG 图像、调试数据）
    void save_image(const Image& img, const std::string& tag);
    void save_height_map(const HeightMap& hmap, const std::string& tag);
    void save_result(const InspectionResult& result);

    // 日志目录管理
    void set_log_dir(const std::string& dir);
    void set_max_days(int days);         // 自动清理
    void set_max_size_mb(int mb);        // 磁盘限制
}
```

**Python 侧：**

```python
import vxl

vxl.log.set_level(vxl.log.INFO)
vxl.log.add_file_sink("/var/log/vxl/")
vxl.log.info("检测开始")

# GUI 接收日志
vxl.log.add_callback_sink(lambda level, msg: update_log_panel(level, msg))

# 保存 NG 数据
if not result.ok:
    vxl.log.save_image(frames, "ng")
    vxl.log.save_result(result)
```

**设计原则：**
- 零配置可用（默认 console + INFO 级别）
- 日志和图像保存分离（日志轻量高频，图像按需保存）
- 文件自动轮转和清理（产线 7x24 运行不能让磁盘爆掉）
- callback sink 让任何 GUI（Demo/VxlApp/Studio）都能接收日志
- 结构化事件日志为后续追溯和 SPC 打基础

### 4.6 设备抽象接口

**只包装硬件接口，不包装算法库。** Phase 1 先实现自有相机，接口为后续扩展准备：

```cpp
namespace vxl {
    class ICamera2D;       // 2D 工业相机接口
    class ICamera3D;       // 结构光/3D 相机接口
    class IProjector;      // 投影器接口
    class ITrigger;        // 触发源接口
    class IIO;             // 数字 IO 接口
    class IRobot;          // 机器人接口（预留）
    class IPLC;            // PLC 通信接口（预留）
}
```

### 4.7 第三方库使用策略

**核心原则：包装硬件，开放算法。**

我们的包装精力应该花在哪里，不应该花在哪里：

| 类别 | 策略 | 理由 |
|------|------|------|
| **硬件/设备接口** | **完整包装**（ICamera, IIO, IPLC...） | 硬件 SDK 各家不同，用户不应直接碰；包装后可换硬件不影响上层 |
| **结构光重建** | **完整自研** | 开源空白，核心护城河 |
| **我们的检测算子** | **自有 API** | 这是产品价值，用 vxl:: 类型 |
| **通用图像/点云处理** | **不包装，直接暴露第三方** | OpenCV/Open3D 的 API 已经是行业标准，再包一层只会增加学习成本和维护负担 |
| **模型推理** | **薄包装** | 统一接口加载 ONNX 模型，但不重造推理框架 |

**用户在 Python SDK 层看到的应该是这样：**

```python
import vxl           # 我们的：采集、重建、检测、设备
import cv2           # 直接用 OpenCV
import open3d as o3d # 直接用 Open3D
import numpy as np

# === 用 vxl 做采集和重建（硬件相关，必须走我们的） ===
cam = vxl.Camera.open("SL-001")
frames = cam.capture_sequence()
hmap = vxl.reconstruct(frames, cam.calib)

# === 用 vxl 做检测（我们的算子） ===
result = vxl.Inspector3D().run(hmap)

# === 转出去用 OpenCV 做自定义处理（用户自由选择） ===
np_hmap = hmap.to_numpy()                    # 零拷贝转 numpy
edges = cv2.Canny(np_hmap, 50, 150)          # 直接用 OpenCV
contours, _ = cv2.findContours(edges, ...)   # 直接用 OpenCV

# === 转出去用 Open3D 做点云分析 ===
cloud = vxl.reconstruct_cloud(frames, cam.calib)
o3d_cloud = cloud.to_open3d()                # 转 Open3D
o3d_cloud = o3d_cloud.voxel_down_sample(0.1) # 直接用 Open3D
mesh = o3d.geometry.TriangleMesh.create_from_point_cloud_poisson(o3d_cloud)

# === 混合使用：OpenCV 处理完的结果转回 vxl ===
custom_mask = cv2.threshold(np_hmap, 0.5, 1, cv2.THRESH_BINARY)[1]
defects = vxl.defect_cluster(vxl.Image.from_numpy(custom_mask))
```

**为什么不全部包装？**

1. **维护成本：** OpenCV 有 2500+ 函数，Open3D 有几百个类。包装一层意味着每次上游更新你都要跟
2. **学习成本：** 用户已经会 OpenCV/Open3D，再学一套 `vxl.cv.canny()` 没有价值
3. **功能滞后：** 你的包装永远追不上上游的新功能
4. **社区资源：** 用户遇到问题可以直接搜 OpenCV 的解答，搜 `vxl.cv` 什么都搜不到

**我们要包装的是"用户不会自己做、做不好、或者各家不一样"的部分：** 硬件驱动、结构光重建、标定、专业检测算子。通用图像/点云处理让用户直接用第三方，我们只提供零拷贝互转。

### 4.8 结构光重建管线（核心护城河）

开源界几乎空白，必须自研。

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

**关键工程难点：**

| 难点 | 说明 |
|------|------|
| 反光/高亮 | HDR 多曝光融合 |
| 暗区/低调制 | 可靠性掩码，低质量区域标记为无效 |
| 标定精度 | 相机-投影器标定直接决定最终精度 |
| 速度 | GPU 加速相位计算（CUDA / OpenCL） |
| 材质适应性 | 金属反光、黑色塑料低反射、透明件 |

### 4.9 3D 检测算子

从 PCB 场景提炼，天然可复用：

| 算子 | 输入 | 输出 | 复用性 |
|------|------|------|--------|
| `ref_plane_fit` | 高度图 + 板面 ROI | 基准平面参数 | 通用 |
| `height_measure` | 高度图 + ROI | max/min/avg 高度、体积 | 通用 |
| `coplanarity` | 高度图 + 多 ROI | 共面性偏差 | PCB/连接器 |
| `flatness` | 高度图 + ROI | 平面度值 | 通用 |
| `height_threshold` | 高度图 + 阈值 | 二值化区域 | 通用 |
| `defect_cluster` | 二值图 | 缺陷列表(面积/位置/高度) | 通用 |
| `template_compare` | 当前高度图 + 参考 | 偏差图 + NG 区域 | 通用 |

### 4.10 2D 检测算子

| 算子 | 基于 | 说明 |
|------|------|------|
| `template_match` | OpenCV | 定位 + 角度矫正 |
| `blob_analysis` | OpenCV | 面积、形状、颜色异常 |
| `edge_detect` | OpenCV | 缺口、裂纹 |
| `ocr` | OpenCV / ONNX | 丝印识别 |
| `anomaly_detect` | ONNX Runtime + Anomalib | 无监督异常检测 |

### 4.11 Python API (pybind11)

```python
import vxl

# 采集
cam = vxl.Camera.open("SL-001")
frames = cam.capture_sequence()

# 重建
cloud = vxl.Reconstruct.from_fringe(frames, cam.calib)
hmap = cloud.to_height_map(resolution=0.05)

# 检测
inspector = vxl.Inspector3D()
inspector.set_reference(ref_hmap)
inspector.add_roi("pad_area", rect)
result = inspector.run(hmap)

print(result.ok)       # True/False
print(result.defects)  # [{type, area, height, position}, ...]

# Recipe 驱动的完整检测循环
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

### 4.12 Core 头文件清单

**Phase 1 实现：**

```
core/include/vxl/
├── core.h              # 全局初始化 / 版本
├── error.h             # ErrorCode 枚举 / Result<T> / 错误回调
├── types.h             # 统一数据对象 + 第三方互转（to_cv_mat, to_numpy, to_open3d...）
├── log.h               # 日志系统（多 sink、文件轮转、图像保存）
├── camera.h            # ICamera2D, ICamera3D 接口 + 工厂
├── reconstruct.h       # 结构光重建管线
├── calibration.h       # 标定
├── height_map.h        # 高度图处理
├── point_cloud.h       # 点云操作
├── inspector_3d.h      # 3D 检测算子
├── result.h            # 检测结果
└── recipe.h            # 检测方案
```

**Phase 2-3 实现：**

```
├── inspector_2d.h      # 2D 检测算子
├── inference.h         # 模型推理
├── io.h                # PLC / 数字 IO
└── device.h            # 设备抽象接口族 IProjector, ITrigger, IIO, IRobot, IPLC
```

**Phase 4 实现：**

```
├── plugin_api.h        # C ABI 插件接口
├── guidance.h          # 机器人引导
├── vlm.h               # VLM 辅助接口（Copilot 角色）
└── transport.h         # 远程通信 gRPC / WebSocket
```

---

## 5. Demo 应用（Phase 1-2）

### 定位

**Demo 是 Core 的使用示范，不是临时产品。** 它验证 Core API 的可用性，同时作为 PCB 检测场景的快速落地工具。

### 技术方案

PyQt6（PySide6），直接调用 Core 的 Python 绑定。

### 功能

**工程模式：**
- 相机连接与采集预览（2D + 高度图 + 点云）
- 标定向导
- 检测参数调试（交互式 ROI、阈值、即时预览）
- 样本管理（保存 / 加载）
- Recipe 编辑与保存

**运行模式：**
- 实时图像 + OK/NG 指示
- 一键启停
- 统计面板（良率、缺陷分布）
- 日志

### Demo 的长期价值

- **Core API 验证器** — 如果 demo 写起来别扭，说明 API 设计有问题
- **客户演示工具** — 可以拿去客户现场做 POC
- **Python 二开范例** — 客户拿到 Python API 后，demo 就是最好的参考代码
- **交付工程师工具** — 在 Studio/VxlApp 成熟前，demo 就是日常使用的工具

---

## 6. Studio（Phase 3-4）

项目设计器 / 流程编排器 / 测试器 / 打包器 / 部署器。

**技术方案：** Tauri 2 + WebView + TypeScript / React

**Rust 壳层职责：** 前端 IPC → Core 薄封装 → 文件/权限/安全 → sidecar 调度

**Core 接入：** C ABI 动态库 > sidecar > 直接混绑

**功能工作区：** Project / Device / Calib / Flow / Debug / Sample / Param / UI Design / Deploy / Plugin

**Phase 3-4 实施，当前只保留空目录。**

---

## 7. VxlApp（Phase 3-4）

Studio 产出的轻量运行端，加载 .vxap 包并执行。

**技术方案：** C++ 宿主 + Python / Dear PyGui

**C++ 宿主管：** Core 加载、设备采集、算法执行、流程调度、线程/资源、PLC/IO
**Python/Dear PyGui 管：** 操作界面构建、图像/结果可视化、参数面板、交互响应、自定义面板

**启动流程：**
1. C++ 宿主加载 core.dll/.so
2. 初始化嵌入式 Python + Dear PyGui
3. 读取 .vxap 包 manifest.json
4. 导入 UI 脚本，构建操作界面
5. 按 pipeline.json 调度 Core + Python 节点

**Phase 3-4 实施，当前只保留空目录。**

---

## 8. VxAP 包格式（Phase 3-4 定义）

Studio 导出的项目包（`.vxap`）：

```
my_project.vxap
├── manifest.json          # 项目元数据
├── pipeline.json          # 流程描述
├── params.default.json    # 默认参数
├── scripts/               # Python 脚本
│   ├── ui_main.py         # Dear PyGui 界面定义
│   ├── ui_custom.py       # 自定义面板
│   ├── precheck.py
│   ├── postprocess.py
│   └── rules.py
├── assets/
├── models/                # AI 模型
│   └── defect_xx.onnx
└── plugins/
```

**现场部署：** 拷贝 VxlApp → 导入 .vxap → 运行

---

## 9. 二次开发三级体系

| 级别 | 能力 | 角色 | 可用阶段 |
|------|------|------|----------|
| **级别 1：纯配置** | 调参数、选模板、改 ROI | 操作员 / 实施工程师 | Phase 1+（Demo/VxlApp） |
| **级别 2：Python 脚本** | 判定逻辑、MES 对接、自定义面板 | 高级工程师 | Phase 1+（Python API） |
| **级别 3：C++ 插件** | 新算法、新设备驱动 | 内部 / 合作伙伴 | Phase 4（plugin_api.h） |

---

## 10. 技术选型

### Phase 1-2 技术栈

| 领域 | 选型 | 许可证 |
|------|------|--------|
| Core | C++17 | - |
| Demo GUI | PyQt6 / PySide6 | LGPL |
| Python 绑定 | pybind11 | BSD |
| 2D 处理 | OpenCV | Apache 2.0 |
| 3D 处理 | Open3D | MIT |
| 模型推理 | ONNX Runtime | MIT |
| 异常检测训练 | Anomalib | Apache 2.0 |
| 构建 | CMake | - |
| 包管理 | vcpkg | MIT |

### Phase 3-4 追加

| 领域 | 选型 | 许可证 |
|------|------|--------|
| Studio | Tauri 2 (Rust) + WebView + TS/React | MIT / Apache 2.0 |
| VxlApp GUI | Dear PyGui (Python) | MIT |
| 脚本语言 | Python (主) + 可选 Lua | - |
| 插件接口 | C ABI | - |

---

## 11. 项目目录结构

```
VxlStudio/
├── core/                              # C++ Core 动态库 [Phase 1]
│   ├── CMakeLists.txt
│   ├── include/vxl/                   # 公开头文件
│   │   ├── core.h
│   │   ├── types.h                    # 统一数据对象
│   │   ├── camera.h                   # ICamera2D, ICamera3D
│   │   ├── reconstruct.h             # 结构光重建
│   │   ├── calibration.h
│   │   ├── height_map.h
│   │   ├── point_cloud.h
│   │   ├── inspector_3d.h
│   │   ├── inspector_2d.h            # Phase 2-3
│   │   ├── inference.h               # Phase 2-3
│   │   ├── recipe.h
│   │   ├── result.h
│   │   ├── io.h                       # Phase 2-3
│   │   ├── log.h
│   │   ├── device.h                   # Phase 2-3, 设备抽象接口族
│   │   ├── plugin_api.h               # Phase 4, C ABI 插件
│   │   ├── guidance.h                 # Phase 4, 机器人引导
│   │   ├── vlm.h                      # Phase 4, VLM 辅助
│   │   └── transport.h               # Phase 4, 远程通信
│   ├── src/
│   │   ├── camera/
│   │   ├── reconstruct/              # 核心护城河
│   │   ├── calibration/
│   │   ├── height_map/
│   │   ├── point_cloud/
│   │   ├── inspector_3d/
│   │   ├── inspector_2d/
│   │   ├── inference/
│   │   ├── recipe/
│   │   ├── result/
│   │   ├── io/
│   │   └── log/
│   └── tests/
│
├── python/                            # Python 绑定 [Phase 1]
│   ├── bindings/                      # pybind11 绑定代码
│   ├── vxl/                           # Python 包 (import vxl)
│   │   ├── __init__.py
│   │   ├── camera.py
│   │   ├── reconstruct.py
│   │   ├── inspect.py
│   │   └── utils.py
│   ├── examples/
│   └── tests/
│
├── demo/                              # PyQt Demo 应用 [Phase 1-2]
│   ├── main.py
│   ├── views/
│   │   ├── capture_view.py            # 采集预览
│   │   ├── calib_view.py              # 标定向导
│   │   ├── inspect_view.py            # 检测调试
│   │   ├── result_view.py             # 结果分析
│   │   └── run_view.py                # 运行模式
│   ├── widgets/
│   │   ├── image_viewer.py            # 2D 图像查看器
│   │   ├── heightmap_viewer.py        # 高度图/色谱图
│   │   ├── pointcloud_viewer.py       # 3D 点云
│   │   └── roi_editor.py              # ROI 编辑器
│   └── resources/
│
├── studio/                            # Studio [Phase 3-4, 当前空]
│   └── README.md                      # 说明：Phase 3-4 实施
│
├── vxlapp/                            # VxlApp 运行端 [Phase 3-4, 当前空]
│   └── README.md                      # 说明：Phase 3-4 实施
│
├── 3rds/                              # 第三方库源码（引用版本存档）
│   ├── README.md                      # 版本清单与更新规则
│   ├── opencv/                        # OpenCV
│   ├── open3d/                        # Open3D
│   ├── pybind11/                      # pybind11
│   ├── onnxruntime/                   # ONNX Runtime
│   └── ...                            # 其他依赖
│
├── models/                            # AI 模型
│   ├── anomaly/
│   └── pretrained/
│
├── recipes/                           # 检测模板
│   ├── pcb_smt/
│   ├── flatness/
│   └── surface_defect/
│
├── tools/                             # 辅助工具
│   ├── data_collector/                # 样本采集
│   └── label_tool/                    # 标注工具
│
└── docs/
```

---

## 12. 开发路线图

### Phase 1：Core 引擎（2-3 个月）

**目标：** 结构光采集到检测输出全链路跑通 + Python API 可用。

- [ ] 相机 SDK 封装（自有结构光相机）
- [ ] 结构光重建管线
  - [ ] N-step PSP 相位计算
  - [ ] 多频外差 / 时间相位展开
  - [ ] 三维坐标计算
  - [ ] 点云后处理（去噪、去飞点）
  - [ ] 高度图生成（投影、插值、分辨率控制）
- [ ] 相机-投影器标定
- [ ] 高度图基本处理（基准面拟合、ROI、滤波）
- [ ] 3D 检测算子（height_measure, height_threshold, defect_cluster, ref_plane_fit, flatness）
- [ ] 统一数据对象（types.h 完整实现）
- [ ] Python 绑定（pybind11）
- [ ] Recipe 加载/保存（JSON）
- [ ] CMake + vcpkg 构建系统
- [ ] 单元测试

**交付物：** `libvxl_core` + `vxl` Python 包，命令行可跑 PCB 检测。

### Phase 2：Demo 应用（2-3 个月）

**目标：** PyQt Demo 可用，能去客户现场演示。

- [ ] PyQt 主界面（Dock 布局、工程/运行模式切换）
- [ ] ImageViewer / HeightMapViewer / PointCloudViewer
- [ ] 采集预览（2D + 高度图实时）
- [ ] 标定向导
- [ ] ROIEditor（交互式绘制）
- [ ] 检测调试界面（参数面板 + 即时预览）
- [ ] 运行模式界面（OK/NG + 统计 + 日志）
- [ ] Recipe 保存/加载
- [ ] 2D 检测算子（模板匹配、Blob、OCR）
- [ ] 异常检测集成（Anomalib）
- [ ] 基本报表（检测记录 + NG 图像保存）

**交付物：** 可演示的 PyQt 桌面应用，客户现场 POC。

### Phase 3：产品化 + 上层框架启动（3-4 个月）

- [ ] 多相机支持
- [ ] IO / PLC 对接（io.h, device.h 实现）
- [ ] 用户权限 / 审计日志 / 追溯
- [ ] 样本管理
- [ ] coplanarity / template_compare 算子
- [ ] Script/Flow 层基础（Python 脚本节点 + Pipeline JSON）
- [ ] VxlApp 原型（C++ 宿主 + Dear PyGui 基本 UI）
- [ ] .vxap 包格式定义与加载
- [ ] 安装包 / 部署方案

### Phase 4：平台化（条件触发）

**前提：已交付 3+ 客户。**

- [ ] Studio 实现（Tauri + WebView）
- [ ] VxlApp 完整实现（流程引擎 + Dear PyGui 全功能）
- [ ] 插件系统（plugin_api.h 实现）
- [ ] 机器人引导（guidance.h 实现）
- [ ] VLM 辅助（vlm.h 实现，Copilot 角色）
- [ ] 远程通信 / 服务化（transport.h）
- [ ] 更多行业模板

---

## 13. 竞品定位

| 对手 | 威胁 | 应对 |
|------|------|------|
| **基恩士** | 一体化，品牌强，但贵 | 价格 + 定制灵活性 |
| **海康机器人** | 生态大，VisionMaster 免费 | 3D 检测深度差异化 |
| **国产结构光厂商** | 硬件价格战 | 软件 + 平台化差异 |
| **客户自研** | 用 OpenCV/HALCON 自己做 | 开箱即用 + Python API + 平台演进 |

参考学习：HALCON（算子体系）、ZEISS INSPECT（3D 工作流）、Cognex VisionPro（产线部署感）、海康 VisionMaster（产品结构）、凌云光 VisionWARE（AI+规则融合）

---

## 14. VLM 集成策略（Phase 4）

VLM 定位为 Copilot，不替代检测引擎：

| 用途 | 说明 |
|------|------|
| 辅助标注 | 给未标注样本建议标签 |
| 辅助复判 | 人工不确定时辅助判断 |
| 辅助报告 | 总结批次质检结果 |
| 辅助建站 | 建议光学方案、阈值初值、ROI |

---

## 附录：与历史方案的关系

| 内容 | 来源 |
|------|------|
| 四层架构、Studio/VxlApp 分离、.vxap 包、三级二开、VLM 策略 | 老架构（product-design-v0.1.md） |
| 结构光重建深度设计、PCB 单场景聚焦、竞品分析、Phase 分期 | 新架构（product-design-claude-v0.1.md） |
| Demo 作为 Core 消费者、长短期策略分离、两者融合 | 本文档（统一版） |

详细对比见 [architecture-comparison.md](architecture-comparison.md)。
