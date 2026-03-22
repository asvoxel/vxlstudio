# VxlApp（Phase 3-4）

Studio 产出的轻量运行端，加载 .vxap 包并执行。

**当前只保留空目录，Phase 3-4 实施。**

## 技术方案

C++ 宿主 + Python / Dear PyGui

- **C++ 宿主管：** Core 加载、设备采集、算法执行、流程调度、线程/资源、PLC/IO
- **Python/Dear PyGui 管：** 操作界面构建、图像/结果可视化、参数面板、交互响应、自定义面板

## 启动流程

1. C++ 宿主加载 core.dll/.so
2. 初始化嵌入式 Python + Dear PyGui
3. 读取 .vxap 包 manifest.json
4. 导入 UI 脚本，构建操作界面
5. 按 pipeline.json 调度 Core + Python 节点

## VxAP 包格式

```
my_project.vxap
├── manifest.json
├── pipeline.json
├── params.default.json
├── scripts/
│   ├── ui_main.py
│   ├── ui_custom.py
│   ├── precheck.py
│   ├── postprocess.py
│   └── rules.py
├── assets/
├── models/
│   └── defect_xx.onnx
└── plugins/
```

部署：拷贝 VxlApp → 导入 .vxap → 运行

## 跨平台

VxlApp 支持 **macOS + Windows + Linux**，重点是 **Linux 部署**（工控机/边缘盒子）。
