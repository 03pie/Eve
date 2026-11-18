# P5-M5-e 最终资格与交付

日期：2026-11-19。基线d47210a6148edfe6733c33a0136232b670174b57，已审计提交同步、15提交blob匹配且工作区干净。e0实施前设计见[ADR-0056](adr/0056-qualified-native-product-catalog.md)，当前尚未实现产品名单，M5/P5未完成。

## 分段与提交屏障

| 段 | 出口 | 状态 |
|---|---|---|
| e0 | 核名单来源/产品入口/拥有者、资格更新及最终矩阵/人工出口；审查设计 | 独立设计审查通过，待精确提交同步 |
| e1 | 受审精确档案、显式候选导出、私有产品名单与原Workspace接线、正确桌面启动项；相关验证/审计 | 待e0提交同步且clean |
| e2 | 最终五配置完整构建/全量清单与实际执行、兼容/产物/ASan审计、FR-10/H01/H02/S09等完整映射 | 待e1提交同步且clean |
| e3 | 新Release产品桌面升级/保存/退出人工回执、整P5代码与架构审计、正式发布档案及状态收口 | 待e2提交同步且clean |

每段先设计再实现，完成审查修正和必要验证后精确提交同步，clean才进下一段。发现实质缺口先修正承担该职责的实现，不堆叠旁路；不以新增代码量或测试数代表完成。

## e0 只读核对

ModuleManager默认空NativeUnloadWhitelist、首次策略、来源/ledger/预约、不可清PIN与Qt resident保持。现产品名cordis_platform_workbench_demo仍由CORDIS_PLATFORM_QUALIFICATION宏进入openQualification，bootstrap按本次固定六种产物临时批准；必须与真实产品消费受审名单分开。普通Native Pipeline仍resident 1.0，无需改造成第二升级宿主。现desktop.ps1 run启动配置工作台，也不是本次三模块升级人工入口。

名单源只能包含三个共享资格profile各Counter/Reporter A/B四份。当前十二DLL及十二manifest实际摘要仍等于d2b归档。原normal_artifacts保持报告共36条DLL/manifest配对记录（72文件），含其他用途/配置及旧故障Notebook，不可整体晋升。D1终报不包含全部工具链/产物字段，需关联原JSONL的真实来源、实际编译命令/产物和归档证据链；d2b whole_platform=false、passed单值不构成正式批准。

实际当前CTest注册清单（仅登记，不是新执行）：

| 目录 | 全部注册 | 其中P5 | 配置边界 |
|---|---:|---:|---|
| build/ela-msvc-release | 1475 | 759 | 共享Core/Qt/Ela，Release |
| build/ela-msvc-debug | 1475 | 759 | 同范围Debug |
| build/p3-q3-asan | 1475 | 759 | 同范围RelWithDebInfo、O1与实际ASan |
| build/qt-msvc-release | 1002 | 491 | 静态Core，Ela/示例关闭 |
| build/core-gcc-debug | 459 | 198 | 无Qt，静态Core |

五清单命令文件均存在，DISABLED=0；只有三共享各有d1/d2六项原生工作台入口。最终e1改变后必须重新取清单，不套用上述数；静态/无Qt不声称运行Windows工作台。e2全量含d1/两个seed/三个proof，不将d2b选择5项当全量。

## 历史与最终证据

M1–M4、M5-a/c的架构/状态/故障与d1/d2实际字节资格保留原范围。d2b沿用未变D1的理由已记录，不是M5-d缺口；最终全量仍新执行所有适用项。e2运行前归档d1/d2a固定输出，d2b每次run目录独立保留；输入、清单、实际执行名/返回码、最终exe/模块/名单SHA、编译对象/依赖与失败历史分别记录，不拼重跑计数。

SDK0.1.5八件旧实物/provenance保持；最终用保存的Release旧宿主/插件两个normal进程见证当前Core/Ui实际加载与摘要，范围仅该同工具链实物，不泛化ABI。ASan记录最终实际对象、真实依赖和插桩/mtime/SHA，不固定套用历史51/174数字。

验收表需补d2a/d2b及最终FR-10/H01/H02/S09、状态恢复/物理卸载/失败保留映射。全量和库存审计都不能替代新增桌面人工；使用最终Release产品exe及独立yaml/store，核Native升级/消费者业务、Notebook草稿和页/命令换代、应用与保存区别、报告/驻留原因及关闭真实退出。原P3/UI人工通过继续保留其历史范围，不冒充本次通过。

最终正式发布档案关联受审名单源、相同已测DLL/manifest/依赖、最终产品exe和矩阵/人工回执；未完成全部出口前不宣布M5/P5完成。
