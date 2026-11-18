# ADR-0056：资格档案与产品 Native 精确名单

日期：2026-11-19。状态：M5-e0实施前设计经独立审查采纳，随本次提交归档。基线d47210a，M5-d已审计提交同步且工作区干净。

## 现有边界与问题

ModuleManager的load/stage参数NativeUnloadWhitelist已是唯一首次映射批准输入，verifyModuleSource和selectPolicy是唯一实际政策门，默认名单为空。Workspace只有一套安装、Session/Driver、升级和关闭拥有链；原openQualification从固定六种实际业务产物临时批准四个Native A/B，适合资格测试，不能作为产品默认授权。native_services/Pipeline的resident 1.0演示不是另一升级宿主，不扩充它。

M5-d完成了三共享配置固定业务的H02、资源基线、组合/回放/缩减；H01/S09已有M5-c真实故障证据。仍须完成精确名单的产品接线、最终全支持矩阵、需求映射、整P5审计及新增桌面升级/退出验收。不能仅把资格入口改名字，不能从用户选中的manifest、环境开关或任意运行时报告补批准。

## 受信数据与部署边界

采用“显式导出候选资格档案→代码审查固定名单源→生成私有C++纯值并嵌入产品→打包相同已测字节”。源档案按具体profile保存Counter/Reporter各A/B四项，记录module/plugin/version/explicit身份、DLL和manifest SHA、实际编译器/CRT/模式/ASan/生产命令及相关依赖环境和已完成资格的原证据摘要。普通build只消费受审档案，不扫描当前manifest生成批准，不自动把候选导出提升成受信源。

只纳入ela-msvc-release、ela-msvc-debug、p3-q3-asan三个有相应资格的profile，各四份Native，共十二份DLL及其manifest。正常产物保持报告也包含其他配置/旧resident业务，不等于都取得H02资格。Qt、故障产物、resident 1.0和未取得资格的静态配置不进入名单。每个产品仅嵌入本profile四项，禁止十二项取并集。

profile必须含构建模式和ASan状态；Release与当前ASan都可MD/debug=false，仅qtToolchain()不足以区分。编译时用真实CMake配置/编译器/CRT及ASan选择档案，Qt实际ABI继续沿原qtToolchain/inspect门禁，不把profile变成ModuleManager的新策略系统。生成代码只包含受审纯值；档案文件缺失、损坏或身份/格式非法使配置/生成失败，不吞错。有效档案没有匹配profile时明确生成空名单，产品首次explicit准入沿原ModulePinned拒绝，构建仍可用于重新资格。产品无运行时名单文件选择、修改或批准API。

清单表示已审确定字节的Native资格，不代替整P5发布出口。最终发布档案在全矩阵和新增桌面验收后，关联该名单源的SHA、实际产品exe、相同DLL/manifest与运行依赖及最终证据；不能提前宣布整P5完成。名单源和最终发布证据分开，避免为回填最终报告而循环重新链接已资格业务DLL。

## 产品装配与所有权

新增具体openPlatform入口，产品main从本构建嵌入名单取得纯值并传入；注入仅属宿主内部受信装配链，不从CLI、用户文件、环境或普通插件接收任意批准。原openQualification保留给现有Scene/故障/压力资格调用者，原因明确；产品默认main删除对它的调用及资格编译宏含义。三种真实装配来源为原Notebook、资格业务、产品业务，使用明确有限模式；产品与资格共享原业务装配和已有approvals_，不复制装配、事务、目录、名单注册表或状态机，不增加公开SDK接口。

固定业务来源检查由资格与产品两个实际消费者共用；将仅适用资格的内部命名改为实际共同职责，删除被替代名称，不保留无调用旧路径。产品名单在装配前一次冻结；初始装载、已有保存定位、升级预览/执行仍使用同一份值，最终由原ModuleManager重新固定字节。候选选择不能改变名单；失败不改旧运行/目标或已保存文件。

无匹配profile或实际SHA不匹配时，explicit沿原ModulePinned拒绝；档案缺损在构建层拒绝。不得降为resident、补批准、改首次策略或清PIN。原普通resident与Qt loader root/PreventUnloadHint保持，名单只批准四种明确Native字节。纯名单不持Generation、Runtime、Scope、Task、QObject或DLL闭包；档案只依赖产品名单消费者，不成为Native DLL的POST_BUILD/链接依赖，最终发布证据也不反向参与链接。

## freshbuild与资格更新

PE/链接元数据变化也会改变SHA；相同源码、版本、路径或工具链名不等于同一产物。全新构建没有命中已审字节时产品应明确拒绝，不能忽略时间戳或自动更新批准。固定资格入口仍可验证新构建；显式工具校验固定来源和原报告链，输出候选档案及已测产物位置，经审核更新受信输入，再只重建产品名单消费者并核打包字节。不得由CMake POST_BUILD从manifest自动授权。

导出工具只出候选，不代替代码/契约审查或人工出口，也不伪造最终矩阵。开发脚本保持构建职责；为已有desktop工具增加固定业务工作台启动方式，正确选择产品exe/独立yaml和store，不增加批准开关。原配置工作台入口保留原实际用途。

## 验证与分段

e0仅设计/清单审查并提交同步；e1实现档案导出/私有名单生成、产品装配和启动接线，检查正确四项、空/错SHA/跨profile/未知模块拒绝、已有保存定位重开、Native A→B→A及Qt/resident保持。导出拒缺资格、错产物或不完整记录；不能只信passed布尔。原资格链继续回归并说明保留原因。

e2冻结最终输入，五现有支持目录完整构建、重新获取各自全CTest清单并实际执行，核最终名单/字节、SDK旧实物、实际ASan对象/依赖、公开契约与需求映射；源或产物变化按影响重新资格。e3用最终Release产品窗口完成新增Native升级、消费者恢复、Notebook草稿/页面/命令换代、应用/保存区别及正常关闭验收，登记真实回执和退出，再做整P5架构/旧路径删除/兼容审查及最终归档。每段审计、提交同步、clean后才进下一段，不用历史UI确认或隐藏HWND代替新人工验收。
