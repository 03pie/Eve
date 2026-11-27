# Eve 平台架构

Eve 由可嵌入的插件内核和装配它的宿主组成。内核沿用本项目 Cordis 阶段的实例、服务和资源生命周期模型。从 0.2.0 起，宿主、独立插件和构建工具统一通过 Eve 开发接口协作。[命名与兼容边界](adr/0069-eve-platform-identity.md)

| 模块 | 职责 |
| --- | --- |
| `src/core` | 实例调度、生命周期、服务依赖、任务和清理屏障。公开接口通过 `Eve::Core` 提供。 |
| `src/loader` | 配置解析和验证、目标修订、事务保存、文件观察、配置与 Kernel 协调。 |
| `src/module` | 清单、预检、模块目录、装载策略以及 Native C ABI 适配。Qt 模块也通过 ModuleManager 进入。 |
| `src/qt` | Qt 调度、线程和连接的生命周期适配、Qt 插件身份与资源边界。 |
| `src/replacement` | 候选代、依赖闭包、资源预留和状态迁移，协调模块替换及提交或回退。 |
| `src/ui` | Ela 界面壳、页面与命令贡献、配置界面、通知和工作区日志。 |
| `app/plugin_workbench` | 组装 Windows 工作台与启动配置，提供独立的归档查询工具。 |
| `sdk` | 面向外部插件的受支持公开接口、CMake 包和通用 Qt 插件模板。 |

Core 可独立构建。配置、加载器和替换使用 Core 状态与契约。Qt/Ela 位于宿主与 Qt 插件一侧，Native C ABI 使用单独的版本协商。Windows 上，宿主及 Qt 插件使用同一兼容的共享 Core、Qt 和 UI 库。加载器验证清单、文件摘要和运行时身份。[Core 契约](core-contracts.md) · [Native C ABI](abi-v1.md) · [Qt SDK](sdk-consumer.md)

工作区目标声明期望的配置和实例状态。实例管理其激活代及托管资源，服务发布和失活各有生命周期边界。替换和工作台关闭要经过清理屏障，页面、连接、线程和模块借用在所属资源域撤销。Native 物理卸载只对精确匹配已批准产物启用，常规 Native 与 Qt 模块驻留。[架构决策](adr/README.md)

平台和插件输出进入工作区日志归档，按运行、来源和实例分组。通知与问题界面可关联具体日志事件，独立查询工具读取同一归档。[运行日志](runtime-logging.md)
