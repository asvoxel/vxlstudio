# VxlStudio - 3D 结构光缺陷检测平台 产品设计文档 v0.1

> **状态：已合并到 product-design-unified-v0.2.md。** 本方案的架构设计（四层、Studio/Runtime 分离、三级二开、VLM 策略、.asapp 包机制等）已纳入统一版作为长期方向。本文保留作为详细参考。

---

## 1. 产品定位

**一句话定位：** 以结构光 3D 检测为核心，向 2D 缺陷检测和机器人引导两侧扩展的国产可定制视觉平台。

**核心设计思想：**

> **Core 负责能力，Studio 负责"生产 Runtime"。**
> **二次开发尽量脚本化，Runtime 尽量轻量化、可部署化。**

### 核心优势

- 自有结构光工业相机硬件（Z 轴精度约 0.15mm），以及单目工业相机产品线
- 软件原生支持结构光相机，而非后补 3D 模块
- 面向中国客户：可定制、可私有部署、可深度适配 PLC/机器人/MES

### 覆盖三类场景

| 场景 | 说明 |
|------|------|
| **2D 外观缺陷检测** | PCB、塑料件、金属件、晶圆/芯片表面 |
| **3D 几何/表面检测** | 高度差、翘曲、缺口、凹凸、平面度、轮廓偏差、焊点共面性、元件高低差 |
| **3D 引导任务** | 分拣、抓取、码垛、上下料 |

### 差异化卖点

- 原生支持结构光相机
- 2D + 3D 联合判定
- 规则法 + anomaly detection + 检测分割模型混合
- Studio 设计 + Runtime 执行的产品形态
- 支持 VLM 辅助标定、缺陷描述、报告生成、规则推荐
- 三级二次开发能力（配置 / 脚本 / 原生插件）

---

## 2. 竞品参考

### 国外重点参考

| 软件 | 学习点 |
|------|--------|
| **MVTec HALCON** | 算子体系、脚本与图形流程并存、2D/3D/DL 统一、工程部署成熟 |
| **ZEISS INSPECT** | 3D 工作流、点云到网格、nominal-actual comparison、色谱图、报告 |
| **Cognex VisionPro** | 工业软件产品感、项目部署、2D 缺陷与装配验证、低门槛 AI 导入 |

### 国内重点参考

| 软件 | 学习点 |
|------|--------|
| **海康机器人 VisionMaster** | 产品结构、图形化界面、SDK 二开、算子设计模式 |
| **凌云光 VisionWARE** | 行业解决方案化、AI + 规则融合打法 |
| **汇萃智能 HCVisionQuick** | 平台 + AI 训练 + 行业交付的产品形式 |
| **OPT SciDeepVision** | 标注、训练、评估一体化工具 |

### 产品形态类比

| 类比 | Studio | Runtime |
|------|--------|---------|
| Unity | Editor | 导出游戏 |
| Node-RED | Editor | 部署运行流 |
| PLC 编程 | 编程软件 | 下载到运行端 |
| VisionPro | Designer | 运行时执行工程 |

**VxlStudio 也是这种模式：Studio = IDE/Builder/Composer，Runtime = Player/Runner/Operator App。**

---

## 3. 技术架构（四层架构）

```
┌──────────────────────────────────────────────────────────────┐
│  Studio 层 (Tauri 2 + WebView + TS/React)                    │
│  项目设计器 | 流程编排器 | 测试器 | 打包器 | 部署器            │
│  Rust 壳层: IPC | Core 薄封装 | 文件/权限 | sidecar 调度      │
│                         ↓ 导出 .asapp                        │
├──────────────────────────────────────────────────────────────┤
│  Runtime 层 (C++ 宿主 + Python/Dear PyGui 操作界面)           │
│  加载 .asapp | 执行流程 | 操作 UI | 设备通信 | 日志           │
├──────────────────────────────────────────────────────────────┤
│  Script / Flow 层 (Python + JSON/YAML)                       │
│  脚本节点 | 流程描述 | 参数模板 | 规则表达式 | UI 脚本         │
├──────────────────────────────────────────────────────────────┤
│  Core 层 (C++)                                               │
│  采集 | 重建 | 算法 | 推理 | 标定 | 调度 | 通信 | 插件 | 日志  │
└──────────────────────────────────────────────────────────────┘
```

### 3.1 Core 层

纯 C++，保持稳定、高性能、可部署。**原则上不给现场用户直接改源码。**

**职责：**

- 图像采集（工业相机 + 结构光相机）
- 3D / 结构光点云处理与重建
- 缺陷检测核心算法
- 几何测量
- 标定（2D/3D/手眼/相机-投影器/坐标系）
- 数据模型
- 任务调度
- 流程执行引擎
- 插件加载
- 参数系统
- 日志 / 告警 / 权限
- 统一序列化格式
- 模型推理（ONNX Runtime / TensorRT）
- PLC / 机器人 / IO 通信

**统一硬件接口定义：**

```
ICamera2D / ICamera3D / IProjector / ITrigger
IIO / IRobot / IPLC / IInferenceEngine
```

**算法模块（C++ 可插拔）：**

| 模块 | 说明 |
|------|------|
| `mod_acquire_2d` | 2D 工业相机采集 |
| `mod_acquire_3d` | 结构光相机采集 |
| `mod_calib` | 标定 |
| `mod_reconstruct_sl` | 结构光重建 |
| `mod_imgproc_2d` | 2D 图像处理 |
| `mod_pointcloud_3d` | 点云处理 |
| `mod_measure_3d` | 3D 测量 |
| `mod_defect_rule` | 规则法缺陷检测 |
| `mod_defect_ai` | AI 缺陷检测（anomaly / 分割 / 分类） |
| `mod_robot_guidance` | 机器人引导 |
| `mod_report` | 报表与追溯 |
| `mod_export` | 数据导出 |

**统一数据对象（不直接暴露 OpenCV/Open3D/PCL 底层类型）：**

```
ImageFrame / DepthMap / HeightMap / PointCloudData
MeshData / Pose6D / DefectRegion / InspectionResult
```

### 3.2 Script / Flow 层

**面向二次开发。** 现场工程师做的不是"开发一个软件"，而是：选几个 Core 提供的能力块 → 用流程串起来 → 写少量脚本补空白 → 把参数和操作页面配出来。

**支持能力：**

- Python 脚本节点
- JSON / YAML / DSL 流程描述
- 参数模板
- 规则表达式
- UI 描述 schema

**Pipeline 核心概念：** Node / Port / Param / DataPacket / Recipe / ExecutionContext

**检测流程示例：**

```
采集3D → 重建高度图 → ROI → 建立基准面 → 高度差检测 → 缺陷聚类 → 结果判定 → 报表输出
```

**分拣流程示例：**

```
采集点云 → 目标分割 → 姿态估计 → 抓取点评分 → 坐标转换 → 输出机器人位姿
```

**Python 的五大角色：**

| 角色 | 说明 |
|------|------|
| **操作界面** | 通过 Dear PyGui 构建 Runtime 操作面板、图像显示、状态监控、报表展示 |
| **编排胶水** | 调用多个算法节点、规则判断、结果转化、与外部系统交互 |
| **轻度算法扩展** | 特征计算、阈值筛选、结果清洗、数据导出、调模型接口 |
| **设备流程逻辑** | 相机拍照时序、机械手抓取判定、PLC 信号联动、异常处理 |
| **自定义面板** | 二次开发工程师用 Dear PyGui 创建项目专用的交互界面 |

**关键原则：C++ 宿主管系统主干（设备、算法、线程、资源），Python 管界面与业务逻辑。**

### 3.3 Studio 层

**Studio 不是运行时主程序，而是项目设计器 / 流程编排器 / 测试器 / 打包器 / 部署器。**

**技术方案：** Tauri 2 + WebView + TypeScript / React

#### 为什么选 Tauri

Studio 大量使用表格、树、属性面板、流程编辑、搜索、文档、授权、插件管理等复杂 UI。Tauri 2 面向桌面与移动，窗口、菜单、插件体系完整，前端到 Rust 侧走 command，Rust 侧通过事件/channel 回调前端。

#### Tauri 与 Core 的关系

**Tauri 不是算法宿主，而是 Studio 壳层。** 通信路径：

```
Web 前端 (React/TS) → Tauri (Rust command) → Core SDK (C ABI / sidecar)
```

**Rust 层主要做四件事：**

1. 前端 IPC 入口
2. 调 Core 的薄封装
3. 文件系统、设备、打包、权限、安全
4. sidecar / 插件调度

**Core 接入优先级：C ABI 动态库 > sidecar 进程 > 直接 Rust/C++ 混绑。**

理由：后面要考虑 Windows/macOS/Linux 打包、现场部署、版本兼容、插件隔离，C ABI 和 sidecar 更稳。Tauri 2 原生支持 sidecar，shell/plugin 权限模型要求显式声明可执行程序和参数范围，对工业软件反而是好事。

#### 功能覆盖

| 工作区 | 功能 |
|--------|------|
| **Project** | 项目 / Recipe / 型号管理、版本管理 |
| **Device** | 设备接入配置 |
| **Calib** | 相机 / 结构光 / 手眼标定 |
| **Flow** | 流程编排（拖拽式节点图） |
| **Debug** | 算法调试、结果回放 |
| **Sample** | 样本管理 |
| **Param** | 参数编辑 |
| **UI Design** | 操作面板设计 |
| **Deploy** | Runtime 打包导出、权限 / 授权管理 |
| **Plugin** | 插件安装与管理 |

Studio 可以"重"一些，功能全面。

### 3.4 Runtime 层

**Runtime 是 Studio 产出的轻量应用，负责加载项目包并执行。**

**技术方案：** C++ 宿主 + Python (Dear PyGui) 操作界面

#### 架构：C++ 宿主 + Python/Dear PyGui

Runtime 采用**双进程/双层协作**模式：

- **C++ 宿主**：管理 Core 加载、设备采集、算法执行、流程调度、线程/内存/资源、PLC/IO 通信等重型任务
- **Python + Dear PyGui**：负责操作界面的构建与交互，通过 pybind11 调用 Core API，用 Dear PyGui 渲染操作面板、图像显示、状态监控等

```
Runtime.exe (C++ 宿主)
  + core.dll / .so              # Core 能力库
  + embedded python + dearpygui # 嵌入式 Python + Dear PyGui
  + builtin node types          # 内置节点类型
  + app package loader          # .asapp 加载器
```

#### 为什么用 Dear PyGui

Dear PyGui 基于 Dear ImGui 构建，GPU 加速、跨平台，但对 Python 暴露的是 retained-mode API，比原始 ImGui immediate-mode 更易用。适合这种"需要 Python 灵活操作 GUI + 图像/图表/面板多"的工业场景。Python 脚本可以直接创建、修改、响应 GUI 组件，实现操作界面的快速定制。

#### 启动流程

1. C++ 宿主启动，加载 Core 能力（core.dll/.so）
2. 初始化嵌入式 Python + Dear PyGui
3. 读取 .asapp 包中的 manifest.json
4. 把项目包路径加入 `sys.path`
5. 导入项目脚本模块（含 UI 定义脚本）
6. Python 通过 Dear PyGui 构建操作界面
7. C++ 宿主根据 pipeline.json 调度 Core 节点和 Python 节点
8. Dear PyGui 渲染循环与 C++ 宿主协作运行

#### C++ 与 Python 的职责边界

| C++ 宿主负责 | Python + Dear PyGui 负责 |
|-------------|-------------------------|
| Core 加载与生命周期 | 操作界面布局与组件创建 |
| 设备采集主线程 | 按钮/滑块/面板等交互响应 |
| 算法执行与流程调度 | 图像/点云/结果的可视化展示 |
| 线程/内存/资源管理 | 参数面板与联动逻辑 |
| PLC/机器人/IO 通信 | 状态监控与告警显示 |
| 数据缓存与序列化 | 报表展示与简单报表逻辑 |
| 高频实时数据采集 | 二次开发的自定义面板 |

**关键原则：C++ 宿主管"硬"的（设备、算法、通信、资源），Python/Dear PyGui 管"软"的（界面、交互、展示、定制）。**

#### Python 操作 GUI 示例

```python
import dearpygui.dearpygui as dpg
import core  # pybind11 暴露的 Core API

dpg.create_context()

with dpg.window(label="检测控制台"):
    dpg.add_button(label="单次检测", callback=lambda: core.run_once())
    dpg.add_slider_float(label="阈值", default_value=0.5,
                         min_value=0.0, max_value=1.0,
                         callback=lambda s, v: core.set_param("thresh", v))
    dpg.add_checkbox(label="保存 NG 图像",
                     callback=lambda s, v: core.set_param("save_ng", v))

# 图像显示区
with dpg.window(label="实时图像"):
    dpg.add_image("live_view")

# 结果显示区
with dpg.window(label="检测结果"):
    dpg.add_text("", tag="result_text")
    dpg.add_plot(label="高度图", tag="height_plot")
```

相比纯 Schema 驱动模式，Dear PyGui 方式让 Python 拥有更大的 GUI 自由度，适合：
- 需要动态构建界面的场景
- 需要复杂图表/可视化的场景
- 二次开发工程师需要自定义操作面板

#### Core API 暴露给 Python（pybind11）

```python
# 算法调用
img = core.get_frame()
roi = core.get_roi("left_top")
res = core.detect_scratch(img, roi, threshold=0.73)
core.set_output("ng", res.ng)

# 设备控制
core.camera.trigger()
core.plc.write("Y0", True)

# 参数访问
core.set_param("thresh", 0.8)
val = core.get_param("thresh")

# 流程控制
core.pipeline.run_once()
core.pipeline.start_continuous()
core.pipeline.stop()
```

#### 不走"生成代码再编译"路线

现场二次开发（调参、改流程、写脚本、换模型、改 UI 面板）都不需要重新编译 C++。编译型扩展仅作为高级能力（C++ 原生插件），不是默认二开路径。

#### 部署原则

**Runtime 不依赖"现场有 Python 开发环境"。** Python runtime、Dear PyGui、依赖包、脚本 API 全部随应用封装。终端用户不需要预装任何开发环境。

**职责：**

- 加载一个项目包（`.asapp`）
- 加载 Core 能力
- 初始化嵌入式 Python + Dear PyGui
- 执行流程
- Python/Dear PyGui 构建并渲染操作 UI
- 实时显示图像 / 状态 / 结果
- 接收用户操作
- 与 PLC / 设备 / 工控接口通信
- 记录日志与结果

**Runtime 原则：**

- 轻、稳、启动快
- 不依赖本地编译环境或 Python 开发环境
- 不暴露复杂设计功能
- 不承担"设计器"职责
- C++ 管系统主干（设备、算法、资源），Python/Dear PyGui 管界面与业务扩展

---

## 4. App Package 机制

Studio 最终生成的不是源码，而是一个 **App Package（.asapp）**：

```
my_app.asapp
├── manifest.json          # 项目元数据
├── pipeline.json          # 流程描述
├── params.default.json    # 默认参数
├── scripts/               # Python 脚本
│   ├── ui_main.py         # Dear PyGui 界面定义（主操作面板）
│   ├── ui_custom.py       # 自定义面板/二开界面
│   ├── precheck.py        # 前处理脚本
│   ├── postprocess.py     # 后处理脚本
│   └── rules.py           # 业务规则脚本
├── assets/                # 资源文件
│   ├── icons/
│   └── templates/
├── models/                # AI 模型
│   └── defect_xx.onnx
└── plugins/               # 插件
    └── vendor_xxx.plugin
```

**现场部署流程：**

```
拷贝 Runtime → 导入 .asapp → 运行
```

而不是：安装 VS → 装 CMake → 配编译器 → 编译 C++ → 解决依赖

---

## 5. 二次开发三级体系

| 级别 | 能力范围 | 适用角色 |
|------|----------|----------|
| **级别 1：纯配置** | 调参数、调流程、选模块、改 UI 布局 | 普通实施工程师 |
| **级别 2：脚本扩展** | Python 写少量逻辑（判定、后处理、MES 对接等） | 高级实施工程师 |
| **级别 3：原生插件** | C++ 插件接入 Core（新算法、新设备驱动等） | 内部团队 / 高级合作伙伴 |

---

## 6. 技术选型（开源底座）

### 核心原则

开源底座 + 自研产品层 + 可替换商业算法/硬件适配层。所有开源组件许可证友好，适合商业化。

| 领域 | 选型 | 许可证 |
|------|------|--------|
| Core 语言 | C++ | - |
| Studio 框架 | Tauri 2 (Rust) + WebView + TypeScript / React | MIT / Apache 2.0 |
| Runtime GUI | Dear PyGui (Python, 基于 Dear ImGui) | MIT |
| 2D 图像处理 | OpenCV | Apache 2.0 |
| 3D 点云/网格 | Open3D | MIT |
| 3D 点云处理 | PCL (Point Cloud Library) | BSD |
| 模型推理 | ONNX Runtime / TensorRT | MIT / NVIDIA |
| 异常检测训练 | Anomalib | Apache 2.0 |
| Python 绑定 | pybind11 | BSD |
| 脚本语言 | Python（主） | - |
| 工程描述 | JSON / YAML | - |
| 插件接口 | C ABI / pybind11 / gRPC（可组合） | - |
| 3D 验证工具 | CloudCompare (仅内部工程验证) | GPL/LGPL |

### 脚本语言选型

| 语言 | 优势 | 劣势 | 推荐用途 |
|------|------|------|----------|
| **Python** (第一推荐) | 算法生态最强、用户接受度高、适合视觉/AI | 部署要小心、隔离管理要做好 | Runtime 脚本主力 |
| **Lua** (可选补充) | 嵌入简单、运行轻、稳定 | AI/视觉生态弱 | 规则控制脚本 |
| **JS/TS** | 和 WebView Studio 一致性强 | 工业视觉算法生态弱 | Studio 侧 |

### 技术分工总览

| C++ 宿主负责 | Rust (Tauri) 负责 | Python + Dear PyGui 负责 | WebView/TS 负责 |
|-------------|-------------------|--------------------------|-----------------|
| Core 算法/引擎 | Studio 前端 IPC 入口 | Runtime 操作界面构建与渲染 | Studio 全部 UI |
| Device SDK 封装 | Core 薄封装调用 | 图像/点云/结果可视化 | 流程编排界面 |
| 结构光重建 | 文件系统/权限/安全 | 自动化脚本/客户定制逻辑 | 参数编辑界面 |
| 点云/高度图处理 | sidecar/插件调度 | 模型训练/推理试验/VLM 调用 | UI 面板设计 |
| 流程执行引擎 | | 数据标注辅助/报表生成 | 项目管理 |
| 线程/内存/资源管理 | | 参数面板/交互响应/告警 | 部署打包 |
| PLC/机器人/IO 通信 | | 二次开发自定义面板 | 样本管理 |

---

## 7. 六大核心引擎

| 引擎 | 职责 |
|------|------|
| **2D 规则视觉引擎** | 滤波、分割、模板、边缘、几何测量、OCR/OCV |
| **3D 重建与几何引擎** | 相位/条纹重建、点云预处理、配准、网格、曲面分析 |
| **缺陷检测引擎** | 规则法 + anomaly detection + 检测/分割模型 |
| **机器人引导引擎** | 姿态估计、抓取点、托盘/箱体模型、码垛规则 |
| **标定引擎** | 2D/3D/手眼/相机-投影器/坐标系管理 |
| **报告与追溯引擎** | 缺陷图、3D 色谱图、SPC、批次追溯、图像回放 |

---

## 8. 模块化策略

### 三类模块

| 类别 | 说明 | 示例 |
|------|------|------|
| **内核模块** | 自己维护，强绑定产品版本，接口变更需谨慎 | 采集、重建、测量、点云、缺陷检测、标定 |
| **扩展插件** | 给客户/交付工程师扩展，需插件接口、版本号、权限控制 | 行业检测模板、特定报表、PLC/机器人协议适配 |
| **脚本模块** | 快速试验与粘合，作为流程中的 Script Node | 数据预处理、自动调参、VLM 辅助 |

---

## 9. VLM/大模型集成策略

VLM 定位为 **Copilot**（辅助角色），不替代检测引擎。上线判定链仍靠可解释、可固化、可验收的规则和模型。

| 用途 | 说明 |
|------|------|
| **辅助建站** | 根据样品图片/点云，自动建议光学方案、阈值初值、ROI |
| **辅助标注** | 生成缺陷标签、缺陷描述、复判建议 |
| **辅助调参** | 根据误检/漏检案例推荐规则调整方向 |
| **辅助报告** | 自动生成批次质检总结、缺陷分布说明、换线建议 |

---

## 10. 行业模板优先级

### 第一优先级（先做）

**PCB / 电子装配 / 塑料件 / 金属加工件**

- 缺陷定义相对清晰
- 2D + 3D 结合价值高
- 客户容易理解 ROI
- 可快速形成标准工具集

### 第二优先级（后做）

**晶圆 / 芯片**

- 光学链路要求高
- 亚表面缺陷检测
- 超高精度平台
- 洁净/稳定性要求
- 客户验证周期长

---

## 11. 功能优先级

### 第一优先级

| 功能 | 说明 |
|------|------|
| **3D 检测** | 高度图/点云显示、ROI、基准面建立、高度差/平面度/轮廓偏差、CAD/模板面比对、NG 区域输出与报表 |
| **2D 外观缺陷** | 划痕、脏污、缺料、溢胶、破损、异物、色差；规则法 + anomaly 双路并行；良率统计与样本回流 |
| **机器人引导基础** | 目标定位、Pick point/pose、简单码垛模板、坐标变换与手眼标定 |

### 第二优先级

- 多工位/多相机同步
- Recipe 管理
- SPC / MES 对接
- 权限/审计/追溯
- Auto labeling / 缺陷复判

### 暂缓

- 完整数字孪生
- 全自动项目生成
- 通用 VLM 大模型诊断代理
- 云端多租户平台

---

## 12. 产品路线图

### v0.1 验证版

- 单机桌面软件
- 接结构光相机 + 一种 2D 相机
- 3D 高度图 + 2D 图像联动显示
- 规则法检测 + anomaly 检测
- 基本报表
- 项目 / Recipe 保存加载

### v0.5 工程版

- Studio 流程编排（WebView）
- Runtime 执行器（IMGUI）
- App Package 机制（.asapp）
- SDK 与插件接口
- PLC / 机器人接口
- 多相机同步
- 样本管理、模型训练、版本管理
- 审计日志、追溯、SPC

### v1.0 产品版

- 行业模板包：PCB、塑料件、金属件
- 机器人分拣/码垛模板
- VLM 辅助建站
- 远程诊断与升级
- 客户化部署体系
- 三级二次开发体系完善

---

## 13. 项目目录结构

```
VxlStudio/
├── core/                    # C++ 核心（Core 层）
│   ├── runtime/             # 任务调度、日志、异常
│   ├── pipeline/            # 节点式流程引擎
│   ├── data_model/          # 统一数据对象
│   ├── plugin_api/          # 插件接口定义 (C ABI)
│   ├── script_host/         # Python 脚本宿主
│   └── params/              # 参数系统、序列化
├── modules/                 # C++ 算法模块
│   ├── acquire_2d/
│   ├── acquire_3d/
│   ├── calib/
│   ├── reconstruct_sl/
│   ├── inspect_2d/
│   ├── inspect_3d/
│   ├── robot_guidance/
│   └── report/
├── sdk/                     # 对外 SDK
│   ├── cpp/
│   └── python/              # pybind11 绑定
├── studio/                  # Studio 层 (Tauri 2 + WebView + TS/React)
│   ├── src/                 # React/TS 前端
│   ├── src-tauri/           # Rust 壳层 (command, sidecar, 权限)
│   ├── public/
│   └── package.json
├── runtime/                 # Runtime 层 (C++ + IMGUI)
│   ├── app/                 # Runtime 主程序
│   └── ui/                  # IMGUI 界面
├── plugins/                 # 插件
│   ├── builtin/
│   └── external/
├── scripts/                 # Python 脚本
│   ├── examples/
│   └── templates/
├── recipes/                 # 行业模板
│   ├── pcb/
│   ├── plastic/
│   └── metal/
└── docs/                    # 文档
```

---

## 14. 下一步：四个优先定义项

在进入编码之前，以下四个定义需要先确定：

### 1. Project / App 包格式（.asapp）

定义 manifest.json、pipeline.json、ui_schema.json、params.default.json 的结构。

### 2. Pipeline / Node 描述格式

定义节点类型、端口类型、参数 Schema、数据包格式、执行上下文。

### 3. 参数 Schema

定义参数类型系统、默认值、约束、UI 映射、序列化格式。

### 4. 脚本与 Core 的边界接口

定义 Python 可调用的 Core API、数据交换格式、生命周期管理、隔离机制。

**这四个一旦定了，后面的 Studio、Runtime、插件系统都会顺很多。**

---

## 附录：3D 结构光检测的核心工作流

```
结构光/条纹投影 → 点云/网格重建
    → CAD 对比 / 厚度 / 平面度 / 轮廓偏差 / 缺口 / 变形 / 塌陷 / 鼓包
    → 偏差色谱图 + 报告
```

这是区别于纯 2D AOI 的核心价值链，也是本产品最重要的差异化能力。
