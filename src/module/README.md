# Module host

P4-M1 提供 Cordis::Module 的宿主分类账目/唯一租约，以及 Windows x64 的 Cordis::ModulePreflight 声明、最终路径与 SHA256 预检。公开头不暴露 Qt；Core 不反向依赖 Module。

GenerationLedger 排空不等于允许卸载，永久 pin 独立保留；租约只能在其真实资源完整析构后释放。当前预检不加载或执行 DLL，也不证明 PE/ABI 有效，返回值只是一次文件快照。

预检的根必须为带盘符或完整 UNC 共享的路径。路径检查、元数据及摘要读取使用同一句柄，持有期间拒绝写入/删除共享，冲突解除后可以重试；函数返回后句柄释放，未来装载仍需重新校验。关闭示例构建不会禁用路径边界回归。

M2a 新增 Cordis::ModuleNative / ModuleManager：固定并重验产物、PE/平台检查、受限搜索及真实 DLL 只读描述握手。模块目录进程共享，同 ID 相同内容复用，失败和 drain 不自动卸载；原生 PIN 与主产物只读保护保留至进程退出。默认诊断仍为预检，显式 --load 才执行 DLL。

M2b 新增独立 plugin.h、完整业务表协议和 Cordis::ModuleAbi 私有协商器，见 [ABI V1](../../docs/abi-v1.md)及 [M2b 记录](../../docs/p4-m2b-development.md)。ModuleManager 仍只做 M2a；业务表不从公共管理器泄露，真实请求/服务/effect 与 ModuleLease 集成留给 M3，QPluginLoader 留给 M4。

M2 装载/协商层已完成，不代表整个 P4 或物理卸载完成；M2a 历史见 [发现记录](../../docs/p4-m2-development.md)及 [ADR-0030](../../docs/adr/0030-native-module-discovery.md)。

M3a 的 Cordis::ModuleRequests 提供私有请求仲裁/JSON 复制与真实 ledger 租约。close 与最终 seal 分开，drained 等待真实退役；投递/协议不确定时保留账目。它尚未替代 M3b–M3d 的原生宿主和 Kernel 资源桥，见 [M3 记录](../../docs/p4-m3-development.md)。

M3b 的 Cordis::ModuleHost / NativeModule 提供稳定基础 HostTable、真实 DLL 生命周期、跨线程回调 Call 租约和显式关闭；不确定失败保留对象和模块。Serving 只表示业务宿主已建立；capabilities=0，不将未接入服务/effect 伪报为生产能力。实现及后续边界见 [M3b 记录](../../docs/p4-m3b-development.md)。

M3c1 开启生产 DISPATCH/EFFECTS：复制及排队调用、独立请求/ACK、真实 Core effect、逆序清理和最后捕获退役屏障。服务能力仍关闭，M3c2 接入服务后再进入 Kernel/Session 联合验收，见 [资源桥记录](../../docs/p4-m3c-development.md)。上述 capabilities=0 是 M3b 提交时的历史状态。

M3c2 已开启生产服务能力；M3d1 在同一桥上新增 NativeModule.runtime/NativePlugin 与 Core 托管模式。输入借用 Core 预提交租约、输出由 Core 统一发布并保留到完整清理；低层 open/create 继续可用。宿主通过 Kernel.registerRuntime 保留定义身份，并以冻结配置目录接入 Session，见 [接入与验证](../../docs/p4-m3d1-development.md)及 [ADR-0036](../../docs/adr/0036-managed-native-runtime.md)。托管实例先由 Core/Kernel 关闭，之后关闭模块会话、排空代次、最后关闭 scheduler。当前真实 DLL 夹具不替代 M3d2 业务示例/Qt 事件循环或 M4 UI DLL；默认 PIN 驻留不变。

M3d2a 已增加 [两个业务服务 DLL 与真实 Qt/Kernel/Session 示例](../../examples/native_services/README.md)，包含实际磁盘保存、多实例/realm、启动失败和清理失败驻留；仍沿用 M3d1 托管桥和默认 PIN。[M3d2b 的分层 S06/S07](../../docs/p4-m3d2b-development.md)已通过，M3 当前范围收口；M4 UI DLL 不由该示例交付。

M4a 将文件/祖先固定、二次声明/摘要及 PE 检查提取为私有 VerifiedSource，原生后端与 Cordis::ModuleQtPreflight 共用。manifest 明确区分 c-abi/qt-ui，原生后端拒绝 Qt 类型；Qt 端仅返回宿主工具链/产物快照，不调用 load/instance，不创建 root 或第二套目录。Qt 工厂/页面适配在 M4b/c，见 [架构边界](../../docs/adr/0037-qt-module-boundaries.md)和 [M4a 记录](../../docs/p4-m4a-development.md)。

M4b1 新增 Cordis::ModuleQt / loadQtModule：两个装载后端共用进程目录、受限映像搜索及 PIN；Qt 对相同产物创建一次 loader root，并保留 loader/root 租约。root 活跃时 drain 拒绝，错误构造保守驻留；不手动 delete root 或 unload。此时 descriptorAccepted/businessAccepted 仍为 false，尚不能创建业务实例或插件页面；M4b2 工厂/实例适配继续，见 [记录](../../docs/p4-m4b1-development.md)。

M4b2 提供 IQtPluginV1/IQtInstanceV1、Cordis::QtPluginSdk 和 qtRuntime。工厂通过实际 Core 身份和插件目录握手，宿主创建普通 Runtime；业务对象等待全部 Context 资源、输出对象和后登记的末尾清理完成，再通过 QObjectResource 完整析构并释放模块引用。输入 committed 租约在末尾清理成功后才释放，失败保留未确认对象。Windows/MSVC/Qt 默认使用共享 Core（Debug 加 d），SDK 与 Core 拒绝 /MT；无 Qt 默认静态 Core。根工厂继续进程驻留，创建中失活只清理、不激活旧代。实现与验证见 [M4b2](../../docs/p4-m4b2-development.md)，页面 DLL 在 M4c。
