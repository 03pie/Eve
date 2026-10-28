# P4 原生模块验收

本单随 P4 逐包补齐。当前只验证 M1 预检/账目，不将这些检查计为完整 P4 或 DLL 卸载验收。最新执行结果见 [P4 开发记录](../../docs/p4-development.md)。

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

M2–M4 待补：真实 DLL、C/C++ ABI 布局、query/descriptor 校验、重复/晚到 completion、请求取消后完成、回调捕获及析构的 ModuleLease、清理失败驻留、服务及同工具链 UI DLL、退出链和 A/S06/S07 映射。上述项目尚未验收，不计入 M1 测试通过数量。
