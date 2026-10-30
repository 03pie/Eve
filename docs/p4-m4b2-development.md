# P4-M4b2 真实 Qt 工厂与 Core 业务实例

日期：2026-10-30。基线 cd5bf2a。按用户要求先审查职责、依赖、状态和所有权，再实现；架构决定见 [ADR-0038](adr/0038-qt-runtime-ownership.md)。本段仍不完成 M4c 或整个 P4。

## 实现与边界

IQtPluginV1 / IQtInstanceV1 为严格同工具链的 C++ 接口，工厂目录必须与既有 manifest 集合一致，不能只凭元数据 IID 字符串授予业务调用。qtRuntime 返回由宿主构造、冻结依赖/输出的既有 Runtime；每次激活/换代创建独立 QObject。QtResidency 继续保存在同一进程目录，没有增加第二套 Registry、Instance 或页面管理器。

为避免静态 SDK 复制进程身份，Windows/MSVC/Qt 默认改为共享 Core DLL，同工具链插件使用 Cordis::QtPluginSdk；测试 DLL 也真正通过此目标导入 Core。Core 保持无 Qt 依赖，无 Qt GCC 默认静态构建；原 Release 目录不变，MSVC 共享构建的可执行文件统一放在 bin，与 Core DLL 相邻，Debug 加 d 后缀；SDK 与共享 Core 以编译期门禁拒绝 /MT。CORDIS_SHARED_CORE=OFF 仍可使用 C ABI/只读预检，不提供需要同一 Core 的 Qt 业务目标。自动导出函数不构成稳定二进制 SDK，公开全局数据不是当前契约。

Core 新增 afterCleanup 作为主资源静止后的所有者释放阶段，内部复用 EffectScope/EffectToken，按登记逆序串行且遇失败即停。先释放本代输出服务对象，再析构业务所有者，最后才释放输入 committed 租约。主 scope 或子实例失败时不启动末尾析构；末尾失败阻止新 epoch，并保留仍未确认的资源。InstanceSnapshot 的独立末尾账目、finalizing/hasRetainedResources 与投递受阻状态接到现有配置检查点及管理诊断，避免把“主 scope 已空”误报成可重试。

适配在工厂调用前取得 Instance 租约、登记业务末尾清理。所有正常插件资源及捕获先通过 Core 清理；QObjectResource 负责完整业务对象/子对象析构，之后才释放保留的 DLL 激活 Task 状态与租约。根 QObject 始终归 loader 并按策略驻留，定义单独持有 ExternalHandle。异常工厂/错误对象所有权保留不确定引用；普通 nullptr 创建拒绝和激活失败按 Core 回滚，错误接口的可安全接管 QObject 完整删除后仍保留协议诊断。

## 审查与复现

由当前代理审查，未使用子代理。初次测试使用不可赋值 Result 的赋值表达式导致编译失败，改为移动取出 Runtime；没有放宽 Result 契约。共享 Core 的首个负向构建脚本漏掉本目录 find_package(Threads)，失败日志保留，修正后才取得真实负向证据。

审查补上末尾清理 LIFO/失败停止机制，否则插件后登记的清理可能仍使用业务对象。另复现输入租约在末尾清理之前释放导致提供者提前清理：p4.finalizer_dependencies 修复前 0/1；修复后该项与真实 DLL 输出/Tail 等场景 4/4。配置检查点和管理重试同时使用完整账目，避免只检查主 scope。

负向控制将测试 DLL 改为真实静态 Core，shared_core 按预期 0/1；共享 SDK 恢复后 1/1。临时改成并行末尾清理，lifo/lifo_failure 按预期失败；源码逐字节恢复并重编译后通过。负向构建均未修改参考/第三方、放宽断言或靠旧产物冒充。

## 验证范围

Core 用真实 gate 检查主清理/末尾清理先后、LIFO、失败停止与保留、换代、错误线程/旧上下文、子实例、晚到初始化、输入依赖屏障及调度器关闭。配置驱动额外检查主 scope 空但末尾失败的 unsafe 检查点。

独立 Qt 入口用真实 DLL 验证目录/接口、只协商一次、多实例、配置 epoch、晚到 start/cleanup、启动/清理错误、nullptr/异常创建、root/parent/共享对象拒绝、缺实例接口、错误目录/查询异常、元数据合法但缺工厂、线程/scheduler 门禁；并验证共享 Core 的跨 DLL 身份、插件内 inject、带 DLL deleter 的服务输出，以及插件末尾清理延迟/失败。QObject destroyed 回调中观察租约仍存在且子对象尚未析构，最终关闭后才确认捕获和子对象都已释放。此夹具没有 QWidget 页面，不冒充 UI DLL 示例或原生验收。

最后的重入复审以真实 QObject ChildAdded 同步回调复现三项问题：创建中 stop/update 仍激活旧代，且另一 scheduler 可重入同一工厂。修复前 create_stop/create_update/create_busy 为 0/3，query 已正确拒绝；增加创建门禁及外部构造/metacast 返回后的 Context 复查，修复后四项 4/4。另增加 DLL 内虚调用的实际 Core 身份握手，避免只依赖构建目标名；真实静态 Core 的错链 DLL 已由生产握手在 query/create 前拒绝，正常共享 SDK 恢复后通过。两个 /MT 编译探针均按明确错误拒绝。

最终 MSVC Release、Debug、ASan 全量各 689/689，无 Qt GCC 257/257。原 Release 关闭示例的 P4 221/221；进一步关闭共享 Core 后原生/只读兼容路径 P4 177/177；完成后恢复两个开关 ON 并重建。关键 9 项在 Debug/ASan 各连续 20 次，共各 180 次通过。

冻结 21 项源码/构建哈希一致，11 个实际变更 C++ 编译单元的 ASan 参数已核对；公开 host 头在 MSVC/GCC 无 Qt 目录独立编译，Core DLL 不导入 Qt，Debug/Release 插件分别导入 cordis_cored.dll / cordis_core.dll。UTF-8/LF、修改区域格式、引用路径及只读参考边界检查通过。

最终日志前缀为 build/p4-m4b2-reviewed-，包含 {release,debug,asan,gcc}-{build,tests}.log、no-examples-*、static-*、restore-*、{debug,asan}-repeat.log、source-sha256.json、asan-commands.json、headers.log、imports.log 与 audit.log。core-baseline-* 的 684 项、pre-cookie-* 的 688 项及 220 项专项是过程历史，不替代最终矩阵。红绿及负向证据分别为 dependencies-{red,green}-*、reentry-{red,green}-*、static-core-*、parallel-tail-*、core-cookie-* 与 static-crt-*；先前缺 Threads 的脚本失败及临时未声明错误夹具变量的编译失败日志均保留。M4b2 已完成审查与验证，随本提交归档；P4 仍进行中，下一段 M4c 必须提供真实页面 DLL、配置装配、模块诊断和桌面验收映射。

M4c 接入前须检查 Qt/Ui SDK 的链接边界：QtScheduler 的私有唤醒类型不能随静态库在多个 DLL 中各自注册，页面系统也不应复制平台状态。此项在实际 UI SDK 跨 DLL 使用时实施，不在本段建立额外空接口。
