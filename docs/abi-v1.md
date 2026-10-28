# C 业务 ABI V1

日期：2026-10-28。对应 [独立 C11 头](../sdk/include/cordis_abi/plugin.h)、[ADR-0031](adr/0031-business-abi.md) 和 [M2b 开发记录](p4-m2b-development.md)。这是 Windows x64 业务表的签名及布局基线；当前完成协商层，实际 Kernel/请求/服务/effect 桥在 M3 实施。

## 平台与版本

函数、函数指针和入口均为 C linkage / 显式 CORDIS_CALL，Windows 使用 __cdecl，入口由 CORDIS_ABI_EXPORTS 显式导出。使用平台自然对齐，不使用 pack；Windows x64 的实际调用及对齐遵循 [Microsoft x64 ABI](https://learn.microsoft.com/en-us/cpp/build/x64-software-conventions)。不跨边界传递 STL、QString、bool、异常、RTTI 或需要另一侧 delete 的对象。GCC 的无 Qt 源码/布局检查不代表 GCC DLL 与 MSVC 宿主已兼容。

业务入口 cordis_query_v1 与 M2a 的 cordis_module_query_v1 相互独立。前者协商完整业务表，后者只描述模块；默认 ModuleManager.load 仍只做 M2a，不自动创建业务实例。

host_abi 是 major，minor、结构长度和能力位在表中。宿主将输出 struct_size 设为可写容量；生产者最多写 min(容量, 自身长度)，返回自身长度。major 必须相同；同 major 的新 minor/尾部扩展，在已知前缀完整且能力均可识别时允许。所有 reserved 为零；不兼容变更升级 major，不在已有字段中改含义。

| 类型 | x64 大小/对齐 | 关键偏移 |
|---|---|---|
| CordisStatus | 4 字节 | int32，不使用 Core ErrorCode 的序号 |
| CordisHandle / CordisRequestId | 8 字节 | uint64；0 无效 |
| CordisTableHeaderV1 | 16 / 4 | size=0，major=4，minor=8，reserved=12 |
| CordisBytesV1 | 16 / 8 | data=0，size=8 |
| CordisServiceKeyV1 | 32 / 8 | name=0，major=16，reserved=20，realm=24 |
| CordisServiceMetadataV1 | 24 / 8 | size=0，minor=4，capabilities_json=8 |
| CordisHostV1 | 112 / 8 | capabilities=16，complete=32，末项=104 |
| CordisPluginApiV1 | 152 / 8 | capabilities=16，module_id=32，plugins=72，create=80，末项=144 |

C11 与 C++17 共享同一份完整字段偏移断言，见 tests/abi/business_layout_asserts.h。HostTable 的三个基础槽为 complete/log/request_cancel；插件基础槽为 create/start/request_stop/destroy/cancel_request/request_complete，均不可空。

## 能力协商

| 位 | 值 | HostTable 槽 | PluginApi 槽 |
|---|---|---|---|
| DISPATCH | 1 | dispatch | dispatch |
| SERVICES | 2 | service_register / service_withdraw / service_invoke | invoke_service |
| EFFECTS | 4 | effect_create / effect_ready / effect_request_cleanup | request_cleanup |

SERVICES 依赖 EFFECTS，因为每个提供者绑定必须归属 effect。capabilities 表示实际协商的功能集合，须是宿主集合的子集；required_host_capabilities 须全部包含在协商集合中。功能开启时对应全部槽非空，关闭时对应槽全部为空，未知位拒绝。功能可用不等于任意服务名、callback id 或 effect 归属均有效。

首版没有共享 buffer/allocator 能力。JSON 和文本通过借用 span 同步复制；不得跨 CRT free，不能以空函数伪装已支持的 buffer 扩展。未来 buffer 必须增加明确能力、长度和 release 所有权协议。

## 值与句柄所有权

| 值 | 所有权与期限 | 限额/含义 |
|---|---|---|
| 输入 span / key / metadata | 发送方持有到调用返回；接收方若要保存，返回前自行复制 | size>0 则 data 非空，不依赖终止零 |
| query 模块 ID/版本/插件数组 | 模块只读内存，映射期间有效；协商器另存宿主字符串副本 | ID 128 字节，版本 64 字节，插件 1–64 项且唯一，必须与 manifest 精确匹配 |
| config/result/payload/error JSON | UTF-8 单个 JSON 值；按长度复制，不传内部指针 | 最多 1 MiB；零长度仅表示没有结果，不等于 JSON null |
| log 文本 / effect label | UTF-8 按长度复制 | 分别最多 64 KiB / 256 字节 |
| service key | 名称、接口 major、精确 realm 构成键，不跨域回退 | 名称/方法最多 128 字节；major/realm 非零 |
| service metadata | minor 及 capabilities_json | capabilities_json 是唯一能力名组成的 JSON 数组，调用期间借用 |
| host_context | 宿主所属实例/epoch/模块代次的不透明能力 | 不得伪造、跨代复用或拿来当插件实例 |
| instance | 模块所有，由 create 生成、destroy 回收 | 同一实例只属于一次激活上下文；失败 destroy 不回收 |
| effect / binding | 宿主所有，归属创建上下文 | 不可转移至其他实例或 epoch |
| callback / service_token | 模块私有非零整数标记 | 宿主只回传给对应模块函数，不作为地址解引用 |
| request | 宿主发放，按上下文/代次验证，生命周期内不重用 | 取消后仍保留直到真实终结 |

HostTable 必须不可变，并覆盖全部实例、请求、回调和可能使用它的模块代码的寿命；不能以 query 返回或管理器析构当作终结。内部 BusinessApi 含 DLL 函数/元数据借用指针；调用方仍须保持映射、HostTable 和 ModuleLease/CallLease，有宿主副本不代表可卸载。

## 每个函数的约定

除 complete/log 可在任意线程调用外，所有函数在宿主 owner 线程执行。complete/log 仅复制值和投递，不直接修改 Kernel registry 或内联执行回调。query 不调用宿主函数、不注册资源、不执行生命周期；成功后才可保留 HostTable。任何边界均不得传播异常。

| 方向/函数 | 输入归属与行为 | 返回/终结 |
|---|---|---|
| 插件 create | 宿主 context、plugin_id、借用 config；同步轻量、不等事件循环 | 仅 OK 产生非零 instance，失败输出 0；不允许 PENDING |
| 插件 start | 模块实例、已登记宿主 request | OK/错误同步终结，或 PENDING 后 complete |
| 插件 request_stop | 模块实例、独立停止 request；先关新业务，再清资源 | 同上；停止请求本身不是取消所有旧请求的完成证明 |
| 插件 destroy | 停止且所有请求/资源/回调屏障后调用 | 仅同步 OK/错误；失败保留实例及模块 |
| 插件 cancel_request | 宿主发起的插件请求 | 仅同步取消应答，不替代原请求的完成 |
| 插件 request_complete | 插件发起的宿主操作最终结果，借用 JSON | 同步 OK/错误应答，不允许 PENDING，不递归 complete |
| 插件 dispatch | 模块私有 callback id、借用 payload、宿主 request | 同 start 的异步规则 |
| 插件 invoke_service | 模块私有 service_token、方法、借用 payload、宿主 request | 同 start 的异步规则 |
| 插件 request_cleanup | 所属 effect、私有 cleanup_callback、宿主清理 request | 所有资源及其回调捕获真正退出后终结；失败保守驻留 |
| 宿主 complete | context、已登记 request、最终 status、借用 result_json | 返回前复制；只接受一次；OK 仅表示已接收，不代表 Kernel 已推进 |
| 宿主 log | context、级别、借用文本 | 同步复制；无业务回调 |
| 宿主 request_cancel | 插件发起的宿主 request | 同步取消应答，结果仍通过 request_complete 到达 |
| 宿主 dispatch | context、私有 callback、借用 payload | 接受时 PENDING + 非零 request；返回前复制，稍后分派 |
| 宿主 service_register | context、所属 effect、借用 key/metadata、私有 token | 同步 OK + binding；暂存/发布仍服从激活屏障 |
| 宿主 service_withdraw | context、所属 binding | 接受时 PENDING + request；拒新调用，等待旧调用与持有者排空 |
| 宿主 service_invoke | context、key、minimum_minor、所需能力数组、方法和 payload | 接受时 PENDING + request；冻结目标与数据，最终回 request_complete |
| 宿主 effect_create | context、借用 label、私有 cleanup_callback；先登记再创建资源 | 同步 OK + effect；归属本次激活 |
| 宿主 effect_ready | context、effect、最终 setup status、借用 error_json | setup 恰好报告一次，不作为清理完成 |
| 宿主 effect_request_cleanup | context、所属 effect | 接受时 PENDING + request；最终回 request_complete |

所有同步输出 handle/request 参数在失败时为 0；宿主异步操作成功统一返回 PENDING，不以 OK + 空 ID 表示“以后也许完成”。关闭期间拒绝新业务，但已登记请求的 complete、日志及受限清理仍必须可达，不能因关闭抹掉旧代账目。

## 请求状态与取消

宿主在调用插件入口前保留请求、实例/epoch 和调用租约。complete 可能在插件返回 PENDING 之前从工作线程到达：先有界复制并仲裁一次，入口返回后再与返回值核对，不能提前推进状态机。返回 OK/错误却发送 complete 属于 ABI 协议错误；重复/未知请求也返回 E_ABI_PROTOCOL，不重复推进。

插件调用宿主异步操作时，宿主先返回非零 request，稍后才分派或调用 request_complete。若还要进入另一个模块，内部目标调用使用独立请求，不把消费者请求冒充提供者请求。即使取消或 epoch 过期，也要释放复制的数据、捕获和模块引用，并保留完成义务直到真实终结。

PENDING=1，OK=0；错误固定为 INVALID_ARGUMENT=-1、UNSUPPORTED=-2、NOT_FOUND=-3、CLOSED=-4、WRONG_THREAD=-5、CANCELLED=-6、ABI_PROTOCOL=-7、LIMIT=-8、FAILED=-9、BUSY=-10。complete/effect_ready/request_complete 接受的 status 只能为 OK 或上述负值。未知线码、query/destroy/create 的 PENDING 均为协议错误；Core 错误与线码的运行桥映射留给 M3。

这些是 M3 的实现约束。M3a 已实现独立请求仲裁/复制与退役组件，见 [请求协议决策](adr/0032-native-request-arbitration.md)；宿主 JSON 扫描限制容器深度 64、Unicode 标量编码/转义，保留数字及重复成员原文，业务 schema 另验。M2b 协商器及测试宿主没有升级为完整生产桥；真实 DLL 生命周期、资源排空与 Kernel 接入仍须在 M3b–M3d 完成。
