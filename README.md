# VxlStudio

Open-source 3D + 2D industrial inspection platform, purpose-built for structured light depth cameras.

> [中文文档](docs/README_zh.md)

## What is VxlStudio?

VxlStudio is a full-stack machine vision framework for automated defect detection and measurement. It combines 3D reconstruction, AI inference, and robot guidance into a single, extensible platform.

**Core Library** — High-performance C++17 engine with 20+ modules:

| Module | Capability |
|--------|-----------|
| **Reconstruct** | Structured light 3D surface reconstruction |
| **HeightMap** | 2.5D height map processing and analysis |
| **PointCloud** | 3D point cloud operations |
| **Inspector3D** | 3D defect detection operators (flatness, step height, volume) |
| **Inspector2D** | 2D image analysis operators (blob, edge, template matching) |
| **Calibration** | Camera, projector, and hand-eye calibration |
| **Inference** | ONNX Runtime model execution (YOLO, anomaly detection) |
| **Compute** | GPU abstraction — CPU, CUDA, Metal backends |
| **Pipeline** | JSON-defined inspection workflows |
| **Guidance** | Robot grasp pose calculation (3D/2.5D) |
| **Camera** | Device abstraction (structured light, 2D, simulated) |
| **Transport** | JSON-over-TCP communication protocol |
| **IO/Device** | PLC and digital I/O integration |
| **Plugin** | C ABI dynamic plugin system |
| **VisualAI** | Vision-language model integration |

**VxlApp** — Lightweight runtime that executes `.vxap` inspection packages. Runs headless (production lines) or with GUI (debugging).

**Demo** — PySide6 reference application demonstrating full Core API usage.

**VxlStudio IDE** — Visual pipeline designer (closed-source, available as binary download in [Releases](https://github.com/asvoxel/vxlstudio/releases)).

## Architecture

```
┌──────────────────────────────────────────────────┐
│  Studio IDE (binary)   │  VxlApp (Python runtime) │
├────────────────────────┴─────────────────────────┤
│              Python Bindings (pybind11)            │
├───────────────────────────────────────────────────┤
│               libvxl_core (C++17)                  │
│  Camera │ Reconstruct │ Inspect │ Pipeline │ AI    │
├───────────────────────────────────────────────────┤
│  OpenCV │ ONNX Runtime │ spdlog │ SQLite │ CUDA   │
└───────────────────────────────────────────────────┘
```

## Quick Start

### Prerequisites

- CMake 3.21+
- C++17 compiler (GCC 9+, Clang 14+, MSVC 2019+)
- Python 3.10+ (for bindings, demo, vxlapp)
- [vcpkg](https://github.com/microsoft/vcpkg) (recommended for dependencies)

### Build

```bash
# Install dependencies via vcpkg
vcpkg install opencv4 nlohmann-json spdlog pybind11

# Build
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=[vcpkg-root]/scripts/buildsystems/vcpkg.cmake
make -j$(nproc)
```

### Run Demo

```bash
# Install Python package
pip install -e python/

# Run PyQt demo
python -m demo.main
```

### Run VxlApp

```bash
# Run an inspection package
python -m vxlapp.main examples/pcb_demo.vxap

# Headless mode (for production)
python -m vxlapp.main examples/pcb_demo.vxap --headless --once
```

## Project Structure

```
VxlStudio/
├── core/               # C++ engine (libvxl_core)
│   ├── include/vxl/    # 26 public headers
│   └── src/            # Module implementations
├── python/             # pybind11 bindings → import vxl
├── demo/               # PySide6 demo application
├── vxlapp/             # .vxap package runtime
├── plugins/            # Plugin examples (C ABI)
├── recipes/            # Industry templates (PCB, flatness, surface)
├── tools/              # Packaging and model utilities
├── docs/               # Design documentation
└── cmake/              # CMake helper modules
```

## Industry Templates

Ready-to-use inspection recipes:

| Recipe | Application |
|--------|------------|
| PCB solder joint | Solder quality, missing component, bridge detection |
| Flatness | Surface flatness measurement |
| Surface defect | Scratch, dent, stain detection |

## Plugin System

Extend VxlStudio with C ABI plugins:

```cpp
#include <vxl/plugin_api.h>

VXL_PLUGIN_EXPORT int vxl_plugin_init(const vxl_plugin_host_t* host) {
    host->register_provider("my_camera", &my_camera_create);
    return 0;
}
```

See `plugins/builtin/sample_plugin/` for a complete example.

## Studio IDE

The visual pipeline designer is available as a pre-built binary:

- [Download latest release](https://github.com/asvoxel/vxlstudio/releases)
- macOS (.dmg) | Linux (.deb) | Windows (.exe)

## Links

- VxlSense SDK (depth camera driver): [github.com/asvoxel/vxlsense-sdk](https://github.com/asvoxel/vxlsense-sdk)
- VxlROS2 (ROS2 integration): [github.com/asvoxel/vxlros2](https://github.com/asvoxel/vxlros2)
- Website: [asvoxel.com](https://asvoxel.com)

## License

Apache-2.0 — see [LICENSE](LICENSE).
