# Phase 3 决策记录

> 2026-03-20 确认

## 决策结果

| # | 问题 | 决策 | 影响 |
|---|------|------|------|
| 1 | 相机 SDK | **全平台可用**（macOS 开发环境直接可用） | 跨平台不阻塞 |
| 2 | PLC/IO | **Siemens Modbus + 通用 GPIO** | IO 模块先做 Modbus TCP/RTU + USB GPIO |
| 3 | AI 模型 | **先用开源模型，架构可扩展** | 找一个开源异常检测模型集成，Inference 保持通用 |
| 4 | 审计日志 | **Phase 3 必须** | 需要实现权限 + 审计日志 |
| 5 | 流程编排 | **先线性序列，后续支持 Python 脚本** | Script/Flow 层从简，不做 DAG |
| 6 | GPU 加速 | **CPU/GPU 都支持，提供接口切换，GPU 优先** | 需要 GPU 抽象层，CUDA/Metal/OpenCL 按平台选择 |
| 7 | Studio | **Phase 4 再做** | Phase 3 不涉及 Tauri/React |

## 对 Phase 3 执行计划的影响

### 相机（不阻塞）
SDK 全平台可用，Phase 3 跨平台移植无外部依赖风险。macOS → Windows → Linux 顺序推进。

### IO 模块
- **Modbus TCP/RTU**：用 libmodbus（LGPL 3.0）
- **通用 GPIO**：USB-serial（libserialport 或直接 POSIX/Win32 serial）
- Siemens S7 PLC 通过 Modbus TCP 通信

### AI 模型策略
- 找一个开源异常检测 ONNX 模型（如 Anomalib 预训练的 PaDiM/PatchCore 模型）
- 集成到 recipes/ 中作为默认模型
- Inference 接口保持通用，用户可替换自有模型

### 审计日志（Phase 3 必做）
- 用户角色：operator / engineer / admin
- 审计日志：参数修改、Recipe 切换、NG 事件、IO 操作
- 存储：SQLite 本地数据库
- 保留期：可配置（默认 90 天）

### 流程编排（从简）
- Phase 3：线性 Pipeline（JSON 配置，顺序执行）
- 后续：Python 脚本作为编排层（已有 Python API 支持）
- 不做 DAG、不做图形化节点编辑器

### GPU 加速
- 抽象接口：`IComputeBackend`（CPU / CUDA / Metal / OpenCL）
- Phase 3 先实现 CPU + CUDA（Linux/Windows）+ Metal（macOS）
- 配置方式：ReconstructParams 或全局设置选择 backend
- 重建管线（相位计算、展开）优先 GPU 化
