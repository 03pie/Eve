# Cordis 仓库外插件 SDK

当前交付面向 Windows x64、匹配 MSVC、动态 CRT 与 Qt 5.15.2 的共享 C++ 插件。SDK 版本为 0.1.5；此版本号不代表任意 C++ 二进制 ABI 稳定。SDK producer、插件和实际宿主必须使用兼容的同一组 Core/Qt/Ui 共享库，Qt loader 仍检查实际 Core 身份和 metadata。

第一方代码的公开许可证待确定。随源码使用的第三方依赖及其来源见[第三方声明](../THIRD_PARTY_NOTICES.md)。

## 生成与安装

在 x64 MSVC 开发终端中设置 `CORDIS_QT5152_ROOT` 指向匹配 Qt kit。下面使用 Release 预设。

```powershell
cmake --preset ela-msvc-release -DCORDIS_INSTALL_SDK=ON
cmake --build --preset ela-msvc-release
$sdkPrefix = Join-Path $PWD 'build/sdk-install'
cmake --install build/ela-msvc-release --prefix $sdkPrefix
```

安装前缀可整体搬迁。消费者自行提供匹配的 Qt 开发套件。Ela 的库、公开头与 CMake 配置随 SDK 一起安装。运行宿主时需要匹配的共享运行库。

## 外部消费

复制 `share/Cordis/templates/qt-ui-plugin` 到仓库外目录，使用 `find_package(Cordis 0.1.5 EXACT CONFIG REQUIRED COMPONENTS QtUiPluginSdk)` 及 `cordis_add_qt_ui_plugin`。接口目标为 `Cordis::Core`、`Cordis::Qt`、`Cordis::Ui`、`Cordis::QtPluginSdk` 与 `Cordis::QtUiPluginSdk`；插件通常只链接最后一个目标。ModuleHost、Session、Replacement 及工作台内部头不是此安装组件，不应为方便调用把宿主实现静态链入插件。

构建模板后，在新工作区加载生成的 `module.json`。将 `$moduleManifest` 设置为生成文件的绝对路径，再从平台仓库运行。

```powershell
.\tools\desktop.ps1 -Action run -Preset ela-msvc-release -Workspace (Join-Path $PWD 'build/sdk-workspace') -ModuleManifest $moduleManifest
```

helper 接口为：

```cmake
cordis_add_qt_ui_plugin(my_plugin
    SOURCE plugin.cpp
    MODULE_ID org.example.my-plugin
    PLUGIN_ID example.my-plugin
    MODULE_VERSION 1.0.0
    SOURCES implementation.cpp contract.hpp)
```

`SOURCE` 内含 QObject factory、`Q_PLUGIN_METADATA` 及 `<source-stem>.moc`。metadata 文件名固定为 `cordis_plugin_metadata.json`；helper 用安装 SDK 版本而不是消费者项目版本填入工具链记录，使用原 manifest writer 在链接完成后生成 `module.json`。输出为本 target 独立的 `module/<target>` 目录，`qt-ui` loader 与 `resident` 策略固定。

## 契约与资源

ModuleManager 是唯一动态模块入口。loader root 只做 factory，交给 QPluginLoader 管理；每次 `create` 返回新建、无 parent、处于应用线程的业务 QObject。业务对象的页面、服务、连接、任务与线程必须登记到本代 context，并在 cleanup 屏障内完成释放。不得手动删除 root、绕过失活代身份、保存服务借用中的裸引用或用未托管回调继续访问插件。页面通过 `ui.pages` 服务贡献，宿主内部窗口不成为 SDK 接口。

独立工作台接受一个声明单个插件的驻留 Qt UI 模块。宿主提供 `ui.pages`、`ui.commands`、`ui.output` 和 `qt.scheduler`，包括页面注册、命令、持久化输出和受管调度入口。

配置、持久业务数据与热替换状态是三个独立契约；运行态快照不自动变成跨进程持久化。数据版本、插件版本、SDK/ABI、应用版本分别记录。Qt 插件只驻留；C ABI Native 物理卸载必须对准确新产物做独立资格、审查及明确采用。SDK、manifest、构建成功或模板不会扩大批准名单。

## 分层诊断

- 找不到 Cordis：检查安装完整性、`Cordis_DIR` 或 `CMAKE_PREFIX_PATH`；不要把仓库 build 路径伪装成安装前缀。
- 平台、编译器、CRT、配置不匹配：使用与 producer 完全匹配的 x64 MSVC /MD（Debug 为 /MDd）和单配置 Debug/Release；SDK 配置会给出实际不匹配原因，不自动回退到另一配置。
- 找不到 Qt/Ela：提供 Qt 5.15.2 开发安装，保持整个 SDK 前缀。CMake 消费需要开发头和 import library；只拿运行包不够。
- metadata/manifest 或 Core 身份被拒：检查 plugin ID、模块版本、实际工具链以及宿主/插件是否使用同一共享库；不要修改宿主预检或填假摘要。
- 依赖等待、实例失败、清理保留或业务保存错误：检查宿主 Module/Session 原诊断与插件业务数据契约。不要将配置成功等同于装载成功，或把业务错误改为新的 SDK 批准来源。

启用 `CORDIS_INSTALL_SDK` 后，`ctest --preset ela-msvc-release -L sdk` 从阶段安装目录复制模板，在独立目录构建，并通过工作台验证实际装载、页面贡献与退出。
