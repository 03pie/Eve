# 开发环境和构建

准备 CMake 3.21 或更高版本、Ninja 和 C/C++ 编译工具链。Core 使用 C++17。YAML 配置由独立的 `Eve::ConfigYaml` 提供，所需 LibYAML 源码随仓库提供。

## 预设

| 用途 | Debug | Release |
| --- | --- | --- |
| GCC Core | `core-gcc-debug` | `core-gcc-release` |
| Windows MSVC x64 Core | `core-msvc-debug` | `core-msvc-release` |
| Windows MSVC x64 Qt 基础层 | `qt-msvc-debug` | `qt-msvc-release` |
| Windows MSVC x64 Ela 工作台 | `ela-msvc-debug` | `ela-msvc-release` |

每个预设拥有独立的 `build/<preset>` 目录。配置、构建和测试依次使用同一预设。

```sh
cmake --preset core-gcc-debug
cmake --build --preset core-gcc-debug
ctest --preset core-gcc-debug
```

## Windows 桌面

安装 Visual Studio 2022 C++ x64 工具、Windows SDK、Ninja、CMake 和 Qt 5.15.2 的 MSVC x64 kit。普通 PowerShell 可调用包装脚本，脚本在独立进程加载开发环境。将 Qt 路径改为本机安装目录。

```powershell
$env:EVE_QT5152_ROOT = 'C:/Qt/5.15.2/msvc2019_64'
.\tools\desktop.ps1 -Action doctor -Preset ela-msvc-release
.\tools\desktop.ps1 -Action verify -Preset ela-msvc-release
.\tools\desktop.ps1 -Action run -Preset ela-msvc-release -Workspace (Join-Path $PWD 'build/workspace')
```

`-QtRoot` 可以显式指定 kit。`EVE_CMAKE_BIN` 可以指定 CMake 所在的 `bin` 目录。配置示例用 `-Workbench configuration` 启动。平台入口可通过 `-ModuleManifest` 指向独立构建且受信的 Qt 模块清单。命令行日志工具与工作台位于同一构建目录的 `bin` 下。[运行日志](runtime-logging.md)

在已初始化的 x64 MSVC 终端中，也可以直接执行同一组 CMake 预设。测试前将匹配的 Qt DLL 加入当前进程的 `PATH`。

```powershell
$env:Path = "$(Join-Path $env:EVE_QT5152_ROOT 'bin');$env:Path"
cmake --preset ela-msvc-release -DEVE_INSTALL_SDK=ON
cmake --build --preset ela-msvc-release
ctest --preset ela-msvc-release
```

工作台、测试和 SDK 使用相同的 MSVC 工具链、动态 CRT 与 Qt 版本。需要独立插件工程时，先安装 SDK，再使用安装目录中的模板和 CMake package。[SDK 消费指南](sdk-consumer.md) · [验证矩阵](testing.md)
