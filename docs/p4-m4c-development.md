# P4-M4c 页面 DLL 与最终验收

日期：2026-10-30。基线 4866657。P4 仍进行中；按小阶段实施、审查、验证、提交并同步远端。

| 子段 | 范围与完成条件 | 状态 |
|---|---|---|
| M4c1 | 收敛 Qt/UI 共享运行库与 SDK，真实 DLL 验证调度、页面完整析构及兼容回归 | 已完成审查与最终验证，随本提交归档 |
| M4c2 | 真实业务 UI DLL 示例、Kernel/Session 配置、多实例/换代/失败退出与模块诊断 | 待实施 |
| M4c3 | P4 需求逐项映射、联合回归及真实页面的原生桌面验收，确认后收口 | 待实施 |

## M4c1 结构与审查

先检查跨 DLL 的实现归属；发现共享 Core 之外仍存在静态 Qt/UI 实现复制。真实页面夹具复现 DLL 通过 QtScheduler 具体类型投递时的私有唤醒事件不匹配：原静态实现的 scheduler 场景 0/1，明确报出 DLL posted job did not run，页面 lifetime/multi/update 对照 3/3。改为共享 Qt/UI 后初轮四项 4/4，通过 SDK 聚合现有页面服务，不新增生命周期或页面系统。见 [ADR-0039](adr/0039-shared-qt-ui-runtime.md)。

夹具通过实际 IQtPluginV1/IQtInstanceV1 和既有 Qt 装载路径创建业务对象，从宿主注册表消费 ui.pages，构造 ElaText 页面。子对象与工厂捕获在 DLL 中析构；检查页面完整销毁后才释放工厂捕获和业务对象。正常停止后执行引用释放，root 的 QObject 租约仍保留，drain 继续拒绝，不宣称可卸载。

由当前代理审核，未使用子代理。首轮测试误用 InstanceSnapshot/OperationHandle 字段导致编译失败，按现有 revision/target API 修正，没有修改生产接口或放宽警告。源码/接口、链接边界、关闭顺序及新测试已复查；未发现阻止本小阶段提交的剩余问题。

| 最终检查 | 结果 | 日志（build/p4-m4c1- 前缀） |
|---|---|---|
| 原目录 MSVC Release/Debug/ASan 全量 | 各 693/693 | {release,debug,asan}-{build,tests}.log |
| 无 Qt GCC 全量 | 257/257 | gcc-{build,tests}.log |
| 原 Release 关闭示例 P4 | 225/225 | no-examples-{configure,build,tests}.log |
| 继续关闭共享 Core，静态兼容 P4 | 177/177 | static-{configure,build,tests}.log |
| 恢复示例与共享运行库 ON，重建并检查共享 Core/页面 | 5/5 | restore-{configure,build,tests}.log |
| 实际宿主/插件 DLL 导入 | Release 三份运行库一致；Debug 均为 d 后缀，Qt 不依赖 UI/Module | {release,debug}-imports.log、fixture-imports.log |
| 源码/构建快照和 ASan | 7 项源码/构建 hash；16 个源文件与 1 个生成资源编译单元均带 ASan | source-sha256.json、asan-commands.json |

实际 DLL 的 QtScheduler::post 和 UiPagesService::add 都由共享运行库导入；QtUiPluginSdk 不链接模块宿主/目录。首次编译参数审计将生成 qrc 也纳入筛选而触发数量检查，已区分 16 个源文件与 1 个生成单元；没有减少覆盖范围。编码、格式、文档引用、只读参考边界及 git diff --check 通过。最终审计见 audit.log。

日志前缀 build/p4-m4c1-；原静态复现为 red-{build,tests}.log，首轮共享修复为 green-{build,tests}.log，初轮三个页面对照与调度失败保留在 static-baseline-tests.log。最终关闭共享 Core 的兼容回归使用 static-{configure,build,tests}.log。原 Release 路径不变。
