# 架构决策记录

这些记录说明平台接口和实现边界的来由。部分记录描述演进时的边界，后续决定通过交叉引用标明。平台入口见[架构概览](../architecture.md)，复现方式见[测试指南](../testing.md)。

## Core 契约与资源屏障

- [ADR-0001 首阶段范围与基础约束](0001-foundation.md)
- [ADR-0002 异步完成与回调投递分离](0002-task-delivery.md)
- [ADR-0003 清理结果与资源释放确认分离](0003-effect-quiescence.md)
- [ADR-0004 Qt 调度与退出边界](0004-qt-scheduler.md)
- [ADR-0005 Qt 对象与计时资源的完整删除屏障](0005-qt-resource-barriers.md)
- [ADR-0023 托管 Qt 信号触发连接](0023-managed-qt-connections.md)
- [ADR-0024 插件工作线程的原生退出屏障](0024-managed-workers.md)
- [ADR-0025 托管工作线程的类型化数据结果](0025-typed-worker-results.md)
- [ADR-0026 类型化 Qt 信号的数据冻结与退出屏障](0026-typed-qt-connections.md)
- [ADR-0027 原生 Qt 信号的显式值前缀](0027-native-signal-prefix.md)

## Ela 工作台与 UI 贡献

- [ADR-0006 Ela 外壳中的宿主页面贡献](0006-ela-page-contributions.md)
- [ADR-0007 命令调用屏障与纯值通知](0007-ui-commands-notifications.md)
- [ADR-0008 基础管理页与只读诊断](0008-management-diagnostics.md)
- [ADR-0009 Ela 组件与主题统一](0009-ela-components-theme.md)
- [ADR-0010 项目内维护 Ela 基础库与原生导航](0010-maintained-ela-navigation.md)
- [ADR-0028 Home 卡片管理窗口与平台添加入口](0028-home-management-windows.md)
- [ADR-0051 UI 贡献复用原发布组](0051-ui-contribution-publication.md)
- [ADR-0063 日志查询与显示边界](0063-log-query-contract.md)

## 配置、协调与持久化

- [ADR-0011 配置结构候选与差异预览](0011-configuration-preview.md)
- [ADR-0012 受限 YAML 与内置插件候选校验](0012-restricted-yaml-validation.md)
- [ADR-0013 目标接受与串行协调基础](0013-configuration-coordinator.md)
- [ADR-0014 配置文件事务基础](0014-configuration-file-transactions.md)
- [ADR-0015 目标与保存状态、单写队列和退出刷新](0015-configuration-store.md)
- [ADR-0016 顶层内置配置的真实 Kernel 驱动](0016-kernel-configuration-driver.md)
- [ADR-0017 实例级类型化配置依赖](0017-instance-inject-contracts.md)
- [ADR-0018 宿主跨代观察与类型化子实例桥](0018-activation-context-bridge.md)
- [ADR-0019 内置配置子树的真实协调](0019-configuration-subtrees.md)
- [ADR-0020 配置会话与可选 Ela 配置页](0020-configuration-session-ui.md)
- [ADR-0021 Home 与管理页的配置路由](0021-configuration-management-routing.md)
- [ADR-0022 配置后台 I/O 与文件监听](0022-configuration-background-io-watcher.md)
- [ADR-0047 配置与运行共用不可变定义目录](0047-configuration-definition-identity.md)
- [ADR-0066 工作区配置的归属](0066-workspace-configuration.md)

## 原生模块、ABI 与 Qt 模块

- [ADR-0029 原生模块预检与宿主驻留账目](0029-module-preflight-ledger.md)
- [ADR-0030 原生模块装载与只读描述 ABI](0030-native-module-discovery.md)
- [ADR-0031 业务 ABI 完整函数表与协商](0031-business-abi.md)
- [ADR-0032 原生请求仲裁与终结屏障](0032-native-request-arbitration.md)
- [ADR-0033 稳定原生宿主与真实生命周期](0033-native-host-lifecycle.md)
- [ADR-0034 原生排队操作与 effect 资源桥](0034-native-dispatch-effects.md)
- [ADR-0035 原生服务注册、提交租约与清理调用](0035-native-services.md)
- [ADR-0036 在现有原生桥上增加 Core 托管模式](0036-managed-native-runtime.md)
- [ADR-0037 Qt 模块边界与渐进实现](0037-qt-module-boundaries.md)
- [ADR-0038 同工具链共享 Core 与业务对象末尾清理](0038-qt-runtime-ownership.md)
- [ADR-0039 同工具链 UI 插件共用 Qt/UI 运行库](0039-shared-qt-ui-runtime.md)
- [ADR-0040 配置驱动的业务 UI DLL 与宿主诊断](0040-configured-qt-workspace.md)
- [ADR-0050 白名单原生模块的映像终结](0050-approved-native-module-unloading.md)
- [ADR-0065 外部 Qt 插件的生成与安装契约](0065-sdk-generation.md)

## 代次管理、受控发布与替换

- [ADR-0041 同一进程目录中的当前代与候选代](0041-module-candidate-generations.md)
- [ADR-0042 固定来源与 hash 目录安装事务](0042-module-artifact-installation.md)
- [ADR-0043 同一服务索引中的受控发布组](0043-controlled-service-publication.md)
- [ADR-0044 以真实服务图和 Kernel 所有权计算替换闭包](0044-replacement-dependency-closure.md)
- [ADR-0045 Core 替换闭包的排他所有权](0045-replacement-scope-ownership.md)
- [ADR-0046 模块预约与共同创建准入](0046-module-replacement-reservation.md)
- [ADR-0048 由宿主持有整体替换操作](0048-host-replacement-operation.md)
- [ADR-0049 替换状态由实例提供、由宿主事务持有](0049-versioned-replacement-state.md)
- [ADR-0064 Native 替换来源的所有权](0064-native-input-ownership.md)

## 依赖诊断与运行验证

- [ADR-0052 依赖评估的原位只读统计](0052-dependency-evaluation-observation.md)
- [ADR-0053 服务可用性按键通知原激活作业](0053-keyed-activation-notifications.md)
- [ADR-0054 同进程真实 Native 连续替换资格](0054-native-workspace-endurance.md)
- [ADR-0055 真实工作台有限动作轨迹与独立进程回放](0055-workspace-action-traces.md)

## 桌面发行

- [ADR-0067 Windows 工作台运行时的安装边界](0067-windows-runtime.md)
- [ADR-0068 Windows 桌面安装器](0068-windows-installer.md)
- [ADR-0069 Eve 平台与开发接口命名](0069-eve-platform-identity.md)
- [ADR-0070 官方桌面插件和运行日志的所有权](0070-kernel-desktop-plugin.md)
- [ADR-0071 本地扩展库、工作区实例与可切换桌面](0071-extension-packages-and-desktop-selection.md)
- [ADR-0072 首页插件卡片与扩展详情窗口](0072-home-extension-details.md)
- [ADR-0073 工作台顶栏与快速打开](0073-workbench-titlebar-and-quick-access.md)
- [ADR-0074 工作台材质与主题](0074-workbench-surface-appearance.md)

0068 及以前的记录沿用决策时的 Cordis 项目名和接口名；当前模块名称与开发入口见[平台架构](../architecture.md)。

内部交付和验收记录可在 Git 历史中查看。
