# P7 SDK 仓库外交付记录

日期：2026-11-24。范围：匹配 Windows/MSVC/x64/Qt 5.15.2 的共享 C++ 源码 SDK、最小外部插件模板及真实仓库外构建。P7 总体场景与工作台装配由 ADR-0062 及对应实现记录负责。

## 修改前的架构与缺口

`sdk/include` 已有公开契约；`Cordis::QtPluginSdk` 和 `Cordis::QtUiPluginSdk` 已沿 Core → Qt → Ui 共享运行库提供插件依赖，插件不链接宿主 ModuleHost、Session 或 Replacement 实现。Qt loader 的实际 Core 身份握手、精确 SDK/工具链预检和 QObject 资源清理仍是唯一接入机制。当前根工程只提供仓库内 ALIAS，没有 Cordis 安装配置和仓库外消费模板；直接把静态 Core 链入插件会复制身份与服务状态。

## 方案与职责

- 根工程新增显式 `CORDIS_INSTALL_SDK` 选项，集中由 `cmake/CordisSdk.cmake` 安装公开头和 Core/Qt/Ui 共享库，导出原插件接口目标；此选项不改变旧构建默认值或重新批准旧产物。首段只交付插件 SDK，不隐式导出宿主静态依赖闭包。
- 安装的 `CordisConfig.cmake` 使用相对前缀定位自身及原 Ela 安装包；查找匹配 Qt，检查 Windows/MSVC/x64、精确编译器版本、动态 CRT 和可用配置。配置与宿主不匹配时给出错误，不用 Release/Debug 回退掩盖差异。C++ SDK 不是跨工具链稳定二进制 ABI。
- `cordis_add_qt_ui_plugin` 管理实际 metadata/moc 和链接后的 manifest，复用原 `QtPluginContract.cmake`、`WriteModuleManifest.cmake`。模板只消费安装目标，不引用仓库源码、私有目录或绝对 build 路径。metadata 中 SDK 版本取自 Cordis 包，不能误取外部项目版本。
- 模板只提供按代托管的最小页面。配置、业务持久数据及热替换快照仍由具体插件分别定义；模板不提供虚构持久化、后台任务或批准系统。真实文献插件由独立作者实现。
- SDK 包和生成的 manifest 不构成批准名单。Qt 插件保持 resident；新 Native DLL 若需要物理卸载，必须沿原独立资格、审查与明确采用流程逐产物批准，默认 Native 名单仍为空。

## 状态和资源所有权

SDK producer 由根在新主构建目录 configure/build/install；外部消费者验证使用仓库外的新目录，既有 P6 产物与资格目录保持原字节。CMake helper 只生成本 target 的构建输入和输出，不启动插件或更改宿主状态。Qt loader 持有 root，Core context/effect 持有每代页面和回调，模板不手工删除 loader root，不保留失活代的业务能力。

验证包括真实安装前缀搬迁后的 `find_package`、插件 configure/build、metadata/manifest SHA 检查，以及错误工具链/CRT/缺组件/配置的明确诊断。构建成功不代替实际装载、业务持久化、生命周期或 GUI 验收。测试与证据完成后在本记录追加真实结果。

## SDK 外部消费实测

根在全新 `build/p7-reader-release` 实际 configure/build/install 自然退出 0，安装前缀为其 `sdk`。外部验证复制此 159 文件安装树至仓库外 `D:/WorkSpace/P7SdkExternal20261124-1/sdk-moved`，复制与验证前后逐路径、长度、SHA 相同；安装 CMake 文件没有生产者源码或 build 的绝对路径依赖。匹配的 Qt 开发依赖仍由外部消费者明确提供，不将本测试称为无开发依赖的干净运行环境。

外部模板真实 `find_package` configure（PID 41980）和编译（PID 22028）均退出 0。模板项目版本为 1.0.0，生成 metadata 正确记录 SDK 0.1.5、MSVC 19.44.35222.0、Qt 5.15.2、x64、MD/Release；链接后 manifest 等于实际 DLL SHA，loader 为 `qt-ui`、策略为 `resident`。错误配置 Debug、静态 CRT、请求宿主 ModuleHost 组件、非规范模块版本和非法 ID 的五次独立 configure 均实际退出 1，并给出对应诊断。

原始命令、PID、退出、stdout/stderr、模板源、metadata/moc 和编译命令保留在 `build/p7-sdk-external-evidence-20261124-1/execution-1`；结果 SHA 为 `ef2f37aade02cdddb39ae1e61fe62dca613aa5d883e9c0869629a385ba176cca`。本段未装载或执行模板 DLL，未修改批准文件、P6 资格产物或业务数据。实际文献插件装载、多实例、依赖等待、持久化与关闭由 P7 场景另行验证。

SDK 独立交付为 `D:/WorkSpace/delivery/Cordis-P7-literature-reader/Cordis-0.1.5-windows-x64-msvc-release-sdk.zip`：6,993,008 字节，SHA `58e0a1e0ea1aa9ed02b5c2d6bccc1f6d8b6187dcc57a485ede9d79f7562a3cfd`。159 份安装文件及 3 份清单/摘要元数据共 162 条，逐条长度、SHA、完整解压集合和源稳定性核对通过。独立复核见 `build/p7-sdk-independent-review-20261124-1/review.json`，SHA `dbbb9d06b224ca31e4978f9761a400b40d6de3b6cfd5f24a4a46a7fdd7e5c066`。Qt 开发文件作为单独的 5.15.2 prerequisite，不混入 SDK ZIP。

本轮 Release SDK 与实际工作台共用四份 DLL，SDK 内记录及已闭合宿主的摘要完全相同：Core `13c49c14def1790852b62595f8940eb7b3328dba05b8ab115a5a224033785997`，Qt adapter `02baa82c8fda1378fc7d8ef859a4d4af65c020e9396a0ba8f7c14904831e869b`，Ui `162a7e9dac12c66e2c521cd6df774248395934e5aa49174bbc716e2cf4d1c37f`，Ela `501dc57da52a0d8b170e99810600e2a256fff596562a0dcb8fdff26dd12af2c3`。它们是共享平台运行库，不是新增 Native 卸载批准。实际外部模板装载及阅读器端到端由 `tests/p7` 完成，其最终证据索引为 `build/p7-e2e-check-20261124-1/evidence-index.json`，范围和失败历史见 `tests/p7/README.md`。
