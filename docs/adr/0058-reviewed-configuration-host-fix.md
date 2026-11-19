# ADR-0058：Unicode 宿主修复与独立交付证据

日期：2026-11-19。状态：P6-2 实施前取舍。P6-1 基线 9af5d4d，原包和 P5 原件保持历史事实。

## 实际问题

P6-2 使用原配置 exe，系统 ACP936，以 Windows Unicode CreateProcess 参数传入真实存在的 `中文 空格 扩展𠮷/config.yaml`。PID18788 在显示窗口前退出1，stderr 为 `configuration read failed`，原配置字节不变；原件见[probe](../../build/p6-2-unicode-probe/report.json)和[stderr](../../build/p6-2-unicode-probe/stderr.txt)。不是仅靠静态猜测，也不以普通中文/ASCII路径通过回避。

配置入口在 QApplication 初始化后仍遍历 narrow `argv`，用 fromLocal8Bit 取 --file；Windows CRT 代码页不能无损表示所有有效路径。业务入口已使用 `app.arguments()`。改为同一 Qt Unicode 数据来源，保持现有选项、错误、默认路径、smoke 与资源拥有链；删除被替代的窄参数路径，不新增 CLI 框架、全局代码页修改、注入器或替代文件位置。

## 为什么需要新的证据

P6-1 正确地冻结 P5 完整来源和两宿主。修复会改变配置入口源码/对象/exe，旧配置 exe 摘要不能继续作为新宿主证明。也不能因原打包脚本严格而不修真实问题，或把新摘要写进旧 P5 原件。

采用明确两部分交付输入：P5 原生模块/Qt模块/七运行库及业务产品仍逐原证据核对；P6 修复的唯一配置宿主另有受版本控制的证据冻结。该冻结仅允许 `examples/qt/config_workbench.cpp` 一项源变更及对应 `cordis_config_workbench.exe`，包含旧/新 source SHA、Release exe SHA、相同工具链/依赖、实际构建/回归/Unicode/正常退出及独立审查原件摘要。它不参与 ModuleManager、NativeProductCatalog 或插件批准，不提供任意源/DLL例外。

原四份 P5 索引、939 来源清单及6c源不改。打包时检查原939键，唯一配置入口按新受审记录核，其余938保持原值；新 Config exe必须匹配新记录，业务产品/六A-B/七依赖仍必须匹配原值。删除“配置exe还应等旧 P5 exe”的已过时校验，保留旧exe原件/历史包用于溯源。清单分别标出原 P5 部分和 P6 宿主修复，不把整个包说成原 P5 字节全部不变。

打包工具消费该新冻结，不从当前exe、运行结果或候选自动刷新它。新的原生模块或核心/Qt/UI/Ela 字节变化仍走原资格机制，不受此配置宿主记录授权。未来其他源变化需要新的具体设计/审查，不能扩成通用 source override。

## 实施与验证

1. 原 P6-1 ZIP已保留原 Config 字节；修复前另记录当前三共享目录的关键产物及源码 SHA。
2. 只重建原目录的配置 exe 目标，沿匹配 Release/Debug/ASan 参数，不重链业务模块或产品。核七运行库、六模块及业务exe前后严格不变。
3. 跑对应现成 smoke 两例与实际 Release Unicode/空格/任意 cwd 读写、同文件新 PID 重开/正常退出；保留原失败，不据后续通过改写。
4. 独立审代码、编译来源、退出/保存物证、未变依赖及定向回归后采用精确宿主记录；对错源、旧exe、篡改记录和尝试放行 DLL 的负例拒绝。
5. P6-2 记录器/包级测试继续使用新包，完成实际部署与独立机器验证。新宿主记录不使 D06–D16 自动通过。

依据：[Microsoft main/wmain 参数说明](https://learn.microsoft.com/en-us/cpp/cpp/main-function-command-line-args?view=msvc-170)；Qt Unicode arguments 复用项目已有业务入口，而非增加平台专用参数分支。
