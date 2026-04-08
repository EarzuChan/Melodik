# MeloDick 架构基线

更新时间：2026-04-08

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

- 编辑真相：音高线只属于 `NoteBlob`，轨道不再持有真相级 F0。
- `NoteBlob` 统一顺序管线：
  - `original_pitch_curve`
  - `handdraw_patch_midi`（单实例，NaN 表示空值点）
  - `line_patches`（多实例：glide/vibrato/free）
  - `global_transpose_semitones`
  - `time_ratio`
  - `final_pitch_curve()`（派生，不落盘）
- 脏区语义：由 `edit_revision` 与渲染缓存 revision 派生，不做手工维护真相。
- 会话音频标准：导入后统一标准化为 `44.1kHz / mono / float32`。
- 多轨语义：每轨只持有 `NoteBlob` 集合与渲染状态，不保留外部源文件依赖。
- 渲染语义：
  - 未编辑 NoteBlob 走原音频直通；
  - 编辑后 NoteBlob 走重合成；
  - 时值拉伸必须重合成。
  - 连接组（RenderGroup）支持一次后端渲染，再按 Blob 时间窗拆分回缓存。
  - 整体输出由轨道级结果按 `solo/mute/gain` 规则混音。
- 工程语义：`ProjectState` 落盘为 SQLite（二进制化），持有可恢复所需 Blob 资产与模型字段，不依赖外部音频文件。
- 导出语义：后端支持参数化导出（Mixdown / Stems + 采样率/位深/声道），与 UI 解耦。

## 5. 当前状态

- 已完成：能力链路统一、NoteBlob 命名迁移、多轨 Session、导入标准化、NoteBlob 顺序管线、SQLite 工程保存恢复、连接组级渲染、参数化导出后端、CLI 全链路打通。
- 进行中：更高质量的音符级分割算法、编辑器交互层、插件态时钟同步。
