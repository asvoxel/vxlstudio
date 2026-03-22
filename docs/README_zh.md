# VxlStudio 中文文档

开源 3D + 2D 工业检测平台，专为结构光深度相机设计。

> [English](../README.md)

## VxlStudio 是什么？

VxlStudio 是一套完整的机器视觉框架，用于自动化缺陷检测和尺寸测量。它将 3D 重建、AI 推理和机器人引导集成在一个可扩展的平台中。

**核心库 (Core)** — 高性能 C++17 引擎，20+ 模块：

| 模块 | 能力 |
|------|------|
| **Reconstruct** | 结构光 3D 表面重建 |
| **HeightMap** | 2.5D 高度图处理与分析 |
| **PointCloud** | 3D 点云操作 |
| **Inspector3D** | 3D 缺陷检测算子（平面度、阶差、体积） |
| **Inspector2D** | 2D 图像分析算子（连通域、边缘、模板匹配） |
| **Calibration** | 相机、投影仪、手眼标定 |
| **Inference** | ONNX Runtime 模型推理（YOLO、异常检测） |
| **Compute** | GPU 抽象层 — CPU / CUDA / Metal 后端 |
| **Pipeline** | JSON 定义的检测工作流 |
| **Guidance** | 机器人抓取位姿计算（3D / 2.5D） |
| **Camera** | 设备抽象（结构光、2D、仿真相机） |
| **Transport** | JSON-over-TCP 通信协议 |
| **IO/Device** | PLC 和数字 I/O 集成 |
| **Plugin** | C ABI 动态插件系统 |
| **VisualAI** | 视觉语言模型集成 |

**VxlApp** — 轻量运行时，执行 `.vxap` 检测包。支持 headless（产线）和 GUI（调试）两种模式。

**Demo** — PySide6 参考应用，展示 Core API 的完整用法。

**VxlStudio IDE** — 可视化 Pipeline 设计器（闭源，二进制下载见 [Releases](https://github.com/asvoxel/vxlstudio/releases)）。

## 架构

```
┌──────────────────────────────────────────────────┐
│  Studio IDE (二进制)   │  VxlApp (Python 运行时)   │
├────────────────────────┴─────────────────────────┤
│              Python 绑定 (pybind11)                │
├───────────────────────────────────────────────────┤
│               libvxl_core (C++17)                  │
│  相机 │ 重建 │ 检测 │ Pipeline │ AI 推理           │
├───────────────────────────────────────────────────┤
│  OpenCV │ ONNX Runtime │ spdlog │ SQLite │ CUDA   │
└───────────────────────────────────────────────────┘
```

## 快速开始

### 环境要求

- CMake 3.21+
- C++17 编译器（GCC 9+、Clang 14+、MSVC 2019+）
- Python 3.10+（绑定、demo、vxlapp）
- [vcpkg](https://github.com/microsoft/vcpkg)（推荐用于依赖管理）

### 编译

```bash
# 通过 vcpkg 安装依赖
vcpkg install opencv4 nlohmann-json spdlog pybind11

# 编译
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=[vcpkg-root]/scripts/buildsystems/vcpkg.cmake
make -j$(nproc)
```

### 运行 Demo

```bash
# 安装 Python 包
pip install -e python/

# 运行 PyQt Demo
python -m demo.main
```

### 运行 VxlApp

```bash
# 运行检测包
python -m vxlapp.main examples/pcb_demo.vxap

# Headless 模式（产线部署）
python -m vxlapp.main examples/pcb_demo.vxap --headless --once
```

## 项目结构

```
VxlStudio/
├── core/               # C++ 引擎 (libvxl_core)
│   ├── include/vxl/    # 26 个公开头文件
│   └── src/            # 模块实现
├── python/             # pybind11 绑定 → import vxl
├── demo/               # PySide6 演示应用
├── vxlapp/             # .vxap 包运行时
├── plugins/            # 插件示例 (C ABI)
├── recipes/            # 行业模板（PCB、平面度、表面缺陷）
├── tools/              # 打包和模型工具
├── docs/               # 设计文档
└── cmake/              # CMake 辅助模块
```

## 行业检测模板

开箱即用的检测配方：

| 模板 | 应用场景 |
|------|---------|
| PCB 焊点 | 焊接质量、漏件、桥接检测 |
| 平面度 | 表面平面度测量 |
| 表面缺陷 | 划痕、凹坑、污渍检测 |

## 插件系统

通过 C ABI 插件扩展 VxlStudio：

```cpp
#include <vxl/plugin_api.h>

VXL_PLUGIN_EXPORT int vxl_plugin_init(const vxl_plugin_host_t* host) {
    host->register_provider("my_camera", &my_camera_create);
    return 0;
}
```

完整示例见 `plugins/builtin/sample_plugin/`。

## Studio IDE

可视化 Pipeline 设计器提供预编译安装包：

- [下载最新版本](https://github.com/asvoxel/vxlstudio/releases)
- macOS (.dmg) | Linux (.deb) | Windows (.exe)

## 相关项目

- VxlSense SDK（深度相机驱动）：[github.com/asvoxel/vxlsense-sdk](https://github.com/asvoxel/vxlsense-sdk)
- VxlROS2（ROS2 集成）：[github.com/asvoxel/vxlros2](https://github.com/asvoxel/vxlros2)
- 官网：[asvoxel.com](https://asvoxel.com)

## 许可证

Apache-2.0 — 详见 [LICENSE](../LICENSE)。
