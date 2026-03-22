# VxlStudio 产品设计 v0.1

> **状态：已合并到 product-design-unified-v0.2.md。** 本方案的执行策略（Core 优先、PCB 聚焦、Phase 分期、结构光重建深度设计等）已纳入统一版。本文保留作为详细参考。

---

## 1. 产品定位

> **面向制造业质检的 3D+2D 缺陷检测软件，原生适配自有结构光相机，解决"2D 看不出来、高端 3D 用不起"的中间地带问题。**

核心资产是结构光相机硬件（Z 轴精度 ~0.15mm）。软件的目标是让硬件更好卖、更容易交付、更容易复制到新客户。

### 精度定位

| 精度范围 | 适合场景 | 竞争对手 |
|---------|---------|---------|
| 0.01-0.05mm | 晶圆、精密电子、光学元件 | ZEISS、基恩士、海克斯康 |
| **0.05-0.2mm** | **PCB 元件高度、塑料件翘曲、金属件缺口、焊点共面性** | **中低端结构光厂商、2D AOI 升级** |
| 0.2-1mm | 大型铸件、汽车钣金、物流分拣 | 低成本 3D / TOF |

### 差异化

1. **3D 检测开箱即用** — 买相机就能检测，不需另购 HALCON 授权
2. **软硬一体标定** — 相机和软件同源，标定精度和稳定性最优
3. **行业模板** — PCB 检测、平面度检测等模板开箱即用
4. **Python 可定制** — 比基恩士灵活，比客户自研省时间

---

## 2. 场景优先级

### 第一场景：PCB / SMT 检测

结构光 3D 在 PCB 检测中价值最明确：焊点高度、元件浮高、共面性、锡量、桥接。缺陷定义有 IPC 标准，市场大、复购强。

```
结构光采集 → 3D 重建 → 高度图 → 基准面
                                    ↓
                          PCB 专项检测算子
                          ├── 焊点高度/体积
                          ├── 元件浮高/偏移/缺失
                          ├── 共面性
                          ├── 桥接/短路
                          └── 翘曲度
                                    ↓
                          OK/NG + 缺陷分类 → 报表/MES/IO
```

2D 通道辅助：丝印、极性、异物、脏污。

### 第二场景：塑料件 / 金属件表面检测

鼓包/缩水/翘曲、缺口/毛刺/变形 — 用的是同一套高度图分析能力，从 PCB 场景自然延伸。

### 暂缓

- **晶圆/芯片** — 精度不够（需亚微米级），光学链路差异大
- **分拣/码垛** — 不同产品方向，市场已有成熟方案，等检测站稳再扩展
- **VLM 集成** — 当前延迟高、确定性差，不适合产线判定，等核心跑通后再加辅助功能

---

## 3. 技术架构

### 原则

1. **最少技术栈**：C++ + Python + Qt，不引入额外语言和框架
2. **先单体应用，再拆分**：一个程序跑通全流程，等 Phase 4 再考虑分离
3. **Core 必须独立于 GUI**：Core 是不变的基础能力，未来无论上什么 GUI 都不影响
4. **接口预留扩展，实现只做当前**：头文件定义清晰边界，实现先满足第一场景

### 两层架构

```
┌──────────────────────────────────────────────────────────────┐
│  应用层 (Qt 6 Widgets)                                        │
│  工程模式: 采集 | 标定 | 调试 | 样本管理 | Recipe              │
│  运行模式: 实时图像 | OK/NG | 统计 | 告警 | 日志               │
├──────────────────────────────────────────────────────────────┤
│  Core (C++ 动态库 + Python 绑定)                              │
│  采集 | 重建 | 标定 | 高度图 | 检测3D | 检测2D | 推理 | IO    │
└──────────────────────────────────────────────────────────────┘
```

**Core 是独立的 C++ 动态库（`libvxl_core`），不依赖 Qt，不依赖任何 GUI。** 这意味着：
- 未来可以接任何 GUI（Qt / Web / 终端）
- 可以作为 SDK 给客户集成到自有系统
- 可以跑无头模式（命令行 / 服务 / 边缘盒子）
- Python 绑定天然可用

应用层当前用 Qt，但它只是 Core 的一个消费者。

### 扩展预留（当前不实现）

Core 的头文件设计中预留以下扩展点，但 Phase 1-3 不实现：

| 扩展点 | 预留方式 | 未来用途 |
|--------|---------|---------|
| **插件加载** | `core/plugin_api.h` 定义 C ABI 插件接口 | 第三方算法/设备驱动 |
| **设备抽象层** | `core/device.h` 定义 ICamera/ITrigger/IIO 接口族 | 多种相机/PLC/IO 适配 |
| **多场景 Recipe** | `Recipe` 类支持 type 字段 | PCB / 平面度 / 表面 / 自定义 |
| **机器人引导** | `core/guidance.h` 预留头文件 | 分拣/码垛/抓取 |
| **流程编排** | Core API 本身就是编排原语 | 未来可在上层加 Pipeline 引擎 |
| **远程通信** | `core/transport.h` 预留 | gRPC / WebSocket 服务化 |
| **VLM 辅助** | `core/vlm.h` 预留接口 | 辅助标注/复判/报告（Copilot 角色） |
| **Studio/Runtime 分离** | Qt 应用中工程逻辑与运行逻辑分模块 | 未来可拆成独立程序（参考归档方案） |

---

## 4. Core 设计（重点）

### 4.1 模块划分

```
libvxl_core.so / vxl_core.dll
│
├── vxl::Camera          # 相机采集（结构光 + 2D 工业相机）
├── vxl::Reconstruct     # 结构光重建（护城河）
├── vxl::Calibration     # 标定（相机、投影器、手眼）
├── vxl::HeightMap       # 高度图处理（基准面、ROI、滤波、插值）
├── vxl::PointCloud      # 点云操作（配准、滤波、分割）
├── vxl::Inspector3D     # 3D 检测算子
├── vxl::Inspector2D     # 2D 检测算子
├── vxl::Inference       # 模型推理（ONNX Runtime）
├── vxl::Recipe          # 检测方案（参数集 + 算子组合）
├── vxl::Result          # 检测结果（OK/NG、缺陷列表、测量值）
├── vxl::IO              # PLC / 数字 IO / 串口
└── vxl::Log             # 日志、图像保存、追溯
```

### 4.2 结构光重建管线（核心护城河）

开源界几乎空白。OpenCV `structured_light` 模块仅支持基础格雷码+正弦，无多频外差、无相位展开优化、无反光处理。**必须自研。**

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

### 4.3 3D 检测算子

从 PCB 场景提炼，天然可复用到塑料件/金属件：

| 算子 | 输入 | 输出 | 复用性 |
|------|------|------|--------|
| `ref_plane_fit` | 高度图 + 板面 ROI | 基准平面参数 | 通用 |
| `height_measure` | 高度图 + ROI | max/min/avg 高度、体积 | 通用 |
| `coplanarity` | 高度图 + 多 ROI | 共面性偏差 | PCB/连接器 |
| `flatness` | 高度图 + ROI | 平面度值 | 通用 |
| `height_threshold` | 高度图 + 阈值 | 二值化区域 | 通用 |
| `defect_cluster` | 二值图 | 缺陷列表(面积/位置/高度) | 通用 |
| `template_compare` | 当前高度图 + 参考 | 偏差图 + NG 区域 | 通用 |

### 4.4 2D 检测算子

| 算子 | 基于 | 说明 |
|------|------|------|
| `template_match` | OpenCV | 定位 + 角度矫正 |
| `blob_analysis` | OpenCV | 面积、形状、颜色异常 |
| `edge_detect` | OpenCV | 缺口、裂纹 |
| `ocr` | OpenCV / ONNX | 丝印识别 |
| `anomaly_detect` | ONNX Runtime + Anomalib | 无监督异常检测 |

### 4.5 Python API

通过 pybind11 暴露，这就是二次开发接口：

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

# 结果
print(result.ok)       # True/False
print(result.defects)  # [{type, area, height, position}, ...]

# 完整检测循环
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

### 4.6 Core C++ 头文件清单

Phase 1 实现的头文件：

```
core/include/vxl/
├── core.h              # 全局初始化 / 版本 / 错误码
├── types.h             # 基础类型（Image, DepthMap, HeightMap, PointCloud, Mesh, ROI, Pose6D, DefectRegion, InspectionResult）
├── camera.h            # ICamera 接口 + 工厂
├── reconstruct.h       # 结构光重建管线
├── calibration.h       # 标定
├── height_map.h        # 高度图处理
├── point_cloud.h       # 点云操作
├── inspector_3d.h      # 3D 检测算子
├── inspector_2d.h      # 2D 检测算子（Phase 3）
├── inference.h         # 模型推理（Phase 3）
├── recipe.h            # 检测方案
├── result.h            # 检测结果
├── io.h                # PLC / 数字 IO（Phase 3）
└── log.h               # 日志 / 图像保存
```

预留但不实现的头文件：

```
core/include/vxl/
├── device.h            # 设备抽象接口族 ICamera/ITrigger/IIO（Phase 2-3）
├── plugin_api.h        # C ABI 插件接口（Phase 4）
├── guidance.h          # 机器人引导（Phase 4）
├── vlm.h               # VLM 辅助接口（Phase 4）
└── transport.h         # 远程通信接口（Phase 4）
```

---

## 5. 应用层设计

### Qt 桌面应用，两种模式

**工程模式（调试工程师）：**
- 相机连接与采集预览（2D + 高度图 + 点云）
- 标定向导
- 检测参数调试（交互式 ROI、阈值滑块、即时预览结果）
- 样本管理（保存 / 加载 / 标注）
- Recipe 编辑与导出

**运行模式（产线操作员）：**
- 简洁界面：实时图像 + OK/NG 大字指示
- 一键启停
- 统计面板（良率、缺陷分布）
- 告警 + 日志
- 切换 Recipe

两种模式是同一个程序通过权限/菜单切换。

### 自定义 Qt 控件

| 控件 | 用途 |
|------|------|
| `ImageViewer` | 2D 图像查看（缩放、平移、叠加标注） |
| `HeightMapViewer` | 高度图色谱显示（热力图、等高线、剖面线） |
| `PointCloudViewer` | 3D 点云（QOpenGLWidget，旋转/缩放/测量） |
| `ROIEditor` | 交互式 ROI 绘制与编辑 |
| `ParamPanel` | 检测参数面板（自动从 Recipe 生成） |
| `ResultTable` | 检测结果表格（批次、缺陷类型、统计） |

---

## 6. 技术选型

| 领域 | 选型 | 许可证 | 说明 |
|------|------|--------|------|
| Core 语言 | C++17 | - | 性能 + 生态 |
| GUI | Qt 6 Widgets | LGPL / Commercial | 工业验证最充分 |
| Python 绑定 | pybind11 | BSD | 轻量、成熟 |
| 2D 处理 | OpenCV | Apache 2.0 | |
| 3D 处理 | Open3D | MIT | 比 PCL 轻、API 现代 |
| 模型推理 | ONNX Runtime | MIT | 跨平台 CPU/GPU |
| 异常检测训练 | Anomalib | Apache 2.0 | 工业异常检测首选 |
| 构建 | CMake | - | |
| 包管理 | vcpkg | MIT | C++ 依赖管理 |

**不引入的技术（当前阶段）：** Rust / Tauri / TypeScript / React / Dear PyGui / gRPC / PCL

---

## 7. 二次开发策略

Python API + 配置文件 + GUI 参数面板覆盖 90% 的二开需求：

| 需求 | 方案 |
|------|------|
| 改阈值/参数 | Recipe JSON + GUI 参数面板 |
| 改 ROI | GUI 交互绘制 |
| 特殊判定逻辑 | Python 脚本 |
| 对接 MES | Python 脚本 |
| 前处理/后处理 | Python 脚本 |
| 新设备驱动 | C++ 插件（Phase 4） |
| 新算法 | C++ 插件（Phase 4） |

不需要"流程编排引擎"或"UI Schema" — Python 本身就是最好的编排语言。

---

## 8. 项目目录结构

```
VxlStudio/
├── core/                          # C++ Core（动态库）
│   ├── CMakeLists.txt
│   ├── include/vxl/               # 公开头文件
│   │   ├── core.h
│   │   ├── types.h
│   │   ├── camera.h
│   │   ├── reconstruct.h
│   │   ├── calibration.h
│   │   ├── height_map.h
│   │   ├── point_cloud.h
│   │   ├── inspector_3d.h
│   │   ├── inspector_2d.h
│   │   ├── inference.h
│   │   ├── recipe.h
│   │   ├── result.h
│   │   ├── io.h
│   │   ├── log.h
│   │   ├── device.h               # 预留，Phase 2-3（设备抽象接口族）
│   │   ├── plugin_api.h           # 预留，Phase 4
│   │   ├── guidance.h             # 预留，Phase 4
│   │   ├── vlm.h                  # 预留，Phase 4
│   │   └── transport.h            # 预留，Phase 4
│   ├── src/
│   │   ├── camera/
│   │   ├── reconstruct/           # 结构光重建（核心护城河）
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
├── python/                        # Python 绑定
│   ├── bindings/                  # pybind11 绑定代码
│   ├── vxl/                       # Python 包
│   │   ├── __init__.py
│   │   ├── camera.py
│   │   ├── reconstruct.py
│   │   ├── inspect.py
│   │   └── utils.py
│   ├── examples/                  # 示例脚本
│   └── tests/
├── app/                           # Qt 桌面应用
│   ├── CMakeLists.txt
│   ├── src/
│   │   ├── main.cpp
│   │   ├── mainwindow.h/cpp
│   │   ├── views/
│   │   │   ├── capture_view/      # 采集预览
│   │   │   ├── calib_view/        # 标定向导
│   │   │   ├── inspect_view/      # 检测调试
│   │   │   ├── result_view/       # 结果分析
│   │   │   └── run_view/          # 运行模式
│   │   └── widgets/
│   │       ├── image_viewer/
│   │       ├── heightmap_viewer/
│   │       ├── pointcloud_viewer/
│   │       └── roi_editor/
│   └── resources/
├── models/                        # AI 模型（异常检测、分类、分割）
│   ├── anomaly/                   # Anomalib 训练输出
│   └── pretrained/                # 预训练模型
├── recipes/                       # 预设检测模板（params + ROI + 模型引用）
│   ├── pcb_smt/
│   ├── flatness/
│   └── surface_defect/
├── tools/                         # 辅助工具
│   ├── data_collector/            # 样本采集
│   └── label_tool/                # 标注工具
└── docs/
```

---

## 9. 开发路线图

### Phase 1：Core 引擎（2-3 个月）

**目标：从结构光采集到检测输出，全链路跑通。**

- [ ] 相机 SDK 封装（自有结构光相机）
- [ ] 结构光重建管线
  - [ ] N-step PSP 相位计算
  - [ ] 多频外差 / 时间相位展开
  - [ ] 三维坐标计算
  - [ ] 点云后处理（去噪、去飞点）
  - [ ] 高度图生成（投影、插值、分辨率控制）
- [ ] 相机-投影器标定
- [ ] 高度图基本处理
  - [ ] 基准面拟合
  - [ ] ROI 定义与管理
  - [ ] 滤波
- [ ] 3D 检测算子
  - [ ] height_measure
  - [ ] height_threshold
  - [ ] defect_cluster
  - [ ] ref_plane_fit
  - [ ] flatness
- [ ] Python 绑定（pybind11）
- [ ] CMake 构建系统 + vcpkg 依赖管理
- [ ] 单元测试

**交付物：** `libvxl_core` + `vxl` Python 包，命令行可跑 PCB 检测 demo。

### Phase 2：桌面应用（2-3 个月）

**目标：有 GUI，可去客户现场演示。**

- [ ] Qt 主界面（Dock 布局）
- [ ] ImageViewer / HeightMapViewer / PointCloudViewer 控件
- [ ] 采集预览（2D + 高度图实时）
- [ ] 标定向导
- [ ] ROIEditor（交互式绘制）
- [ ] 检测调试界面（参数面板 + 即时预览）
- [ ] 运行模式界面（OK/NG + 统计 + 日志）
- [ ] Recipe 保存/加载（JSON）
- [ ] 基本报表（检测记录 + NG 图像保存）

**交付物：** 可安装桌面应用，客户现场 POC。

### Phase 3：产品化（3-4 个月）

- [ ] 2D 检测通道（模板匹配、Blob、OCR）
- [ ] Anomalib 异常检测集成
- [ ] coplanarity / template_compare 算子
- [ ] 多相机支持
- [ ] IO / PLC 对接
- [ ] 用户权限
- [ ] 审计日志 / 追溯
- [ ] 样本管理
- [ ] 安装包 + 部署方案
- [ ] 用户手册

### Phase 4：平台化（条件触发）

**前提：已交付 3+ 客户，发现明确的重复模式。**

- [ ] 提炼通用检测框架
- [ ] 插件系统（plugin_api.h 实现）
- [ ] 机器人引导（guidance.h 实现）
- [ ] Studio / Runtime 分离（参考 `product-design-v0.1.md`）
- [ ] 流程编排系统
- [ ] 远程通信 / 服务化
- [ ] 更多行业模板

---

## 10. 竞品定位

你的竞争对手不是 HALCON（它是算子库，不卖相机），而是卖"相机+软件"整体方案的厂商：

| 对手 | 威胁 | 应对 |
|------|------|------|
| **基恩士** | 一体化，品牌强，但贵 | 价格 + 定制灵活性 |
| **海康机器人** | 生态大，VisionMaster 免费 | 3D 检测深度差异化 |
| **国产结构光厂商** | 硬件价格战 | 软件能力差异化 |
| **客户自研** | 用 OpenCV/HALCON 自己做 | 开箱即用 + Python API |

---

## 附录 A：VLM 集成策略（Phase 3+ 再考虑）

VLM 当前不适合产线实时判定（延迟高、确定性差、成本高）。适合做离线辅助：

| 适合 | 不适合 |
|------|--------|
| 离线复判 | 实时判定 |
| 辅助标注 | 自动标注 |
| 报告生成 | 实时告警 |
| 客户沟通 | 算法调参 |

## 附录 B：与归档方案的对比

归档方案（`product-design-v0.1.md`）的核心思路在 Phase 4 平台化时可重新评估：

| 维度 | 当前方案 | 归档方案（Phase 4 参考） |
|------|---------|------------------------|
| 架构 | 两层（Core + App） | 四层（Core/Script/Studio/Runtime） |
| 技术栈 | C++ + Python + Qt | C++ + Rust + TS + Python + Dear PyGui |
| GUI | Qt 统一 | Tauri (Studio) + Dear PyGui (Runtime) |
| 二开 | Python API + JSON Recipe | 三级体系 + Pipeline + UI Schema |
| 部署 | 单体应用 | Studio 导出 .asapp，Runtime 加载 |
