# CordisCpp 插件平台

基于 Cordis 思路设计的 C++17 插件内核，目标桌面环境为 Qt 5.15.2 + ElaWidgetTools。

当前处于**内核开发阶段，W1/W2 已完成，W3 进行中**：已有异步基础、EffectScope、实例配置与 epoch 收敛、retry、Kernel 注册和操作/代次查询；独立 CleanupContext、服务依赖、Qt UI 和原生插件装载尚未实现。

## 从这里开始

在当前目录运行 PowerShell：

```powershell
./tools/dev.ps1 -Action doctor
./tools/dev.ps1 -Action verify -Preset core-gcc-debug
```

`verify` 依次配置、编译、运行 CTest。CMake 不在 PATH 时脚本会寻找常见 Qt Tools 安装位置；也可以设置进程内的 `CORDIS_CMAKE_BIN` 指向 CMake 的 bin 目录。不修改系统 PATH，不下载依赖。

本机已有的 MinGW GCC 用于验证无 Qt 的 C++17 基础工程。这不代表已验证目标 MSVC/Qt 5.15.2/Ela 组合。详见 [开发环境](docs/development.md)。

## 当前内容

| 路径 | 用途 |
|---|---|
| `sdk/include/cordis` | 源码 SDK 基础类型，不是稳定二进制 ABI |
| `src/core` | 不依赖 Qt/Ela 的内核库起点 |
| `examples/builtin` | 构建信息及异步 Task 演示；插件生命周期示例待实现 |
| `tests` | 现有 SDK 编译/链接契约检查及待实施验收规范 |
| `tools/dev.ps1` | 环境检查、配置、构建、测试入口 |
| `docs` | 决策、接口契约、首阶段工作单和准备记录 |
| `sources` | 同步参考资料，只读；不纳入产品构建或 Git |
| `research` | 独立研究副本，不纳入产品构建或 Git |

## 设计及实施入口

- [开发进度](docs/progress.md)：当前状态、工作包、验证台账及下一任务。
- [W1 交付与验证](docs/w1-verification.md)：异步基础能力和四配置测试结果。
- [W2 交付与验证](docs/w2-verification.md)：Effect 清理屏障、超时观察及四配置各 60/60 检查。
- [W3 开发记录](docs/w3-development.md)：单次激活、停止屏障及剩余工作，四配置各 61/61 检查。
- [软件设计说明书](Cordis插件平台-软件设计说明书.md)：详细设计与验收依据。
- [开发指南](Cordis-Cpp17-Qt5152-开发指南.md)、[研究证据](research-evidence.md)：研究基线。
- [基础决策](docs/adr/0001-foundation.md)：本轮冻结的约束。
- [首阶段接口契约](docs/core-contracts.md)：实现 Task/Effect/Instance 前的接口语义。
- [第一阶段工作单](docs/p1-work-items.md)：开发顺序与完成条件。
- [验收场景](tests/acceptance/p1-core.md)：待实现行为，不属于当前已通过测试。
- [准备完成记录](docs/preparation-status.md)：实际检查结果与剩余环境条件。
- [开发环境验证报告](docs/environment-verification.md)：GCC/MSVC 四配置实测结果与 Qt 缺项。

产品没有选定开源许可证，研究上游的 MIT 许可证不自动适用于本项目。
