# 线程模型、内存管理与性能指标

## 1. 核心原则

**Core 是库，不是框架。Core 不管理线程策略。**

- Core 的所有 API 都是同步调用——调用方（Demo/VxlApp/Studio）决定在哪个线程调用
- Core 内部不创建长生命周期线程（GPU 加速等可能有短暂内部并行，但对外不可见）
- Core 的数据对象是线程安全的（引用计数原子操作），但 API 调用不保证可重入——同一个对象不要从多个线程同时操作

### 为什么这样设计

- **Core 简单专注**：只提供能力，不做调度
- **应用层灵活**：Demo 可以用简单的单线程 + 定时器；VxlApp 可以用复杂的多线程流水线；客户集成可以用自己的线程池
- **避免框架锁定**：如果 Core 内部搞了线程池和回调体系，上层就被迫适应它的模式

## 2. 应用层线程策略（推荐模式）

以 PCB 检测的 Demo/VxlApp 为例，推荐这样分线程：

```
┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│ 采集线程     │────→│ 处理线程     │────→│ 输出线程     │
│ Camera.capture│    │ Reconstruct  │    │ IO.set_output│
│              │    │ Inspector    │    │ Log.save     │
└─────────────┘     └─────────────┘     └─────────────┘
                                              │
┌─────────────┐                               │
│ GUI 主线程   │←──── 结果通知 ─────────────────┘
│ 显示/交互    │
└─────────────┘
```

| 线程 | 职责 | 调用的 Core API |
|------|------|----------------|
| **采集线程** | 等待触发，调用相机采集 | `Camera.capture()` |
| **处理线程** | 重建 + 检测（CPU/GPU 密集） | `Reconstruct`, `Inspector3D` |
| **输出线程** | IO 输出 + 日志保存 | `IO.set_output()`, `Log.save()` |
| **GUI 主线程** | 界面渲染和用户交互 | 只读取结果，不调用重型 API |

线程之间通过下文的消息队列 + 共享数据传递。

**Core 不关心你开了几个线程。** 你甚至可以全在一个线程里顺序调用（简单 demo 场景够用）。

## 3. 大数据共享：引用计数内存管理

### 问题

图像和点云是大块数据（一帧图像 ~10MB，一片点云 ~50MB），在采集线程、处理线程、GUI 线程之间传递时不能拷贝。

### 方案：`vxl::SharedBuffer` + 引用计数

```cpp
namespace vxl {
    // 底层共享缓冲区，引用计数管理
    class SharedBuffer {
    public:
        static SharedBuffer allocate(size_t bytes);
        void* data();
        const void* data() const;
        size_t size() const;
        int ref_count() const;       // 调试用：查看当前引用数
        // 拷贝构造/赋值 → 引用计数 +1（原子操作）
        // 析构 → 引用计数 -1，归零时释放
    };
}
```

**所有大数据类型内部持有 `SharedBuffer`：**

```cpp
struct Image {
    SharedBuffer buffer;     // 像素数据
    int width, height;
    PixelFormat format;
    // 拷贝 Image → 只增加引用计数，不拷贝像素
    // to_numpy() → 共享同一块内存，numpy 持有引用
    // to_cv_mat() → 共享同一块内存
};

struct HeightMap {
    SharedBuffer buffer;     // float 高度值
    int width, height;
    float resolution;        // mm/pixel
    // 同理
};

struct PointCloud {
    SharedBuffer buffer;     // 点数据 (x,y,z,...)
    size_t point_count;
    PointFormat format;
};
```

### 生命周期规则

1. `Camera.capture()` 返回 `Image` — 相机驱动分配 buffer，引用计数 = 1
2. 传给处理线程 — 引用计数 = 2（采集线程 + 处理线程各持有一份）
3. 采集线程释放自己的引用 — 引用计数 = 1
4. 处理线程用完释放 — 引用计数 = 0，内存释放

**泄漏排查：** 每个 `SharedBuffer` 记录分配时的调用栈（debug 模式），定期打印仍存活的 buffer 列表和引用计数。

### Python 侧

```python
img = cam.capture()           # ref_count = 1
np_arr = img.to_numpy()       # ref_count = 2（numpy 持有引用）
del img                       # ref_count = 1（numpy 仍有效）
del np_arr                    # ref_count = 0，释放
```

pybind11 绑定中通过 `py::buffer_protocol` 实现零拷贝和引用绑定。

## 4. 进程内消息队列

### 问题

线程间需要传递控制消息（触发信号、检测完成、参数变更、告警）。直接用回调会导致模块耦合。

### 方案：`vxl::MessageBus`

轻量的进程内发布/订阅消息总线：

```cpp
namespace vxl {
    // 消息基类
    struct Message {
        std::string topic;
        int64_t timestamp;
        // 派生出具体消息类型
    };

    // 预定义消息
    struct FrameCaptured : Message {       // 采集完成
        Image image;                       // SharedBuffer，零拷贝
        std::string camera_id;
    };
    struct ReconstructDone : Message {      // 重建完成
        HeightMap height_map;
        PointCloud cloud;
    };
    struct InspectionDone : Message {       // 检测完成
        InspectionResult result;
    };
    struct ParamChanged : Message {         // 参数变更
        std::string key;
        std::string value;
    };
    struct AlarmTriggered : Message {       // 告警
        std::string alarm_id;
        std::string description;
    };

    // 消息总线
    class MessageBus {
    public:
        // 发布（线程安全）
        void publish(std::shared_ptr<Message> msg);

        // 订阅（返回 subscription ID，可取消）
        uint64_t subscribe(const std::string& topic,
                          std::function<void(std::shared_ptr<Message>)> handler);
        void unsubscribe(uint64_t id);

        // 同步分发（在调用 publish 的线程执行 handler）
        // 或异步分发（handler 在订阅者指定的线程执行）
        enum class DispatchMode { SYNC, ASYNC };
        void set_dispatch_mode(DispatchMode mode);
    };

    // 全局消息总线（Core 提供，应用层使用）
    MessageBus& message_bus();
}
```

### 使用模式

```cpp
// 采集线程
auto frame = camera.capture();
auto msg = std::make_shared<FrameCaptured>();
msg->image = frame;          // SharedBuffer 引用计数 +1
msg->camera_id = "SL-001";
vxl::message_bus().publish(msg);

// 处理线程（订阅）
vxl::message_bus().subscribe("frame_captured", [](auto msg) {
    auto frame_msg = std::dynamic_pointer_cast<FrameCaptured>(msg);
    auto hmap = vxl::reconstruct(frame_msg->image, calib);
    // ... 检测 ...
    auto result_msg = std::make_shared<InspectionDone>();
    result_msg->result = result;
    vxl::message_bus().publish(result_msg);
});

// GUI 线程（订阅检测结果）
vxl::message_bus().subscribe("inspection_done", [](auto msg) {
    auto result_msg = std::dynamic_pointer_cast<InspectionDone>(msg);
    update_display(result_msg->result);  // 更新界面
});
```

### Python 侧

```python
def on_inspection_done(msg):
    print(f"OK: {msg.result.ok}")

vxl.message_bus().subscribe("inspection_done", on_inspection_done)
```

### 设计要点

- **消息中的大数据用 SharedBuffer**：FrameCaptured 里的 Image 是引用，不拷贝
- **线程安全**：publish/subscribe 内部加锁，handler 可配置同步或异步执行
- **解耦**：采集模块不知道谁在处理，处理模块不知道谁在显示
- **可选**：MessageBus 是 Core 提供的工具，不是强制的。用户也可以不用它，自己传数据

## 5. Core 的线程安全级别

| 类别 | 安全级别 | 说明 |
|------|----------|------|
| `SharedBuffer` | **线程安全** | 引用计数原子操作 |
| `MessageBus` | **线程安全** | publish/subscribe 内部同步 |
| `Log` | **线程安全** | 多线程可同时写日志 |
| `Camera` | **单线程** | 同一个相机实例不要多线程同时调用 |
| `Reconstruct` | **单线程** | 同一个重建实例不要多线程同时调用 |
| `Inspector3D` | **单线程** | 同一个实例不要多线程同时调用 |
| `Recipe` | **只读线程安全** | 加载后可多线程读，不可边读边改 |

**原则：无状态的工具函数线程安全，有状态的对象实例单线程使用。** 需要并行就创建多个实例。

## 6. 性能指标

### PCB/SMT 检测目标

| 指标 | 目标值 | 说明 |
|------|--------|------|
| **单板检测总节拍** | **< 3 秒** | 从触发到 OK/NG 输出 |
| 采集时间 | < 500ms | 含条纹投影序列（取决于相机帧率和条纹数） |
| 重建时间 | < 800ms | 相位计算 + 展开 + 坐标变换 + 高度图（GPU 加速后 < 300ms） |
| 检测时间 | < 200ms | 3D 算子 + 2D 辅助 |
| IO 输出 | < 10ms | OK/NG 信号 |
| 高度图分辨率 | 0.05mm/pixel | 可配置，精度/速度权衡 |
| 点云规模 | 100万~500万点/帧 | 取决于相机分辨率 |

### 内存预算

| 数据 | 单帧大小 | 缓存深度 | 占用 |
|------|----------|----------|------|
| 原始图像（条纹序列 12 帧） | ~120MB | 2 组 | ~240MB |
| 高度图 | ~8MB | 3 帧 | ~24MB |
| 点云 | ~50MB | 2 帧 | ~100MB |
| 检测结果 | ~1MB | 100 条 | ~100MB |
| **合计** | | | **~500MB** |

### 存储预算（产线 7x24）

| 数据 | 频率 | 单条大小 | 日增量 |
|------|------|----------|--------|
| 检测日志 | 1次/3秒 | 1KB | ~30MB |
| NG 图像 | ~5%检出率 | 10MB | ~15GB |
| 全量图像（可选） | 1次/3秒 | 10MB | ~300GB |

日志自动轮转（默认保留 30 天），NG 图像按磁盘容量自动清理。

## 7. GPU 加速策略

Phase 1 先 CPU 实现，性能不达标时逐步加 GPU：

| 环节 | CPU 预估 | GPU 目标 | 技术 |
|------|----------|----------|------|
| 相位计算 | ~400ms | < 100ms | CUDA / OpenCL / Metal |
| 点云生成 | ~200ms | < 50ms | CUDA / OpenCL / Metal |
| 高度图插值 | ~100ms | < 30ms | CUDA / OpenCL / Metal |

macOS 优先考虑 Metal 或 OpenCL；Linux/Windows 用 CUDA（NVIDIA GPU 覆盖率高）。

Phase 1 在 macOS 上先用 CPU 跑通，不阻塞。
