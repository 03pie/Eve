# Eve 项目维护的插件

`builtin/` 存放随 Eve 一起维护和交付的插件。启动器通过 Cordis 挂载它们，实例的服务和清理回调由内核管理。第三方插件在各自项目开发，通过 SDK 构建。

| 源码 | 实例 | 交付方式 |
| --- | --- | --- |
| `builtin/desktop-ela/` | `eve.desktop`，以及它提供的配置页 `host.configuration` | 可单独构建的 Qt 模块，随产品安装到 `system/desktop/` |
| `builtin/desktop-ela/logging/` | `eve.desktop.logs` | 启动器静态装配的初始插件，在桌面窗口加载前建立归档 |
| `builtin/workspace-extensions/` | `eve.workspace.extensions` | 启动器静态装配的初始插件，发布工作区扩展管理服务 |

在已配置的仓库中运行 `cmake --build --preset ela-msvc-release --target eve_desktop` 可重建桌面 DLL 和相应清单。修改静态装配的初始插件时，需要重新链接启动器。官方桌面的仓外构建、外部桌面布局和业务插件开发分别见 [SDK 开发指南](../docs/sdk-consumer.md)。
