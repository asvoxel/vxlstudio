# Phase 3 执行计划

> 基于 phase3-decisions.md 的决策制定

## 工作流划分（6 条并行线）

```
Sprint 1 (W1-6)          Sprint 2 (W7-12)         Sprint 3 (W13-18)
─────────────────        ─────────────────        ─────────────────
WS-A: IO/PLC             WS-D: 多相机             WS-F: 部署打包
WS-B: GPU 抽象层         WS-E: 审计日志           跨平台测试
WS-C: 线性Pipeline       3D 算子补全              首客户现场
      + 开源模型集成
```

---

## Sprint 1（W1-6）：基础能力

### WS-A：IO / PLC 模块

**目标：** Modbus TCP/RTU + USB GPIO 通信

| 任务 | 说明 | 工作量 |
|------|------|--------|
| libmodbus 集成 | CMake 引入，3rds/ 存源码 | 1 天 |
| IIO 实现 - ModbusIO | Modbus TCP/RTU 读写线圈和寄存器 | 1 周 |
| IIO 实现 - SerialIO | USB-serial GPIO（DTR/RTS 控制或自定义协议） | 1 周 |
| ITrigger 实现 - ModbusTrigger | 等待 Modbus 输入信号变化 | 3 天 |
| IOManager 工厂 | enumerate() + open() | 2 天 |
| Python 绑定 | bind_io.cpp | 2 天 |
| 测试 | 单元测试 + 模拟设备测试 | 3 天 |

**交付物：** 可以通过 Modbus 读写 PLC 信号，通过串口控制 GPIO。

### WS-B：GPU 加速抽象层

**目标：** CPU/GPU 可切换，重建管线 GPU 化

```cpp
// core/include/vxl/compute.h
enum class ComputeBackend { CPU, CUDA, METAL, OPENCL };

class IComputeBackend {
    virtual ComputeBackend type() const = 0;
    virtual Result<cv::Mat> phase_shift(const std::vector<cv::Mat>& frames, int steps) = 0;
    virtual Result<cv::Mat> phase_unwrap(const cv::Mat& phase, ...) = 0;
};

// 全局设置
void set_compute_backend(ComputeBackend backend);
ComputeBackend get_compute_backend();
```

| 任务 | 说明 | 工作量 |
|------|------|--------|
| compute.h 接口设计 | IComputeBackend + 全局切换 | 2 天 |
| CPU Backend | 从现有代码提取（已有） | 3 天 |
| CUDA Backend | 相位计算 + 展开的 CUDA kernel | 2 周 |
| Metal Backend（macOS） | 相位计算 Metal shader | 1.5 周 |
| 集成到 Reconstruct | 根据 backend 选择实现 | 3 天 |
| Python 绑定 | set/get_compute_backend | 1 天 |
| 性能基准测试 | CPU vs GPU 对比 | 2 天 |

**交付物：** `vxl.set_compute_backend(vxl.CUDA)` 一行代码切换。

### WS-C：线性 Pipeline + 开源模型

**目标：** JSON 配置的线性执行序列 + 一个可用的异常检测模型

| 任务 | 说明 | 工作量 |
|------|------|--------|
| Pipeline JSON Schema | 线性步骤序列定义 | 2 天 |
| PipelineRunner | 加载 JSON，顺序执行步骤 | 1 周 |
| 步骤类型 | capture / reconstruct / inspect_3d / inspect_2d / output | 3 天 |
| 开源异常检测模型 | 下载 Anomalib 预训练 PaDiM/PatchCore ONNX 模型 | 1 天 |
| 模型集成测试 | Inference 加载模型 + anomaly_detect 跑通 | 3 天 |
| Python 绑定 | PipelineRunner | 2 天 |

**Pipeline JSON 示例：**
```json
{
  "version": "1.0",
  "steps": [
    {"type": "capture", "camera": "SL-001"},
    {"type": "reconstruct", "method": "structured_light"},
    {"type": "inspect_3d", "recipe": "pcb_model_a.json"},
    {"type": "inspect_2d", "operators": ["anomaly_detect"]},
    {"type": "output", "io_ok": "Y0", "io_ng": "Y1", "save_ng": true}
  ]
}
```

---

## Sprint 2（W7-12）：产品化能力

### WS-D：多相机支持

| 任务 | 说明 | 工作量 |
|------|------|--------|
| 多相机注册管理 | CameraManager 管理 N 台相机实例 | 3 天 |
| 同步触发（硬件） | 通过 ITrigger 发送同步信号 | 1 周 |
| 同步触发（软件） | 时间戳对齐，容差配置 | 1 周 |
| 多结果聚合 | 每台相机结果 → 整板 OK/NG | 3 天 |
| Recipe 扩展 | 支持 multi_camera 字段 | 2 天 |

### WS-E：审计日志 + 用户权限

| 任务 | 说明 | 工作量 |
|------|------|--------|
| 用户模型 | User 结构（id, name, role: operator/engineer/admin） | 2 天 |
| 认证 | 本地用户名密码（SHA-256 哈希） | 3 天 |
| 权限控制 | 操作前检查角色（修改 Recipe → engineer+，修改用户 → admin） | 3 天 |
| 审计日志表 | SQLite：timestamp, user, action, details, checksum | 3 天 |
| 日志记录点 | Recipe 切换/修改、参数变更、NG 事件、IO 操作、登录/登出 | 1 周 |
| 日志查询/导出 | 按时间/用户/事件类型筛选，CSV 导出 | 3 天 |
| Python 绑定 | UserManager, AuditLog | 3 天 |

### 3D 算子补全

| 任务 | 说明 | 工作量 |
|------|------|--------|
| coplanarity | 多 ROI 共面性检查（RANSAC 鲁棒拟合） | 1 周 |
| template_compare | 高度图差异检测（高斯差分 → 阈值 → 聚类） | 1 周 |
| 测试 | 合成数据验证 | 3 天 |

---

## Sprint 3（W13-18）：部署与交付

### WS-F：跨平台 + 部署

| 任务 | 说明 | 工作量 |
|------|------|--------|
| Windows 编译 | MSVC + vcpkg 依赖，相机 SDK 集成 | 2 周 |
| Linux 编译 | GCC + 依赖打包，相机 SDK 集成 | 2 周 |
| CI/CD | GitHub Actions：macOS + Windows + Linux 自动构建 | 1 周 |
| macOS DMG | CPack DMG，含 Python + 依赖 | 3 天 |
| Windows 安装包 | NSIS 或 WiX | 3 天 |
| Linux AppImage/DEB | CPack DEB + AppImage | 3 天 |
| VxlApp 原型 | C++ 宿主 + Dear PyGui 基本 UI + .vxap 加载 | 2 周 |

---

## Phase 3 新增头文件

| 头文件 | 内容 | Sprint |
|--------|------|--------|
| `compute.h` | IComputeBackend, ComputeBackend enum, set/get | S1 |
| `pipeline.h` | PipelineRunner, PipelineStep, Pipeline JSON 加载 | S1 |
| `audit.h` | AuditLog, AuditEntry, User, UserManager, Role | S2 |

实现已有占位头文件：
| 头文件 | 实现内容 | Sprint |
|--------|----------|--------|
| `io.h` | IOManager (Modbus + Serial) | S1 |
| `device.h` | ITrigger, IIO 实现 | S1 |

---

## Phase 3 目标交付物

1. **可在 Windows/macOS/Linux 上运行的安装包**
2. **Modbus PLC + GPIO IO 通信**
3. **GPU 加速的重建管线**（CPU fallback）
4. **线性 Pipeline 执行器**（JSON 配置）
5. **审计日志 + 用户权限**（SQLite）
6. **多相机同步**
7. **coplanarity + template_compare 算子**
8. **集成开源异常检测模型**
9. **VxlApp 原型**（.vxap 包加载 + 基本 UI）

## 预估周期

**18 周（约 4.5 个月），2-3 人团队。**
