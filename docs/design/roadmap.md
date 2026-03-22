# 开发路线图

## 跨平台策略

| 组件 | 初始平台 | 最终平台 |
|------|----------|----------|
| Core | **macOS** | macOS / Windows / Linux |
| Demo | **macOS** | macOS / Windows |
| Studio | macOS | macOS / Windows |
| VxlApp | macOS | **Linux（重点）** / macOS / Windows |

Phase 1 在 macOS 上开发和验证，跑通后再处理跨平台。

GPU 加速：macOS 用 Metal 或 OpenCL，Linux/Windows 用 CUDA。CPU 后端已实现，CUDA/Metal 为 stub。

## Phase 1：Core 引擎（2-3 个月）

**目标：** 结构光采集到检测输出全链路跑通 + Python API 可用。

- [x] CMake + vcpkg 构建系统（macOS）
- [x] 统一数据对象（types.h：Image, HeightMap, PointCloud... + SharedBuffer 引用计数）
- [x] 统一错误处理（error.h：ErrorCode + Result\<T\>）
- [x] 统一日志（log.h：多 sink、文件轮转）
- [x] 消息总线（MessageBus：发布/订阅 + 线程安全）
- [x] 相机 SDK 封装（自有结构光相机）
- [x] 结构光重建管线
  - [x] N-step PSP 相位计算
  - [x] 多频外差 / 时间相位展开
  - [x] 三维坐标计算
  - [x] 点云后处理（去噪、去飞点）
  - [x] 高度图生成（投影、插值、分辨率控制）
- [x] 相机-投影器标定
- [x] 高度图处理（基准面拟合、ROI、滤波）
- [x] 3D 检测算子（height_measure, height_threshold, defect_cluster, ref_plane_fit, flatness）
- [x] Recipe 加载/保存（JSON Schema）
- [x] Python 绑定（pybind11）
- [x] 单元测试
- [x] IDepthProvider 多源深度接口（from_stereo, from_depth, register_provider）

**交付物：** `libvxl_core` + `vxl` Python 包，命令行跑 PCB 检测 demo。

## Phase 2：Demo 应用（2-3 个月）

**目标：** PyQt Demo 可用，客户现场演示。

- [x] PyQt 主界面（Dock 布局、工程/运行模式切换）— main_window, engineering_view, run_view
- [x] ImageViewer / HeightMapViewer / PointCloudViewer（placeholder）
- [x] 采集预览（2D + 高度图实时）
- [x] 标定向导（calibration_wizard.py）
- [x] ROIEditor（交互式绘制）
- [x] 检测调试界面（参数面板 + 即时预览）
- [x] 运行模式界面（OK/NG + 统计 + 日志）
- [x] Recipe 保存/加载
- [x] 2D 检测算子（inspector_2d.h：template_match, blob_analysis, edge_detect, ocr, anomaly_detect — 全部 OpenCV 实现）
- [x] 推理引擎（inference.h：ONNX Runtime 已集成）
- [x] 异常检测集成（Anomalib ONNX Runtime 推理，anomaly_detect 算子可用）
- [x] 基本报表（NG 图像保存 + 日志系统）

**交付物：** 可演示的 PyQt 桌面应用。

## Phase 3：产品化 + 上层框架启动（3-4 个月）

- [x] 多相机支持（camera_manager.h：CameraManager）
- [x] IO / PLC 对接（io.h, device.h — Modbus TCP + Serial GPIO）
- [x] 用户权限 / 审计日志 / 追溯（audit.h：UserManager + AuditLog，SQLite + salted SHA-256）
- [ ] 样本管理 UI
- [x] coplanarity / template_compare 算子
- [x] Script/Flow 层基础（pipeline.h：Pipeline JSON 定义 + 线性执行）
- [x] GPU 抽象（compute.h：IComputeEngine，CPU 后端已实现，CUDA/Metal stub）
- [x] VxlApp 原型（C++ 宿主 + Dear PyGui + headless runner）
- [x] .vxap 包格式定义与加载（vxap_loader.py）
- [ ] 安装包 / 部署方案
- [ ] Windows / Linux 移植

## Phase 4：平台化（条件触发：已交付 3+ 客户）

- [x] 插件系统（plugin_api.h：PluginManager + sample plugin，C ABI 动态加载）
- [x] VLM 辅助（vlm.h：VLMAssistant，多 provider 支持）
- [x] 远程通信 / 服务化（transport.h：TransportServer + TransportClient，JSON-over-TCP）
- [x] 机器人引导（guidance.h：GuidanceEngine，3D/2.5D 抓取位姿）
- [x] Studio scaffold（Tauri + React，VSCode 风格布局）
- [ ] Studio 完整实现（后端连接）
- [ ] VxlApp 完整 GUI 实现
- [ ] 更多行业模板

## 项目目录结构

```
VxlStudio/
├── core/                    # C++ Core 动态库
│   ├── CMakeLists.txt
│   ├── include/vxl/         # 26 个公开头文件
│   ├── src/                 # 按模块分目录（19 个子目录）
│   │   ├── audit/           # 用户权限 + 审计日志
│   │   ├── calibration/     # 标定
│   │   ├── camera/          # 相机采集
│   │   ├── compute/         # GPU 抽象（CPU/CUDA/Metal）
│   │   ├── guidance/        # 机器人引导
│   │   ├── height_map/      # 高度图处理
│   │   ├── inference/       # ONNX Runtime 推理
│   │   ├── inspector_2d/    # 2D 检测算子
│   │   ├── inspector_3d/    # 3D 检测算子
│   │   ├── io/              # PLC / 数字 IO
│   │   ├── log/             # 日志
│   │   ├── pipeline/        # 检测管线
│   │   ├── plugin/          # 插件系统
│   │   ├── point_cloud/     # 点云操作
│   │   ├── recipe/          # 检测方案
│   │   ├── reconstruct/     # 结构光重建
│   │   ├── result/          # 检测结果
│   │   ├── transport/       # 远程通信
│   │   └── vlm/             # VLM 辅助
│   └── tests/
├── python/                  # Python 绑定 (pybind11)
│   ├── bindings/
│   ├── vxl/                 # import vxl
│   ├── examples/
│   └── tests/
├── demo/                    # PyQt Demo
│   ├── views/
│   ├── widgets/
│   └── resources/
├── studio/                  # Tauri Studio [scaffold]
│   ├── src/                 # TypeScript + React
│   └── src-tauri/           # Rust backend
├── vxlapp/                  # VxlApp 运行端
│   ├── main.py
│   ├── gui_runner.py        # Dear PyGui GUI
│   ├── headless_runner.py   # 无头运行
│   └── vxap_loader.py       # .vxap 包加载器
├── plugins/                 # 插件目录
├── 3rds/                    # 第三方库源码
├── models/                  # AI 模型
├── recipes/                 # 检测模板
├── tools/                   # 辅助工具
└── docs/
    ├── design/              # 设计文档（当前）
    └── archive/             # 历史文档
```
