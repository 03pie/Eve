# Eve 仓库外插件 SDK

Eve 0.3.0 工作台接受正式 0.2.0 SDK 构建的同工具链业务 DLL。桌面作为系统模块提供页面、命令、通知和输出服务，业务项目链接 `Eve::QtUiPluginSdk`，使用原有服务标识。0.3.0 随附的 SDK 新增桌面模板、`Eve::DesktopSdk` 和扩展包构建助手。安装包的产品版本与 `find_package` 所用 SDK 版本分别维护。

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

日常开发可以直接加载模板生成的 `module.json`。将 `$moduleManifest` 设置为生成文件的绝对路径，再从平台仓库运行。

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

### 制作离线扩展包

在调用 `eve_add_qt_ui_plugin` 后加入以下构建声明。`README` 指定详情介绍，`CHANGELOG` 指定版本变更记录，`ICON` 指定 PNG 图标，三项均可省略。

```cmake
eve_add_extension_package(my_plugin PACKAGE_JSON package.json README README.md CHANGELOG CHANGELOG.md)
```

两份文档均使用 UTF-8 编码，每份最多 256 KiB，打包后位于包根目录的 `README.md` 或 `CHANGELOG.md`。工作台在确认安装前即可展示文档，安装后仍可离线阅读。修改已发布版本的说明文档时也需要递增版本号。

向已发布的 0.3.1 及更早支持离线包的工作台分发时，省略 `README` 和 `CHANGELOG` 并保留对应的安装包。那些读取器只接受包描述、模块清单、DLL 和可选 PNG 图标。SDK 的 `find_package` 版本约束描述插件接口兼容性，能否读取新增包成员取决于工作台。打包助手检查文档存在和长度，发布前可用目标工作台打开成品，预览名称、正文和变更记录并检查编码。

下面是业务扩展的 `package.json`。模块 ID、插件 ID、模块版本和 DLL 的 SHA-256 都取自生成的 `module.json`。展示名称与发布者由作者填写，单行摘要 `description` 和默认实例配置 `defaultConfig` 可以省略。

```json
{
  "formatVersion": 1,
  "kind": "business",
  "name": "项目面板",
  "publisher": "示例团队",
  "categories": ["工作区"],
  "description": "为当前工作区提供项目面板。",
  "defaultConfig": {}
}
```

可选字段 `categories` 最多允许 32 个分类，每项最多 64 个字符。同名分类忽略大小写、保留首次出现的名称与顺序。`homepage` 使用网页 URL，`repository` 和 `bugs` 可以使用 URL 字符串或含 `url` 属性的对象。所有资源链接都须是无登录凭据的 HTTP 或 HTTPS 地址。`license` 可填写简短的许可证标识、SPDX 表达式或许可证网页地址。这些字段和 README 一起参与发布内容校验。

执行 `cmake --build build --target my_plugin_package`。将 `build/my_plugin.evepkg` 交给用户，在首页选择安装包，检查详情并确认安装。业务实例由工作区配置保存。更新时递增 CMake 的 `MODULE_VERSION`。平台会检查映像摘要和本机工具链兼容性。

### 开发独立桌面

复制安装前缀中的 `share/Eve/templates/desktop`。模板使用 `find_package(Eve ... COMPONENTS DesktopSdk)`，链接 `Eve::DesktopSdk`，并在 `package.json` 中指定 `"kind": "desktop"`。模板中的 `QMainWindow`、工具栏和顶级布局由扩展创建。工作区为其中一个拥有空布局的子控件。

桌面通过真实实例的 `Context` 取得 `eve::desktop::LayoutServiceV1`，在应用线程调用 `attach` 将顶层窗口交给平台。窗口交付时须保持隐藏、没有父对象并关闭 `WA_DeleteOnClose`。平台将当前工作区嵌入指定位置，复用业务页面、持久输出、命令、设置、扩展和通知服务。自定义导航可用 `invoke` 调用平台动作。实例退役时，平台先分离工作区并销毁已接管的窗口，再释放插件实例。

构建 `external_desktop_package`，从首页安装生成的扩展包。点击该桌面的卡片，在详情窗口中选择使用该桌面并重启。正常重启会先保存工作区。平台在事件循环中确认新的顶层窗口和工作区都可见后记录启动成功。启动失败或缺少回执时恢复随包 Ela。开发中可用 `eve_plugin_workbench --safe-desktop` 单次进入随包桌面，再从首页卡片切换或卸载相关版本。

### 单独构建随包 Ela 工作台

`plugins/builtin/desktop-ela` 是官方完整桌面的插件工程，负责工作台窗口、页面服务和配置界面。将该目录复制到仓库外，使用上文安装的开发前缀和匹配的 Qt、MSVC 工具链即可构建。工程通过 `find_package(EveWorkbench … EXACT)` 取得与产品版本匹配的装配契约、配置库和品牌资源。下面在复制后的插件根目录执行，`$sdkPrefix` 指向开发前缀。

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$sdkPrefix;$env:EVE_QT5152_ROOT"
cmake --build build --target eve_desktop --parallel 4
cmake --install build --prefix .\stage
```

`stage/system/desktop` 包含 `eve_desktop.dll` 和对应的 `module.json`。部署时将二者一起放入相同产品版本的运行包 `system/desktop`，下次启动即会校验并挂载。官方身份由随包部署保留，普通安装入口接收使用独立标识的 `.evepkg`。现有 `DesktopSdk` 模板用于通过 `LayoutServiceV1` 提供顶层窗口与导航的桌面扩展。

### 业务插件

ModuleManager 是唯一动态模块入口。loader root 只做 factory，交给 QPluginLoader 管理；每次 `create` 返回新建、无 parent、处于应用线程的业务 QObject。业务对象的页面、服务、连接、任务与线程必须登记到本代 context，并在 cleanup 屏障内完成释放。不得手动删除 root、绕过失活代身份、保存服务借用中的裸引用或用未托管回调继续访问插件。页面通过 `ui.pages` 服务贡献，宿主内部窗口不成为 SDK 接口。

每份业务包声明一个驻留 Qt UI 插件，同一工作区可以启用多个包的实例。宿主提供 `ui.pages`、`ui.commands`、`ui.output` 和 `qt.scheduler`，包括页面注册、命令、持久化输出和受管调度入口。

旧版业务插件可以原样使用这些服务。自行构造 `ElaShell` 或调用 `ManagementController` 的程序属于嵌入式宿主。此类程序需要从源码构建并链接桌面实现，静态嵌入使用 `EVE_SHARED_CORE=OFF` 和 `Eve::Ui`。安装 SDK 中保留的宿主头文件可供源码引用，安装版 `eve_ui.dll` 提供业务插件服务接口。

配置、持久业务数据与热替换状态是三个独立契约；运行态快照不自动变成跨进程持久化。数据版本、插件版本、SDK/ABI、应用版本分别记录。Qt 插件只驻留；C ABI Native 物理卸载必须对准确新产物做独立资格、审查及明确采用。SDK、manifest、构建成功或模板不会扩大批准名单。

## 分层诊断

- 找不到 Eve：检查安装完整性、`Eve_DIR` 或 `CMAKE_PREFIX_PATH`；不要把仓库 build 路径伪装成安装前缀。
- 平台、编译器、CRT、配置不匹配：使用与 producer 完全匹配的 x64 MSVC /MD（Debug 为 /MDd）和单配置 Debug/Release；SDK 配置会给出实际不匹配原因，不自动回退到另一配置。
- 找不到 Qt/Ela：提供 Qt 5.15.2 开发安装，保持整个 SDK 前缀。CMake 消费需要开发头和 import library；只拿运行包不够。
- metadata/manifest 或 Core 身份被拒：检查 plugin ID、模块版本、实际工具链以及宿主/插件是否使用同一共享库；不要修改宿主预检或填假摘要。
- 依赖等待、实例失败、清理保留或业务保存错误：检查宿主 Module/Session 原诊断与插件业务数据契约。不要将配置成功等同于装载成功，或把业务错误改为新的 SDK 批准来源。

启用 `EVE_INSTALL_SDK` 后，`ctest --preset ela-msvc-release -L sdk` 从阶段安装目录取得模板，在独立目录构建。回归用例 `sdk.installed_qt_consumer` 覆盖业务页面、桌面打包、真实外部窗口的启动与退出、恢复 Ela 和不兼容桌面的拒绝。
