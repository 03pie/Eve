# P4 原生模块验收

本单随 P4 逐包补齐。M1/M2a 在当前支持矩阵已验收；不将这些检查计为完整 P4、业务 ABI 或 DLL 卸载验收。最新执行结果见 [P4 开发记录](../../docs/p4-development.md)及 [M2 记录](../../docs/p4-m2-development.md)。

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

M2b–M4 待补：业务 HostTable、重复/晚到 completion、请求取消后完成、回调捕获及析构的 ModuleLease、清理失败驻留、服务及同工具链 UI DLL、退出链和 A/S06/S07 映射。上述项目尚未验收，不计入 M1/M2a 测试通过数量。
