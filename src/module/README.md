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

M3c2 开启生产 SERVICES，NativeModule.runtime 将原生实例绑定真实 Core Context，并复用声明服务、minor/capability/realm、异步调用和旧绑定清理。低层 create 未绑定 Core 时服务操作拒绝；托管实例先关闭 Core 所有者，再关闭模块会话。配额拒绝不新增依赖或隔离健康提供者，同步 ACK 不占异步仲裁预算。见 [服务桥记录](../../docs/p4-m3c2-development.md)；上文“服务关闭”为 M3c1 历史状态。
