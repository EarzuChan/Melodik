# MeloDick 进度追踪（MVP）

更新时间：2026-04-07

## 当前阶段

当前处于「地基完成，可并行推进 UI 接线与编辑能力迭代」阶段。

- 底层链路已打通：`F0 提取 -> NoteBlob 分割 -> 重合成渲染 -> 输出音频`。
- 会话与渲染语义已统一：编辑状态、脏区、懒渲染、混音输出一致。
- 仍待攻坚项：高质量音符粒度分割算法、完整编辑器交互、插件态时钟同步。

## 已完成（本轮重点）

### 1. 能力链路重构

- 新增 `CapabilityChain`，统一承接三项核心能力：
  - F0 提取
  - NoteBlob 分割
  - NoteBlob 重合成
- `Session` 不再零散拼接能力调用，改为通过 `CapabilityChain` 走统一入口。
- 模块分层已改为能力导向：移除上层 `ai/analysis` 语义拆分，合并为 `melodick_capabilities`。

### 2. NoteSegment -> NoteBlob

- 核心代码已完成术语迁移，不赖。

### 3. 渲染语义完善

- 脏区改为派生语义：通过 `NoteBlob.edit_revision` 与渲染缓存 revision 对比得到。
- 未编辑直通：`is_unedited()` 为真时，直接复用原始音频片段，不走 vocoder。
- 时值拉伸走重合成：拉伸后的 NoteBlob 必经重渲染，不做低质量机械拉伸替代。

### 4. 工程保存（底层语义）

- 新增项目态模型：`project::ProjectState`。
- 已支持序列化与反序列化：
  - `save_project_state(path, state)`
  - `load_project_state(path)`
- `Session` 已支持：
  - `capture_project_state(...)`
  - `restore_project_state(...)`

### 5. 稳定性修复

- `standalone` 增加 `catch (...)` 兜底，避免未知异常导致不透明退出码。
- 清理后全量重编译验证通过，CLI 链路可稳定完成渲染并输出文件。

## 对了

如果输入文件不是 44.1kHz，现在会做两次采样率转换：

F0 提取前会先把输入重采样到 16kHz（RMVPE）onnx_backends.cpp (line 334)。
人声重合成前会把片段重采样到 44.1kHz（HiFiGAN 工作采样率），模型输出后再重采样回会话采样率（通常就是原采样率）onnx_backends.cpp (line 466), onnx_backends.cpp (line 587)。

## 当前可用能力清单

- 导入单轨干声（wav mono）
- RMVPE 提取全局 F0
- 启发式分割为 NoteBlob
- NoteBlob 级移调、时值拉伸
- 派生脏区 + 懒渲染
- 重合成并混音输出整轨
- 工程态保存/恢复（底层）

## 下阶段优先级

1. UI 接线：只读音符视图、选中联动、基础编辑工具闭环
2. 分割算法 v2：从“可用”提升到“接近 Melodyne 粒度”
3. 编辑能力扩展：切分/合并/连接/断开、HandDraw 与 NoteBlob 同步
4. 插件态策略：宿主时钟同步、后台渲染与缓存策略
