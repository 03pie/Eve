# 项目内维护的 Ela UI 基础库

来源：https://github.com/Liniyous/ElaWidgetTools

导入基线：aae58f5d0d98e05eabe10c263aa7da5f2491d702，ElaWidgetTools 2.0.0 库目录及其图标字体/图片资源。原始归档 SHA256：3f52ac2a47387ebaee0975c72a251795873f927cffee6ba179b17f2d1a2a307a。

本目录纳入版本控制，作为本项目 UI 基础设施持续维护，可按需求修改。仅导入库及资源，不导入上游演示资源或执行上游工程顶层配置。保留原始 MIT LICENSE；原有版权声明继续适用，分发时由构建入口复制许可至 DLL 旁。

保留上游原有的文件尾空行、行尾空格及注释格式；第一方规范检查与本目录相对上游的本地补丁检查分别执行，避免格式化整个导入目录掩盖功能修改。

本地修改：

- ElaNavigationBarPrivate 延迟滚动使用 QPersistentModelIndex，节点移除后不再访问旧 QModelIndex 的悬空节点。
- ElaNavigationModel 删除节点时清空相关选择，导航/页脚动画使用 QPointer 借用节点。
- ElaNavigationBar 新增可选 setNavigationGuard 通用导航谓词，由宿主在切页前校验生命周期。
- 紧凑菜单及展开状态缓存改用稳定 key；撤页同步删除菜单动作与空菜单，恢复模式时重新解析仍存在的分组。
- 原生切页/页脚信号和鼠标释放路径检查 QPointer，导航返回后只使用仍有效的持久索引，防止嵌套事件循环中的窗口或节点删除造成悬空访问。
- ElaNavigationView 显式销毁其样式，导航/页脚动画由所属样式或 delegate 拥有，页脚 delegate 归属其视图，防止窗口删除后继续访问 viewport。
- ElaWindow 新增 setWorkbenchWidgets 通用布局入口，支持活动栏、可调整侧栏、中央页面及底部面板；模式切换时恢复完整工作台容器。

升级时须明确比较上游提交、合并本地修正，并重跑动态节点删除、导航重入、主题及 ASan 测试。构建直接使用本目录，不下载/自动更新上游，不修改 sources/research 参考资料。
