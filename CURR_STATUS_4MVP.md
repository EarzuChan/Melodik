# MeloDick 进度追踪（MVP）

更新时间：2026-04-08

## 当前阶段

当前处于「多轨 Blob 会话 + SQLite 工程落盘完成，可开始 UI 接线」阶段。

- 底层链路已打通：`导入标准化 -> F0 提取 -> NoteBlob 分割 -> Blob 资产化 -> 连接组重渲染 -> 多轨混音输出`。
- 会话与渲染语义已统一：多轨编辑状态、派生脏区、懒渲染、轨道混音一致。
- 仍待攻坚项：高质量音符粒度分割算法、完整编辑器交互、插件态时钟同步。

## 已完成

### 能力链路重构

- 新增 `CapabilityChain`，统一承接三项核心能力：
  - F0 提取
  - NoteBlob 分割
  - NoteBlob 重合成
- `Session` 不再零散拼接能力调用，改为通过 `CapabilityChain` 走统一入口。
- 模块分层改为能力导向：移除上层 `ai/analysis` 语义拆分，合并为 `melodick_capabilities`。

### 渲染语义完善

- 脏区改为派生语义：通过 `NoteBlob.edit_revision` 与渲染缓存 revision 对比得到。
- 未编辑直通：`is_unedited()` 为真时，直接复用原始音频片段，不走 vocoder。
- 时值拉伸走重合成：拉伸后的 NoteBlob 必经重渲染，不做低质量机械拉伸替代。
- HiFiGAN 输入 `uv` 已改为利用 RMVPE 的发声置信度链路，不再只靠 `f0 > 0` 粗暴二值化。
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
- `source_mel` 已从工程真相中移除，改为运行期可丢弃缓存；工程恢复后按需重建。

### 多轨与导入标准化（本轮完成）

- `Session` 已升级为原生多轨：
  - `create_track / import_audio_as_new_track / import_audio_to_track`
  - 轨级控制：`mute / solo / gain_db`
  - 轨级查询：`track_blobs / track_dirty_timeline`
- 导入标准化已落地：
  - 输入支持任意采样率、任意声道（内部先 downmix 为 mono）
  - 会话内部统一到 `44.1kHz / mono / float32`
- 轨内真相已重构：
  - Track 不再持有整段源音频
  - Track 不再持有真相级 F0
  - 导入后直接肢解为 NoteBlob 资产（每 Blob 持有源波形片段 + 原始 F0 + patch 模型 + 编辑元数据 + 可选中间特征）
  - 工程恢复不再依赖外部音频文件回读
- 多轨混音规则已落地：
  - 有任一 `solo` 时只混 `solo` 轨
  - 无 `solo` 时混所有 `!mute` 轨
  - 轨级 `gain_db` 作用于该轨混音前结果

### 稳定性修复

- `standalone` 增加 `catch (...)` 兜底，避免未知异常导致不透明退出码。
- 清理后全量重编译验证通过，CLI 链路可稳定完成渲染并输出文件。

### 渲染与导出（本轮完成）

- 连接组一次渲染已落地：RenderUnit 使用 `note_ids`，组内编辑音符合并调用后端，再按时间窗拆回 Blob 缓存。
- 导出后端能力已落地（与 UI 解耦）：
  - Mixdown 导出
  - Stems 导出（按轨）
  - 参数化输出：采样率、声道、位深、PCM/Float

## 采样率现状

- 会话层已固定 `44.1kHz`，导入阶段统一标准化，不再把“原采样率”带进会话真相。
- 模型层仍按既有实现：
  - RMVPE：输入重采样到 `16kHz` 做 F0 提取。
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

## 全面版目标对齐（2026-04-08）

你提出的“会话 44.1k/mono、多轨、音符块内真相、按块渲染拼装、导出可选重采样与分轨”已纳入设计稿，当前实现对齐度如下。

已对齐：

- `NoteBlob` 作为主编辑对象，块内持有 `original_pitch_curve + handdraw_patch + line_patches + transpose + time_ratio` 顺序管线模型。
- 脏区由 revision 派生，不靠手工 set/unset。
- 未编辑直通、编辑后重合成、按块缓存并最终拼接混音。
- 工程状态可保存/恢复（SQLite，包含 NoteBlob 资产与模型字段）。

部分对齐：

- 分割算法仍是启发式 v1，尚未达到 Melodyne/SynthV 级别的稳定音符粒度。

未对齐：

- 尚未接入真正的音符级自动切分算法 v2（当前为启发式 v1）。
- UI 侧尚未接入导出参数面板与轨内多素材插入交互。

## 下阶段优先级

1. UI 接线：正式开始创建UI，完成只读音符视图、选中联动、基础编辑工具闭环
2. 分割算法 v2：从“可用”提升到“接近 Melodyne 粒度”
3. 编辑能力扩展：切分/合并/连接/断开、HandDraw 与 NoteBlob 同步

插件什么的以后再说，不急，先完成好眼下的任务
