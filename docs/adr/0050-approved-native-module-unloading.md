# ADR-0050：白名单原生模块的映像终结

日期：2026-11-17。状态：采纳，先完成 M4-0 架构审计；实现按 M4 工作单分段。

## 问题与依据

M3-c 已随 b454d85 审计、提交并同步。当前唯一进程目录在 Generation 构造时无条件登记 residentPolicy，mapGenerationImage 无条件执行 Windows 永久 PIN；manifest 和发现协议 1.0 也只接受 resident。Generation 永久持有禁止写入/删除共享的 artifactFile，Native HostRegistry 还缓存业务/状态函数表；64 代限额按历史总量计算。因此不能在 drain 末尾追加 FreeLibrary 来实现卸载。

Windows 的 PIN 持续到进程结束，不能通过 FreeLibrary 撤销；FreeLibrary 成功只证明本方引用被递减，引用归零才移除映像。[GetModuleHandleEx](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getmodulehandleexw)、[FreeLibrary](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-freelibrary)。装卸会运行持有 loader lock 的 DllMain，故平台不得在目录、代次或宿主锁中调用系统释放函数；测试卸载见证也只做简单原子写入。[DllMain](https://learn.microsoft.com/en-us/windows/win32/dlls/dllmain)、[DLL 最佳实践](https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-best-practices)。

说明书 §11.4/§12 和开发指南 §10.2 要求全部执行引用归零、无 pin、后端支持、配置允许；H01/H02 通过后才向实际插件开放白名单。M4 只用专用测试产物验证能力，业务名单发布继续受 M5 压力门禁约束。

## 权限与兼容契约

区分三项：插件的卸载声明、宿主精确白名单、首次映射选定的实际策略。manifest 保存 unloadPolicy:resident 或 unloadPolicy:explicit；请求本身不授权。首轮白名单仅为 C ABI 后端，键为 (c-abi, moduleId, sha256)，使用 verifyModuleSource 重新固定的真实产物比较；版本名、旧预检对象或路径文本不能代替摘要。路径边界继续由受控根、最终路径/文件身份和同句柄摘要保证。

宿主在 load/stage 时显式传入纯值白名单，默认空；沿原 loadGeneration/reserveGeneration/mapGenerationImage 接线，不设置全局可变开关或另建装载目录。实际策略在第一次 LoadLibraryEx 前固定到同一 Generation，复用时必须匹配，不能升级历史 resident 或覆盖首次装载 owner。resident 声明即使命中白名单仍走原驻留/PIN 路径；显式声明未获批准在映射前拒绝。Qt UI 的入口与适配器始终 resident，拒绝显式卸载，未知后端不被推断为支持。

原发现 1.0 的 RESIDENT 含义保持不变；新增 1.1 的显式卸载声明协商，保留原结构布局与 1.0 常量，旧驻留路径继续请求 minor 0，显式路径请求 minor 1 并要求对应互斥声明。新 descriptor flag 使用独立位值 2，不能与 RESIDENT 位 1 同时设置；request flags 仍为零，runtime/business ABI 仍为 1.0。不把旧 RESIDENT 位改成提示，不以旧 DLL 拒绝新请求作为卸载授权。plugin.h、业务 ABI 与状态 IID 无需改变；module_query.h 的语义扩展须有新旧真实 DLL 和 C 布局测试。

公共 ModuleManifest、加载参数和诊断值变化时，M4-a 将 C++ SDK 升为 0.1.4，宿主与同工具链插件一起重建；M4-0 不改 SDK/ABI。白名单只批准经过审核的确定产物及部署依赖环境，摘要不是安全证明；首轮不允许未跟踪线程/返回路径、TLS 析构、静态注册和外部回调。host.complete 返回不能证明插件线程已退出，需要此能力的产物须另有 stop/join 实证，不引入签名或沙箱体系。

## 唯一终结路径和所有权

Generation 记首次装载 ownerThread。显式 tryUnload 同时核对 facade owner 和映像 owner；最后一个 ModuleLease 可在任意线程释放，但只记账，不调度或触发卸载。drain 保留原关闭准入语义，不隐式卸载或停止业务；tryUnload 不承担实例/会话清理，调用者沿原所有者先完成清理和工厂释放。

Current、Candidate、Retired 使用相同门禁：实际策略允许、描述协商成功、原映射引用仍有明确所有者、无装载/协商/创建 admissions、无 replacement/readers、无来源记录、ledger 全类别为空且无 pin、Native 会话关闭。Runtime/配置定义及状态契约捕获仍由原来源租约阻止卸载。不能以 Disposed、一次 processEvents 或一次账目快照代替原子准入关闭。

锁内只取得资格、封闭新准入并标记 Unloading，保持目录→代次→账目的既有顺序；所有同代装载/创建/卸载重入拒绝，其他代独立。退出全部内部锁后，才退役 Native 缓存和调用系统释放。HostRegistry.modules 的旧表须失效；原 createBound 等入口应先核对已关闭/终结状态再读 api，旧 facade 的查询和幂等 close 不得解引用已清表。函数表清除只在真实来源/引用归零后进行，不发明另一套引用计数。

只消费本方拥有的映射引用一次。系统调用失败时保留原引用、文件保护和独立卸载诊断；可证明原引用仍在的普通失败允许显式重试，业务准入不恢复。不确定清理、协议故障等原 pin 保持单向增加，不提供清零、移除 pin 或释放别人租约的接口。

系统释放成功后，使用拥有引用的 FROM_ADDRESS 探测、实际文件身份和映像范围检查确认结果。正常情况下探测无法再取得目标引用；若可靠确认原范围已不再属于目标映像，可以进入 Unloaded，image 为空、nativeMapped 为 false。若地址被另一映像复用，核对身份后只释放本方新取得的探测引用；不能调用旧入口。

若仍是同一产物，将新取得的探测引用交回 Generation，保守 Resident，并向同一 ledger 新增永久 unknownExternalMapping pin；取得拥有引用只恢复所有权，不解除未知性，不再允许再次减引用。不能循环 FreeLibrary 消费外部引用。

检查失败或无法证明目标离开时不能报告 Unloaded，保留可取得的受控映射引用与准确诊断。若原引用已消费、无法取得拥有的探测引用，且范围检查也失败或含糊，image 必须为空、nativeMapped 为 false，记录观测不确定并保留文件保护/永久 pin。借用句柄不是所有权，不能留下裸 HMODULE 冒充原引用或继续减引用。固定文件保护一直保持到身份核对完成；此不确定分支与前述能够证明目标离开的正常成功分支不同。

## 退役、历史与再次装载

只有确认目标映像离开后，才发布 Unloaded，role 转 Retired，并释放 image、backend、Native 缓存及 artifactFile；仅保留宿主值历史。旧 Current 不再参加当前选择，也不隐式提升任何存量 Candidate；旧 Candidate 不再可被复用。参与 current/matching/别名/复用及业务准入的扫描先跳过 Unloaded，禁止解引用空 artifactFile；snapshot 和按 ID 的纯诊断继续返回未淘汰历史。同产物重新装载取得新的 GenerationId，不能复活旧控制块、函数表或句柄。

64 限额继续约束持有映像/文件/失败/不确定资源的重代次，不把已卸载纯历史算作驻留泄漏。同一 catalog 容器最多保留 128 条无执行引用的 Unloaded 历史，超额只淘汰最旧 Unloaded；不复用 ID，不另建历史目录或预算注册表。外部已复制的纯值报告仍有效，已淘汰 ID 的查询明确返回不存在。

替换提交保持原 Core/Module/Driver 唯一发布点。退役代的卸载尝试发生在操作终结、外部旧 Runtime/定义/会话释放后；恢复仍可能需要旧代时禁止尝试。物理卸载失败不撤销已成功的业务提交，驻留原因由模块诊断单独呈现，不把它改写成 ReplacementReport 的业务失败。

## 证据与取舍

真实 DLL 同一进程内同时证明 stop/destroy/捕获及 deleter 完成、全部账目归零、动态 DLL_PROCESS_DETACH 恰一次、原范围不再属于该映像、文件可独占写入/改名/删除，以及重新装载重新 query/业务且 ID 更新。DLL 的动态 detach 只向宿主预先提供且存活期覆盖系统释放调用的固定原子槽写入；宿主在调用返回后读取，不在 DllMain 中回调宿主、分配或等待。lpvReserved 非空的进程退出 detach 不计成功证据。释放 NativeSource.session 等探针能力后才尝试卸载，绝不调用旧入口。[VirtualQuery](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualquery)。

增加外部 LoadLibrary 引用、地址观察不确定、系统释放失败、同代重入/独立代并发、跨线程最后释放、实际 Runtime/请求/队列捕获/服务/状态与清理失败的阻碍证据。失败注入限制在私有系统调用边界，保持真实映像和所有权；不使用无效 HMODULE 或 DllMain 返回值伪装卸载失败。

不采用仅在 drain 末尾释放、撤销 PIN、由插件自报授权、析构时自动卸载、扩大 64 限额、第二代次目录或自动在提交锁中卸载。现有默认驻留、Qt root/PreventUnloadHint、失败保留、唯一 ledger/来源/预约继续复用；应替换的是无条件策略选择、永久缓存/文件持有和以全历史计重资源预算的实现。
