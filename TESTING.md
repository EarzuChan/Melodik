# MeloDick 测试规范

更新时间：2026-04-07

## 1. 原则

- 只测关键链路，优先覆盖高风险回归点。
- 自动化优先，听感验证作为 AI/DSP 结果补充。
- 不追求形式化堆量，追求能快速发现“破坏主链路”的问题。

## 2. 自动化测试层次

### T0 构建与冒烟

```powershell
cmake --preset debug-dev
cmake --build --preset debug-dev
ctest --preset debug-dev --output-on-failure
```

### T1 核心单元

- `NoteBlob`：移调、拉伸边界行为
- `NoteBlobSegmenter`：最小切分行为
- `RenderGroupPlanner`：连接音符组装逻辑
- `DirtyTimeline`：脏区标记/清理
- `LazyRenderScheduler`：按播放点调度脏区单元

### T2 会话集成

- `Session` 导入 -> 分析 -> 分割 -> 渲染规划 -> 懒渲染 -> 混音输出
- 脏区派生收敛
- 未编辑直通与拉伸重渲染语义
- 工程态保存/恢复 roundtrip

### T3 真实 ONNX 冒烟

```powershell
.\build\debug-dev\melodick_standalone_bootstrap.exe .\samples\sample1.wav .\build\debug-dev\sample1_out.wav 0
.\build\debug-dev\melodick_standalone_bootstrap.exe .\samples\sample2.wav .\build\debug-dev\sample2_out.wav 2
```

最小验收：

- 进程退出码为 0
- 输出 wav 存在且可播放
- 日志显示链路完整执行

### T4 分析可视化回归

```powershell
.\build\debug-dev\melodick_analysis_dump.exe .\samples\sample1.wav .\build\debug-dev\analysis\sample1
.\build\debug-dev\melodick_analysis_dump.exe .\samples\sample2.wav .\build\debug-dev\analysis\sample2
D:\Python\Python314\python.exe .\scripts\plot_analysis.py .\build\debug-dev\analysis\sample1 .\build\debug-dev\analysis\sample1_overlay.png
D:\Python\Python314\python.exe .\scripts\plot_analysis.py .\build\debug-dev\analysis\sample2 .\build\debug-dev\analysis\sample2_overlay.png
```

最小验收：

- 导出 `analysis_meta.json / analysis_waveform.csv / analysis_f0.csv / analysis_segments.csv`
- overlay 图可读，能对比切分与 F0 连续性变化

## 3. 输入素材前提

- 当前默认输入为尽量干净的干声。
- 本阶段不负责去混响、降噪、去伴奏。
- 输入质量直接影响输出质量。
