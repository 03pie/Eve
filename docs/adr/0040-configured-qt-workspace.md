# ADR-0040：配置驱动的业务 UI DLL 与宿主诊断

日期：2026-10-30。状态：采用（M4c2）。承接 [共享 Qt/UI 运行库](0039-shared-qt-ui-runtime.md)。

## 边界

业务笔记 DLL 仅链接 Cordis::QtUiPluginSdk，沿用 IQtPluginV1/IQtInstanceV1、UiPagesService、CommandService 和 OutputService。页面按钮调用同一个 CommandHandle，经现有命令门禁、队列和在途屏障执行；不在原生 Qt 信号闭包里直接调用业务 QObject。页面、命令和工厂捕获先退出，再由既有 afterCleanup/QObjectResource 释放业务对象。插件不链接模块目录、Loader、Session 或宿主配置 UI，没有测试专用导出。

示例宿主装配 QtRuntime、KernelReconcileDriver、ConfigurationSession 和现有配置管理适配器。宿主配置/诊断 Runtime 与配置拥有的业务 Runtime 都受管理保护，不能从手动停止/重试入口绕过配置所有权。模块窗口只复制 ModuleManager 和 DiagnosticsService 的现有值，显示逻辑状态、真实执行引用、驻留原因及配置失败原因链；不把 Serving 误称为业务实例运行成功，也不提供卸载。该单一宿主装配不增加通用插件框架、第二个进程目录或 Ui→Module 依赖。

## 添加配置实例

原管理适配器通过通用文本 helper 追加实例，默认 module=builtin；外部 Qt 插件会在冻结目录校验中被拒绝。配置归属不能由页面从现有条目猜测，更不能让插件指定任意产物。

新增 ConfigurationSession.addInstance：从本会话已冻结的 PluginDefinition 取得 PluginModule，在同一业务访问屏障内追加条目，必要时登记精确 manifest 别名，再经完整解析、schema、驱动预检及 CAS 接受。已存在的别名冲突拒绝覆盖。既有 addConfigurationEntry 增加默认值为 builtin 的宿主模块参数，保持旧源码调用兼容，并继续保留其他配置数字原文。

UI 配置适配器仅委托该入口。未应用的已有磁盘配置、未知插件、旧修订、重复身份、错误线程和关闭重入均不能偷偷创建或接受目标。预览、添加和配置编辑都不装载新 DLL，仍仅使用宿主已登记的模块。

## 退出与失败

退出先显式保存当前目标；写入冲突保留开放 Session、业务页面和窗口，让用户处理冲突后重试。保存成功后依次关闭 Session、Kernel、页面/外壳，撤回平台服务，释放宿主定义和配置引用，最后关闭 scheduler。业务清理失败保留对象与模块执行引用，不强制删除或伪报静止。

DLL 使用构建专属的元数据及链接后真实 SHA256 声明。正常退出仅释放执行引用，Qt loader root 和映像继续驻留。M4c2 的独立示例与自动化不代替 M4c3 的真实桌面操作确认和整个 P4 收口。
