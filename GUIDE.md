# MeloDick 开发、构建与测试全指南

本指南涵盖了 MeloDick 项目的构建规范、操作流程以及多层级测试准则。

## 1. 构建规范与配置

### 1.1 构建预设（Presets）
项目通过 `CMakePresets.json` 固化配置，以消除环境差异：
- **开发构建**：`debug-dev`（包含调试符号，优化等级低）
- **分发构建**：`release-prod`（开启全量优化，去除调试信息）

### 1.2 版本标识
- **版本格式**：建议采用 `YYYY.MMDD.gitsha`（例如：2026.0520.a1b2c3d）。
- **自动化**：版本号应由构建脚本自动生成，严禁手工维护。

---

## 2. 操作指南（快速开始）

可以使用提供的 PowerShell 脚本或原生 CMake 命令进行操作。

### 2.1 开发循环（Debug）
```powershell
# 方案 A：使用脚本（推荐）
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Preset debug-dev
powershell -ExecutionPolicy Bypass -File .\scripts\test.ps1 -Preset debug-dev

# 方案 B：使用原生 CMake
cmake --preset debug-dev
cmake --build --preset debug-dev
ctest --preset debug-dev --output-on-failure
```

### 2.2 发布流水线（Release）
```powershell
# 1. 构建与测试
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Preset release-prod
powershell -ExecutionPolicy Bypass -File .\scripts\test.ps1 -Preset release-prod

# 2. 打包分发
powershell -ExecutionPolicy Bypass -File .\scripts\package.ps1 -Preset release-prod
```

---

## 3. 测试规范

### 3.1 测试原则
- **关键链路优先**：优先覆盖高风险回归点，而非追求 100% 覆盖率。
- **自动化优先**：听感验证作为 AI/DSP 结果的辅助，而非唯一标准。
- **快速反馈**：确保能快速发现“破坏主链路”的破坏性变更。

### 3.2 自动化测试层次

#### T0：构建与冒烟
确保代码在指定 Preset 下能编译通过并完成基础 CTest 扫描。

#### T1：核心单元（Unit Tests）
重点关注以下逻辑：
- `NoteBlob`：移调、拉伸边界行为。
- `NoteBlobSegmenter`：最小切分行为。
- `RenderGroupPlanner`：连接音符组装逻辑。
- `DirtyTimeline`：脏区标记与清理。
- `LazyRenderScheduler`：按播放点调度脏区单元。

#### T2：会话集成（Session Integration）
验证全链路逻辑：`Session` 导入 -> 分析 -> 分割 -> 渲染规划 -> 懒渲染 -> 混音输出。包含工程态保存/恢复的 roundtrip 验证。

#### T3：真实 ONNX 冒烟（Standalone 验证）
手动验证或自动化脚本运行独立程序：
```powershell
# 格式：程序 输入文件 输出文件 移调半音 [模型路径/其他参数]
.\build\debug-dev\melodick_standalone_bootstrap.exe .\samples\sample1.wav .\build\debug-dev\out.wav 2 .\build\debug-dev\sample1.mldk
```
**最小验收标准**：进程退出码为 0；输出 wav 存在且可播放；日志显示链路完整。

#### T4：分析可视化回归（Analysis Regression）
通过 Python 脚本验证分析算法的准确性：
```powershell
# 1. 导出数据
.\build\debug-dev\melodick_analysis_dump.exe .\samples\sample1.wav .\build\debug-dev\analysis\sample1

# 2. 绘图对比
D:\Python\Python314\python.exe .\scripts\plot_analysis.py .\build\debug-dev\analysis\sample1 .\build\debug-dev\analysis\sample1_overlay.png
```
**最小验收标准**：生成 CSV/JSON 元数据；Overlay 图表中 F0 轨迹与切分点连续性符合预期。

---

## 4. 分发与交付

### 4.1 分发包内容
当前阶段提供 **Standalone 开发包**，包含：
- `melodick_standalone_bootstrap` 可执行文件。
- 运行所需依赖（ONNX 运行库、AI 模型文件等）。
- 基础用户文档（README/使用说明）。

### 4.2 输入素材前提
- **质量要求**：输入应为尽量干净的**干声**。
- **处理边界**：当前引擎不负责去混响、降噪或去伴奏这些前处理。输入素材的信噪比直接影响输出质量。

---
*更新时间：2026-04-10*