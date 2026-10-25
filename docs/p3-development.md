# P3 桌面适配开发记录

最后更新：2026-10-25。状态：进行中；Q0–Q3c、Q4a/Q4b 校验、Q4c1 协调及 Q4d1/Q4d2 文件和 Store 已实现。用户已确认此前工作台的原生桌面人工验收完成；Q4c2a/b 已接入顶层内置配置及动态 inject 的真实 Kernel 驱动，显式配置子树、watcher/后台 I/O 和管理页保存继续待办。

## 已安装与验证

用户授权安装后，以 build/qt-tools 下的 Python 3.10 虚拟环境运行 aqtinstall 3.3.0，从 Qt 公共仓库安装 Qt 5.15.2 win64_msvc2019_64 的 qtbase 归档到 `E:/Qt/5.15.2/msvc2019_64`，包含 Core、Gui、Widgets、Test 与相应工具、Debug/Release 库。仅安装本阶段所需 qtbase，不声称安装了全部 Qt 模块或 Creator。原有 `E:/Qt/6.9.1` kit 保留。

```powershell
py -3.10 -m venv build/qt-tools
./build/qt-tools/Scripts/python.exe -m pip install aqtinstall==3.3.0
./build/qt-tools/Scripts/python.exe -m aqt install-qt windows desktop 5.15.2 win64_msvc2019_64 -O E:/Qt --archives qtbase
```

安装日志：`build/qt5152-install.log`。qmake 实测 5.15.2；CMake 实际解析 Core/Widgets/Test 均来自上述 kit。未修改机器全局 PATH；开发脚本为所选 Qt 命令临时添加 Qt bin，结束或失败后恢复调用方 PATH 和 CORDIS_QT5152_ROOT。

本轮使用 VS2022 Professional 的 MSVC 19.44 x64/v143，Debug /MDd、Release /MD；Qt 库来自 msvc2019_64 包。该组合已实际编译、链接、加载和运行探测。微软的二进制兼容约束允许支持版本的较新工具链链接较旧库，但仍要求合适的运行库和链接版本，/GL 等另有限制，不能推广成任意 Qt/插件组合都已验证。[微软兼容性说明](https://learn.microsoft.com/en-us/cpp/porting/binary-compat-2015-2017)

v142 未安装；当前选定并验证的是上述 v143 组合，未把它记为 v142 验收。下文各轮记录保留当时的验证状态；原生桌面交互的最新用户确认见本文末尾及验收单。

## 项目配置与运行

新增 qt-msvc-debug / qt-msvc-release，继承原 MSVC 内核预设，启用精确 Qt 5.15.2 检查。Kit 通过 -QtRoot 或 CORDIS_QT5152_ROOT 显式选择，不将本机绝对安装路径写入共享预设。需先进入 x64 Developer PowerShell，使用现有 TEMP/TMP 设置；本机中文 showIncludes 前缀覆盖仍按已有审核记录配置。

```powershell
$env:TEMP = (Resolve-Path build/msvc-tmp).Path
$env:TMP = $env:TEMP
& 'C:/Program Files/Microsoft Visual Studio/2022/Professional/Common7/Tools/Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
$env:CORDIS_QT5152_ROOT = 'E:/Qt/5.15.2/msvc2019_64'
cmake --preset qt-msvc-debug '-DCORDIS_MSVC_SHOWINCLUDES_PREFIX=注意: 包含文件:'
cmake --preset qt-msvc-release '-DCORDIS_MSVC_SHOWINCLUDES_PREFIX=注意: 包含文件:'
./tools/dev.ps1 -Action verify -Preset qt-msvc-debug
./tools/dev.ps1 -Action verify -Preset qt-msvc-release
```

也可为每次 dev.ps1 调用指定 `-QtRoot E:/Qt/5.15.2/msvc2019_64`。直接调用 ctest 前也需要将该 kit 的 bin 加入当前进程 PATH，以解析正确 DLL。

## 探测证据

`p3.qt_environment` 编译期检查 Qt 5.15.2 与 x64，运行期检查实际加载的 qVersion。使用 QApplication 的真实事件循环、QWidget 与 QSignalSpy 验证 Widgets/Test 链接、对象析构通知、工作线程 QueuedConnection 不内联、回调抵达 owner，以及 deleteLater 的延迟删除。测试采用 offscreen 平台，不弹窗口；5 秒 QTimer 仅为故障退出保护。

目标设置 QT_NO_KEYWORDS，验证先包含 Qt 头后仍能使用 EventBus::emit，避免 Qt emit 宏冲突。此探测中的 EventBus 使用 FakeScheduler，只验证头文件互操作；QtScheduler 的独立证据见下文 Q1。

| 检查 | 结果 |
|---|---|
| Qt/MSVC Debug、Release | 各 114/114，通过原 113 项与 Qt 探测 |
| 无 Qt GCC Debug | 113/113，保持原有独立构建 |
| Qt6 错误 kit | dev.ps1 在配置前明确拒绝；PATH/QtRoot 环境恢复检查通过 |

日志为 `build/p3-qt-msvc-debug.log`、`build/p3-qt-msvc-release.log`、`build/p3-core-gcc-debug.log`、`build/p3-qt6-rejected.log`。尚未运行可视化桌面验收、Ela 编译或 DLL 卸载测试。

## Q1 QtScheduler

新增 `Cordis::Qt` 静态目标与 `cordis/qt/scheduler.hpp`，仅 Qt 预设启用。核心不引入 Qt；适配目标公开依赖 Core 和 Qt5::Core，并传播 QT_NO_KEYWORDS。公开头通过前置声明 QObject 保持可独立包含。

应用线程构造 QtScheduler，可指定同线程 lifetime，默认应用。宿主 FIFO 登记 ScheduledJob，Qt 自定义事件只负责唤醒，不承载插件闭包。post 始终排队；任务在锁外建立唯一所有权存储，锁内只转移指针，避免 std::function 小对象移动隐式调用用户代码。接受和关闭共用锁，执行、丢弃及捕获析构在锁外。一次只处理一项，执行和捕获析构期间禁止嵌套事件循环启动下一项业务。

lifetime 的 Qt 子对象持有控制块，最后一个 scheduler 句柄可在 worker 释放且不取消已接受任务；lifetime 销毁或应用 aboutToQuit 才执行兜底关门。owner 可显式 close，排队项被丢弃，已进入的任务继续完成；snapshot 记录 open/running/queued/accepted/executed/discarded/callbackErrors，executed 包含当前执行项。任务及 discarded 的异常被隔离并计数。

正常退出必须在 Qt 循环仍运行时等待 Kernel.close，再关闭 scheduler，最后退出应用；close 本身不代表 Kernel 或 QObject 资源已经静止。lifetime 不可迁移线程，处理事件时使用 deleteLater，资源销毁确认留待 Q2。

`tests/qt/scheduler.cpp` 使用真实 QCoreApplication/QEventLoop；QTimer 仅为 5 秒失败保护，不使用 sleep 或 FakeScheduler 驱动：

| CTest 后缀（前缀 p3.qt_scheduler_） | 覆盖行为 |
|---|---|
| fifo | owner 排队、FIFO、回调内追加、嵌套 processEvents 不重入 |
| tasks | 已完成 Task 不内联、worker 完成通知回 owner、错误线程拒绝 |
| discard | 关闭丢弃、thenOn 回执 SchedulerClosed、通知抛错与重入隔离 |
| lifetime_race | lifetime 析构与 worker 连续投递竞争，接受/丢弃恰好一次 |
| dropped_handle | 最后一个句柄在 worker 释放后任务仍由 Qt owner 执行 |
| exceptions | run 异常隔离，执行中关闭允许当前项返回并丢弃后项 |
| kernel_shutdown | worker 完成异步 cleanup，Kernel 静止后才关闭 scheduler |
| application_quit | aboutToQuit 丢弃未运行任务，不伪报执行成功 |
| callable_reentry | 用户可调用对象的移动/析构可重入查询，覆盖接受、执行、丢弃与拒绝 |
| close_releases_owner | 丢弃通知释放最后一个句柄及 lifetime 后抛错，仍完成后续通知 |

| Q1 检查 | 结果 |
|---|---|
| Q1 初次验证 | Qt/MSVC Debug、Release 各 122/122；无 Qt GCC Debug 113/113 |
| 提交前 Qt/MSVC Debug、Release | 各 126/126：115 项内核/示例、Q0 探测、10 项 Q1 场景 |
| 提交前无 Qt GCC Debug、Release | 各 115/115；Qt 适配目标未进入构建 |
| 公开头独立包含 | GCC C++17，仅 SDK include 路径，QtScheduler/FakeScheduler 头语法检查通过 |

初次日志：`build/p3-q1-qt-msvc-debug.log`、`build/p3-q1-qt-msvc-release.log`、`build/p3-q1-core-gcc-debug.log`。提交前日志：`build/review-p3-qt-msvc-<mode>.log`、`build/review-p3-core-gcc-<mode>.log`。仍使用上述 Qt5 msvc2019_64 + MSVC19.44/v143 组合，不构成原生 DLL ABI 或 v142 验收。

提交前审查同时修复 FakeScheduler 的同类持锁移动，以及任务捕获析构提前解除 pump 门禁的问题；新增 w1.scheduler_callable_reentry 和 w1.scheduler_destructor_guard（正常/异常路径）。Qt post/close 增加本地控制块保留，防止用户回调释放所有者后控制块提前销毁。复现证据与审查范围见 [审核记录](code-review.md)。

## Q2 Timer/QObject

新增 `cordis/qt/object.hpp` 的 QObjectResource 和 `cordis/qt/clock.hpp` 的 QtClock，归入现有 Cordis::Qt 目标。实现只依赖 QtCore；资源测试额外链接 Widgets 并使用 offscreen QApplication，无可视化桌面交互验收。无 Qt 内核构建保持独立。

QObjectResource 在 owner 接管 parent 为空的 `unique_ptr<QObject>&`，成功后清空调用方指针，失败保留原所有权。close 返回完整析构屏障：deleteLater 等 Qt 批准删除时机，独立守卫接管 DeferredDelete 并执行完整 delete，返回后才完成 Task。destroyed 只记 observed；子对象析构与嵌套 processEvents 期间不得提前完成。外部销毁或守卫提前消失报告 CleanupFailed；接管后禁止外部删除、迁移、重设 parent 或拦截延迟删除。

QtClock 实现 IClock，接受 0..INT_MAX 毫秒单次任务；零延迟也排队。native timeout 只投递宿主记录，业务回调由 QtScheduler 执行。Token 可跨线程取消，但取消不代表载荷已经释放。close 同步关门/停表/丢弃，异步等待原生 QObject 树和当前回调捕获析构完成；snapshot 区分 pending、running、执行/丢弃/取消数量、回调错误及 nativeDestroyed。已到期排队项可在 close 时释放业务闭包，残留通知仅持宿主记录。完整契约见 [ADR-0005](adr/0005-qt-resource-barriers.md)。

以下注册方式让资源屏障进入已有 Context/Effect 清理链，关闭前保持 Qt 循环与 scheduler 运行：

```cpp
std::unique_ptr<QObject> owned = std::make_unique<QObject>();
auto object = std::make_shared<cordis::qt::QObjectResource>(owned, scheduler);
auto clock = std::make_shared<cordis::qt::QtClock>(scheduler);
context.onCleanup([object](cordis::CleanupContext) { return object->close(); });
context.onCleanup([clock](cordis::CleanupContext) { return clock->close(); });
```

示意省略已有上下文的错误处理；真实闭环与检查见 `p3.qt_resources_kernel_shutdown`。正常退出先等待 Kernel.close（含上述资源清理），再关闭 scheduler，最后 Qt quit；丢弃 facade 或退出兜底不替代这一顺序。

`tests/qt/resources.cpp` 提交前审查后共 14 个独立 CTest，前缀为 p3.qt_resources_：

| 后缀 | 覆盖行为 |
|---|---|
| object_barrier | destroyed/完整析构分离，子对象析构重入仍持屏障 |
| object_guards | parent 冲突、错误线程、失败保留所有权、重复关闭 |
| object_external | 非受控外部删除得到 CleanupFailed |
| widget_barrier | QWidget 与子对象实际销毁确认 |
| clock_fire | 真实单次计时、零延迟排队、owner 回调、observeFor、范围校验 |
| clock_cancel | worker 取消、载荷保留至回收、owner 析构及 native 关闭 |
| clock_close | run/discarded 异常隔离、丢弃重入、幂等和拒绝新计时 |
| clock_inflight | native 先删除也须等待当前 run 和捕获析构 |
| clock_scheduler_closed | 提前关闭 scheduler 后的到期丢弃与 native 删除 |
| clock_queued | 已到期排队项取消/关闭，释放业务载荷并安全处理残留通知 |
| kernel_shutdown | Context 资源清理接入，Kernel 只在完整屏障后静止 |
| dropped_handle | worker 释放 facade 后资源由 Qt owner 管理，退出丢弃残留计时 |
| close_releases_handles | 丢弃回调/子对象析构释放最后一个 facade 后，整批通知与完整删除屏障仍完成 |
| application_quit | aboutToQuit 与 exec 收尾后的实际资源核对 |

| Q2 验证 | 结果 |
|---|---|
| Q2 初次 Qt/MSVC Debug、Release | 各 139/139：此前 126 项及新增 13 项资源场景 |
| 提交前 Qt/MSVC Debug、Release | 各 140/140，资源场景增至 14 项 |
| 提交前无 Qt GCC Debug、Release | 各 115/115，Qt 适配目标未进入构建 |
| MSVC AddressSanitizer 专项 | 14/14 资源场景，第一方 core/qt 与测试插桩，Qt 预编译库未插桩 |
| 两份新公共头独立包含 | 仅 SDK include，GCC C++17 语法检查通过 |

日志为 `build/p3-q2-qt-msvc-debug.log`、`build/p3-q2-qt-msvc-release.log`、`build/p3-q2-core-gcc-debug.log`。未执行 connection 排空、worker join、watcher、Ela 交互、ABI 或物理卸载验收。

### Q2 提交前审查

修正 discarded 回调内断言被预期异常掩盖的问题，将观察值移到回调外检查；waitTask 使用共享通知标志，避免测试超时后排队通知访问已结束的栈。补充最后一个资源句柄在清理中释放的场景，整理公共所有权契约和标准头依赖。未发现阻止提交的功能性问题；详细范围见 [审核记录](code-review.md)。

最新日志为 `build/review-q2-qt-msvc-<mode>.log`、`build/review-q2-core-gcc-<mode>.log` 和 `build/review-q2-asan.log`。ASan 使用独立构建目录，不修改共享预设；先进入本页前述 MSVC x64 环境，并将 Qt bin 加入当前进程 PATH：

```powershell
cmake --preset qt-msvc-release -B build/review-q2-asan `
  '-DCORDIS_MSVC_SHOWINCLUDES_PREFIX=注意: 包含文件:' `
  '-DCMAKE_BUILD_TYPE=RelWithDebInfo' `
  '-DCMAKE_CXX_FLAGS=/DWIN32 /D_WINDOWS /GR /EHsc /fsanitize=address /Zi' `
  '-DCMAKE_CXX_FLAGS_RELWITHDEBINFO=/O1 /Zi /DNDEBUG'
cmake --build build/review-q2-asan --target cordis_qt_resource_tests --parallel 2
ctest --test-dir build/review-q2-asan -L resources --output-on-failure
```

Qt 预编译库未插桩；本检查不构成全程序内存安全、泄漏、任意等待环或模块卸载证明。

## Q3a Ela 页面贡献

新增 Cordis::Ui、UiPagesService、PageHandle 与宿主 ElaShell；页面贡献通过 ui.pages/v1、Context.effect 和现有服务依赖连接。id/分类按 InstanceId/Epoch 隔离，工厂只创建一次，重复 id 不执行工厂。旧 Context/旧 PageHandle、错误线程与跨 scheduler 请求都拒绝。停止实例自动撤销页面，服务提供者重新发布后消费者以新 epoch 重建页面。

Ela 固定提交、归档 hash 与受控构建入口见 [ADR-0006](adr/0006-ela-page-contributions.md)。首次 Ela 配置下载约 108 MiB 的固定归档，缓存于 build/deps；不依赖 research 目录参与产品构建。Qt 与核心预设不下载/构建 Ela，未改动全局 PATH、上游顶层配置或只读参考代码。生成的 bin 目录包含对应 Debug/Release Ela DLL 与 MIT 许可证。

实际集成曾复现原生 Ela navigation 延迟滚动使用已释放模型节点；ASan 已定位。当前采用 ElaWindow 内的宿主导航和页面堆栈，不使用其原生动态导航。页面清理经过 Q2 完整对象树删除，贡献记录保留到 effect 关闭；输入门禁阻止停用后、异步删除前的用户按钮操作。该选择与未覆盖资源边界详见 ADR。

在前述 MSVC x64 环境中运行：

```powershell
$env:CORDIS_QT5152_ROOT = 'E:/Qt/5.15.2/msvc2019_64'
cmake --preset ela-msvc-debug '-DCORDIS_MSVC_SHOWINCLUDES_PREFIX=注意: 包含文件:'
cmake --preset ela-msvc-release '-DCORDIS_MSVC_SHOWINCLUDES_PREFIX=注意: 包含文件:'
./tools/dev.ps1 -Action verify -Preset ela-msvc-debug
./tools/dev.ps1 -Action verify -Preset ela-msvc-release
$env:PATH = "$env:CORDIS_QT5152_ROOT/bin;$env:PATH"
./build/ela-msvc-debug/bin/cordis_ela_demo.exe
```

交互示例显示真实 Ela 窗框及内置插件页面，关闭窗口通过 Kernel、页面、外壳及 scheduler 的完整异步关闭链。`--smoke` 自动导航并通过 QWidget::close 退出；CTest 使用 offscreen，预览保存在 `build/ela-msvc-<mode>/bin/ela-smoke.png`。Windows offscreen 预览显式读取系统 msyh.ttc 字体，不打包或修改该字体。

| 新增测试 | 覆盖 |
|---|---|
| p3.ui_pages_lifetime | 当前/非当前页、分类子页、重复 id、工厂保留与完整删除 |
| p3.ui_pages_namespaces | 同名实例/分类隔离、停止一个实例不影响另一个当前页 |
| p3.ui_pages_recovery | ui.pages 提供者退出、消费者 Pending、新绑定恢复到新 epoch |
| p3.ui_pages_factories | 工厂抛错与空返回的 effect 回滚 |
| p3.ui_pages_reentrant | 工厂中重入 shell.close，未返回的 effect 仍进入关闭屏障 |
| p3.ui_pages_guards | 错误线程、Context.identity 与跨 scheduler 门禁 |
| p3.ui_pages_shutdown | 手动关闭外壳等待页面，禁止重新贡献或导航 |
| p3.ui_pages_input_gate | 停用后、页面尚未删除时拒绝真实鼠标输入 |
| p3.ela_demo | 实际窗口创建、页面导航、截图和原生关闭事件链 |

| Q3a 验证 | 结果 |
|---|---|
| Q3a 初次 Ela/MSVC Debug、Release | 各 149/149，含原 140 项及 9 项 UI 场景 |
| 提交前 Ela/MSVC Debug、Release | 各 151/151，新增键盘导航及快捷键门禁两项 |
| 提交前无 Qt GCC Debug、Release | 各 115/115，核心保持独立 |
| 提交前 MSVC ASan 专项 | 11/11 UI 场景，第一方与固定 Ela 库均插桩；Qt 预编译库未插桩 |
| 新公共头独立包含 | pages.hpp/ela_shell.hpp 仅 SDK include，GCC C++17 通过 |

日志：`build/p3-q3-ela-msvc-debug.log`、`build/p3-q3-ela-msvc-release.log`、`build/p3-q3-core-gcc-debug.log`、`build/p3-q3-asan-fixed.log`。原生导航失败证据单独保留为 `build/p3-q3-native-navigation-asan-failure.log`。ASan 构建沿用 Q2 的独立 flags，将预设改为 ela-msvc-release、目录改为 build/p3-q3-asan，构建 cordis_ui_pages_tests/cordis_ela_demo 后执行 `ctest --test-dir build/p3-q3-asan -L ela --output-on-failure`。

Q3a 不代表命令/通知服务、插件管理页、任意 queued connection、原生 Windows 人工交互或物理卸载验收完成。

### Q3a 提交前审查

修复单独关闭页面后、异步清理开始前仍可导航/操作的问题：关闭请求同步撤销业务门禁，错误线程请求不改变状态。快捷键事件沿 QObject parent 判断归属，中文输入法提交单独过滤；新增/增强回归均在修复前确认失败。导航改用选中行变化信号，方向键与鼠标共用门禁，关闭当前页后回到永久安全页。整理直接头依赖、重入保留、错误 causes 及注释，详见 [审核记录](code-review.md#p3-q3a-提交前审核)。

本轮日志：`build/review-q3-ela-msvc-<mode>.log`、`build/review-q3-core-gcc-<mode>.log`、`build/review-q3-asan.log`；失败证据为 `build/review-q3-before.log` 和 `build/review-q3-input-method-before.log`。ASan 沿用上述独立构建与 flags，未把预编译 Qt 或原生桌面交互记作已验证。

## Q3b1 命令与通知

新增 commands.hpp 的 CommandService/CommandHandle 与 notifications.hpp 的 NotificationService，均由 ElaShell 创建，并通过现有 ui.commands/v1、ui.notifications/v1 输出/依赖声明接入。外壳增加宿主命令列表及最近通知状态区；示例贡献“显示笔记提醒”，执行后导航到笔记页并显示纯文本通知。默认交互运行支持双击/回车，--smoke 会实际执行命令、检查通知、截图并完成退出。

命令 execute 始终排队，在进入谓词/回调前及谓词返回后检查 owner/epoch。手动关闭同步撤销门禁；排队未进入项跳过，已进入项等源 Task 完成且由 owner 处理后才退出。谓词求值也计入保留，防止嵌套 Qt 循环提前完成清理。贡献 effect 静止前必须释放所有回调捕获；初始投递拒绝返回错误，已接受调用推进遭阻断则保持 pending 并标记 deliveryBlocked。详情见 [ADR-0007](adr/0007-ui-commands-notifications.md)。

通知校验当次 Context，但只保留复制的文本/级别/OperationId/归属值。历史最多 100 条，每条最多 4096 字节，未知级别、空文本或过长文本拒绝且不占序号；QLabel 固定 PlainText。实例停止后仍可查历史，历史本身不延长插件生命周期，也不声称已实现持久化或完整通知管理页。

新增 tests/qt/ui_services.cpp，初次 11 项，提交前审查后共 13 个独立 CTest，前缀 p3.ui_services_：

| 后缀 | 行为 |
|---|---|
| namespaces | 实例命名空间、重复 id、手动关闭即时关门及旧代拒绝 |
| inflight | 两个异步 gate 分别完成、worker/owner 确认、闭包保留及停止等待最后一项 |
| queued_cancel | 已接受未进入项撤销后不执行谓词/回调，保留最终释放 |
| failures | 禁用、谓词/执行异常、计数收敛 |
| predicate_reentry | 谓词内关闭及嵌套 Qt 循环，捕获在返回前不得释放 |
| recovery | 提供者退役、消费者 Pending、重新发布后新 epoch，旧能力拒绝 |
| notifications | 纯文本、有界历史、操作归属、非法输入、历史不阻止实例退出 |
| guards | 错误线程、跨 scheduler、非法命令、错误线程关闭不改门禁 |
| shutdown | 外壳同时等待命令和页面；关闭中禁止新贡献和通知 |
| interaction | 真实键盘回车通过宿主列表执行，禁用/空消息错误都有通知 |
| blocked | 源结果回程投递受阻后保守 pending/保留及原子诊断 |
| registration_reentry | MSVC 回调移动中关闭宿主，重查门禁后拒绝且不新增 effect |
| row_reentry | Qt 模型插入中重入关闭/循环，setup 返回前保留闭包及窗口 |

| Q3b1 验证 | 结果 |
|---|---|
| 初次 Ela/MSVC Debug、Release | 各 162/162，原 151 项及新增 11 项 |
| 提交前 Ela/MSVC Debug、Release | 各 164/164，补充注册和 Qt 插入重入 |
| 提交前无 Qt GCC Debug、Release | 各 115/115，未引入 Qt/Ela 构建依赖 |
| 提交前 MSVC ASan | 24/24 UI 场景，含页面、命令/通知和更新后的演示；第一方与 Ela 插桩，Qt 预编译库未插桩 |
| 公开头独立包含 | commands.hpp、notifications.hpp、ela_shell.hpp 仅 SDK include，GCC C++17 通过 |

日志为 build/p3-q3b-ela-msvc-debug.log、build/p3-q3b-ela-msvc-release.log、build/p3-q3b-core-gcc-debug.log 和 build/p3-q3b-asan.log。ASan 沿用 build/p3-q3-asan 的独立配置，构建 cordis_ui_services_tests、cordis_ui_pages_tests、cordis_ela_demo 后执行 `ctest --test-dir build/p3-q3-asan -L ela --output-on-failure`。命令 blocked 场景故意提前关闭 scheduler，保留无法证明静止的账目；通过不代表强制释放或泄漏检查通过。

更新后的预览位于 build/ela-msvc-debug/bin/ela-smoke.png，已检查中文命令列表、笔记页和通知展示；CTest 仍为 offscreen，尚未完成原生 Windows 人工交互。完整插件管理页、Loader/Store、任意连接/worker/watcher、ABI/物理卸载尚未实现或验收。

### Q3b1 提交前审查

修复移动回调中关闭宿主后仍可注册的问题，接管后重查门禁，并保留 Context 值副本。列表项初始化纳入 effect setup，防止 Qt 插入信号重入时提前释放命令闭包。空错误消息提供宿主缺省文本，增强交互回归；异步调用测试改为两个 gate，验证停止等待最后一项。规范、所有权和边界核对见 [审核记录](code-review.md#p3-q3b1-提交前审核)。

本轮日志为 `build/review-q3b-ela-msvc-<mode>.log`、`build/review-q3b-core-gcc-<mode>.log`、`build/review-q3b-asan.log`；失败证据为 `build/review-q3b-before.log` 和 `build/review-q3b-row-before.log`。公开头独立包含与差异检查通过。

## Q3b2 基础管理页

新增 ManagementController 和 DiagnosticsService（diagnostics/v1）。外壳永久管理页不属于插件贡献，缺服务或插件失败不阻止进入。宿主通过 `shell.management()->attach(sharedKernel)` 绑定同 scheduler 内核；后端只弱持 Kernel，原宿主释放后读取返回错误，不延长内核生命周期或使用悬空引用。普通插件只获得只读诊断服务，快照内不含 Kernel、Instance、Qt 对象、Task 或回调。契约见 [ADR-0008](adr/0008-management-diagnostics.md)。

管理页展示顶层实例的名称/状态/修订/代次、精确 realm/minor/capability 依赖原因、effect 清理/保留、服务与 UI 调用账目、通知及管理操作历史。KernelSnapshot 增加值名称映射，Kernel.findInstance 只查本内核顶层实例，Instance.dependencies 复用激活预检但不提交租约。当前子实例只显示数量，未展开完整子树。

停止/重试在接受时锁定实例号并排队，进入前再检查内核和实例归属。停止等待真实 CleanupReport，清理失败保留错误及资源；重试观察新 OperationId 的 StableReport，保留配置修订，失败不能显示为激活成功。操作历史最多 100 条；实例从内核回收后结果仍可查看。停止是终结 InstanceId，配置启用/停用和重新挂载随 Loader/Store 接入。

管理页使用 owner 的 250 ms 定时刷新及手动刷新；刷新不执行命令谓词或插件服务。关闭先关门、停刷新，再等待当前界面写入栈退出，避免 Qt 模型信号中嵌套事件循环提前删除窗口。已选实例消失后保持无选择，后续刷新也不自动选中另一实例，避免连续点击停止其他条目。第三方内部导航命令的提示通过其公开开关关闭，平台业务通知保留，未改动第三方源码。

演示新增一个缺依赖实例和首次启动失败、可重试的实例；默认打开管理页，原笔记仍可从导航进入。`--smoke` 实际运行笔记命令、检查四个实例及 Pending/Failed 状态、截图再完成退出。预览为 build/ela-msvc-debug/bin/ela-smoke.png，已检查中文状态、按钮、依赖和四个详情标签。

| 新增 CTest | 覆盖 |
|---|---|
| p3.diagnostics_lookup | 无 Qt 名称值快照、归属/线程/回收、查询不启动实例 |
| p3.diagnostics_dependencies | 精确 realm/minor/capability、诊断不增加 committed 保留 |
| p3.management_pending | 缺依赖不阻止管理页、错误原因与提供者发布后恢复 |
| p3.management_diagnostics | 普通消费者读取 diagnostics/v1、纯文本值快照与 UI 账目 |
| p3.management_retry | 真实按钮重试、防重复、新操作号/epoch、真实失败结果 |
| p3.management_stop | worker 清理屏障、实例回收后操作记录、无选择时连续点击不停止其他实例 |
| p3.management_deselect | 取消选择后当前单元格仍存在，按钮关闭且刷新不恢复旧选择 |
| p3.management_guards | 错误线程、跨 scheduler/内核、未知实例、关闭期间拒绝 |
| p3.management_queued_close | 关闭后跳过已排队重试，晚到结果不访问原生控件 |
| p3.management_expired_kernel | Kernel 弱生命周期、清除界面、禁止重绑定旧控制器 |
| p3.management_view_reentry | 模型插入信号里关闭和推进 Qt，当前刷新返回前窗口仍存活 |
| p3.management_navigation_reentry | 切页信号中关闭并处理 Qt 延迟删除，导航返回前窗口仍存活 |
| p3.management_cleanup_failure | 失败原因/causes、资源保留及禁止不安全重试 |
| p3.management_blocked | 管理派发遭丢弃时 pending/原子诊断，不伪报完成 |

| Q3b2 验证 | 结果 |
|---|---|
| 初次 Ela/MSVC Debug、Release | 各 176/176，原 164 项及新增 12 项 |
| 提交前 Ela/MSVC Debug、Release | 各 178/178，补充取消选择和导航关闭重入 |
| 无 Qt GCC Debug、Release | 各 117/117，新增两个 core 诊断场景 |
| 提交前 MSVC ASan | 36/36 UI 场景，初次 34 项及新增 2 项；第一方与固定 Ela 插桩，预编译 Qt 未插桩 |
| 公共头独立包含 | management.hpp、ela_shell.hpp、kernel.hpp 仅 SDK include，GCC C++17 通过 |

日志为 `build/p3-q3b2-ela-msvc-<mode>.log`、`build/p3-q3b2-core-gcc-<mode>.log` 和 `build/p3-q3b2-asan.log`。ASan 沿用 build/p3-q3-asan 独立配置，构建 cordis_management_tests、cordis_ui_services_tests、cordis_ui_pages_tests、cordis_ela_demo 后以 `ctest --test-dir build/p3-q3-asan -L ela --output-on-failure` 检查 UI；该标签不覆盖两个 core 诊断场景。清理失败和投递阻断测试按设计保留未静止资源，不构成泄漏或任意等待环证明。

本轮交付基础管理页，不等同于完整配置/保存/模块管理、系统安全模式或原生 Windows 人工交互验收。Q3 的桌面人工交互仍待验证，P3 整体保持进行中。本轮交付与审查随本次提交归档。

### Q3b2 提交前审查

复现并修复管理页切换信号中关闭、处理延迟删除导致的崩溃：程序入口与导航信号共用入口，窗口关闭等待完整导航栈返回，重入关门后停止改变选择。另修复取消选择后仍把 currentRow 当作已选行、保持按钮可用的问题，改用 selectionModel 解析实例号。两项回归在修复前分别 SegFault 和断言失败，修复后通过；规范和边界核对见 [审核记录](code-review.md#p3-q3b2-提交前审核)。

审查日志为 `build/review-q3b2-ela-msvc-<mode>.log`、`build/review-q3b2-core-gcc-<mode>.log`、`build/review-q3b2-asan.log`；失败证据为 `build/review-q3b2-before.log` 和 `build/review-q3b2-navigation-before.log`。三个公共头独立包含及差异检查通过。ASan、无 Qt 隔离和原生桌面人工交互的验证边界保持上述约定。

## Q3c Ela 组件、原生导航与工作台

Q3b2 完成的是管理功能与生命周期基线，本轮补齐 Ela 组件和主题，并按用户授权将组件库作为项目内维护的 UI 基础设施。third_party/ela 导入 aae58f5d0d98e05eabe10c263aa7da5f2491d702 的库及所需资源，约 4.5 MB；保留 MIT LICENSE 与 UPSTREAM.md，构建不再联网下载。架构决策见 [ADR-0010](adr/0010-maintained-ela-navigation.md)，[ADR-0009](adr/0009-ela-components-theme.md) 保留初次组件迁移记录。

组件使用 ElaTableView、ElaListView、ElaText、ElaPushButton、ElaPlainTextEdit、ElaTabWidget，笔记演示同样接入 Ela 文本/按钮。表格和命令使用宿主模型；导航改为真正的 ElaWindow 原生页面、展开分组和页脚节点，保留图标与紧凑/展开模式。普通插件继续通过贡献服务协作，Core 不引入 Qt/Ela，公开 SDK 不新增 Ela 类型。

布局参考官方示例的 Fluent 组件与 VS Code 的工作台分区：左侧原生 Ela 活动栏、资源管理器、中央页面页签/页面区、底部命令和输出面板、状态栏。页签及资源树以原生 key 同步，撤销时一同移除。Qt splitter 可调整侧栏宽度与面板高度；Ctrl+B/侧栏按钮控制资源树，Ctrl+J/面板按钮或左下终端按钮控制底部面板。输出只保留有界纯文本，不伪造系统终端或工作区持久化能力。

移除固定浅色导航背景，处理稳定 objectName 对 Ela 默认样式选择器的影响，详情容器调色板跟随 ElaTheme，检查最终合成背景。诊断和中央页面标签禁止拖出独立窗口。浅/深预览为 build/ela-msvc-debug/bin/ela-smoke.png 与 ela-smoke-dark.png，已查看 offscreen 图像；这不等于原生桌面人工验收。

本地修正原生导航生命周期：延迟滚动使用持久索引并检查有效性；节点删除清除选择，动画借用使用 QPointer；导航视图拥有并显式删除样式，动画由样式/delegate 拥有，防止窗口删除后继续更新旧 viewport；通用 navigationGuard 在切页前接入宿主生命周期。Debug 调用栈实际定位到旧动画 valueChanged 回调，见 build/q3-workbench-trace.log；临时诊断代码已移出产品构建。页面仍由 QObjectResource 确认完整析构，原生 removeNavigationNode 不能代替清理屏障。路由回退和独立页面窗口继续关闭。

新增/增强检查：

| CTest | 覆盖 |
|---|---|
| p3.management_theme | 实际 Ela 组件、明暗颜色/合成背景、选择和 epoch 保持，固定标签不能拖出 |
| p3.management_workbench | 中央页签按需打开与同步、面板隐藏恢复、纯文本输出和撤页后的页签同步删除 |
| p3.ela_demo_dark | 深色状态检查、截图及完整退出；原 p3.ela_demo 检查浅色 |
| p3.ui_pages_deferred_removal | 十二轮导航后立即停止并删除页面，窗口存活时经过原生延迟滚动触发 |
| p3.ui_pages_native_modes | 原生模式切换、页面归属及直接调用原生导航不能绕过撤销门禁 |
| p3.ui_pages_animation_cleanup | 根窗口关闭后原生导航样式和动画实际销毁 |

| Q3c 开发验证（提交前审查之前） | 结果 |
|---|---|
| Ela/MSVC Debug、Release | 各 184/184 |
| 无 Qt GCC Debug、Release | 各 117/117，保持构建隔离 |
| MSVC ASan | 42/42 UI 场景；第一方及项目内 Ela 插桩，预编译 Qt 未插桩 |
| 差异检查 | git diff --check 通过 |

日志：build/q3-workbench-ela-msvc-debug.log、build/q3-workbench-ela-msvc-release.log、build/q3-workbench-core-gcc-debug.log、build/q3-workbench-core-gcc-release.log 和 build/q3-workbench-asan.log。原生导航初次接回仍出现崩溃，修正动画归属后通过；较早的临时构建混用修改前后的内部类布局，不能将那些堆布局错误误记为独立内核问题。

原生 Windows 的缩放、主题动画及完整鼠标/键盘操作仍待按 [桌面验收单](../tests/acceptance/p3-ui.md) 执行。清理失败/投递阻断按契约保留未静止资源，ASan 不构成泄漏或物理卸载证明。

### Q3c 提交前审查

审查修复四项问题：紧凑菜单残留并捕获旧节点、恢复导航模式时访问已删除分组、原生导航信号重入关闭后的悬空访问，以及输出信号重入提交导致通知返回序号错误。菜单和展开缓存改用稳定 key，撤页同步释放动作/空菜单；原生信号返回后检查 QPointer 与模型归属，后续滚动只用有效持久索引；通知返回本次提交时保存的序号。菜单创建逻辑集中复用，选择同步控制流与直接包含依赖同时整理。

新增 p3.ui_pages_compact_removal、p3.ui_pages_removed_expanded_group、p3.ui_pages_native_close_reentry，增强既有 p3.ui_services_notifications。两项 UAF 均由修复前 ASan 实际复现，失败日志与审查细节见 [代码审查记录](code-review.md#p3-q3c-提交前审核)。

审查后 Ela/MSVC Debug、Release 各 187/187，无 Qt GCC Debug、Release 各 117/117；MSVC ASan 的 45/45 UI 场景通过。日志为 build/review-q3c-ela-msvc-<mode>.log、build/review-q3c-core-gcc-<mode>.log 和 build/review-q3c-asan.log。人工桌面验收和 ASan 的验证边界保持上述约定。

## Q4a 配置结构候选与差异预览

本轮在 Q3c 人工验收待办期间，先完成与 UI/运行树无副作用耦合的 Loader 前置工作；未改写桌面验收结果。新增 Cordis::Loader 独立静态库、cordis/loader/configuration.hpp 和七项无 Qt 测试，不改变 Core/Qt/UI 实现。决策和精确语义见 [ADR-0011](adr/0011-configuration-preview.md)。

validateDocument 校验格式版本、标识、模块引用、兄弟 id 唯一性、inject 和资源预算，以显式栈生成按 EntryPath 排序的值候选。父禁用通过 effectiveDisabled 级联，不覆盖子条目的配置开关。依赖按 name/major 排序、capability 去重。DocumentCandidate 的构造被封闭，复制值不共享输入的可变节点。

previewChanges 区分 Add/Remove/Disable/Restart/Recreate/NoOp；普通 config 变化为 Restart，定义、manifest、realm、inject 变化为 Recreate，父重启要求启用后代重建。目标变化标记独立于运行意图，未使用模块和禁用条目的修改不被吞掉。输出按 EntryPath 稳定排序，保留调用方基线修订；不执行操作、不修改实例或修订，也不冒充实际运行差异。

该候选还不是完整 ValidatedCandidate；YAML 解析、插件是否存在/schema、路径/产物校验、依赖收紧校验、执行队列、CAS、Superseded 和 Store 均未实现。没有为这些待办导出占位方法。

| CTest | 覆盖 |
|---|---|
| p3.loader_structure | 版本、路径标识、模块引用、重复兄弟 id 及不同父节点同名 |
| p3.loader_disabled | 父禁用级联、子自身开关、重新启用及候选值隔离 |
| p3.loader_limits | 条目、深度、字节、模块、inject 和 capability 预算边界 |
| p3.loader_normalization | 依赖排列及 capability 去重不造成重建，无效声明拒绝 |
| p3.loader_actions | 六类动作区分、稳定顺序、基线修订及输入不变 |
| p3.loader_subtree | 祖先重启带来的后代重建，排除相似名称前缀的其他根 |
| p3.loader_definitions | manifest/realm/inject 变化，未使用模块的目标变化 |

| Q4a 验证 | 结果 |
|---|---|
| Ela/MSVC Debug、Release | 各 194/194，含 7 项 Loader 新场景及既有回归 |
| 无 Qt GCC Debug、Release | 各 124/124，Loader 目标无需 Core/Qt/Ela 链接 |
| MSVC ASan | 7/7 Loader 场景，库和测试均插桩；本轮未重复执行既有 UI ASan |
| 差异检查 | git diff --check 通过；新公共头在两个编译器中首先单独包含 |

日志：build/p3-q4a-ela-msvc-<mode>.log、build/p3-q4a-core-gcc-<mode>.log、build/p3-q4a-asan.log。ASan 复用 build/p3-q3-asan 配置，构建 cordis_loader_tests 后运行 `ctest --test-dir build/p3-q3-asan -L loader --output-on-failure`。仅新增结构校验和纯预览；无文件读写或计划应用，不构成完整 L01–L04/S05 验收。Q4a 与后续 Q4b/Q4c1 一并审查归档，见下文。

## Q4b 受限 YAML 与内置定义校验

新增 Cordis::ConfigYaml、cordis/loader/yaml.hpp，解析层私有链接仓库内固定 LibYAML 0.2.5，基础 Cordis::Loader 和 Core 均不依赖它。根工程及预设启用匹配的 C/C++ 编译器，库实现和 MIT 许可保持原文；来源/hash、接口核对及限制见 [ADR-0012](adr/0012-restricted-yaml-validation.md) 和 third_party/libyaml/UPSTREAM.md。

parseConfiguration 使用事件接口构建有界宿主树，不展开别名。单份 UTF-8 文档必须显式填写 formatVersion 和 plugins；拒绝 tag、anchor、alias、merge、重复键、非字符串键、多文档及未知结构字段。disabled 严格布尔，结构整数严格 uint32，条目限额继续复用 Q4a；错误带阶段、行列和可定位的 EntryPath。config 转为按键排序的 JSON 字符串，缺省 {}；normalizedYaml 可往返解析，不保留注释/排版。移动后的来源失效，不能误作为清空目标的新候选。

validateCandidate 检查内置插件定义唯一性、schema、插件归属以及所有条目（含禁用项）的负载；完整定义预检先于 schema 调用。schema 返回错误保留原始原因，抛错转为配置错误。有效依赖按定义与用户声明联合，minor 取最大值、capability 取并集；结果不持有校验回调。外部 modules 声明目前一律拒绝，完整路径、产物与 ABI 验证随 P4 接入；不是把结构可解析当作模块可用。

可运行的端到端示例（无文件写入和实例动作）：

```powershell
./build/core-gcc-debug/examples/builtin/cordis_configuration_demo.exe `
    examples/configuration/base.yaml examples/configuration/candidate.yaml
```

已实际输出 added:Add、obsolete:Remove、paused:Add、profile:Recreate、profile/detail:Recreate；profile 同时改变配置及附加依赖，因此按重建显示。把候选替换为 examples/configuration/rejected.yaml，会带来源行列和 demo.profile 的枚举 schema 原因退出 1。负例 CTest 同时核对退出码与指定 schema 诊断，缺文件或其他错误不能冒充成功。

新增八项 p3.config_yaml_* 单测：parsing、mappings、restricted、types、limits、roundtrip、schemas、dependencies，另有 p3.configuration_demo / p3.configuration_rejected 两项文件到预览集成检查。含不合法 UTF-8、NUL、预算边界、往返/负载类型保持、schema 异常、未知定义先拒绝、定义捕获释放和依赖不可削弱。

最终验证与日志在下表；未把已有 UI ASan 重复计入本轮解析层结果。

| Q4b 验证 | 结果 |
|---|---|
| Ela/MSVC Debug、Release | 各 204/204，含 10 项新增 YAML/文件示例场景 |
| 无 Qt GCC Debug、Release | 各 134/134 |
| MSVC ASan | 17/17 Loader 场景；第一方结构/解析/定义校验及 LibYAML C 库均插桩 |
| 格式与来源 | 第一方差异检查、固定源码一致性及新头独立包含通过 |

日志为 build/p3-q4b-ela-msvc-<mode>.log、build/p3-q4b-core-gcc-<mode>.log、build/p3-q4b-asan.log。ASan 复用 build/p3-q3-asan；新启用的 C 编译器使用 /fsanitize=address /Zi、RelWithDebInfo /O1，C++ 插桩配置保持原值；构建 cordis_loader_tests、cordis_config_yaml_tests、cordis_configuration_demo，再运行 `ctest --test-dir build/p3-q3-asan -L loader --output-on-failure`。本轮不包含泄漏或物理卸载证明。

Q4b 交付范围不含目标 CAS、运行协调、Superseded、持久化、watcher 或配置 UI；后续 Q4c1 补齐通用协调协议，见下文。上述文本校验不构成完整 L01–L04/S05 验收。

## Q4c1 目标接受与串行协调基础

新增 Cordis::ConfigCoordinator 和 cordis/loader/coordinator.hpp。accept 验证有效候选及 expectedDesiredRevision，预检返回后重查 CAS/关闭门禁；接受成功发布完整目标与独立 settled Task。初始修订为 0，成功接受递增；执行失败、Pending 或被替代不偷偷改回目标。驱动查询失败时，snapshot 仍提供 desired，运行错误单独放在 runningError，不误报运行列表为空即完全静止。

IReconcileDriver 提供 preflight、实际状态规划、动作执行、快照及停止协议。协调器不把 PlanPreview 直接当作执行计划：A 当前动作未结束时接受 B/C，B 未开始即 Superseded；A 等真实检查点保留已应用部分，其余项 Superseded，随后从实际部分状态重新为 C 规划。至多一个在途动作，失败默认跳过本计划后续项；不安全检查点、异常 Task 或非法结果会停止新执行入口。

close 同步关门，优先广播驱动停止，不等待未完成动作先稳定；同时等关闭证据和在途回执完成。同步驱动调用期间，嵌套泵送不能提前进入下一次执行/关闭。跨线程投递故障只改原子门禁和 Promise，向尚未完成的登记目标发布失败；成功与失败并发终结遵循 Task 一次仲裁。未知静止状态保守保留资源控制块。完整边界见 [ADR-0013](adr/0013-configuration-coordinator.md)。

本轮仅完成通用协调基础，尚未提供 Kernel 运行树驱动。测试的 ControlledDriver 通过显式 gate 维护已完成的实际值，用于证明串行、替代和关闭协议；不是产品默认驱动，不证明真实服务依赖排序、父子树运行及 UI 接入。驱动预检必须拒绝自身未实现的能力。

| CTest | 覆盖 |
|---|---|
| p3.coordinator_cas | 接受后才排队、修订冲突、移动后候选、能力拒绝不改目标 |
| p3.coordinator_serial | 单在途动作、实际值与目标分离、NoOp 不改代次 |
| p3.coordinator_latest | A/B/C gate、旧报告保留部分成功、最新目标从实际部分重建 |
| p3.coordinator_failure | 默认遇错停后项、保留目标和已生效项、再次接受后从实际状态重试 |
| p3.coordinator_pending | 无关项继续运行、Pending 历史任务不随资源恢复改写 |
| p3.coordinator_shutdown | 关闭优先广播、终结在途和待执行目标、同步重入等待调用栈退出 |
| p3.coordinator_unsafe | 未知静止时拒绝新目标、失败关闭后释放 facade 仍保留驱动，成功关闭不留所有权环 |
| p3.coordinator_delivery | worker 完成/预检期间 scheduler 关闭，所有已接受任务终结失败 |
| p3.coordinator_reentry | 预检中接受/关闭重查门禁，规划中替代不应用旧目标 |
| p3.coordinator_guards | 非法计划/结果、驱动异常、错误线程及运行查询失败仍可读取目标 |

| Q4c1 验证 | 结果 |
|---|---|
| Ela/MSVC Debug、Release | 各 214/214，含新增 10 项协调协议及既有回归 |
| 无 Qt GCC Debug、Release | 各 144/144 |
| MSVC ASan | 27/27 Loader 场景，含协调协议 10 项，第一方及 LibYAML 按既有配置插桩 |
| 代码检查 | git diff --check；新公共头在测试中首先独立包含，两个编译器均通过 |

日志：build/p3-q4c-ela-msvc-<mode>.log、build/p3-q4c-core-gcc-<mode>.log、build/p3-q4c-asan.log。ASan 在 build/p3-q3-asan 构建 cordis_config_coordinator_tests 及既有 Loader 测试/文件示例，使用 `ctest --test-dir build/p3-q3-asan -L loader --output-on-failure`。不静止/投递阻断场景按契约保留控制块和驱动；不构成泄漏、真实 Kernel 协调或物理卸载证明。

Q4c 仍有 Kernel 适配和真实依赖/子树执行待完成，不据此登记完整 L01–L04/S05 通过。

### Q4a/Q4b/Q4c1 提交前审查

检查结构/解析预算、严格字段、schema 生命周期、有效依赖、CAS、串行动作、部分失败与关闭/投递路径。修复规划期间 scheduler 关闭后仍启动 apply 的缺陷：同步驱动返回、规划失败报告发布前及下一步执行前重查投递能力，关闭后统一阻断并终结等待任务。新增 p3.coordinator_planning_gate 覆盖规划正常返回、抛错和重入 coordinator.close 三种关闭路径；修复前真实失败，见 build/review-q4-before.log。规范方面修正公共注释，集中投递检查；核对第一方格式、公共头、构建边界和原始第三方许可。详见 [审查记录](code-review.md#p3-q4aq4bq4c1-提交前审核)。

| 审查后验证 | 结果 |
|---|---|
| Ela/MSVC Debug、Release | 各 215/215 |
| 无 Qt GCC Debug、Release | 各 145/145 |
| MSVC ASan | 28/28 Loader 场景，含 11 项协调器场景；第一方与 LibYAML C 库插桩 |
| 来源与格式 | 固定归档 SHA256 及 10 个上游源码文件一致，第一方暂存差异检查通过 |

日志：build/review-q4-ela-msvc-<mode>.log、build/review-q4-core-gcc-<mode>.log、build/review-q4-asan.log。本次将 Q4a/Q4b/Q4c1 一并提交；Core/Qt/UI/Ela 及只读参考未改动，原生桌面人工项继续待验。ASan 不构成泄漏、真实 Kernel 协调或 DLL 卸载证明。

## Q4d1 配置文件事务基础

Q3c 人工验收未完成，Q4c2 运行树/管理页接入门禁保持不变。本轮先完成独立、可用真实文件验证的文件事务；没有把文件测试代填为桌面验收。可选 Cordis::ConfigFileQt 仅依赖 ConfigYaml 和 Qt5::Core，不改变 Core/Qt/UI/Ela 的实现，不在无 Qt 构建中启用。接口与边界见 [ADR-0014](adr/0014-configuration-file-transactions.md)。

readConfigurationFile 有界读取原始字节及 SHA256，缺失和空文件分别建模；基线绑定规范绝对路径。文件预算默认 8 MiB，独立于文本解析预算；拒绝非普通文件、最终链接、无效或歧义路径。写入要求 ValidatedCandidate，持协作锁先比较原始 hash，临时文件写入后再比较一次，最后通过禁止直接覆盖回退的 QSaveFile 提交。冲突返回外部原文及 ConfigConflict，目标仍由调用方保留；目标字节已存在时返回 Unchanged，不重写配置文件。打开/写入/提交等 I/O 故障使用新增的 ConfigIo。

文件事务是同步低层接口，不运行事件循环，不维护 desiredRevision/persistedRevision、dirty、写队列或 watcher，也不接入实例动作。协作写入之间按同路径锁串行化；非协作编辑器在最后检查与重命名之间的竞争仍存在，不能宣称任意文件系统 CAS。

| CTest | 覆盖 |
|---|---|
| p3.config_file_read | 缺失/空文件、已知 SHA256、CRLF/NUL 原文、精确字节预算 |
| p3.config_file_commit | 首次创建、更新、规范文本可重新验证、无变化及陈旧版本 |
| p3.config_file_conflict | 外部仅改注释和无效 YAML，外部来源与输入目标均保留 |
| p3.config_file_existence | 外部新建空文件、删除、冲突后明确使用新基线创建 |
| p3.config_file_guards | 路径/基线/hash/预算/移动后目标，Windows 设备和保留名拒绝 |
| p3.config_file_io | 缺失父目录、目录冒充配置、不遗留锁 |
| p3.config_file_symlink | 最终符号链接/Windows 快捷方式拒绝，真实目标不被改写 |
| p3.config_file_lock | 同路径锁占用立即失败，原文件保留，释放后可重试 |
| p3.config_file_concurrent | 12 轮真实两线程竞争，每轮仅一个提交，另一方报告冲突 |
| p3.config_file_without_application | 无 QCoreApplication/scheduler/GUI 时真实读写 |
| p3.config_file_commit_failure | Windows 真实句柄禁止重命名，提交失败旧字节/hash 保持，释放后重试 |

文件场景仅使用 QTemporaryDir；正常及失败路径均检查无锁/临时文件残留，不写用户配置。公开头在测试中首先包含，源码与测试保持 /W4 /WX；避开 Qt 5.15 QList<QString> 比较触发的 MSVC STL 弃用扩展，不降低编译告警标准。

| Q4d1 验证 | 结果 |
|---|---|
| Ela/MSVC Debug、Release | 各 226/226；Windows 路径细化后 Debug 文件场景复验 11/11 |
| 无 Qt GCC Debug、Release | 各 145/145；编译命令无 Qt/Ela/文件适配目标依赖 |
| MSVC ASan | 39/39 Loader 场景，含本轮 11 项文件测试；第一方及 LibYAML C 库插桩，预编译 Qt 未插桩 |
| 格式及文档 | git diff --check、新文件 UTF-8/LF/行尾检查和修改文档本地链接检查通过 |

日志：build/p3-q4d1-ela-msvc-<mode>.log、build/p3-q4d1-core-gcc-<mode>.log、build/p3-q4d1-file-debug.log、build/p3-q4d1-asan.log。ASan 复用 build/p3-q3-asan，构建 cordis_config_file_tests 及既有 Loader 测试/示例，再运行 `ctest --test-dir build/p3-q3-asan -L loader --output-on-failure`；已核对新文件库和测试的编译命令含 /fsanitize=address。不静止/投递阻断的既有场景继续按契约保留资源，不构成泄漏、断电或物理卸载证明。

Q4d1 与后续 Q4d2 一并审查提交；本节保留文件层当时的实现范围，Store 状态/队列见下节，watcher 和运行树接入仍待完成。

## Q4d2 Store 状态、单写队列和退出刷新

新增无 Qt 的 Cordis::ConfigStore 和可选 Cordis::ConfigStoreQt。公共文件值移到 file.hpp，qt_file.hpp 继续包含它；新 Store 依赖 ConfigYaml/Core，Core 不反向引用 Loader。QtConfigurationWriter 复用真实 Q4d1 文件事务，构造不做 I/O；当前提交在 owner 调用线程同步执行，回执由 Store 排队接收，没有新增 worker 或假后台写入。设计及错误语义见 [ADR-0015](adr/0015-configuration-store.md)。

track 接收协调器已接受的共享不可变目标及修订，修订递增且同修订不可换对象。保存队列固定各次目标，在真正写入时使用上一份成功提交的磁盘基线；历史保存修订与当前目标分开更新，旧目标写完不会把新目标的 dirty 清除。同修订重复请求共享任务，默认最多 64 个在途/排队保存。失败停止后续项，保留目标与历史保存；磁盘冲突保留外部原文，只有空闲时经显式 rebase 决策才换基线。

close 同步关门、排空已接受保存，并在队列空闲后刷新尚未请求保存的最新 dirty 目标；失败会终结关闭失败，不无限重试。投递故障只跨线程终结 Promise，不从 worker 修改保存状态；未知进度保留控制块。facade 释放不会取消已请求保存，正常排空后解除自保留。整应用启动/退出及管理页保存尚未接入，不能把组件级关闭测试记为完整桌面退出验收。

| CTest | 覆盖 |
|---|---|
| p3.store_targets / serial | 目标修订、FIFO、重复请求共享、旧保存与新 dirty 分离、Unchanged |
| p3.store_limits | 已满队列拒绝新请求，退出最终刷新复用空闲槽位 |
| p3.store_failure / conflict | 失败停止/跳过、原基线重试、冲突原文与历史保存保留、显式 rebase |
| p3.store_shutdown / close_failure | 排空与最新目标刷新，拒绝新业务，失败关闭不重试或继续写 |
| p3.store_delivery / reentry | worker 投递故障、同步重入跟踪/保存/关闭，最多一次在途事务 |
| p3.store_guards / lifetime | 协议结果、后端抛错、错误线程、忙时 rebase、scheduler 关闭及正常排空释放 |
| p3.store_unchanged_protocol | 审查新增：Unchanged 不能改变基线，非法回执保留 dirty、停止后续项及失败关闭 |
| p3.store_coordinator | 真实协调器的受控运行失败与目标已保存同时成立；不是 Kernel 驱动验收 |
| p3.store_qt_persist / conflict / failure | 真实文件保存、Unchanged、非法外部 YAML、真实协作锁失败及重试 |
| p3.store_qt_shutdown | 生产 QtScheduler 事件循环保持到最新目标落盘与回执完成 |

| Q4d2 开发验证（审查前） | 结果 |
|---|---|
| Ela/MSVC Debug、Release | 各 242/242，含本轮新增 16 项场景 |
| 无 Qt GCC Debug、Release | 各 157/157，含 12 项通用 Store 场景；编译命令无 Qt/Ela/文件适配依赖 |
| MSVC ASan | 55/55 Loader 场景；新 Store/Qt 后端及两组测试的 4 个编译单元均核对插桩，预编译 Qt 未插桩 |
| 代码与文档 | 第一方差异/新文件格式、公共头独立包含及本地文档链接检查 |

日志：build/p3-q4d2-core-gcc-<mode>.log、build/p3-q4d2-ela-msvc-<mode>.log、build/p3-q4d2-asan.log。定向记录为 build/p3-q4d2-store-debug.log、build/p3-q4d2-store-msvc-debug.log。ASan 在 build/p3-q3-asan 构建 cordis_config_store_tests、cordis_config_store_qt_tests 及既有 Loader 目标，运行 `ctest --test-dir build/p3-q3-asan -L loader --output-on-failure`。阻断场景按契约保守保留控制块，不构成泄漏、后台 worker 或物理卸载证明。

Q4d1/Q4d2 本次一并审查提交。真实 Kernel、watcher、后台 I/O、管理页保存入口与全应用退出装配继续待办，人工验收表未代填。

### Q4d1/Q4d2 提交前审查

发现通用 Store 接受与基线不一致的 Unchanged 回执，可能误推进保存修订并清除 dirty。新增 p3.store_unchanged_protocol，修复前真实失败，见 build/review-q4d-before.log；修复后要求完整版本及原文均与基线一致。非法回执停止后续保存、保留原历史与 dirty，关闭不能虚报成功。另统一文件大小查询错误分类、Qt 空目标错误阶段和直接包含依赖。详细范围见 [审查记录](code-review.md#p3-q4d1q4d2-提交前审核)。

审查后 Ela/MSVC Debug、Release 各 243/243，无 Qt GCC Debug、Release 各 158/158；Loader ASan 56/56，其中 17 项 Store 场景。日志为 build/review-q4d-ela-msvc-<mode>.log、build/review-q4d-core-gcc-<mode>.log、build/review-q4d-asan.log。公开头首包含、无 Qt 依赖隔离、第一方暂存格式和本地文档链接检查通过。Qt 预编译库未插桩，原有人工验收及资源保留边界保持不变。

## Q3c Example 交互与插件内部布局

按用户要求重新核对固定基线 aae58f5 的 ElaWidgetToolsExample/mainwindow.cpp、T_BasePage、T_Setting、T_Navigation，参考资料只读。复用 Ela 原生 MenuBar/Menu、SuggestBox、ToolButton、ScrollPage/ScrollPageArea、ComboBox、ToggleSwitch；标题栏放置菜单、前进/后退和页面搜索，中央提供可关闭/恢复的页签与欢迎/设置/关于页，底部提供命令/输出、隐藏、清空/复制输出，状态栏显示当前页。F5 刷新管理状态，Ctrl+P 搜索、方向键/回车打开、Escape 收起；Alt+←/→ 前进后退，Ctrl+W 关闭视图但保留页面状态。

用户明确资源管理器属于插件页内部，因此删除全局资源树和对应重复账目。全局只保留 Ela 插件导航，Ctrl+B 展开/收起它；树/列表等内容由插件根 QWidget 拥有。笔记演示以内部 ElaTreeView 切换临时笔记及使用说明，并提供 Ela 编辑器和计数。离开笔记页时内部树一起隐藏，关闭页签后重开仍保留内容；停止 notes 才销毁页面资源。未增加新公共 SDK 或 Core 依赖。

历史只记录每窗口最多 128 个稳定 key，支持前进分支截断；撤页同步裁剪历史及搜索建议。Cordis 禁用原生全局 Ela 命令记录，库默认行为保留。搜索建议增加键盘入口、撤销模型同步、QPointer 借用及动画所有权；重查取消旧收起动画，点击时先复制数据再通知，避免回调撤页/删除控件后的访问。搜索框创建时即处于根窗口下，保证建议弹层不被标题栏裁剪。Shell 在原生堆栈切换后同步最终选择，重入导航不覆盖最终历史。

新增 7 项 workbench 场景覆盖历史、页签状态、搜索/动态撤销、菜单/设置、窗口隔离、建议点击删除和重入导航。人工清单同步增加菜单、搜索、历史与插件内部布局项，仍为待验；工作区布局持久化、文件保存和完整 Example 控件展示馆不在本次范围。验证证据见本节后续记录。

本轮 Ela/MSVC Debug、Release 各 250/250，全 UI 标签 52 项 ASan 检查通过；新增测试及工作台、搜索模型/动画等修改单元均使用 ASan 插桩，Qt 预编译库未插桩。日志为 build/p3-example-workbench-ela-msvc-debug.log、build/p3-example-workbench-ela-msvc-release.log、build/p3-example-workbench-asan.log。已查看管理页和插件内部布局的浅/深色 offscreen 截图：各构建 bin/ela-smoke[-dark].png、ela-notes-smoke[-dark].png。原生 Windows 缩放、鼠标/键盘和主题体验仍按 [人工清单](../tests/acceptance/p3-ui.md) 验收，本轮未提交。

### 多行卡片高度修复

人工反馈欢迎、设置等页面出现文字被压扁和按钮重叠。原因是 ElaScrollPageArea 构造时 setFixedHeight(75)，官方 Example 将其用于单行设置项，而本轮页面把标题、说明和多个控件放入同一张卡片。第一方卡片解除固定高度，以布局的最小尺寸和换行高度确定大小；内容布局明确最小尺寸约束，窗口较小时使用滚动条。笔记插件的内容/使用说明卡片一并修复，保留组件原有主题及通用库默认值。

新增 p3.workbench_layout 和 layout_hidpi：使用真实中文字体检查欢迎/设置/关于的明暗主题、1200×800、1000×520、1880×900 窗口及 150% 缩放。逐项验证控件高度足以显示文字、控件位于卡片内、超出视口时存在滚动范围；明暗截图已查看。修复前回归实际失败于控件高度断言，见 build/ui-card-before.log。设置预览见 build/ela-msvc-debug/bin/CordisSettingsPage-light-100.png，高缩放图片后缀为 150。本次不代替原生桌面人工验收。

原 Release 曾因演示进程占用 DLL 报 LNK1104，临时使用独立目录验证。随后按用户要求回到原目录，以 ela-msvc-release 预设完成重新编译及验证。临时 build/ui-card-release 的递归删除被自动审批拒绝（工具仅返回 blocked by policy），目录仍保留，待手动清理。

修复后原目录 Debug/Release 均 252/252，通过包含 54 项 UI 回归的完整检查；新增两项覆盖布局及 150% 缩放。本次只调整第一方布局，不重复将既有 ASan 结果计为新验证。日志为 build/ui-card-ela-msvc-debug.log、build/ui-card-ela-msvc-release.log，定向布局记录为 build/ui-card-layout.log。Release 启动文件仍为 build/ela-msvc-release/bin/cordis_ela_demo.exe。

### 插件直达、空工作区与 Home 卡片中心

根据进一步交互要求，左侧分组由稳定键默认目标直接打开插件页签，右键保留子页菜单。默认目标随用户主动选择更新，关闭页签时自动回落到其他已打开页不会覆盖该选择；默认页撤销后换成同组现存页。分组标题取首个页面标题，图标使用插件图标；原生展开/紧凑模式和 epoch 门禁保持。未配置默认目标的 Ela 调用方继续原有展开/菜单行为。

注册页面不再提前创建页签，只在访问时打开。概览、管理、设置、关于和插件页签全部允许关闭；关闭视图仍保留对象、工厂和插件状态。最后一个页签关闭后中央显示不占导航节点的空画布，撤销未打开插件不会自动打开概览。空画布不记为伪页面，回退可以重开最近关闭的视图。新增通用空工作区入口同时清空导航选择及两处切页缓存，避免回退或重新点击同一页面时被旧缓存拦截。

参考固定基线 ElaWidgetToolsExample 的 T_Home，新增 HomePage：ElaImageCard 品牌背景、快捷入口、ElaFlowLayout 流式 ElaPopularCard 与悬浮详情。背景由原生 Qt 绘制，不引入研究目录资源或新增图片下载。首页按插件实例聚合卡片，合并只读管理快照和贡献目录；有界面插件打开最近主动选择的页面，无界面/待依赖/失败/已停止实例进入管理页。演示默认打开 Home，四个实例均有状态卡片，内容由贡献/诊断自动更新，插件无须获得宿主窗口。

动态卡片清理同时修正 ElaPopularCard 浮层所有权、动画 parent 和点击返回后的存活检查；浮层点击重入删除卡片也受保护。新增 workbench_empty、activation、cards 覆盖全页签关闭、空画布回退/重开、单击直达、右键子页、默认目标撤销、卡片聚合/最近目标、悬浮中停止插件以及卡片/浮层点击中删除。既有布局测试继续覆盖 Home 的宽窄窗口、主题及 150% 缩放。

原目录 Ela/MSVC Debug/Release 各 255/255，57 项 UI ASan 检查通过；新首页及卡片/浮层等修改单元使用 ASan，预编译 Qt 未插桩。日志为 build/home-ela-msvc-debug.log、build/home-ela-msvc-release.log、build/home-asan.log。已核对各构建 bin/ela-home-smoke.png、ela-home-smoke-dark.png 及 ela-empty-workbench.png；演示截图同时断言四个插件卡片均已登记。原生桌面人工项仍待执行，改动未提交；Release 沿用 build/ela-msvc-release/bin/cordis_ela_demo.exe。

按后续偏好，底部命令/输出工作区启动时默认收起；面板按钮、终端入口、Ctrl+J 和 Ctrl+Shift+P 继续可用，通知记录照常保留。设置页开关按面板真实显隐状态初始化，管理/菜单既有回归断言同步调整。原目录 Debug/Release 的 57 项 UI 检查各自通过，日志为 build/panel-default-debug.log、build/panel-default-release.log。

### 工作台与 Home 提交前审查

2026-10-25 审查并修复四项问题：首页刷新信号重入关闭后的控件访问、关闭页签时相邻目标失效、查询文本规范化/模型复位重入导致结果过时、卡片迁移后旧浮层区域失效。新增四项回归，前三项修复前 0/3 通过，首页刷新有 ASan 异常证据；第四项复现失效区域仍被返回。规范整理包括直接 include、语句/缩进、平面模型父索引契约及避免无变化的卡片布局/属性更新。详情见 [审核记录](code-review.md#p3-ela-工作台与-home-提交前审核)。

原目录 Ela/MSVC Debug/Release 各 259/259，无 Qt GCC Debug/Release 各 158/158，UI ASan 61/61 通过；日志为 build/review-workbench-ela-msvc-<mode>.log、build/review-workbench-core-gcc-<mode>.log、build/review-workbench-asan.log。本次将此前工作台/Home/布局/面板默认收起改动与审查修复一并提交；人工验收和后续 Loader/Store 装配状态保持不变。

## Q3c 人工桌面验收完成

2026-10-25 用户在本次会话明确回复“桌面人工验收完成”。按该确认登记 [Q3c 验收单](../tests/acceptance/p3-ui.md) 为通过，解除 Q4c2 真实运行驱动的前置条件。此记录来源于用户实际验收，未补造 Windows 版本、分辨率或缩放明细；之前各轮“人工待验”保留为历史状态。

## Q4c2a 顶层内置配置驱动

新增 `Cordis::ConfigKernel` 与 `KernelReconcileDriver`，将 Q4c1 通用协调协议接到真实 Kernel 的 mount/update/retry/stop 和稳定操作。按精确服务键/类型/版本/能力规划提供者先启动、消费者先移除；配置修改复用实例、realm/插件变化完整停止后重建。运行失败保留用户目标，缺依赖为 Pending，后续依赖恢复不改写历史结果。关闭只排空驱动拥有的实例，不接管共享 Kernel 中其他实例。

预检拒绝本轮未实现的显式 children 和动态 inject 契约，检查 realm 归属、配置内重复提供者及环。跨启用路径交换输出键要求显式移除阶段；清理失败或投递故障保留资源证据并阻断后续执行。新只读 Runtime 契约和 ServiceRegistry::ownsRealm 供适配检查，Core 保持无 Loader/Qt/Ela 依赖。协议与限制见 [ADR-0016](adr/0016-kernel-configuration-driver.md)。

新增 13 项真实 Kernel 单测及 `cordis_reconcile_demo` 冒烟示例。示例不依赖 Qt：先后接受服务值 1、2 两份 YAML，消费者随提供者配置重启恢复，然后等待全部实例静止退出。运行 `build/core-gcc-debug/examples/builtin/cordis_reconcile_demo.exe`；Ela 预设对应程序同样位于其现有构建目录的 examples/builtin。

验证结果（2026-10-25）：

| 检查 | 结果 |
|---|---|
| 原目录 Ela/MSVC Debug、Release | 各 273/273；新增 13 项真实 Kernel 场景及 1 项示例 |
| 无 Qt GCC Debug、Release | 各 172/172；公开驱动头 C++17 独立包含通过 |
| 最终边界补充 | Debug 两工具链各 14/14 定向回归；覆盖重建途中关闭及版本/能力拒绝 |
| MSVC ASan | Loader 70/70；新驱动/单测/示例及 Core 服务编译单元插桩已核对，预编译 Qt 未插桩 |
| 规范及边界 | UTF-8/LF、差异空白、本地文档链接及 Core/驱动无 Qt/Ela 依赖检查通过 |

日志为 `build/p3-q4c2-core-gcc-<mode>.log`、`build/p3-q4c2-ela-msvc-<mode>.log`、`build/p3-q4c2-final-<core|ela>-debug.log` 和 `build/p3-q4c2-asan.log`。Release 初次链接因正在运行的原目录演示程序占用 exe 失败；请求正常关闭后在同一目录完整重建并通过，无新增 Release 构建目录。最后补充的驱动/测试调整以 Debug 定向构建复核，Release 的完整回归包含最终版本。

ASan 验证内存访问，不证明阻断场景没有保守驻留或具备物理卸载能力。本轮未提交，不包含 UI 配置管理入口或全应用保存/退出装配。

## Home 状态卡片与电源操作

2026-10-25 按用户要求取消首页插件卡片的打开跳转及 PopularCard 打开浮层，改用 ElaScrollPageArea/ElaText 展示名称、实例号和当前状态。操作控件按后续偏好由电源图标换为 ElaToggleSwitch 左右滑动开关。卡片宽 280、最小高 144，流布局自动换行；长标题省略并保留完整提示，不再保存首页卡片的页面目标或最近子页账目。导航侧栏和首页顶部快捷入口继续使用原有页面入口。

每张卡片只有一个左右开关：右侧开启执行加载/失败重试，左侧关闭执行停止。宿主基于最新快照校验期望状态并使用管理请求去重；加载不切换页签。控制器缓存自己停止的实例，重新加载以最新配置及 realm 创建新身份，旧实例仍为 Disposed。停止缓存及操作历史分别限 100 条，清理未完成不能重挂载。生命周期与归属详见 [管理决策增补](adr/0008-management-diagnostics.md#home-电源操作增补2026-10-25)。

新增 diagnostics_remount、management_load 和 workbench_power，更新状态卡片、停止诊断、关闭重入及示例截图检查；覆盖配置/realm 保留、旧实例不复活、跨内核/错误线程/关闭拒绝、重复点击、新旧身份的在途门禁、排队加载期间关闭和失败重试。

原目录 Ela/MSVC Debug、Release 各 276/276，无 Qt GCC Debug、Release 各 173/173；UI ASan 63/63，加内核 remount 场景 1/1，通过。已核对新 UI/Core/测试单元的 ASan 插桩；预编译 Qt 未插桩。明暗截图 ela-home-smoke[-dark].png 已查看，布局回归含宽窄窗口及 150% 缩放。公开 Kernel/管理头独立包含、Core 无 Qt 依赖、UTF-8/LF、文档链接及差异空白检查通过。

日志为 build/home-power-ela-msvc-<mode>.log、build/home-power-core-gcc-<mode>.log、build/home-power-asan.log 和 build/home-power-core-asan.log；定向记录为 build/home-power-debug-tests.log。原 Release 启动文件仍为 build/ela-msvc-release/bin/cordis_ela_demo.exe。本轮未提交；此前人工验收记录保留，新增 Home 专项待人工复核。

左右开关后续验证：程序同步位置时屏蔽 toggled，刷新不产生管理请求；连续点击仅接受一次。原生开关信号排队处理，撤页后的旧信号忽略。关闭重入改由真实 EnabledChange 事件复核，保留窗口访问屏障。此次仅改变私有 UI 接口，原目录 MSVC Debug/Release 的管理/工作台/示例专项各 34/34，ASan 同范围 34/34 通过，含明暗及 150% 布局检查。已查看更新后的浅/深色 Home 截图；日志为 build/home-toggle-<debug|release|asan>-tests.log。不重复计作无 Qt 内核回归，Release 沿用原目录。

## Q4c2b 动态 inject 契约

本轮补齐配置中增加/收紧依赖的真实执行。Kernel/Instance.mount 可接收类型化附加依赖，按解析后的完整键联合固定要求，minor 取最大值、能力取并集，同键不同类型拒绝。完整契约属于实例，update/retry 及停止后 remount 保留；Runtime 定义不修改、不按配置注册变体。

KernelPlugin.configurableInject 由宿主声明允许按需追加的接口类型、原始域和最低要求。驱动复核 effectiveInject 不弱于固定 Runtime/接口目录，未知新增类型在接受前拒绝。真实规划包含追加边的类型、版本、能力及环；有效契约变化先完整停止再重建，冗余低要求不重复激活。记录实际接受的契约，旧实例清理失败不覆盖运行输入。协议见 [ADR-0017](adr/0017-instance-inject-contracts.md)。

新增 3 项 Core、6 项真实 Kernel 驱动场景，示例改为由 YAML counter/v1 inject 触发消费者预检。补充部分应用期间被最新目标替代的回归：旧高要求消费者与新低版本提供者短暂共存时，旧图只用于清理排序，兼容性由新目标严格检查；修复前失败证据为 build/q4c2b-replan-before.log。

| 检查 | 结果 |
|---|---|
| 原目录 Ela/MSVC Debug、Release 完整回归 | 各 285/285，新增 9 项场景 |
| 无 Qt GCC Debug、Release 完整回归 | 各 182/182，Core 保持无 Loader/Qt/Ela 依赖 |
| 最终重规划修正 | 四配置受影响的驱动/示例各 20/20 再次通过；未改动其余模块 |
| MSVC ASan | Loader 76/76，Core 服务契约及 remount 10/10；本轮修改单元插桩已核对 |
| 规范及边界 | Kernel/驱动公开头独立包含、UTF-8/LF、文档链接、差异空白及依赖隔离通过 |

完整日志为 build/p3-q4c2b-<core-gcc|ela-msvc>-<mode>.log，最终重规划复核为 build/q4c2b-final-<gcc|msvc>-<mode>.log，ASan 为 build/p3-q4c2b-asan-loader.log、build/p3-q4c2b-asan-core.log。Loader ASan 已包含最终修正；预编译 Qt 未插桩。所有构建沿用原有目录，本轮未提交。显式配置子树、管理页保存及全应用装配继续待办。

## Q4c2a/b 与 Home 提交前审查

2026-10-25 对 4f743ed 后全部未提交改动审查并修复三项问题：Core 内部任务被单次拒绝但调度器仍开放时，配置动作/关闭无法终结且快照掩盖阻断；管理加载在途时左右开关误显示关闭；Kernel.remount 读取已移动来源崩溃。修复前 0/3、修复后 3/3，详见 [审查记录](code-review.md#p3-kernel-配置驱动与-home-生命周期操作提交前审核)。源状态和操作权限分离，驱动在故障关闭时仍广播停止并保守保留，没有静止证明不释放资源。

审查后原目录 Ela/MSVC Debug/Release 各 287/287，无 Qt GCC Debug/Release 各 183/183；ASan UI/Loader 141/141、Core 10/10。日志为 build/review-kernel-home-<ela-msvc|core-gcc>-<mode>.log、build/review-kernel-home-asan.log、build/review-kernel-home-core-asan.log。公共头独立包含、Core 隔离、修改单元插桩、UTF-8/LF、文档链接和差异空白通过；Qt 预编译库未插桩。此前各节“本轮未提交”为当时记录，Q4c2a/b、Home 与本次审查修复一并随本次提交归档。Home 专项人工复核和其余 P3 工作保持待办。

## 后续工作

继续 Q4c2 的显式配置子树，再接管理页候选差异、配置拥有条目的启停、保存状态及 Store/驱动的应用装配。Q4d3 处理 watcher、重命名后恢复监听和自身写入 hash 消重，后台 I/O 随专用 worker 屏障接入。模块引用/驻留/卸载详情随 P4 接入；connection、worker 和 watcher 按对应集成资源补充专用屏障。此分解不表示完成了原设计中任何尚未运行的 L/Q/U 场景。
