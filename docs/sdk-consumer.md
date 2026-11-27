# Eve 仓库外插件 SDK

## 从 Cordis 0.1.x 迁移

使用 Eve 0.2.0 SDK 时，将工程改为 `find_package(Eve 0.2.0 EXACT CONFIG REQUIRED COMPONENTS QtUiPluginSdk)`，通过 `eve_add_qt_ui_plugin` 构建。C++ 头路径使用 `<eve/...>`，命名空间使用 `eve::`。以新版模板重新生成 Qt metadata 和工厂 IID。对于 Native 模块，使用 `<eve_abi/...>` 的声明重建并导出 `eve_module_query_v1`、`eve_query_v1` 和所实现的可选扩展。最后由构建助手重新生成与产物匹配的清单及 SHA-256。插件作者原来分配的模块 ID 和插件 ID 可继续使用，已有工作区通过这些身份关联实例。

当前交付面向 Windows x64、匹配 MSVC、动态 CRT 与 Qt 5.15.2 的共享 C++ 插件。安装模板的 CMakeLists 指定生产该 SDK 的精确版本。插件和实际宿主使用兼容的同一组 Core/Qt/Ui 共享库，Qt 加载器检查实际 Core 身份和 metadata。

第一方代码的公开许可证待确定。随源码使用的第三方依赖及其来源见[第三方声明](../THIRD_PARTY_NOTICES.md)。

## 生成与安装

在 x64 MSVC 开发终端中设置 `EVE_QT5152_ROOT` 指向匹配 Qt kit。下面使用 Release 预设。

```powershell
cmake --preset ela-msvc-release -DEVE_INSTALL_SDK=ON
cmake --build --preset ela-msvc-release
$sdkPrefix = Join-Path $PWD 'build/sdk-install'
cmake --install build/ela-msvc-release --prefix $sdkPrefix
```

安装前缀可整体搬迁。消费者自行提供匹配的 Qt 开发套件。Ela 的库、公开头与 CMake 配置随 SDK 一起安装。运行宿主时需要匹配的共享运行库。

## 外部消费

复制安装前缀中的 `share/Eve/templates/qt-ui-plugin` 到仓库外目录。模板用 `find_package` 的 `EXACT` 模式加载生产者版本并调用 `eve_add_qt_ui_plugin`。安装包导出 `Eve::Core`、`Eve::Qt`、`Eve::Ui`、`Eve::QtPluginSdk` 和 `Eve::QtUiPluginSdk`，常规 UI 插件链接最后一个目标。安装树保留完整的 SDK 头目录以兼容已有消费者。可见的 Loader、ModuleHost 和 Replacement 宿主声明由宿主装配使用，插件的受支持链接接口以导出目标为准。

构建模板后，在新工作区加载生成的 `module.json`。将 `$moduleManifest` 设置为生成文件的绝对路径，再从平台仓库运行。

```powershell
.\tools\desktop.ps1 -Action run -Preset ela-msvc-release -Workspace (Join-Path $PWD 'build/sdk-workspace') -ModuleManifest $moduleManifest
```

helper 接口为：

```cmake
eve_add_qt_ui_plugin(my_plugin
    SOURCE plugin.cpp
    MODULE_ID org.example.my-plugin
    PLUGIN_ID example.my-plugin
    MODULE_VERSION 1.0.0
    SOURCES implementation.cpp contract.hpp)
```

`SOURCE` 包含 QObject 工厂、`Q_PLUGIN_METADATA` 和 `<source-stem>.moc`。修改模板的 `PLUGIN_ID` 即可同步生成工厂使用的目标私有字符串宏 `EVE_QT_UI_PLUGIN_ID`、Qt metadata 与模块清单。两个 ID 都以 ASCII 字母开头，点分段至少一个字符，段内可使用 ASCII 字母、数字、下划线和连字符，最长 128 个字符。metadata 文件名为 `eve_plugin_metadata.json`。助手用 SDK 版本填入工具链记录，链接完成后按实际 DLL 生成 `module.json`。输出位于本目标独立的 `module/<target>` 目录，加载器为 `qt-ui`，驻留策略为 `resident`。

## 契约与资源

ModuleManager 是唯一动态模块入口。loader root 只做 factory，交给 QPluginLoader 管理；每次 `create` 返回新建、无 parent、处于应用线程的业务 QObject。业务对象的页面、服务、连接、任务与线程必须登记到本代 context，并在 cleanup 屏障内完成释放。不得手动删除 root、绕过失活代身份、保存服务借用中的裸引用或用未托管回调继续访问插件。页面通过 `ui.pages` 服务贡献，宿主内部窗口不成为 SDK 接口。

独立工作台接受一个声明单个插件的驻留 Qt UI 模块。宿主提供 `ui.pages`、`ui.commands`、`ui.output` 和 `qt.scheduler`，包括页面注册、命令、持久化输出和受管调度入口。

配置、持久业务数据与热替换状态是三个独立契约；运行态快照不自动变成跨进程持久化。数据版本、插件版本、SDK/ABI、应用版本分别记录。Qt 插件只驻留；C ABI Native 物理卸载必须对准确新产物做独立资格、审查及明确采用。SDK、manifest、构建成功或模板不会扩大批准名单。

## 分层诊断

- 找不到 Eve：检查安装完整性、`Eve_DIR` 或 `CMAKE_PREFIX_PATH`；不要把仓库 build 路径伪装成安装前缀。
- 平台、编译器、CRT、配置不匹配：使用与 producer 完全匹配的 x64 MSVC /MD（Debug 为 /MDd）和单配置 Debug/Release；SDK 配置会给出实际不匹配原因，不自动回退到另一配置。
- 找不到 Qt/Ela：提供 Qt 5.15.2 开发安装，保持整个 SDK 前缀。CMake 消费需要开发头和 import library；只拿运行包不够。
- metadata/manifest 或 Core 身份被拒：检查 plugin ID、模块版本、实际工具链以及宿主/插件是否使用同一共享库；不要修改宿主预检或填假摘要。
- 依赖等待、实例失败、清理保留或业务保存错误：检查宿主 Module/Session 原诊断与插件业务数据契约。不要将配置成功等同于装载成功，或把业务错误改为新的 SDK 批准来源。

启用 `EVE_INSTALL_SDK` 后，`ctest --preset ela-msvc-release -L sdk` 从阶段安装目录复制模板，在独立目录构建，并通过工作台验证实际装载、页面贡献与退出。
