# UI 页面贡献

启用 CORDIS_BUILD_ELA_UI 后构建 Cordis::Ui，公开 pages.hpp 不依赖 Ela 类型。UiPagesService 登记 Context.effect，工厂返回 QWidget；PageHandle 支持 ready、navigate、close 与诊断。页面和分类按实例/epoch 隔离，关闭等待 Q2 完整删除和工厂载荷释放。

宿主 ElaShell 使用固定 ElaWindow，动态导航/堆栈由宿主管理，避免固定上游版本已复现的延迟导航 use-after-free。插件不获得 ElaWindow；正常退出先 Kernel.close，再 shell.close，最后停 scheduler 与 Qt。

页面内的任意程序信号/worker 不自动成为托管资源；当前输入门禁覆盖用户输入，其他回调仍须遵循 Context/服务与专用清理契约。命令、通知、管理页和原生桌面人工交互继续推进。契约见 [ADR-0006](../../docs/adr/0006-ela-page-contributions.md)，证据见 [P3 Q3a 记录](../../docs/p3-development.md#q3a-ela-页面贡献)。
