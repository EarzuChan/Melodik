# Debug 构建
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Preset debug-dev

# 跑测试
powershell -ExecutionPolicy Bypass -File .\scripts\test.ps1 -Preset debug-dev

# 发行
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Preset release-prod
powershell -ExecutionPolicy Bypass -File .\scripts\test.ps1 -Preset release-prod

# 打包
powershell -ExecutionPolicy Bypass -File .\scripts\package.ps1 -Preset release-prod

# 手动验链路
.\build\debug-dev\melodick_standalone_bootstrap.exe .\samples\sample1.wav .\build\debug-dev\out.wav 2 .\build\debug-dev\sample1.mds
