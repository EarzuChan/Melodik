# MeloDick 进度追踪（MVP）

更新时间：2026-04-10

## 当前阶段

当前处于「后端内核已成形，可开始 Standalone UI 接线；暂不宜宣称 VST3/插件态 ready」阶段。

- 底层链路已打通：`导入标准化 -> F0 提取 -> NoteBlob 分割 -> Blob 资产化 -> 连接组重合成 -> 多轨混音输出`。
- 会话与渲染语义已统一：多轨编辑状态、派生脏区、懒渲染、轨道混音一致。
- 当前“可开工”的重点是图形 Standalone 编辑器，而不是直接并行推进 JUCE/VST3 插件态。
- 仍待攻坚项：高质量音符粒度分割算法、完整编辑器交互命令层、实时播放/后台渲染调度、插件态时钟同步。

## 已完成

### 能力链路重构

- `CapabilityChain` 统一承接三项核心能力：
  - F0 提取
  - NoteBlob 分割
  - NoteBlob 重合成
- `Session` 通过 `CapabilityChain` 走统一入口。
- 模块分层为能力导向：`melodick_capabilities`。

### 渲染语义完善

- 脏区为派生语义：通过 `NoteBlob.edit_revision` 与渲染缓存 revision 对比得到。
- 未编辑直通：`is_unedited()` 为真时，直接复用原始音频片段，不走 vocoder。
- 时值拉伸走重合成：拉伸后的 NoteBlob 必经重渲染，不做低质量机械拉伸替代。
- HiFiGAN 输入 `uv` 为利用 RMVPE 的发声置信度链路。
- `NoteBlob` 已显式持有 `source_f0_hz + source_voiced_probability`，工程恢复后可直接重建源条件。
- 单 Blob 且未改时值的重合成路径可复用运行期 mel 缓存，避免无意义重算。

### 工程保存（底层语义）

- 新增项目态模型：`project::ProjectState`。
- 已支持序列化与反序列化：
  - `save_project_state(path, state)`
  - `load_project_state(path)`
- 落盘格式为 SQLite（schema v6，二进制 BLOB 存储）。
- `Session` 已支持：
  - `capture_project_state(...)`
  - `restore_project_state(...)`
- `source_mel` 为运行期可丢弃缓存，不落盘（因已有原始音频，随时可计算mel，落盘徒增工程大小）。

### 多轨与导入标准化

- `Session` 为原生多轨：
  - `create_track / import_audio_as_new_track / import_audio_to_track`
  - 轨级控制：`mute / solo / gain_db`
  - 轨级查询：`track_blobs / track_dirty_timeline`
- 导入标准化已落地：
  - 输入支持任意采样率、任意声道（内部先 downmix 为 mono）
  - 会话内部统一到 `44.1kHz / mono / float32`：因之为重合成器的舒适工作环境
- 轨内真相：
  - 导入后直接肢解为 NoteBlob 资产（每 Blob 持有源波形片段 + 原始 F0 + patch 模型 + 编辑元数据 + 可选中间特征）
  - 工程恢复不依赖外部音频文件回读
- 多轨混音规则已落地：
  - 有任一 `solo` 时只混 `solo` 轨
  - 无 `solo` 时混所有 `!mute` 轨
  - 轨级 `gain_db` 作用于该轨混音前结果

### 稳定性这一块

- `standalone` 增加 `catch (...)` 兜底，避免未知异常导致不透明退出码。
- 清理后全量重编译验证通过，CLI 链路可稳定完成渲染并输出文件。
- 当前仓库自动化测试 `21/21` 通过。
- 本地真实 ONNX 冒烟已通过：`sample1.wav -> 37 blobs -> 渲染输出 wav + 写出 mldk 工程文件`。

### 渲染与导出

- 连接组一次渲染已落地：RenderUnit 使用 `note_ids`，组内编辑音符合并调用后端，再按时间窗拆回 Blob 缓存。
- 导出后端能力已落地（与 UI 解耦）：
  - Mixdown 导出
  - Stems 导出（按轨）
  - 参数化输出：采样率、声道、位深、PCM/Float

## 采样率现状

- 会话层已固定 `44.1kHz`，导入阶段统一标准化。
- 模型层既有实现：
  - RMVPE：输入重采样到 `16kHz`，并做一些可选预处理：做 F0 提取。
  - HiFiGAN：按 `44.1kHz` 工作。

## 当前可用能力清单

- 导入多轨音频（任意采样率/声道，导入后统一 44.1k mono float32）
- RMVPE 提取全局 F0
- 启发式分割为 NoteBlob
- NoteBlob 级移调、时值拉伸
- 派生脏区 + 懒渲染
- 多轨重合成与混音输出（支持 solo/mute/track gain）
- 工程态保存/恢复（SQLite，多轨 Blob 资产，无外部源依赖）
- 参数化导出（mixdown/stems + 采样率/声道/位深）

## 当前缺口与边界

- 还没有任何真正的 GUI：当前 `standalone` 仍是命令行 bootstrap，而非图形编辑器。
- 还没有 JUCE 工程壳，也没有 VST3 / `AudioProcessor` / `processBlock` / `getStateInformation` / `setStateInformation` 这类插件态实现。
- `Session` 目前已暴露的编辑接口仍偏少，主要是导入、移调、拉伸、渲染、导出、工程保存恢复；离完整编辑器所需的 `move / split / merge / link-detach / handdraw / line control / undo-redo transaction` 还有明显距离。
- 当前渲染语义更接近离线/批量 `render_all_dirty`；距离设计稿里的后台异步渲染队列、播放缓存、插件环形缓冲还有工程差距。

## 全面版目标对齐（2026-04-08）

你提出的“会话 44.1k/mono、多轨、音符块内真相、按块渲染拼装、导出可选重采样与分轨”已纳入设计稿，当前实现对齐度如下。

已对齐：

- `NoteBlob` 作为主编辑对象，块内持有 `original_pitch_curve + handdraw_patch + line_patches + transpose + time_ratio` 顺序管线模型。
- 脏区由 revision 派生，不靠手工 set/unset。
- 未编辑直通、编辑后重合成、按块缓存并最终拼接混音。
- 工程状态可保存/恢复（SQLite，包含 NoteBlob 资产与模型字段）。

部分对齐：

- 分割算法仍是 v1，尚未达到 Melodyne 的高质量音符粒度：未来要结合原始采样信息（能量跳变）和音高线数据（音高跳变与U/V信息）来进行高质量自动化切分

未对齐：

- 尚未接入真正的多指标“只能”切分算法 v2（当前为启发式 v1）。
- 未落地任何 UI。
- 未落地 JUCE Standalone/VST3 工程壳与宿主集成。
- 未建立完整编辑命令系统与 Undo/Redo 事务层。

## 未来待办项

1. 先落地图形 Standalone UI：完成轨道区、Blob 视图、基础选中/移调/拉伸、dirty 状态反馈、工程保存恢复、导出闭环。
2. 补齐编辑命令层：统一封装 `move / split / merge / link-detach / handdraw / line patch / undo-redo transaction`，避免 UI 直接操作底层结构。
3. 落地 JUCE Standalone 工程壳与基础播放控制。
4. 再推进 VST3：补齐宿主 transport、inner-record、实时缓存、state chunk 生命周期。
5. 音符块分割算法 v2。
