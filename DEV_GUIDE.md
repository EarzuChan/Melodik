# MeloDick 开发与维护全手册

## 1. 项目愿景与规范 (Simplified Dev Spec)
本规范旨在提升交付稳定性，拒绝流程表演。

### 1.1 分支与提交
*   **分支策略**：主干开发，只使用 `main`。
*   **提交节奏**：单次提交仅完成一个明确目标，确保可独立回滚。
*   **提交门槛**：提交前必须确保：**能编译**、**核心自动化测试通过**、`standalone` 程序**可启动**。
*   **管理原则**：**代码由管理员提交，而非 AI 提交**。主次分明，人是决策者。
*   **信息格式**：必须使用前缀：`feat:` `fix:` `refactor:` `test:` `build:` `docs:`。

### 1.2 质量管控
*   **回滚策略**：一旦写坏，果断回滚至上一个提交，不纠结。
*   **变更边界**：
    *   业务语义变更必须同步更新文档（`MELODICK_DESIGN.md`）。
    *   未完成功能必须由开关保护，禁止破坏主流程。
    *   外部依赖升级需注明理由、风险及回退方案。

---

## 2. 构建指南 (Build Guide)

项目利用 `CMakePresets.json` 消除环境差异。

### 2.1 构建预设
*   **开发构建 (`debug-dev`)**：包含调试符号，适合日常开发。
*   **分发构建 (`release-prod`)**：全量优化，用于性能测试与发布。

### 2.2 常用命令 (PowerShell)
| 任务 | 命令 |
| :--- | :--- |
| **Debug 构建** | `powershell -File .\scripts\build.ps1 -Preset debug-dev` |
| **Debug 测试** | `powershell -File .\scripts\test.ps1 -Preset debug-dev` |
| **Release 构建** | `powershell -File .\scripts\build.ps1 -Preset release-prod` |
| **Release 测试** | `powershell -File .\scripts\test.ps1 -Preset release-prod` |
| **打包分发** | `powershell -File .\scripts\package.ps1 -Preset release-prod` |

---

## 3. 测试规范 (Testing Standards)

### 3.1 测试原则
*   只测关键链路，自动化优先。
*   听感验证作为 AI/DSP 结果的补充，不追求形式化堆量。

### 3.2 自动化测试层次
*   **T0 构建冒烟**：`ctest --preset debug-dev` 确保基础可用。
*   **T1 核心单元**：
    *   `NoteBlob`: 移调/拉伸边界行为。
    *   `NoteBlobSegmenter`: 音符切分逻辑。
    *   `DirtyTimeline` & `LazyRenderScheduler`: 脏区标记与调度。
*   **T2 会话集成**：验证 `Session` 从导入、分析、渲染到混音输出的全生命周期。
*   **T3 ONNX 链路冒烟**：
    ```powershell
    # 格式：程序 输入wav 输出wav 移调半音 [工程数据路径]
    .\build\debug-dev\melodick_standalone_bootstrap.exe .\samples\sample1.wav .\build\debug-dev\out.wav 2 .\build\debug-dev\sample1.mldk
    ```
    *验收标准：退出码0、输出文件存在、日志链路完整。*
*   **T4 分析可视化回归**：（这是检视处理过程是否正确的，若管理员未主动提出，则无需进行本检视）
    通过 `melodick_analysis_dump.exe` 导出 CSV，利用 `plot_analysis.py` 绘制 Overlay 图表，对比 F0 连续性与切分准确度。

---

## 4. 发布与分发规范 (Release Spec)

### 4.1 版本标识
*   **格式**：`YYYY.MMDD.gitsha`（由脚本自动化生成）。

### 4.2 分发包内容（当前阶段：Standalone 冒烟可执行文件）
1.  `melodick_standalone_bootstrap` 可执行文件。
2.  必要的运行时依赖（AI 模型文件、DLL等）。
3.  用户手册（后续补充）。

### 4.3 输入素材前提
*   **干声原则**：当前引擎默认输入为**干净的干声**。
*   **边界**：引擎暂不负责去混响、降噪或去伴奏。输入质量直接决定输出质量。

---
*文档更新时间：2026-04-10*