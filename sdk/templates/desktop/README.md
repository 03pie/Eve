# 桌面扩展模板

使用与 Eve 安装版匹配的 Qt、MSVC、CRT 和构建配置，将 SDK 安装前缀传给 CMake 的 `CMAKE_PREFIX_PATH`，运行 `cmake --build build --target external_desktop_package`。在平台首页点击「安装扩展…」，选择生成的 `build/external_desktop.evepkg` 并确认安装。点击该桌面的卡片，在详情窗口中选择使用该桌面并重启。

`Workshop` 创建顶层窗口、工具栏、命令和布局。桌面 SDK 将平台工作区放入空布局，业务实例的页面、输出、通知和配置仍使用当前工作区的同一套服务。桌面导航可以通过 `LayoutServiceV1::invoke` 访问扩展、设置、输出、命令和通知。

桌面必须在应用线程通过本激活代的 Context 调用 `attach`。交付的窗口必须没有父对象、保持隐藏并关闭 `WA_DeleteOnClose`。成功后由 Eve 显示和销毁该窗口。开发期间可用 `eve_plugin_workbench --safe-desktop` 启动内置 Ela 桌面。
