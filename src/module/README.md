# 模块装载与生命周期

ModuleManager 管理模块的当前代、候选代、会话和工厂租约。预检固定清单和 DLL 路径、文件身份与 SHA-256。Native 加载器随后协商 C ABI，Qt 加载器核对 metadata 和共享 Core 身份。预检结果只证明被核对的文件快照，实际装载仍需重验。[预检](../../docs/adr/0029-module-preflight-ledger.md) · [Native 发现](../../docs/adr/0030-native-module-discovery.md) · [Qt 边界](../../docs/adr/0037-qt-module-boundaries.md)

模块安装将声明的产物放入固定摘要目录。暂存候选代和预约切换分别管理目录与操作所有权。Native 的在途请求、服务、effect 与回调捕获由各自的账目保留，状态迁移也绑定实际代次与实例。Core 保持独立，由宿主组装模块、配置与替换。[安装](../../docs/adr/0042-module-artifact-installation.md) · [预约](../../docs/adr/0046-module-replacement-reservation.md) · [状态迁移](../../docs/adr/0049-versioned-replacement-state.md)

Native 和 Qt 模块默认驻留。Native 显式物理卸载要求首次装载时匹配宿主批准的模块 ID 和精确产物摘要。关闭和排空后，宿主还须在原线程显式请求卸载，验证账目归零与实际映像释放。保留的外部映射会阻止卸载。[卸载策略](../../docs/adr/0050-approved-native-module-unloading.md) · [回归测试](../../docs/testing.md)
