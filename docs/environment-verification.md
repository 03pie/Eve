# 开发环境验证报告

验证日期：2026-10-23。工作目录：`D:/WorkSpace/code/cpp/Eve`。

## 结论

**C++17 内核编码环境通过验证；Qt 5.15.2 + Ela 桌面目标环境未就绪。**

本报告验证的是当前基础工程，不是尚未实现的生命周期、服务、插件 ABI 或卸载行为。

## 1. 实际工具版本

| 工具 | 实测版本 / 位置 | 结果 |
|---|---|---|
| Git | 2.50.1.windows.1 | 可用 |
| CMake / CTest | 3.30.5，`D:/Qt/Tools/CMake_64/bin` | 可用，满足工程 ≥ 3.21 |
| Ninja | 1.13.2，PATH 可发现 | 可用 |
| MinGW GCC | 13.1.0，`D:/Qt/Tools/mingw1310_64/bin` | x64 C++17 构建及运行通过 |
| Visual Studio | Professional 2022，17.14.26 | vswhere 确认已安装 C++ 工具组件 |
| MSVC | compiler 19.44.35222.0，tools 14.44.35207 | x64 C++17 构建及运行通过 |
| Windows SDK | 10.0.26100.0，`D:/Windows Kits/10` | 编译、链接及资源工具可用 |
| 已安装 Qt | qmake 实测 6.11.0，`D:/Qt/6.11.0/mingw_64` | 不满足目标 Qt 5.15.2 |

MSVC 位于 `C:/Program Files/Microsoft Visual Studio/2022/Professional`。普通终端没有 cl 不代表未安装；本次通过开发者环境脚本加载后完成了真实验证。之前准备记录中的“未确认 MSVC”结论被本次检查更新。

## 2. 实际构建与运行矩阵

| 配置 | 配置/编译/链接 | CTest | 示例运行 |
|---|---|---|---|
| core-gcc-debug | 通过 | 1/1 通过 | 通过，GCC / 64-bit |
| core-gcc-release | 通过 | 1/1 通过 | 通过，GCC / 64-bit |
| core-msvc-debug | 通过 | 1/1 通过 | 通过，MSVC / 64-bit |
| core-msvc-release | 通过 | 1/1 通过 | 通过，MSVC / 64-bit |

检查编译命令确认 GCC 使用 C++17 和 -Werror；MSVC 使用 C++17、/W4、/WX，Debug CRT 为 -MDd，Release 为 -MD。现有示例只报告构建信息；基础测试覆盖 SDK 类型及头文件/库链接，不涉及插件运行时。

CTest 原始记录保存在 `build/<preset>/Testing/Temporary/LastTest.log`，编译命令在同目录 `compile_commands.json`。这些为本机生成产物，不提交至 Git。

## 3. Qt 与 Ela 检查

对工程显式开启 `CORDIS_REQUIRE_QT5152=ON`，并将已安装 Qt 6.11.0 作为搜索路径。CMake 按预期拒绝配置，提示缺少 Qt5Config.cmake（要求版本 5.15.2），说明没有静默接受 Qt 6 替代品。

检测目录包括 C:/Qt、D:/Qt，且检查了当前环境变量。未发现目标 kit 不等于完成全磁盘清查；如有自定义安装位置，需明确指定后再验证。

固定参考版 Ela 的 `research/ela/CMakeLists.txt` 明确拒绝 Qt > 6.7.0，且推荐 Qt 5.15.2；因此不能直接把现有 Qt 6.11.0 作为本项目目标环境。未修改 Ela 参考源码，也未启动其构建。

**当前尚未验证：** Qt 5.15.2 的 Core/Widgets/Test、目标 kit 与编译器/CRT 组合、Ela 编译、GUI 运行和插件 DLL。

## 4. MSVC 验证复现

在项目根目录的 PowerShell 中执行以下命令。此操作仅设置当前终端进程环境，不改变系统 PATH：

```powershell
& 'C:/Program Files/Microsoft Visual Studio/2022/Professional/Common7/Tools/Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
./tools/dev.ps1 -Action verify -Preset core-msvc-debug
./tools/dev.ps1 -Action verify -Preset core-msvc-release
```

GCC 配置直接执行：

```powershell
./tools/dev.ps1 -Action verify -Preset core-gcc-debug
./tools/dev.ps1 -Action verify -Preset core-gcc-release
```

## 5. 后续环境条件

1. 当前立即可以开展 W1–W7 的无 Qt 内核开发；GCC 和 MSVC 均已验证。
2. 进入桌面集成前准备 Qt 5.15.2 x64 kit，并明确其配套工具链。现有 MSVC 为 v143，不能把内核验证结果当成原设计 msvc2019_64 / v142 组合已验证；如沿用该目标，需补齐 v142 或另行验证并记录所选工具链组合。
3. 指定 Qt5 路径，重新执行精确版本前置检查，再独立编译运行 Qt/Ela 示例，最后验证应用及 UI 插件。

本轮未安装/升级系统软件、未切换目标 Qt 版本、未改动参考仓库，也未修改业务源码。
