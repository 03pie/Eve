# Ela shell 入口

现有 P3 桌面宿主实现在 src/ui，已接入固定版本 ElaWidgetTools、导航、插件管理和配置工作台。可执行入口为 examples/qt 下的 cordis_ela_demo 与 cordis_config_workbench；本目录仍保留给后续独立应用装配，未创建重复 GUI 构建目标。

本地构建及启动见 [开发环境](../../docs/development.md)。当前源码 SDK 与内置插件能力不构成 P4 原生模块或稳定 ABI 交付。
