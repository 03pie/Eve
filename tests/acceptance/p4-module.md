# P4 原生模块验收

本单随 P4 逐包补齐。M1/M2 装载与 ABI 协商层在当前支持矩阵已验收；不将这些检查计为完整 P4、生产业务桥或 DLL 卸载验收。最新执行结果见 [P4 开发记录](../../docs/p4-development.md)、[M2a](../../docs/p4-m2-development.md)及 [M2b](../../docs/p4-m2b-development.md)。

| 编号 | 场景 | 当前自动化依据 |
|---|---|---|
| M1-01 | 正常声明、全部必需字段、未知字段、类型、平台/加载/驻留策略及声明 ABI | p4.manifest_schema |
| M1-02 | 重复键、转义同名键、嵌套重复键、数字舍入、非法 UTF-8、尾随/多文档及深度 | p4.manifest_duplicates / schema |
| M1-03 | DLL 基名拒绝目录、设备名、盘符、流及非法后缀 | p4.manifest_library_names |
| M1-04 | 实际文件摘要、篡改、缺失、目录及不可复用的移动后来源 | p4.manifest_files |
| M1-05 | 根目录明确、目录边界及前缀相似兄弟目录拒绝 | p4.manifest_paths |
| M1-06 | Windows junction 指向根外拒绝，根别名/根内别名允许；关闭示例仍运行 | p4.manifest_junction |
| M1-07 | 精确输入/文件预算、跨块摘要、空文件及非法预算 | p4.manifest_limits |
| M1-08 | 十类资源各自登记、代次隔离、共享句柄、只读值快照 | p4.ledger_categories |
| M1-09 | 租约唯一所有权、移动赋值/自移动、幂等释放及账目句柄先退出 | p4.ledger_ownership |
| M1-10 | 引用排空后 pin 仍保留，原因去重，外部无法清除快照中的真实账目 | p4.ledger_pins |
| M1-11 | 非法类别/标签、记录/原因预算，容量释放后新记录不复用旧 id | p4.ledger_guards |
| M1-12 | 原生多线程持有/释放引用；排空门禁及全部 join 后静止 | p4.ledger_threads |
| M1-13 | 缺盘符、不完整网络根及驱动器相对声明拒绝；中文和扩展路径可用 | p4.manifest_root_guards |
| M1-14 | 声明/产物存在可写句柄时拒绝，关闭后恢复，普通只读共享允许 | p4.manifest_sharing |

M1 的文件可使用合成字节；摘要正确不代表有效 PE 或合规插件。原生装载前应重新核对产物，不能直接信任曾返回的快照。

| 编号 | M2a 场景 | 自动化依据 |
|---|---|---|
| M2a-01 | 独立 C11/C++17 描述 ABI 尺寸、偏移和对齐 | p4.query_layout |
| M2a-02 | 真实 C DLL、中文路径、描述通过但无业务激活 | p4.native_loaded |
| M2a-03 | 跨管理器复用、不同版本冲突，不重复 query | p4.native_duplicate / conflict |
| M2a-04 | 预检后改文件，重新校验拒绝且不创建代次 | p4.native_tampered |
| M2a-05 | 缺入口、错误版本/保留位/目录/指针长度、违规 C++ 异常 | p4.native_incompatible |
| M2a-06 | 非 PE、清除 DLL 标志的 EXE、其他机器架构 | p4.native_image |
| M2a-07 | drain/析构及外部 FreeLibrary 不撤销 PIN | p4.native_lifetime |
| M2a-08 | 驻留产物拒绝写入/删除，允许只读共享 | p4.native_sharing |
| M2a-09 | 非 owner 调用无文件/代次副作用 | p4.native_threads |
| M2a-10 | DLL 目录依赖成功，当前目录依赖不被搜索 | p4.native_dependency / search |
| M2a-11 | 最多 64 个驻留代次，超预算拒绝继续装载 | p4.native_budget |
| M2a-12 | 逻辑缓存命中不跳过其他代次的物理归属冲突 | p4.native_aliases |
| M2a-13 | 已由外部加载器映射的模块拒绝接管、不调用 query | p4.native_untracked |
| M2a-14 | 真实 query 阻塞期间保持 Validated 和 Call 账目，拒绝重复/排空，返回后才发布 Loaded | p4.native_publication |
| M2a-15 | 异常选项只处理完整 token，保留 /EHsc-；覆盖标准/自定义配置且不越过调用目录 | p4.exception_flags |

| 编号 | M2b 场景 | 自动化依据 |
|---|---|---|
| M2b-01 | 独立 C11/C++17 完整字段尺寸/对齐/偏移与固定线码 | p4.business_layout |
| M2b-02 | 基础/可选/必需能力与同 major 新 minor/尾部 | p4.business_negotiation |
| M2b-03 | 宿主版本、保留位、能力依赖及函数族不完整时不调用模块 | p4.business_hosts |
| M2b-04 | 插件表、身份、目录、未知状态和违规 query 异常拒绝 | p4.business_plugins |
| M2b-05 | 精确容量、短前缀、长容量尾部不被写入 | p4.business_capacity |
| M2b-06 | 纯 C 自有实例/复制配置，取消不提前完成、销毁门禁及跨线程复制完成 | p4.business_lifecycle |
| M2b-07 | 已固定/PIN 的真实 DLL 业务 query、echo 与正常清理 | p4.native_business |

M3–M4 待补联合验收：真实 HostTable/Kernel 桥中重复/晚到 completion 与返回竞态、请求取消后完成、回调捕获及析构的 ModuleLease、清理失败驻留、真实服务/effect 及同工具链 UI DLL、退出链和 A/S06/S07 映射。下方 M3a 已验证独立请求组件，M2b 测试宿主仅验证协议/复制值，都不替代联合场景。

| 编号 | M3a 请求组件场景 | 自动化依据 |
|---|---|---|
| M3a-01 | 同步/异步/早到完成与有界复制，结果只在入口退出后投递 | p4.request_sync / pending / early |
| M3a-02 | 一次仲裁、返回与工作线程完成、取消竞态 | p4.request_race / cancel |
| M3a-03 | 同步返回却完成、非法状态保持不静止；错误输入可诊断 | p4.request_protocol / input |
| M3a-04 | 请求及总缓冲预算，容量释放后不复用旧编号 | p4.request_quota |
| M3a-05 | close 仍可清理，seal 后不重开；实际完成前不得排空 | p4.request_closing / fence |
| M3a-06 | 拒收/外线程丢弃/post 抛错保留账目；生产者侧最后捕获退役交回 owner | p4.request_reject / discard / throw / retire |
| M3a-07 | 遗弃请求保守保留，错误线程/旧 context 无状态副作用 | p4.request_abandoned / threads |
| M3a-08 | JSON 标量/容器/数字原文、UTF-8/转义及深度边界 | p4.request_json_valid / json_invalid |

M3a 的组件证据不替代后续真实 DLL/HostTable/Kernel 桥；该联合验收及 M4 仍待完成，见 [M3 记录](../../docs/p4-m3-development.md)。


| 编号 | M3b 真实 DLL/Host 场景 | 自动化依据 |
|---|---|---|
| M3b-01 | 同步/异步/早到启动、取消后完成，多实例/epoch 隔离 | p4.native_host_sync / async / early / multi |
| M3b-02 | 正常启动业务失败可清理，复制异步错误在销毁后保留 | p4.native_host_start_failure |
| M3b-03 | 停止/销毁失败保留实例、模块和资源引用 | p4.native_host_stop_failure / destroy_failure |
| M3b-04 | create 空句柄/PENDING/失败非空句柄协议拒绝 | p4.native_host_create_null / create_pending / create_live_error |
| M3b-05 | 重复/同步混合完成及真实 C++ 违规异常不假静止 | p4.native_host_duplicate / sync_protocol / exception |
| M3b-06 | 缺业务 query、无效输入、非 owner、调度关闭与已接受表复用 | p4.native_host_missing / guards / closed_scheduler / reuse |
| M3b-07 | 结果已发布仍等待宿主 Call；复制投递捕获亦须退役 | p4.native_host_callback / callback_copy |
| M3b-08 | 关闭门禁后迟到日志必须隔离，完成后观察器排队时关闭不能误取消 | p4.native_host_late_log / close_queued |

M3b 只协商基础能力，真实服务/effect、Kernel/Session 和 UI DLL 仍按 M3c/M3d/M4 验收，不将上表当作整个 P4 完成。审查红绿证据及最终矩阵见 [M3b 记录](../../docs/p4-m3b-development.md)。


| 编号 | M3c1 dispatch/effect 场景 | 自动化依据 |
|---|---|---|
| M3c1-01 | 排队、复制整数原文、消费方和目标独立请求、同步/异步/早到回调、最终 ACK | p4.native_resource_dispatch / sync / early |
| M3c1-02 | 排队和在途取消，关闭不假完成、ACK 前不销毁 | p4.native_resource_cancel_queued / cancel_pending / close_queued / close_pending |
| M3c1-03 | 真实 Core effect、逆序关闭、异步清理和 setup 取消后报告 | p4.native_resource_effects / cleanup_pending / setup_pending |
| M3c1-04 | 主动清理、清理失败驻留，正在主动清理时实例关闭先等待 | p4.native_resource_explicit_cleanup / explicit_failure / cleanup_failure / close_during_cleanup |
| M3c1-05 | owner/context/effect 归属、无效参数、操作预算、释放后再创建且不复用编号 | p4.native_resource_guards / effect_reuse |
| M3c1-06 | 重复 ready、违规 ACK、Core effect 投递丢弃保守保留 | p4.native_resource_duplicate_ready / ack_protocol / discard |
| M3c1-07 | ACK 后仍有合法投递副本时不得静止，最后捕获跨线程释放交回 owner | p4.native_resource_retire |
| M3c1-08 | scheduler 对象释放后仍可读取诊断，关闭保守报告不静止 | p4.native_resource_expired_scheduler |

M3c1 的资源桥验证不替代 M3c2 服务或 Kernel/Session 联合验收；红绿复现和最终矩阵见 [M3c 记录](../../docs/p4-m3c-development.md)。

| 编号 | M3c2 真实服务桥场景 | 自动化依据 |
|---|---|---|
| M3c2-01 | 同步/异步/早到服务结果，复制整数原文及两端独立请求 | p4.native_service_sync / async / early |
| M3c2-02 | start 与真实 Core effect ready 双屏障；失败不发布 | p4.native_service_publication / start_failure / effect_barrier |
| M3c2-03 | minor/能力、转义重复、非法参数、线程与归属检查 | p4.native_service_guards |
| M3c2-04 | 精确 realm、同 scheduler 共享索引、活动索引不可切换 | p4.native_service_realms |
| M3c2-05 | 撤回立即拒新调用、通知消费者退出、排空后确认 | p4.native_service_withdraw |
| M3c2-06 | 旧绑定异步清理、提供者等待消费者；清理失败保留 | p4.native_service_cleanup / cleanup_failure |
| M3c2-07 | 排队/在途真实取消，非法取消协议隔离两端 | p4.native_service_cancel_queued / cancel_pending / cancel_protocol |
| M3c2-08 | 提供者与宿主操作预算；拒绝受理后不遗留 committed 租约 | p4.native_service_limits / budget |
| M3c2-09 | 两个真实 C DLL 共享 Core 注册表互调并级联退出 | p4.native_service_shared |
| M3c2-10 | 真实 Core 依赖环；清理期间拒绝新绑定 | p4.native_service_cycle |
| M3c2-11 | ACK 后仍有投递副本时不静止，最后捕获跨线程释放 | p4.native_service_retire |

M3c2 记录见 [服务桥验证](../../docs/p4-m3c2-development.md)。这些 DLL 为回归夹具，M3d 的业务服务 DLL、Kernel/Session 配置激活和完整退出链，以及 M4 UI DLL 仍须另验；不声明 P4 整体完成或物理卸载安全。


## M3d1 Core 托管与冻结配置目录

2026-10-29：在远端优先合并基线 d6d003f 上扩展既有 NativeServices。以下为真实 C DLL 夹具、FakeScheduler 和受控内存 writer 的自动化证据；Session 保存不宣称真实磁盘 I/O，业务服务 DLL 与原生 Qt 事件循环/S06/S07 完整退出由 M3d2 另验。

| 编号 | 场景 | 自动化依据 |
|---|---|---|
| M3d1-01 | Runtime 身份登记、重复/已移动定义、owner/关闭门禁 | p4.catalog_registration |
| M3d1-02 | 冻结目录、未用模块声明、重指向、schema 前拒绝与 YAML 位置 | p4.catalog_catalog |
| M3d1-03 | 启用门禁、抛错、失效模块停用、依赖限额 | p4.catalog_availability / budget |
| M3d1-04 | Core 激活、借用输入不重复计数、异步调用及完整清理 | p4.native_managed_flow |
| M3d1-05 | start/setup 发布屏障、取消及失败回滚 | p4.native_managed_publication / setup_cancel / setup_failure |
| M3d1-06 | Retiring 服务键保留、旧绑定首次清理调用、epoch 取消 ACK | p4.native_managed_key / cleanup / stale_dispatch |
| M3d1-07 | 声明/元数据验证、晚登记、realm 与 scheduler/索引归属 | p4.native_managed_guards / undeclared / metadata / realms / scheduler / index |
| M3d1-08 | 提供者代次隔离、普通业务超限与恢复、满额同步 ACK | p4.native_managed_generation / incoming_budget / ack_capacity |
| M3d1-09 | 真实 NativeModule→Kernel→Session、额外 inject、预览/CAS/保存/移除/退出 | p4.native_managed_session |
| M3d1-10 | 托管 create 满额正常拒绝，借用依赖不被误判为遗留；释放容量后恢复与退出 | p4.native_managed_create_failure |

最终 Release 全量 606/606、GCC 245/245、Debug/ASan Core/P4 各 260/260；关闭示例 P4 138/138 后恢复 ON。六项关键托管场景在 Debug/ASan 各连续 20 次通过，29 项源码/构建快照、15 个 ASan 编译命令、8 个独立 SDK 头和依赖边界核对通过。日志索引和红绿复现见 [开发记录](../../docs/p4-m3d1-development.md)。M3d1 完成不代表 M3d/M4、整个 P4 或物理卸载完成。

M3d1 后续复审新增 M3d1-10：修复前真实失败 0/1，修复后 1/1；最终 Release 全量 607/607、Debug/ASan P4 各 139/139，新场景各连续 20 次通过。见 [独立复审](../../docs/code-review.md#p4-m3d1-独立复审创建拒绝与借用输入)；前段 606/260 等仍为 ed2471d 历史证据。


## M3d2a 业务 DLL 与真实 Qt 配置链路

使用 examples/native_services 的两个业务 DLL，未借用夹具测试导出；真实 QCoreApplication/QtScheduler、Kernel/Session 与 QtConfigurationWriter 写盘并核对 persisted 文本。通过不表示物理卸载，成功关闭后仍为 Resident/PIN。

| 编号 | 场景 | 自动化依据 |
|---|---|---|
| M3d2a-01 | 预览不执行、目录固定、跨 DLL 业务、真实保存及完整正常退出 | p4.native_qt_normal |
| M3d2a-02 | 两个报告器共享计数，结果 1/2，各保留一份输入 | p4.native_qt_multi |
| M3d2a-03 | 两组 realm 独立计数，均从 1 开始 | p4.native_qt_realms |
| M3d2a-04 | start 失败形成 Failed，仍保存目标且正常排空，无模块隔离 | p4.native_qt_start_failure |
| M3d2a-05 | cleanup 失败不假静止，drain 拒绝、PIN 与执行引用保留 | p4.native_qt_cleanup_failure |

最终 Release 全量 612/612、Debug/ASan P4 各 144/144、GCC 全量 245/245、原 Release 关闭示例 P4 144/144 并恢复 ON；五项在 Debug/ASan 各连续 20 次通过。日志、8 项源码快照、3 个 ASan 编译命令及 6 份声明/产物摘要映射见 [阶段记录](../../docs/p4-m3d2a-development.md)。本轮观察启动业务日志，未单独请求报告器 read 方法；取消后晚到的 stop completion（S06）和 DLL 内失活捕获析构（S07）仍属 M3d2b，M4 UI DLL 待实施。


## M3d2b 分层 S06/S07 收口

取消在生产 RequestChannel 请求边界配合真实 DLL 与 Qt 验证，NativeModule 当前无公开 stop-cancel 操作；完整生产 HostTable 的 PENDING stop 等待单独检查。S07 使用生产 Core/Kernel 托管、真实 QtScheduler 和 DLL 内 lambda/Capture；专用控制导出只存在于测试夹具，不加入业务 DLL/SDK。

| 编号 | 场景 | 自动化依据 |
|---|---|---|
| M3d2b-01 / S06 | Stop PENDING→一次取消→另一线程在 DLL 内晚到完成；取消/seal 不假完成，复制后改写缓冲，退役后才 destroy | p4.native_lifecycle_cancel_stop |
| M3d2b-02 | 生产 HostTable 的 PENDING stop 等真实完成，回执不内联 destroy | p4.native_lifecycle_pending_stop |
| M3d2b-03 / S07 | Core 失活抑制 DLL 主体、ACK CANCELLED、DLL 内捕获析构，保留任务副本阻止假静止，最后释放跨线程回到 owner | p4.native_lifecycle_stale_capture |
| M3d2b-04 | 活动对照：主体/ACK/捕获析构各一次，完整排空 | p4.native_lifecycle_live_capture |
| M3d2b-05 | 失败对照：不析构/不 destroy，模块与执行引用保留 | p4.native_lifecycle_failed_capture |

临时去掉执行前门禁的负向控制 0/1，恢复后全部通过；生产源码逐字节恢复，未增加公共取消接口。最终 Release 617/617、Debug/ASan P4 各 149/149、GCC 245/245、原 Release 关闭示例 P4 149/149 并恢复 ON，五项各连续 20 次通过。证据及 4 项源码/3 个 ASan 编译命令见 [本段记录](../../docs/p4-m3d2b-development.md)。M3d1/M3d2a/M3d2b 共同完成 M3 业务/资源范围，M4 UI DLL 和整个 P4 收口仍待完成，物理卸载不在当前证明范围。


## M4a 结构提取与 Qt 只读预检

共同 VerifiedSource 同时被既有 C ABI 装载与新 Qt 预检消费，原生完整回归保护来源/共享/PE/代次行为。Qt 用独立测试入口和真实元数据 DLL，复用产物安装设施；所有 Qt 检查确认无映射、无目录新增。这里未实例化 IQtPluginV1 或 QWidget。

| 编号 | 场景 | 自动化依据 |
|---|---|---|
| M4a-01 | 合法工具链/元数据、快照之后文件变更必须重新预检 | p4.qt_preflight_good |
| M4a-02 | 编译器、Qt patch、CRT、迭代器调试、指针宽度、SDK 及缺字段拒绝 | p4.qt_preflight_compiler / qt / runtime / iterator / pointer / sdk / missing |
| M4a-03 | 目录不匹配拒绝，合法目录顺序不影响集合语义 | p4.qt_preflight_catalog / order |
| M4a-04 | C ABI/Qt 后端双向拒绝、非 owner、真实非 Qt DLL | p4.qt_preflight_backend / thread / not_qt |
| M4a-05 | 来源验证提取不改变路径、摘要、共享、PE 和原生目录/生命周期 | 既有 P4 native/manifest 场景 |

元数据增量依赖修复前 JSON 改动未进入 DLL、错误通过；明确 JSON→moc→DLL 后，修改构建元数据真实触发 0/1 拒绝，恢复并重建后 2/2 通过。最终 Release 630/630、Debug/ASan P4 各 162/162、GCC 245/245、关闭示例 P4 162/162 并恢复 ON，四项新检查各连续 20 次通过。16 项源码/构建、17 个 ASan 编译行及独立公开头证据见 [M4a 记录](../../docs/p4-m4a-development.md)。M4b/c 的 Qt 后端/root/工厂、页面析构屏障和原生 UI 仍待验证；不标记整个 P4 完成。
