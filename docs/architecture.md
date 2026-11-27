# Eve 平台架构

Eve 由可嵌入的插件内核和装配它的宿主组成。内核沿用本项目 Cordis 阶段的实例、服务和资源生命周期模型。从 0.2.0 起，宿主、独立插件和构建工具统一通过 Eve 开发接口协作。[命名与兼容边界](adr/0069-eve-platform-identity.md)

| 模块 | 职责 |
| --- | --- |
| `src/core` | 实例调度、生命周期、服务依赖、任务和清理屏障。公开接口通过 `Eve::Core` 提供。 |
| `src/loader` | 配置解析和验证、目标修订、事务保存、文件观察、配置与 Kernel 协调。 |
| `src/module` | 清单、预检、模块目录、装载策略以及 Native C ABI 适配。Qt 模块也通过 ModuleManager 进入。 |
| `src/extensions` | 预检本地安装包、发布用户级安装目录、校验版本身份以及回收已退役的文件。 |
| `src/qt` | Qt 调度、线程和连接的生命周期适配、Qt 插件身份与资源边界。 |
| `src/replacement` | 候选代、依赖闭包、资源预留和状态迁移，协调模块替换及提交或回退。 |
| `src/ui` | 共享 UI 服务接口与运行日志。官方桌面模块编译 Ela 外壳、设置、模块管理、通知中心和输出面板。 |
| `app/plugin_workbench` | 组装 Windows 工作台与启动配置，提供独立的归档查询工具。 |
| `sdk` | 面向外部插件的受支持公开接口、CMake 包和通用 Qt 插件模板。 |

Core 可独立构建。配置、加载器和替换使用 Core 状态与契约。Qt/Ela 位于宿主与 Qt 插件一侧，Native C ABI 使用单独的版本协商。Windows 上，宿主及 Qt 插件使用同一兼容的共享 Core、Qt 和 UI 库。加载器验证清单、文件摘要和运行时身份。[Core 契约](core-contracts.md) · [Native C ABI](abi-v1.md) · [Qt SDK](sdk-consumer.md)

启动器创建内核、调度器和运行归档，从程序目录的 `system/desktop/module.json` 装载官方模块 `org.eve.desktop`。内核挂载 `eve.desktop`，由其在 Qt 应用线程发布 `ui.pages`、`ui.commands`、`ui.output` 和 `ui.notifications`。选择外部桌面时，宿主再挂载该模块。其桌面实例通过版本化布局服务提交顶层窗口和工作区位置。确认窗口可见后记录启动成功。[桌面插件决策](adr/0070-kernel-desktop-plugin.md)

用户目录保存扩展的安装版本。工作区 YAML 保存模块标识、实例路径、启用目标及配置。宿主按目标装配业务模块，并以受保护的文件句柄校验安装摘要后交给加载器。缺失扩展的配置保留在原位置，其子树暂停，补装后重新协调。开发时仍可通过 `--module` 装配单个清单。[扩展架构决策](adr/0071-extension-packages-and-desktop-selection.md)

工作区目标声明期望的配置和实例状态。实例管理其激活代及托管资源，服务发布和失活各有生命周期边界。替换和工作台关闭要经过清理屏障，页面、连接、线程和模块借用在所属资源域撤销。Native 物理卸载只对精确匹配已批准产物启用，常规 Native 与 Qt 模块驻留。[架构决策](adr/README.md)

宿主持有每次运行独立的日志写入器。平台和插件输出按运行、来源和实例分组。关闭时先保存工作区，等待业务页面和命令退出，再清理桌面。内核确认静止后，宿主写入最终事件并关闭归档。桌面无法启动时也可以用独立的日志工具查询诊断。[运行日志](runtime-logging.md)
