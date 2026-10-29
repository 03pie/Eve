# ADR-0030：原生模块装载与只读描述 ABI

日期：2026-10-28。状态：M2a 实现，验证结果见 [M2 开发记录](../p4-m2-development.md)。

## 范围与版本

P4-M2 分为 M2a（受控装载和只读描述）及 M2b（业务 ABI 签名/能力表冻结）。本次提供真正映射 DLL 的 Cordis::ModuleNative，不提供业务 Runtime、实例或 UI 工厂。M2 整包保持进行中，M3/M4/P5 不提前验收。

新增独立 C11 头 sdk/include/cordis_abi/module_query.h，不依赖 C++、Qt 或 Windows 头。只读入口 cordis_module_query_v1 与设计说明书中将来的业务入口 cordis_query_v1 分开；没有通过空实现提前发布不完整的 HostTable。

query_major/minor 只版本化描述协议；runtime_abi_major/minor 是模块声明的运行接口版本，当前要求与 manifest 的 1.0 一致。描述握手成功不证明尚未实现的业务 ABI 或跨工具链二进制组合可用。

| 类型 | Windows x64 自然布局 | 所有权 |
|---|---|---|
| CordisQueryHeaderV1 | 16 字节 | struct_size 为生产者长度；reserved 必须为零 |
| CordisQueryBytesV1 | 16 字节 | 借用字节，不要求终止零，不跨传 STL 或分配器 |
| CordisModuleQueryV1 | 32 字节 | 仅调用期间借用，不得保存请求指针 |
| CordisModuleDescriptorV1 | 80 字节，对齐 8；plugins 偏移 72 | 字符串/数组在映射期间不可变且有效；宿主只读取有界值 |

函数和函数指针均明确使用 __cdecl，C linkage 与显式导出。宿主预填输出容量，模块最多写 min(容量, 自身长度)，返回自身结构长度；较短结构、未知主/次版本、非零保留位、未知策略、错误标识/版本/插件目录均拒绝。模块标识最多 128 字节，版本最多 64 字节，插件目录 1–64 项且唯一，与已验证 manifest 精确对应。返回状态使用独立 int32 稳定编号，不沿用 Core ErrorCode 枚举值。

只读 query 同步完成，不创建资源、不回调、不激活，也不把异常跨边界。宿主以 /EHs 防御同 MSVC 环境下的违规 C++ 异常，见 [MSVC 异常模型](https://learn.microsoft.com/en-us/cpp/build/reference/eh-exception-handling-model?view=msvc-170)；其他目录保持原模型。没有用 /EHa 吞掉访问违规，不把同进程模块当沙箱，也不声称能安全解引用任意坏指针。

## 固定来源和装载

load 接受根目录/manifest 路径，不能直接接受一份可能过期的 PreflightArtifact 作为装载许可。它先预检，固定声明/产物句柄和祖先目录，再在保护期间重跑 M1 校验并比较来源、内容和目录。祖先从卷根向下逐个固定，每条链最多 128 层，避免最终装载按路径重新解析时祖先被替换。

PE 头从相同文件句柄读取；要求 AMD64、PE32+、DLL 特征及有界偏移。改名为 .dll 的 EXE、其他架构及非 PE 文件在进入装载器前拒绝，不创建驻留代次。

使用 [LoadLibraryExW](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-loadlibraryexw) 的绝对路径，以及 DLL_LOAD_DIR / SYSTEM32 搜索标志；不把当前目录或进程 PATH 加入依赖搜索。载入后再次比对实际映像文件身份，才查询导出。主 DLL 的 hash 不认证其依赖或模块作者；当前只有受信的本地代码/部署范围，没有新增包安装或信任体系。

文件身份采用卷标识与完整 [128 位 FileId](https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-file_id_info)，不能把不可取得/全零或截断的标识当作相等。缺少该能力时原生装载拒绝，M1 只读预检仍可使用。已由外部加载器映射的主模块拒绝接管；缓存命中之前也要遍历全目录检查物理产物归属。取得原生引用后还检查实际文件身份和最终路径，拒绝其他路径映像的意外复用。

库装载可先执行静态初始化。只读描述不能使任意库装载无副作用；已映射的错误模块仍保守保留。生成代次、Loader 拒绝原因与是否实际映射分别记录，失败不伪报为卸载。

nativeMapped 只统计本目录取得的主映射引用，不盘点外部加载器。LoadLibraryEx 失败可能已经触发系统的依赖初始化/回滚，不是“没有执行任何代码”的证明；静态初始化仍必须遵守不创建业务资源的作者契约。

## 默认驻留和账目

每个管理器只允许其创建线程发起操作。进程目录由唯一实现管理，最多保留 64 代；相同 moduleId/版本/hash/插件目录复用同一代，不再次 query。不同内容占用同一 ID、同一物理产物伪装其他 ID、并发重复装载及已排空代次均拒绝。只读快照返回宿主值，不暴露 HMODULE、DLL 指针或业务调用能力。

已映射模块以 [GetModuleHandleExW PIN](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getmodulehandleexw) 固定至进程退出；若 pin 失败，保留已有加载器引用并报错，不调用 FreeLibrary。目录与产物引用有界地驻留在进程控制块中，管理器析构不清空账目或释放 DLL。产物只读保护也保留，不能覆盖驻留的主 DLL。

query 调用持有临时 Call 租约，正常/失败后释放；residentPolicy、nativeProcessPin 和 loadRejected 等原因独立保留。握手期间保持 Validated，成功后才在同一锁中发布 Loaded 与描述通过标志；drain 后为 Resident，Failed 可同时 nativeMapped=true。没有业务实例，所以此处排空不替代将来 Kernel、worker、callback 的完整关闭证明。失败目录及已排空代次不在当前进程自动重试或替换，后续恢复必须另行设计。

## 验证边界

独立 C11/C++17 布局检查及真实 C DLL 配套；覆盖错误描述、异常、重复/冲突、哈希变更、PE/架构、共享写入、默认 pin、跨线程、目录预算和依赖搜索。测试 DLL 进程退出后，外层 CMake 才清除其明确命名的临时目录，避免把无法删除的驻留文件误当泄漏。

整个 P4 的业务 HostTable、completion/取消、服务/Effect 桥、ModuleLease 在真实业务捕获上的托管、Qt UI DLL、可卸载性与替换，均不由 M2a 证明。
