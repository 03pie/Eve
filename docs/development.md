# 开发环境和构建操作

当前补充（2026-10-24）：本页保留早期工具链准备记录；最新 Qt/Ela 及配置模块状态见 [P3 记录](p3-development.md)。Q4b 新增 LibYAML C 静态库，完整工程需要配套 C/C++ 编译器：GCC 预设明确使用 gcc/g++，MSVC 预设均使用 cl。Core 不链接 Qt、Ela 或 LibYAML；解析层作为独立 Cordis::ConfigYaml 链接。依赖源码与许可随仓库提供，构建不联网。

## 当前机器恢复入口（2026-10-26）

本次按用户要求在 D:/WorkSpace/code/cpp/Eve 恢复环境：Qt 5.15.2 msvc2019_64 的 qtbase 位于 build/Qt，提供 Core/Gui/Widgets/Test 的 Debug/Release 库；MSVC 为 VS2022 Professional 19.44.35222/v143，Windows SDK 为 10.0.26100.0。旧记录的 E:/Qt 不作为当前路径。安装器采用 Python 3.10.18 与 aqtinstall 3.3.0，工具与缓存均位于被 Git 忽略的 build 中，未修改机器 PATH。

普通 PowerShell 可直接使用：

```powershell
./tools/desktop.ps1 -Action doctor
./tools/desktop.ps1 -Action verify -Preset ela-msvc-debug
./tools/desktop.ps1 -Action verify -Preset ela-msvc-release
./tools/desktop.ps1 -Action run -Preset ela-msvc-release
# 使用独立验收文件，不影响默认配置：
./tools/desktop.ps1 -Action run -Preset ela-msvc-release -ConfigurationFile build/desktop-acceptance/manual.yaml
```

desktop.ps1 自动发现 VS2022 x64 工具链，默认使用 build/Qt/5.15.2/msvc2019_64；显式 -QtRoot 或 CORDIS_QT5152_ROOT 优先。临时目录、Qt PATH 和构建并行度仅对本次调用生效；成功或失败都恢复调用方环境。Windows PowerShell 脚本使用 UTF-8 BOM，避免中文注释误解码后吞掉下一行。dev.ps1 根据原生进程退出码判定失败，重定向 stderr 的 CMake 警告不会中断验证。

Ninja 的 MSVC 依赖前缀必须匹配实际安装的编译器语言；入口根据 1033/2052 资源选择英文或中文，配合 CORDIS_MSVC_SHOWINCLUDES_PREFIX。只设 VSLANG=1033 不能让缺少英文资源的编译器输出英文。本次修复后执行过 Ela 两配置干净重建，并用 ninja -t deps 确认头文件依赖已记录。

本机依赖恢复命令（uv 已安装；如重装 Python，保持以下安装目录变量）：

```powershell
$env:UV_CACHE_DIR = "$PWD/build/uv-cache"
$env:UV_PYTHON_INSTALL_DIR = "$PWD/build/python"
uv python install 3.10.18
uv venv --python 3.10.18 build/qt-tools
uv pip install --python build/qt-tools/Scripts/python.exe aqtinstall==3.3.0
./build/qt-tools/Scripts/python.exe -m aqt install-qt windows desktop 5.15.2 win64_msvc2019_64 -O build/Qt --archives qtbase
```

归档日志为 build/qt5152-install.log；依赖工具支持的归档选择见 [aqtinstall 命令文档](https://aqtinstall.readthedocs.io/en/stable/cli.html)。现有 Qt 6.11.0 保留。本机组合的源码构建验证不构成 v142 或插件 DLL ABI 验收。

## 1. 两条验证路径

| 路径 | 用途 | 要求 |
|---|---|---|
| Portable core | 现在开展无 Qt 内核编码 | C++17 编译器、CMake ≥ 3.21、Ninja |
| Target desktop | P3/P4 Qt/Ela 和原生模块验收 | Qt 5.15.2 msvc2019_64、一致 MSVC/Windows SDK/CRT、Ninja |

Portable core 通过不等于 Target desktop 已通过。禁止把本机 Qt 6.11.0 自动作为目标版本替代。当前已有真实 Qt 适配库与 Ela GUI 目标，历史准备状态以下文及对应日期为准。

当前 GCC 和 MSVC 的 Debug/Release 内核构建均已实测通过。MSVC 开发环境加载命令、实际版本和 Qt 缺项见 [环境验证报告](environment-verification.md)。

## 2. 现有构建预设

```powershell
# 当前机器可使用 GCC 路径
./tools/dev.ps1 -Action verify -Preset core-gcc-debug
./tools/dev.ps1 -Action verify -Preset core-gcc-release

# MSVC 需要先进入对应的 x64 开发者终端
./tools/dev.ps1 -Action verify -Preset core-msvc-debug
./tools/dev.ps1 -Action verify -Preset core-msvc-release
```

构建目录分别在 `build/<preset>`；不同编译器和构建模式不得共用目录。MSVC Debug 使用 /MDd，Release 使用 /MD；后续插件必须与宿主一致。GCC 产物只用于内核可移植性验证，不能混用到 MSVC Qt UI 插件中。

也可直接使用已加入 PATH 的工具：

```powershell
cmake --preset core-gcc-debug
cmake --build --preset core-gcc-debug
ctest --preset core-gcc-debug
```

默认启用 C++17、严格警告和警告即错误。当前测试覆盖基础类型隔离、头文件自包含、生命周期、依赖、配置和真实 Qt 行为；各层验收范围见进度台账。

## 3. Qt 5.15.2 前置检查

取得目标 kit 后，在 x64 MSVC 开发者终端新增个人 `CMakeUserPresets.json`（已忽略，不提交机器绝对路径）：

```json
{
  "version": 3,
  "configurePresets": [
    {
      "name": "local-qt5152-check",
      "inherits": "core-msvc-debug",
      "binaryDir": "${sourceDir}/build/local-qt5152-check",
      "cacheVariables": {
        "CORDIS_REQUIRE_QT5152": "ON",
        "CMAKE_PREFIX_PATH": "D:/Qt/5.15.2/msvc2019_64"
      }
    }
  ]
}
```

路径仅为示例，替换为实际 kit。执行 `cmake --preset local-qt5152-check`；它要求 Qt 5.15.2 精确版本的 Core、Widgets、Test。Qt 预设构建 Qt 适配与测试；Ela GUI 使用 ela-msvc-* 预设或本文顶部入口。

禁止从 `research/ela` 直接 add_subdirectory：其中的机器路径 FORCE、示例工程及安装行为需要在 P3 独立集成。正式产品依赖版本写入构建清单后再引入。

## 4. 工作区规则

- `sources/` 只读，`research/` 仅作研究副本；两者均不进入产品构建。
- 不把运行配置、DLL 缓存、日志或机器路径提交到仓库。
- 开发新模块时同时增加对应测试，按实际能力扩展 CMake，避免提前建立不能工作的公共 API。
- SDK 基础类型目前仅源码级接口；不要保存枚举数值或向 C ABI 直接暴露 STL 类型。
- 依赖安装需有用户授权；2026-10-26 已按用户要求恢复本机 Qt。开发入口不修改持久机器环境、不添加远程仓库、不自动生成提交。

## 5. 后续构建门禁

每次内核变更运行对应单测及 Portable core 构建。目标工具链就绪后增加 MSVC Debug/Release 验证。Qt/Ela、ABI 和模块卸载测试分别在其阶段加入；在对应测试存在前不宣称全矩阵通过。
