# 测试与本地验收

| 验证范围 | 运行方式 |
| --- | --- |
| 可移植 Core、配置和模块数据契约 | `ctest --preset core-gcc-debug` |
| Windows x64 Core 和 Native 模块 | `ctest --preset core-msvc-debug` |
| Qt、Ela、模块和运行日志 | `ctest --preset ela-msvc-release` |
| 安装目录中的 SDK 与外部消费者 | 配置桌面预设时启用 `CORDIS_INSTALL_SDK=ON`，构建后用 `ctest --preset ela-msvc-release -L sdk` |

先用同一预设配置并构建，Qt 测试需要匹配的 Qt DLL。日常源码 CI 验证 Linux GCC 和 Windows MSVC Core。Windows 桌面验证在具备匹配 Qt kit 的本地环境运行。[工具链和命令](development.md)

- `platform.empty_workbench` 验证空工作区启动、关闭和重开。
- `ctest --preset ela-msvc-release -L logging` 验证实际日志写入、多个来源交错、归档恢复、保留和查询。
- `p5.load_policy_unauthorized` 与 `p5.load_policy_wrong_hash` 用实际 Native 夹具验证批准缺失和散列不匹配。
- 安装树 SDK 测试从安装目录复制通用模板，用独立工程构建并通过工作台装载其页面。模块身份、清理及退出依据实际宿主验证。

本地体验使用空工作区启动平台，检查概览、设置、输出、问题和通知。正常关闭并用同一工作区重开，在「输出」切换历史运行，或用独立工具读取归档。[启动方式](development.md) · [查询示例](runtime-logging.md)
