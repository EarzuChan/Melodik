# MeloDick 架构基线

更新时间：2026-04-07

## 1. 目标

阶段 A 的目标是先把底层能力地基做稳：分析、分割、重合成、会话与渲染语义统一，确保后续 UI/插件化迭代不返工。

## 2. 能力模型（优先）

架构按能力描述，而不是按“某个模块是否 AI”描述：

- `F0ExtractionCapability`：从输入音频提取连续 F0。
- `NoteBlobSegmentationCapability`：将连续 F0 分割成可编辑 NoteBlob。
- `VoiceResynthesisCapability`：根据 NoteBlob 编辑结果重合成音频。

当前由 `CapabilityChain` 统一编排这三项能力。

## 3. 模块分层

- `melodick_core`
  - 核心数据结构：`NoteBlob`、`TimeRange`、`PitchSlice`。
- `melodick_capabilities`
  - 能力域聚合模块，内部按能力子域组织：
  - F0 提取（RMVPE ONNX）
  - NoteBlob 分割（当前启发式 v1）
  - 人声重合成（NSF-HiFiGAN ONNX）
- `melodick_render`
  - `RenderGroupPlanner`、`DirtyTimeline`、`LazyRenderScheduler`。
- `melodick_app`
  - 会话编排层：导入、编辑、派生脏区、懒渲染、混音输出。
- `melodick_project`
  - 工程态序列化/反序列化（ProjectState）。
- `melodick_io`
  - 音频文件读写。

## 4. 核心语义

- 编辑真相：音高线附属于 `NoteBlob`。
- 脏区语义：由 `edit_revision` 与渲染缓存 revision 派生，不做手工维护真相。
- 渲染语义：
  - 未编辑 NoteBlob 走原音频直通；
  - 编辑后 NoteBlob 走重合成；
  - 时值拉伸必须重合成。

## 5. 当前状态

- 已完成：能力链路统一、NoteBlob 命名迁移、工程态保存恢复、CLI 全链路打通。
- 进行中：更高质量的音符级分割算法、编辑器交互层、插件态时钟同步。
