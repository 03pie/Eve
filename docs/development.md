# 开发环境和构建操作

## 1. 两条验证路径

| 路径 | 用途 | 要求 |
|---|---|---|
| Portable core | 现在开展无 Qt 内核编码 | C++17 编译器、CMake ≥ 3.21、Ninja |
| Target desktop | P3/P4 Qt/Ela 和原生模块验收 | Qt 5.15.2 msvc2019_64、一致 MSVC/Windows SDK/CRT、Ninja |

Portable core 通过不等于 Target desktop 已通过。禁止把本机 Qt 6.11.0 自动作为目标版本替代。尚未建立 GUI 可执行目标，也没有空壳“Qt adapter”冒充已实现模块。

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

默认启用 C++17、严格警告和警告即错误。现有测试检查基础类型隔离、头文件自包含以及库的真实链接；生命周期验收尚待实现。

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

路径仅为示例，替换为实际 kit。执行 `cmake --preset local-qt5152-check`；它要求 Qt 5.15.2 精确版本的 Core、Widgets、Test。该选项只检查前置依赖，不会创建尚未实现的 GUI。

禁止从 `research/ela` 直接 add_subdirectory：其中的机器路径 FORCE、示例工程及安装行为需要在 P3 独立集成。正式产品依赖版本写入构建清单后再引入。

## 4. 工作区规则

- `sources/` 只读，`research/` 仅作研究副本；两者均不进入产品构建。
- 不把运行配置、DLL 缓存、日志或机器路径提交到仓库。
- 开发新模块时同时增加对应测试，按实际能力扩展 CMake，避免提前建立不能工作的公共 API。
- SDK 基础类型目前仅源码级接口；不要保存枚举数值或向 C ABI 直接暴露 STL 类型。
- 本轮不自动安装 Qt/MSVC、不修改机器环境、不添加远程仓库、不生成提交。

## 5. 后续构建门禁

每次内核变更运行对应单测及 Portable core 构建。目标工具链就绪后增加 MSVC Debug/Release 验证。Qt/Ela、ABI 和模块卸载测试分别在其阶段加入；在对应测试存在前不宣称全矩阵通过。
