# Eve 插件平台

Eve 是一个 C++17 插件平台，包含可移植的 Core 和 Windows 桌面工作台。Core 管理实例生命周期、服务依赖和资源清理。工作台基于 Qt 5.15.2 与 ElaWidgetTools，提供插件实例管理、配置、输出、问题和通知界面。平台与插件的运行日志按工作区持久化。

平台提供 Native C ABI 和同工具链 Qt 插件 SDK。外部插件可以独立构建，再通过工作台加载受信模块。首次启动可以使用空工作区。

## 构建 Core

安装 CMake 3.21 或以上、Ninja 和支持 C++17 的 GCC 后，在仓库根目录运行以下命令。

```sh
cmake --preset core-gcc-debug
cmake --build --preset core-gcc-debug
ctest --preset core-gcc-debug
```

Windows x64 的 MSVC 使用 Visual Studio 2022 x64 Developer PowerShell 和 `core-msvc-debug` 预设。

## 运行 Windows 工作台

使用已构建的 Windows x64 安装包时，双击安装程序即可为当前用户安装工作台。制作离线安装包、校验文件和卸载流程见 [Windows 安装包](docs/windows-installer.md)。

安装 Visual Studio 2022 C++ x64 工具和 Qt 5.15.2 的 MSVC x64 开发套件。将示例中的路径改为你的 Qt 目录，然后运行以下命令。

```powershell
$env:EVE_QT5152_ROOT = 'C:/Qt/5.15.2/msvc2019_64'
.\tools\desktop.ps1 -Action verify -Preset ela-msvc-release
.\tools\desktop.ps1 -Action run -Preset ela-msvc-release -Workspace (Join-Path $PWD 'build/workspace')
```

## 技术文档

- [开发环境和构建](docs/development.md)
- [平台架构](docs/architecture.md)与[架构决策](docs/adr/README.md)
- [Core 契约](docs/core-contracts.md)与[Native C ABI](docs/abi-v1.md)
- [安装 SDK 并构建外部 Qt 插件](docs/sdk-consumer.md)
- [运行日志与查询](docs/runtime-logging.md)
- [测试与本地验收](docs/testing.md)和[贡献方式](CONTRIBUTING.md)

第一方代码的公开许可证待确定。随库保留的许可、版权和资源来源见[第三方声明](THIRD_PARTY_NOTICES.md)。
