# MeloDick 构建与分发规范

## 1. 构建配置

- 开发构建：`debug-dev`
- 分发构建：`release-prod`
- 配置由 `CMakePresets.json` 固化，避免本机差异。

## 2. 本地构建命令

```powershell
cmake --preset debug-dev
cmake --build --preset debug-dev
ctest --preset debug-dev --output-on-failure
```

## 3. 分发前构建命令

```powershell
cmake --preset release-prod
cmake --build --preset release-prod
ctest --preset release-prod --output-on-failure
```

## 4. 分发包内容（当前阶段）

当前阶段先提供 `Standalone` 开发包，包含：

- `melodick_standalone_bootstrap` 可执行文件
- 运行所需依赖（如AI模型）
- 用户文档（后续补充）

## 5. 版本标识

- 早期开发阶段建议版本号：`YYYY.MMDD.gitsha`
- 版本生成应自动化，避免手工维护。

