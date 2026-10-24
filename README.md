# CordisCpp 插件平台

基于 Cordis 思路设计的 C++17 插件内核，目标桌面环境为 Qt 5.15.2 + ElaWidgetTools。

当前已完成 **W0–W7 首阶段内置内核与联合验收**：包含服务恢复、子实例、联合环诊断、类型化 EventBus、正常关闭及失败残留示例。P3 的工具链、资源屏障、贡献服务与基础管理页，以及 Q3c 项目内 Ela 基础库、原生导航和工作台布局已通过自动化检查；下一步完成原生桌面人工验收，随后推进 Loader/Store。配置计划与完整模块管理仍待实现。

## 从这里开始

在当前目录运行 PowerShell：

```powershell
./tools/dev.ps1 -Action doctor
./tools/dev.ps1 -Action verify -Preset core-gcc-debug
```

`verify` 依次配置、编译、运行 CTest。CMake 不在 PATH 时脚本会寻找常见 Qt Tools 安装位置；也可以设置进程内的 `CORDIS_CMAKE_BIN` 指向 CMake 的 bin 目录。脚本不修改系统 PATH。核心及 Qt 预设不构建 Ela；`ela-msvc-*` 直接构建仓库内的 `third_party/ela`，不再下载上游。

本机 MinGW GCC 用于验证无 Qt 的 C++17 内核，MSVC/Qt 5.15.2/Ela 组合另行构建验证。环境入口见 [开发环境](docs/development.md)，Ela 构建与交互示例命令见 [P3 桌面适配](docs/p3-development.md#q3a-ela-页面贡献)。

## 当前内容

| 路径 | 用途 |
|---|---|
| `sdk/include/cordis` | 源码 SDK 基础类型，不是稳定二进制 ABI |
| `src/core` | 不依赖 Qt/Ela 的内核库起点 |
| `src/qt` | 可选 Cordis::Qt，包含 QtScheduler、QtClock 与 QObjectResource |
| `src/ui` | 可选 Cordis::Ui，包含原生 Ela 导航、工作台、贡献服务和只读诊断 |
| `third_party/ela` | 项目内维护的 Ela 组件库及资源，保留上游基线、本地修改和 MIT 许可 |
| `examples/builtin` | 异步 Task、Effect、EventBus 与插件联合生命周期示例 |
| `examples/qt` | Ela 管理页、页面/命令/通知、缺依赖和失败重试及异步退出示例 |
| `tests` | SDK 契约、内核与真实 Qt 循环测试，以及验收规范 |
| `tools/dev.ps1` | 环境检查、配置、构建、测试入口 |
| `docs` | 决策、接口契约、首阶段工作单和准备记录 |
| `sources` | 同步参考资料，只读；不纳入产品构建或 Git |
| `research` | 独立研究副本，不纳入产品构建或 Git |

## 设计及实施入口

- [开发进度](docs/progress.md)：当前状态、工作包、验证台账及下一任务。
- [W1 交付与验证](docs/w1-verification.md)：异步基础能力和四配置测试结果。
- [W2 交付与验证](docs/w2-verification.md)：Effect 清理屏障、超时观察及四配置各 60/60 检查。
- [W3 开发记录](docs/w3-development.md)：实例与清理上下文交付，四配置各 64/64 检查及后续边界。
- [W4 交付与验证](docs/w4-development.md)：服务依赖、输出契约、realm 视图及异步调用，四配置各 86/86 检查。
- [W5 交付与验证](docs/w5-development.md)：子实例、父代清理屏障及联合环诊断，四配置各 96/96 检查。
- [W6 交付与验证](docs/w6-development.md)：事件协议、作用域、异步屏障及控制台示例，四配置各 110/110 检查。
- [P3 桌面适配](docs/p3-development.md)：Qt 工具链、资源屏障、贡献服务、原生导航与工作台，最新 Ela/MSVC Debug/Release 各 187/187。
- [P3 桌面验收单](tests/acceptance/p3-ui.md)：明暗主题、缩放、组件一致性、交互及正常退出；待人工执行。
- [W7 联合验收](docs/w7-verification.md)：服务/事件/子树完整闭环、失败残留和 A01–A10 证据，四配置各 113/113 检查。
- [软件设计说明书](Cordis插件平台-软件设计说明书.md)：详细设计与验收依据。
- [开发指南](Cordis-Cpp17-Qt5152-开发指南.md)、[研究证据](research-evidence.md)：研究基线。
- [基础决策](docs/adr/0001-foundation.md)：本轮冻结的约束。
- [首阶段接口契约](docs/core-contracts.md)：实现 Task/Effect/Instance 前的接口语义。
- [第一阶段工作单](docs/p1-work-items.md)：开发顺序与完成条件。
- [验收场景](tests/acceptance/p1-core.md)：待实现行为，不属于当前已通过测试。
- [准备完成记录](docs/preparation-status.md)：实际检查结果与剩余环境条件。
- [开发环境验证报告](docs/environment-verification.md)：GCC/MSVC 四配置实测结果与 Qt 缺项。

第一方代码尚未选定开源许可证；third_party/ela 保留原上游 MIT 许可，该许可不自动适用于本项目的第一方代码。
