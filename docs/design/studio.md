# Studio（Phase 3-4）

项目设计器 / 流程编排器 / 测试器 / 打包器 / 部署器。

**当前只保留空目录，Phase 3-4 实施。**

## 技术方案

Tauri 2 + WebView + TypeScript / React

## Rust 壳层职责

1. 前端 IPC 入口
2. Core 薄封装调用（C ABI 动态库 > sidecar > 混绑）
3. 文件系统、权限、安全
4. sidecar / 插件调度

## 功能工作区

Project / Device / Calib / Flow / Debug / Sample / Param / UI Design / Deploy / Plugin

## 跨平台

Studio 支持 **macOS + Windows**（开发和调试用，不部署到产线）。
