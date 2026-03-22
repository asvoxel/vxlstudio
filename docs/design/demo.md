# Demo 应用

## 定位

**Core 的使用示范，不是临时产品。** 验证 Core API 可用性，同时作为 PCB 检测场景的快速落地工具。

## 技术方案

PyQt6（PySide6），直接调用 Core 的 Python 绑定。

## 功能

**工程模式（调试工程师）：**
- 相机连接与采集预览（2D + 高度图 + 点云）
- 标定向导
- 检测参数调试（交互式 ROI、阈值、即时预览）
- 样本管理（保存 / 加载）
- Recipe 编辑与保存

**运行模式（产线操作员）：**
- 实时图像 + OK/NG 指示
- 一键启停
- 统计面板（良率、缺陷分布）
- 日志

## 自定义控件

| 控件 | 用途 |
|------|------|
| `ImageViewer` | 2D 图像查看（缩放、平移、叠加标注） |
| `HeightMapViewer` | 高度图色谱显示（热力图、剖面线） |
| `PointCloudViewer` | 3D 点云（旋转/缩放/测量） |
| `ROIEditor` | 交互式 ROI 绘制与编辑 |
| `ParamPanel` | 检测参数面板（从 Recipe 自动生成） |
| `ResultTable` | 检测结果表格 |

## 长期价值

- **Core API 验证器** — demo 写起来别扭 = API 设计有问题
- **客户演示工具** — 客户现场 POC
- **Python 二开范例** — 客户拿到 API 后最好的参考代码
- **交付工程师工具** — Studio/VxlApp 成熟前的日常工具

## Demo 与 VxlApp 的关系

Demo 不会被 VxlApp 替代。两者长期并存：
- **Demo**：Python 生态，轻量，适合二开/调试/演示
- **VxlApp**：C++ 宿主 + Dear PyGui，适合产线部署/客户交付
