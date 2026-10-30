# ADR-0039：同工具链 UI 插件共用 Qt/UI 运行库

日期：2026-10-30。状态：接受。承接 [ADR-0038](0038-qt-runtime-ownership.md)。

## 问题

共享 Core 只能统一实例与服务身份。原 Cordis::Qt、Cordis::Ui 仍为静态库，若插件直接链接，就会把调度器、页面和平台实现编译到另一个映像中。QtScheduler 的私有 wakeType 在每个映像各自注册事件类型；插件通过 final 具体类型调用 post 可以进入 DLL 内的静态实现，向宿主 receiver 投递它不识别的事件，任务已接受却不执行。宿主正在 drain 时继续取队列会掩盖此问题，因此必须在其静止后从 DLL 发起投递。

真实 Qt/Ela 工厂 DLL 的页面正常创建时，原静态构建的调度回归失败，明确报出 DLL posted job did not run；生命周期、多实例及换代对照通过。该问题需要收敛链接边界，不能通过增加唤醒分支、全局手动 processEvents 或复制页面管理器修补。

## 决定

- CORDIS_SHARED_CORE=ON 时，Core、Qt 和 Ui 使用共享 DLL。Qt/UI 沿用当前同工具链源 SDK 的函数自动导出与 Debug 的 d 后缀，不发布稳定 C++ 二进制 ABI。
- Cordis::QtUiPluginSdk 只聚合 Cordis::QtPluginSdk、共享 Ui/Qt 及 Ela。插件不链接 ModuleManager、模块目录或配置驱动；宿主仍独占模块装载/诊断和配置执行能力。
- Qt 适配器不依赖 UI/Module；Ui 不依赖 Module。ConfigUi 仍为宿主侧静态装配。Ela 已是共享库，继续复用现有主题/组件和品牌资源。
- 关闭共享 Core 时保持原静态兼容路径，不提供 Qt C++ 业务插件 SDK；C ABI 与只读预检保持可用。插件必须使用当前 SDK 的实际共享目标，不支持自行复制 Qt/UI 实现到插件中。
- 页面贡献继续使用 UiPagesService、PageHandle、Core effect 和 QObjectResource。先完整销毁页面及子对象，释放 DLL 工厂捕获，最后由 Core 的末尾屏障销毁业务 QObject；loader root 继续驻留。

## 验证与范围

独立真实 DLL 检查跨模块具体类型调度、服务调用、Ela QWidget、工厂捕获/子对象/业务对象析构、多实例和 epoch 换代。既有工作台与配置回归检查共享库转换兼容性，实际导入表核对宿主/插件依赖同一组运行库。

这段完成 M4c 的运行库前置条件，不替代业务 UI DLL 示例、Kernel/Session 配置装配、模块诊断和原生桌面验收。最终结果见 [M4c 记录](../p4-m4c-development.md)。
