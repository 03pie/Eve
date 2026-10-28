# Module host

P4-M1 提供 Cordis::Module 的宿主分类账目/唯一租约，以及 Windows x64 的 Cordis::ModulePreflight 声明、最终路径与 SHA256 预检。公开头不暴露 Qt；Core 不反向依赖 Module。

GenerationLedger 排空不等于允许卸载，永久 pin 独立保留；租约只能在其真实资源完整析构后释放。当前预检不加载或执行 DLL，也不证明 PE/ABI 有效，返回值只是一次文件快照。

预检的根必须为带盘符或完整 UNC 共享的路径。路径检查、元数据及摘要读取使用同一句柄，持有期间拒绝写入/删除共享，冲突解除后可以重试；函数返回后句柄释放，未来装载仍需重新校验。关闭示例构建不会禁用路径边界回归。

M2a 新增 Cordis::ModuleNative / ModuleManager：固定并重验产物、PE/平台检查、受限搜索及真实 DLL 只读描述握手。模块目录进程共享，同 ID 相同内容复用，失败和 drain 不自动卸载；原生 PIN 与主产物只读保护保留至进程退出。默认诊断仍为预检，显式 --load 才执行 DLL。

业务 HostTable、真实业务资源租约集成及 QPluginLoader 尚未实现。M2a 已完成，M2 整包仍进行中；阶段与验证见 [M2 记录](../../docs/p4-m2-development.md)、[ADR-0030](../../docs/adr/0030-native-module-discovery.md)及 [M1 决策](../../docs/adr/0029-module-preflight-ledger.md)。
