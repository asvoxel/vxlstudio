# VxlStudio

3D+2D 缺陷检测平台，原生适配自有结构光工业相机。

## 设计文档

**唯一执行依据：** [docs/design/](docs/design/)（拆分为多个文件，索引见 [README.md](docs/design/README.md)）

| 文档 | 内容 |
|------|------|
| [architecture.md](docs/design/architecture.md) | 产品定位、四层架构、技术选型、竞品 |
| [core.md](docs/design/core.md) | Core 模块、数据对象、设备抽象、检测算子、Python API |
| [threading.md](docs/design/threading.md) | 线程模型、SharedBuffer 引用计数、MessageBus、性能指标 |
| [recipe-schema.md](docs/design/recipe-schema.md) | Recipe JSON Schema 定义与示例 |
| [demo.md](docs/design/demo.md) | PyQt Demo 应用 |
| [studio.md](docs/design/studio.md) | Studio 设计（Tauri + WebView） |
| [vxlapp.md](docs/design/vxlapp.md) | VxlApp 设计（C++ + Dear PyGui） |
| [roadmap.md](docs/design/roadmap.md) | 开发路线图、目录结构、跨平台策略 |
| [phase3-decisions.md](docs/design/phase3-decisions.md) | Phase 3 决策记录（相机/PLC/GPU/审计等） |
| [phase3-plan.md](docs/design/phase3-plan.md) | Phase 3 执行计划（3 个 Sprint、6 条工作线） |

历史文档见 [docs/archive/](docs/archive/)（仅供参考，不作为开发依据）。

## 总体架构（四层）

```
Studio (Tauri+WebView)  →  VxlApp (C+++DearPyGui)  →  Script/Flow (Python)  →  Core (C++)
[scaffold]                  [implemented]               [implemented]            [implemented]
```

所有四个阶段均已实现。Studio 为 scaffold 状态，其余模块已完成核心功能。

## 模块总览

```
libvxl_core
├── Camera / CameraManager   # 采集（结构光 + 2D + 多相机管理）
├── Reconstruct              # 结构光重建（核心护城河）+ IDepthProvider
├── Calibration              # 标定（相机、投影器、手眼）
├── HeightMap / PointCloud   # 高度图 & 点云处理
├── Inspector3D / Inspector2D # 3D & 2D 检测算子
├── Inference                # ONNX Runtime 模型推理
├── Compute                  # GPU 抽象（CPU/CUDA/Metal）
├── Pipeline                 # 线性执行管线（JSON 可配置）
├── Audit                    # 用户权限 / 审计日志（SQLite）
├── Plugin                   # 插件系统（C ABI 动态加载）
├── VLM                      # 视觉语言模型辅助（Copilot）
├── Transport                # 远程通信（JSON-over-TCP）
├── Guidance                 # 机器人引导（抓取位姿计算）
├── IO / Device              # PLC / 数字 IO
├── Recipe / Result          # 检测方案 & 结果
└── Log                      # 日志、图像保存、追溯
```

## 目录结构

- `core/` — C++ 动态库 `libvxl_core`，不依赖 GUI，命名空间 `vxl::`
  - `include/vxl/` — 26 个公开头文件
  - `src/` — 按模块分目录（camera, reconstruct, calibration, height_map, point_cloud, inspector_3d, inspector_2d, inference, compute, pipeline, audit, plugin, vlm, transport, guidance, io, log, recipe, result）
  - `tests/` — 单元测试
- `python/` — pybind11 绑定，Python 包 `vxl`（`import vxl`）
- `demo/` — PyQt Demo 应用，Core 的使用示范 + 客户演示工具
- `studio/` — Tauri 项目设计器（scaffold）
- `vxlapp/` — 轻量运行端，加载 .vxap 包（headless + GUI runner）
- `plugins/` — 插件目录
- `3rds/` — 第三方库源码存档
- `models/` — AI 模型
- `recipes/` — 行业检测模板
- `tools/` — 辅助工具
- `scripts/` — 发布脚本
  - [`publish_opensource.sh`](scripts/publish_opensource.sh) — 同步公开源码到 GitHub（排除 studio/models）
  - [`clean_pub_repo.sh`](scripts/clean_pub_repo.sh) — 清理公开仓库中残留的私有文件
- `docs/design/` — 设计文档

## 技术栈

**Core (C++):** C++17 / CMake / vcpkg / OpenCV / Open3D / ONNX Runtime / SQLite / spdlog / nlohmann-json / pybind11 / Google Test

**上层框架:** Tauri 2 / Rust / TypeScript / React (Studio) / Dear PyGui (VxlApp) / PyQt6/PySide6 (Demo)

## 核心约束

- Core 是纯库，不管理线程；应用层决定线程策略
- 大数据（图像/点云）用 `SharedBuffer` 引用计数管理，线程间零拷贝传递
- 模块间通信用 `MessageBus` 发布/订阅，解耦采集/处理/输出/GUI
- 公开头文件在 `core/include/vxl/`，命名空间 `vxl::`
- 统一数据对象提供与 OpenCV/Open3D/numpy 的零拷贝互转
- **包装硬件，开放算法**：设备接口完整包装，通用图像/点云处理让用户直接用第三方库
- 统一错误处理：C++ 用 `vxl::Result<T>` + ErrorCode，Python 转异常
- 统一日志：多 sink、文件轮转、图像按需保存
- 结构光重建（`core/src/reconstruct/`）是核心护城河，自研
- GPU 计算通过 `IComputeEngine` 抽象，CPU 后端已实现，CUDA/Metal 为编译期可选 stub
- Pipeline 用 JSON 定义检测流程，支持 CUSTOM 步骤回调
- 审计日志使用 SQLite 存储，SHA-256 防篡改
- 插件通过 C ABI 动态加载（.so/.dylib/.dll）
- VLM 为 Copilot 角色（离线辅助），不参与实时检测
- Transport 使用 JSON-over-TCP 协议
- Guidance 支持 3D 和 2.5D 抓取位姿计算
- 初始平台 macOS，最终 Studio(macOS/Windows)，VxlApp(Linux 重点/macOS/Windows)

## 当前阶段

v0.4.0 — 所有四个阶段均已实现，稳定化和生产加固进行中。任务清单见 [roadmap.md](docs/design/roadmap.md)。

## 仓库与发布

| 仓库 | 地址 | 内容 |
|------|------|------|
| 私有全量 | `git@git.asfly.ltd:ASVoxel/vxlstudio.git` | 全部源码（origin） |
| 公开开源 | `github.com/asvoxel/vxlstudio` | 排除 studio/ models/（github） |

### 日常开发

`git push` 默认推送到 origin (asfly.ltd)。pre-push hook 确保推向任何远程时 origin 总是同步。

### 发版流程

```bash
# 1. 确保代码已提交到 origin
git push

# 2. 同步公开部分到 GitHub（预览）
./scripts/publish_opensource.sh

# 3. 确认无误后推送 + 打 tag
./scripts/publish_opensource.sh --push

# 4. 如需发布 Studio 二进制（将 .dmg/.exe/.deb 放入 studio-releases/ 后）
./scripts/publish_opensource.sh --release
```

### 公开 vs 私有

| 公开（源码） | 私有 |
|-------------|------|
| core/ python/ demo/ vxlapp/ | studio/ (二进制通过 Release 发布) |
| plugins/ recipes/ tools/ docs/ | models/ (预训练权重) |
| cmake/ CMakeLists.txt vcpkg.json | .claude/ |
