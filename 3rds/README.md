# 第三方库源码

本目录存放项目引用的第三方库源代码，确保构建可复现、长期可维护。

## 管理规则

1. **引用最新正式版本**（release tag），不使用 dev/nightly/rc
2. 每个库一个子目录，目录名为库名（小写）
3. 更新版本时同步更新下方版本清单并提交 commit
4. 不修改第三方源码；如需 patch，放在 `<库名>/patches/` 下并记录原因
5. 大型库可使用 git submodule 指向 release tag，避免仓库膨胀

## 版本清单

| 库 | 版本 | 许可证 | 用途 |
|----|------|--------|------|
| OpenCV | - | Apache 2.0 | 2D 图像处理 |
| Open3D | - | MIT | 3D 点云/网格处理 |
| pybind11 | - | BSD | C++/Python 绑定 |
| ONNX Runtime | - | MIT | 模型推理 |
| spdlog | - | MIT | 日志实现（可选） |

> 版本号在实际引入源码时填写。
