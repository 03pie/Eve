# 提交前代码审查记录

## P3 Q4a/Q4b/Q4c1 提交前审核

日期：2026-10-24。范围：4b6139b 之后全部未提交的配置结构/预览、受限 YAML、内置定义/schema、通用协调器、SDK、示例、固定 LibYAML、构建、测试及文档，包含新增文件。sources/research、Core/Qt/UI/Ela 实现均未改动。

- P1，规划返回后的执行门禁缺失：驱动 plan 可同步重入并关闭 scheduler，原实现返回后仍调用 apply，导致无法投递回执时继续启动资源。规划抛错路径还可能先发布正常的失败报告，掩盖投递故障。现在集中检查投递能力，在同步规划返回、发布规划失败及下一步执行前重新核对；调度器已关闭时冻结协调，向未完成任务发布 SchedulerClosed。完成旧工作项后也重新检查关闭入口。
- 回归 p3.coordinator_planning_gate 在修复前真实失败（build/review-q4-before.log），确认关闭后仍产生驱动调用；修复后覆盖规划正常返回、抛错及重入协调器关闭，检查不启动动作、目标任务错误/取消结果及真实关闭证据。
- 规范与质量：修正公共类型和 safeToContinue 的过期/不准确注释，复用投递检查，保持 owner 状态与跨线程 Promise 门禁分离。为新增 C/YAML 类型及 LibYAML 许可固定 LF，避免 Windows 检出改变上游字节。核对 YAML 类型/重复键/预算、候选移动失效、schema 捕获释放、有效依赖联合、CAS 重入、最新目标、部分成功、Pending 历史及保守资源保留；核对公开头独立包含、C/C++ 工具链与静态库边界。
- 依赖来源：LibYAML 0.2.5 固定归档 SHA256 与 UPSTREAM.md 一致，10 个源码文件与本地原始解压目录逐一比对，均未修改；MIT 许可保留，构建不联网。导入原文已有空白不重排，第一方改动单独执行暂存格式检查。

修复后 Ela/MSVC Debug、Release 各 215/215；无 Qt GCC Debug、Release 各 145/145；MSVC ASan 28/28 Loader 场景通过，第一方库和 LibYAML C 库均插桩。日志为 build/review-q4-ela-msvc-<mode>.log、build/review-q4-core-gcc-<mode>.log、build/review-q4-asan.log。未发现其他阻止本次提交的问题。

本次完成 Q4a/Q4b/Q4c1，通用协议测试不代表 Kernel 运行树驱动或完整 L01–L04/S05 已验收。配置执行/保存、原生 Windows 人工交互、外部模块及 ABI 继续待办；非静止与投递阻断按契约保留资源，ASan 不证明泄漏或物理卸载安全。

## P3 Q3c 提交前审核

日期：2026-10-24。范围：8f15edd 之后全部未提交的 Ela 组件迁移、项目内基础库、原生导航与工作台布局、宿主模型、演示、测试和文档，包括新增文件。核对固定上游库与本地修改，保留 MIT 许可和来源；sources/research、Core 和公开 SDK 未改动。

- P1，紧凑菜单残留：菜单动作捕获节点裸指针，撤页未移除动作，分组菜单还以可复用的节点地址作键。compact_removal 在修复前确认旧动作仍存在。现在菜单/动作使用稳定 key，触发时重新查找节点，撤页同步删除旧动作、空菜单及已撤销分组的菜单；同组其他页面仍可导航。
- P1，恢复模式访问已删除分组：紧凑模式缓存展开节点裸指针，插件停止后恢复展开模式会解引用旧分组。removed_expanded_group 在修复前被 ASan 确认为 heap-use-after-free。现在只缓存分组 key，恢复时从当前模型解析，跳过已删除分组。
- P1，原生导航关闭重入：原生切页信号中可关闭外壳并处理 DeferredDelete，返回后仍访问导航私有对象和旧节点；程序导航还会用旧节点安排滚动。native_close_reentry 在修复前被 ASan 确认为 heap-use-after-free。现在切页/页脚信号返回后检查 QPointer 和模型归属，程序导航保存持久索引，窗口或节点消失时直接结束；鼠标释放路径也检查视图存活，宿主选择同步使用导航访问屏障。
- P2，通知返回序号：输出 textChanged 信号中重入发布通知会更新共享 sequence，外层返回后一个通知的序号。增强 notifications 在修复前实际失败。现在提交历史后保存本次序号，展示回调重入不会改变返回值，历史仍有界且为纯文本。
- 规范与质量：合并两个带分组的 addPageNode 重载中重复的四条菜单创建分支，集中处理动作创建与清理；拆开新增选择同步中的嵌套单行控制流，补齐 QMouseEvent/QPointer/QStringList 直接依赖并移除无用包含。核对模型项所有权、分组隔离、导航门禁、固定标签归属、主题适配、输出限额、关闭顺序、CMake 可选构建、原始许可与资源分发。

修复前证据：build/review-q3c-before.log（菜单残留和分组 UAF）、build/review-q3c-native-before.log（原生关闭 UAF）、build/review-q3c-notification-before.log（序号重入）。修复后 Ela/MSVC Debug、Release 各 187/187；无 Qt GCC Debug、Release 各 117/117；MSVC ASan 的 45/45 UI 场景通过，第一方与项目内 Ela 插桩，预编译 Qt 未插桩。完整日志为 build/review-q3c-ela-msvc-<mode>.log、build/review-q3c-core-gcc-<mode>.log、build/review-q3c-asan.log。未发现其他阻止本次提交的问题。

格式检查区分导入原文和本地改动：`git diff --cached --check -- . ':(exclude)third_party/ela/ElaWidgetTools/**'` 通过；Ela 相对固定上游目录的本地补丁没有新增空白告警。整个导入目录的暂存检查会报告上游已有的文件尾空行及少量行尾空格，保留原文以便后续比较，不为这些告警重排库源码；记录为 build/review-q3c-staged-check.log、build/review-q3c-vendor-check.log。

原生 Windows 缩放、主题动画和完整键鼠交互仍按 [桌面验收单](../tests/acceptance/p3-ui.md) 人工执行，未代填验收结果。清理失败/投递阻断按设计保留资源；ASan 结果不构成泄漏或 DLL 物理卸载证明。Loader/Store、模块管理和专用资源适配仍按后续工作包推进。

## P3 Q3b2 提交前审核

日期：2026-10-24。范围：4f3b808 之后全部未提交的管理页、诊断 SDK、Kernel/Instance 查询、外壳集成、示例、构建、测试与文档，包括新增文件。sources/research 未改动。

- P1，导航关闭重入：showManagement 切换堆栈会同步发出 Qt 信号；信号中关闭外壳并处理 DeferredDelete，可在切页调用返回前删除窗口，随后继续访问导航对象。navigation_reentry 在修复前实际 SegFault。现在程序入口与导航信号共用 selectManagement，以导航访问计数阻止窗口提前关闭；切页/选中信号返回后重查门禁，重入关闭返回 ContextClosing。回归验证信号栈内窗口仍存活、关闭未完成，调用返回后可完整删除。
- P2，取消选择仍可操作：QTableWidget.clearSelection 会保留 currentRow；原实现把当前单元格当作已选行，停止按钮仍可用，下一次刷新还会恢复旧选择。deselect 在修复前实际失败。现在只从 selectionModel 的唯一已选行解析 InstanceId，清除选择立即禁用操作，刷新保持无选择，重新选中后才开放操作。
- 规范与质量：管理页导航收敛到单一入口，补充 QItemSelectionModel 直接依赖及中文 Doxygen。核对 SDK 的值语义和无 Qt 头隔离、弱 Kernel 生命周期、同 scheduler/owner 门禁、依赖诊断不增加租约、请求去重/真实操作号/有界历史、纯文本展示、清理失败与投递阻断保留、UI 访问屏障和正常关闭顺序。未修改第三方代码。

修复前证据为 `build/review-q3b2-before.log` 和 `build/review-q3b2-navigation-before.log`；修复后 Ela/MSVC Debug、Release 各 178/178，无 Qt GCC Debug、Release 各 117/117。management.hpp、ela_shell.hpp、kernel.hpp 的 GCC C++17 独立包含及 git diff --check 通过。完整日志为 `build/review-q3b2-ela-msvc-<mode>.log`、`build/review-q3b2-core-gcc-<mode>.log`。

MSVC ASan 的 36/36 UI 场景通过，第一方与固定 Ela 库插桩，Qt 预编译库未插桩；日志 `build/review-q3b2-asan.log`。清理失败/投递阻断测试按设计保留未静止资源，该结果不构成泄漏或物理卸载证明。未发现其他阻止本次提交的问题；Loader/Store、配置/模块管理、完整子树、其他专用资源及原生 Windows 人工交互沿计划另验。

## P3 Q3b1 提交前审核

日期：2026-10-24。范围：0d9a5ac 之后全部未提交的命令/通知 SDK、宿主后端、effect 集成、窗口展示、示例、测试和文档，包括新增文件。sources/research 未改动。

- 注册门禁：MSVC std::function 的小对象移动可执行用户代码。旧 add 在检查宿主状态后移动 CommandSpec；若移动中重入 shell.close，随后仍创建命令 effect/列表项，此时宿主服务的关闭结果可能已经完成。现在先保留 Context 值副本，接管回调后重查宿主及本代 guard，再修改索引和创建 effect。新增 registration_reentry，固定在第二次移动触发关闭，确认请求被拒绝、索引和 effect 数量不变。
- 初始化屏障：列表项原先在 effect setup 返回后创建。Qt model 的 rowsInserted 信号中重入关闭并推进 owner 事件，清理会在列表构造返回前释放命令捕获。现在列表创建/登记全部纳入 effect setup，窗口及闭包必须等待初始化返回；token 返回后再补发宿主关闭请求。新增 row_reentry 验证嵌套循环期间捕获仍在、外壳未完成关闭，随后可完整排空。此问题通过生命周期断言复现，未声称已复现物理模块卸载或堆损坏。
- 错误展示：命令失败携带空 message 时，宿主将空文本转给通知服务，被空文本校验拒绝，界面没有失败提示。现在使用“命令执行失败”作为缺省文本。增强 interaction，确认禁用错误和空错误消息都形成可见错误通知。
- 质量和规范：补全 functional/memory/string 直接依赖；投递失败观察者只弱持 CommandControl 并写其原子标志，不再间接访问可在完成时移动清空的 CommandCall.record。增强 inflight，以两个独立 gate 逐个完成并发调用，确认第一个结束后停止仍 pending、最后一项结束后才释放捕获。核对中文 Doxygen、同 scheduler/epoch、错误线程关闭、谓词重入、排队撤销、worker 完成、关闭顺序、有界纯文本历史和无 Qt 构建隔离。

修复前两项失败见 `build/review-q3b-before.log`，列表初始化失败见 `build/review-q3b-row-before.log`。修复后 Ela/MSVC Debug、Release 各 164/164；无 Qt GCC Debug、Release 各 115/115。三个公共头 GCC C++17 独立包含、git diff --check 通过。日志为 `build/review-q3b-ela-msvc-<mode>.log`、`build/review-q3b-core-gcc-<mode>.log`。

MSVC ASan 专项 24/24 UI 场景通过，第一方与固定 Ela 库均插桩，Qt 预编译库未插桩；日志 `build/review-q3b-asan.log`。投递阻断场景按设计保留未静止资源，本检查不构成泄漏检查或任意等待环证明。未发现其他阻止本次提交的问题；管理页、原生 Windows 人工交互、Loader/Store、专用 connection/worker/watcher 及模块 ABI/物理卸载沿原计划另验。

## P3 Q3a 提交前审核

日期：2026-10-24。范围：b5b8bab 之后全部未提交的 Ela 构建、页面服务、外壳、Context.identity、测试、示例和文档，包括新增文件。sources/research 未改动。

- 正确性：PageHandle.close 原先只请求 effect 异步关闭，cleanup 排队期间仍可 navigate 和点击页面。现在 owner 关闭请求同步标记 revoked，输入及导航立即拒绝，retiring 快照也立即体现撤销；实际 QWidget 删除和 effect 完成仍沿原异步屏障。错误线程关闭不改变门禁。lifetime/input_gate 回归在修复前均失败。
- 输入边界：QShortcutEvent 的接收者是 QShortcut，旧过滤器仅识别 QWidget，会漏过停用页面的快捷键。现在沿 QObject parent 找到 QWidget 后判断页面归属。新增 shortcut_gate 验证 Active 能触发、停止后但删除前被阻断。中文输入法 QInputMethodEvent 也会绕过 KeyPress；input_gate 新增真实输入法提交验证，修复前确认修改了已关闭页面。过滤器补齐 InputMethod、MouseMove 和触摸后续事件，保留任意程序信号/外部 queued callback 仍需专用契约的边界。
- 导航：原先只连接 itemClicked，方向键改变选中行却不切换页面。改用 currentItemChanged，让键盘、鼠标与句柄使用同一导航门禁；新增 keyboard_navigation 检查方向键切页及关闭当前页后回安全页。
- 规范与质量：补全 utility/vector 直接依赖、移除未使用头；事件类型判断改为 switch；navigate 按值保留页面控制块，避免 Qt 调用重入时仅借用句柄成员；窗口关闭错误保留原 effect 的 causes；修正工厂释放与记录移除的注释。核对 Qt parent 所有权、工厂失败/关闭重入、同名贡献隔离、服务恢复、线程/epoch、窗口退出顺序、第三方固定 hash/许可、可选构建和脚本环境恢复。

修复前四项失败证据见 `build/review-q3-before.log`；输入法独立失败见 `build/review-q3-input-method-before.log`。修复后 Ela/MSVC Debug、Release 各 151/151；无 Qt GCC Debug、Release 各 115/115；两份新公共头 GCC C++17 独立包含、git diff --check 通过。日志为 `build/review-q3-ela-msvc-<mode>.log`、`build/review-q3-core-gcc-<mode>.log`。

MSVC ASan 专项 11/11 UI 场景通过，第一方和固定 Ela 库均插桩，Qt 预编译库未插桩；日志 `build/review-q3-asan.log`。开发阶段已经复现并规避的上游原生导航 UAF 见 [ADR-0006](adr/0006-ela-page-contributions.md)，本轮未修改第三方源码。未发现其他阻止提交的问题；命令/通知/管理页、原生 Windows 人工交互、connection/worker/watcher 及插件 ABI/卸载仍需后续验收。

## P3 Q2 提交前审核

日期：2026-10-24。范围：ce43cc6 之后全部未提交的 QObjectResource、QtClock、SDK、资源测试、构建登记与文档，包括新增文件。sources/research 未改动。

- 测试正确性：clock_close 原先在预期抛错的 discarded 回调内使用 CHECK。断言失败同样被隔离并计入 callbackErrors，可能被预期异常数量掩盖。现在回调只记录“重入 close 仍 pending”和“新计时被拒绝”的观察值，回调外独立断言，保留有意抛错以验证异常隔离。没有把这项测试缺陷记作生产关闭逻辑 bug。
- 回调所有权：waitTask 的通知标志改为共享值捕获，避免等待超时后栈已展开而排队通知仍访问局部变量。新增 p3.qt_resources_close_releases_handles，覆盖丢弃回调释放最后一个 QtClock facade 后继续完成整批通知，以及子对象析构释放最后一个 QObjectResource facade 后仍保持完整删除屏障。
- 规范与质量：补全直接使用的 memory/utility 标准头，拆开计时账目初始化；公共构造契约明确只有前置条件错误抛 invalid_argument，其他构造失败仍不转移所有权，并与 ADR 一致列出外部 deleteLater/WA_DeleteOnClose 限制。核对 owner 入口、令牌原子取消、已到期队列载荷、析构重入、异常隔离、退出兜底、Kernel 清理集成与无 Qt 构建边界。

Qt 5.15.2 msvc2019_64 + MSVC 19.44/v143 x64 Debug/Release 各 140/140；无 Qt GCC 13.1 Debug/Release 各 115/115。两份新公共头独立包含及 git diff --check 通过。完整日志：`build/review-q2-qt-msvc-<mode>.log`、`build/review-q2-core-gcc-<mode>.log`。

另以 MSVC /fsanitize=address、RelWithDebInfo、/MD、/O1 和 /EHsc 单独构建第一方 core/qt 与资源测试，14/14 资源场景通过，日志 `build/review-q2-asan.log`，重现命令见 [P3 Q2 记录](p3-development.md#q2-提交前审查)。Qt 5.15.2 预编译库未插桩，该结果不构成全程序内存安全或泄漏证明；未运行 TSan、connection 排空、worker join、Ela UI、ABI 或物理卸载验证。未发现其他阻止提交的问题。

## P3 Q0/Q1 提交前审核

日期：2026-10-24。范围：2e6efa2 之后全部未提交的 Qt 工具链配置、QtScheduler、SDK、测试与文档；并核对 FakeScheduler 的同类队列操作。sources/research 未改动。

- 正确性：QtScheduler 和 FakeScheduler 原先在队列锁内移动 ScheduledJob。MSVC std::function 的小对象存储会调用用户可调用对象的移动构造及移动后析构；若这些函数查询 isOpen，则重入同一把锁。现在在锁外创建 unique_ptr<ScheduledJob>，队列内只移动指针，执行和丢弃路径也不在锁内销毁用户对象。新增 p3.qt_scheduler_callable_reentry 与 w1.scheduler_callable_reentry，在修复前 MSVC Debug 两项均真实失败，见 `build/review-p3-before.log`；修复后均通过。
- 重入边界：FakeScheduler::runOne 在任务捕获析构前清除了 pumping，捕获的析构函数可提前运行后续任务。现在由后析构的 PumpGuard 恢复门禁，覆盖成功及异常返回。新增 w1.scheduler_destructor_guard；修复前 GCC Release 明确违反“后项仍排队”断言，见 `build/review-p3-guard-before.log`，修复后两个分支均通过。
- 所有权：QtScheduler 的 post/close 在进入用户代码前持有本地控制块，避免移动/丢弃回调释放 facade 与 Qt lifetime 后继续访问已释放控制块。新增 p3.qt_scheduler_close_releases_owner，验证丢弃回调释放全部所有者后抛错，仍完成后续通知；该场景为生命周期防护，未声称完成消毒器验证。
- 规范和质量：新增文件、类型及具名函数使用中文 Doxygen；重入探针由 Qt/Core 测试共用，避免只依赖编译器恰好使用堆存储。核对 owner 限制、锁外析构、关闭/投递竞争、异常隔离、账目与队列一致性，以及 CMake 可选目标、QT_NO_KEYWORDS 传播和开发脚本环境恢复。同步 ADR、接口契约和进度，保留初次验证与本轮审核的独立证据。

验证：Qt 5.15.2 msvc2019_64 + MSVC 19.44/v143 x64 Debug/Release 各 126/126；无 Qt GCC 13.1 Debug/Release 各 115/115。日志为 `build/review-p3-qt-msvc-<mode>.log`、`build/review-p3-core-gcc-<mode>.log`；QtScheduler/FakeScheduler 公共头仅使用 SDK include 的 GCC C++17 独立包含检查、git diff --check 通过。未发现其他阻止提交的问题。

本轮仍限 Q0 工具链与 Q1 调度；Q2 Timer/QObject 资源屏障、Ela UI、原生 ABI 与 DLL 卸载未实现或验收，未运行 ASan/TSan。

## W7 提交前审核

日期：2026-10-24。范围：25adfcc 之后全部未提交的联合示例、CTest 登记与验收/进度文档，包括新增文件。未改动核心 SDK 行为。

- 正确性与证据：原 recovery 只检查新 epoch/BindingId，没有实际分发恢复后的事件，无法证明新监听器连接到新服务。现在更换提供者前后均发送保存事件，检查恰好调用一次当前监听器、当前 Store 的实际文档内容和确认结果；旧 Store 必须在替换前过期，新监听器恰好一个。增强现有场景而非增加重复测试入口。
- 防御性检查：绑定读取集中到先验证数量的 binding()，避免空数组越界；清理失败快照先判断 optional 是否存在，弱引用只 lock 一次后读取，避免缺失诊断时空解引用。关闭检查复用不可变快照，并核对根、消费者及提供者的 Disposed、effects 和 committed 数量。
- 状态准确性：失败路径不再记录 consumer cleaned，改为 consumer cleanup failed，并断言提供者及根未运行清理。failure 示例返回成功只表示预期残留得到验证，不表示内核关闭成功。
- 规范与质量：核对所有新增文件、类型、具名函数的中文 Doxygen、标准头依赖和回调捕获所有权；统一资源查询与断言，整理进度工作记录表的空行，避免 Markdown 表格断裂。验收报告引用的 24 个唯一测试名均与 CTest 实际登记核对。

GCC 13.1、MSVC 19.44 x64 Debug/Release 各 113/113 通过，日志为 `build/review-w7-core-<compiler>-<mode>.log`；git diff --check 通过。未发现其他阻止提交的问题。未运行 ASan/TSan、Qt、ABI 或物理卸载验证，首阶段结论仅适用于文档明确的内置范围。

## W2/W3 提交前审核

日期：2026-10-23。范围：全部未提交的 W2 收尾、W3 Kernel/实例实现、SDK、测试及文档。

## 已修复问题

1. 移动后的 Runtime 保留原编号，但激活函数已为空。Kernel 仅核对编号会接受该句柄，随后访问空回调。现在移动后原句柄 id() 返回无效编号，Kernel 拒绝挂载；直接 Instance::mount 在登记资源前抛 invalid_argument。新增回归在修复前实际失败，日志为 `build/review-before-fix.log`。
2. 调度器关闭后 update/retry 可能先替换目标、分配操作，再投递失败或构造 EffectScope 抛错。现在在修改账目前返回 SchedulerClosed。回归同时检查 Active 更新和 Failed 重试被拒绝，操作编号、配置修订和状态保持原值。
3. 本机中文 MSVC 的 /showIncludes 前缀被 CMake 错误解码，Ninja 未记录头文件依赖。Release 增量构建因此混用旧 Kernel 目标和新 SDK。新增可选 CMake 缓存项 CORDIS_MSVC_SHOWINCLUDES_PREFIX，默认仍自动检测；本机使用 `注意: 包含文件:`。清洁构建后已核对依赖表，触碰 instance.hpp 会触发 instance.cpp、kernel.cpp 与 w3.cpp 重编译。

## 验证与边界

GCC 13.1、MSVC 19.44 x64 的 Debug/Release 各 61/61 通过。W3 共十组行为，共用一个 CTest 入口；新增测试不会增加 CTest 入口数量。日志为 `build/review-core-<compiler>-<mode>.log`；MSVC 清洁构建日志为 `build/review-build-core-msvc-<mode>.log`。`git diff --check` 通过。

本机 MSVC 配置需使用 W2 记录中的开发者环境及 TEMP/TMP 设置，再执行：

```powershell
cmake --preset core-msvc-debug '-DCORDIS_MSVC_SHOWINCLUDES_PREFIX=注意: 包含文件:'
cmake --preset core-msvc-release '-DCORDIS_MSVC_SHOWINCLUDES_PREFIX=注意: 包含文件:'
```

修复旧依赖数据库后需使用 `cmake --build --preset <preset> --clean-first` 重建一次。不要只修前缀而继续使用缺失依赖记录的旧目标。此覆盖参数仅在检测错误时使用，值应与实际编译器输出匹配。

未发现其他阻止本次增量提交的问题。W3 仍未完成独立 CleanupContext，服务/realm 未实现；历史操作未裁剪，非合作清理及投递阻断会保守保留资源。上述已知边界见 W3 记录；未运行 ASan/TSan、Qt 或 DLL 验证。

## W4 提交前审核

日期：2026-10-23。范围：c3f3661 之后全部未提交的 CleanupContext、服务注册表、实例依赖收敛、输出声明、realm、异步调用、测试及文档；包含新增未跟踪文件。

发现并修复一项高优先级问题：`Instance::change` 在实例已退役时通过 `scope.close().tryResult()` 查询清理失败。提供者此时可能仍在等待消费者退出，调用 close 会提前启动提供者自身清理，破坏消费者优先清理的约束。现在只在快照表明 scope 已开始关闭时查询其结果，等待下游期间更新仅替换目标配置。

新增 `w4.dependency_update_draining`：消费者清理由 gate 阻塞，提供者连续更新两次，验证 gate 完成前提供者不执行清理、不启动新代，旧操作为 Superseded；消费者仍能通过 CleanupContext 访问旧绑定，之后才完成提供者更新及依赖恢复。修复前真实复现，GCC Debug 85 项通过、该项失败，见 `build/review-w4-gcc-debug-before.log`。

修复后 GCC 13.1、MSVC 19.44 x64 Debug/Release 各 86/86 通过，日志为 `build/review-w4-core-<compiler>-<mode>.log`。同时修正文档中仍称服务依赖/realm 未实现的过期状态。`git diff --check` 通过，未发现其他阻止本次提交的问题。

已知范围：W5 可选依赖、父子所有权与联合等待图未实现；清理失败或投递阻断继续保守保留资源。未运行 ASan/TSan、Qt 或 DLL 验证；这些结果不构成物理卸载安全证明。

## W5 提交前审核

日期：2026-10-24。范围：748c8fb 之后全部未提交的子实例、父代清理屏障、实际/声明联合图、SDK、测试及文档，包括新增文件。

发现并修复一项操作结果一致性问题：实例已经返回 PendingMissingDependency 后，新的声明环或不兼容服务会使实例 Failed，但旧 target 已完成，再次 resolve(Failed) 无效，当前 operation 仍错误显示 Pending。现在激活前失败统一通过 failPreflight 收敛；若旧操作已稳定 Pending，先建立新 OperationId，再发布 Failed，配置修订和 epoch 不变，旧操作继续返回其历史 Pending 结果。

增强 `w5.children_declared_cycle` 检查自动失败操作与历史结果，新增 `w5.children_pending_failure` 检查晚到类型不兼容。修复前两项均真实失败（94/96 通过），见 `build/review-w5-before.log`。

修复后 GCC 13.1、MSVC 19.44 x64 Debug/Release 各 96/96 通过，见 [开发进度](progress.md)，日志为 `build/review-w5-core-<compiler>-<mode>.log`。`git diff --check` 通过，未发现其他阻止本次提交的问题。审查涵盖父代广播停止、子清理失败保留、更新重建、realm 继承和环路径。已知图边界仍为同一托管注册表，候选提供者多义时不猜测连边；不覆盖任意 Task 等待。未运行 ASan/TSan、Qt 或 DLL 验证。

## W6 提交前审核

日期：2026-10-24。范围：462c594 之后全部未提交的 EventBus、Context/EffectScope 适配、测试、示例及文档，包括新增文件。

正确性：复制成功的源结果至 serial 返回任务或 parallel 汇总时，用户负载的复制构造可能抛错。原实现会使内部 continuation 失败，返回任务可能永久 pending、自保留控制块不能解除；parallel 还可能在已经减少 pending 后漏记该监听器结果。现在 serial 将复制异常转换为 CallbackFailed，parallel 每项复制失败记入对应 Result，组装报告本身失败则拒绝返回任务；这些路径都释放已完成的分发屏障。新增 `w6.events_result_copy` 使用可抛复制负载，同时验证 serial/parallel 结果、activeDispatches 归零与所属作用域清理成功。

规范与质量：补全标准库直接依赖，移除未使用的 algorithm；补充负载可移动的编译期约束。快照先成功存入记录再增加保留数，避免 vector 扩容失败留下未记账的保留；析构使用已有记录逐项释放并通知，不再额外分配 Promise 容器。修正“最后快照销毁闭包”的过强注释，明确最终回调所有权仍由 effect 清理闭包释放；拆开多个成员赋值，便于逐项检查。

审查同时核对 once/过滤重入、Next 窗口、注销快照、epoch 门禁、scheduler 关闭、异步投递阻断及实例清理路径。GCC 13.1、MSVC 19.44 x64 Debug/Release 各 110/110 通过；event.hpp 独立包含及 git diff --check 通过，未发现其他阻止提交的问题。矩阵结果见 [开发进度](progress.md)，日志为 `build/review-w6-core-<compiler>-<mode>.log`。未运行 ASan/TSan、Qt 或 DLL 验证；任意用户等待环、跨 ABI、全局 schema 与 post 仍不在本轮范围。
