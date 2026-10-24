# P3 桌面适配开发记录

日期：2026-10-24。状态：进行中；Q0–Q2、Q3a 页面贡献及 Q3b1 命令/通知已完成；Q3b2 管理页、connection/worker/watcher 与 Loader 继续推进。

## 已安装与验证

用户授权安装后，以 build/qt-tools 下的 Python 3.10 虚拟环境运行 aqtinstall 3.3.0，从 Qt 公共仓库安装 Qt 5.15.2 win64_msvc2019_64 的 qtbase 归档到 `E:/Qt/5.15.2/msvc2019_64`，包含 Core、Gui、Widgets、Test 与相应工具、Debug/Release 库。仅安装本阶段所需 qtbase，不声称安装了全部 Qt 模块或 Creator。原有 `E:/Qt/6.9.1` kit 保留。

```powershell
py -3.10 -m venv build/qt-tools
./build/qt-tools/Scripts/python.exe -m pip install aqtinstall==3.3.0
./build/qt-tools/Scripts/python.exe -m aqt install-qt windows desktop 5.15.2 win64_msvc2019_64 -O E:/Qt --archives qtbase
```

安装日志：`build/qt5152-install.log`。qmake 实测 5.15.2；CMake 实际解析 Core/Widgets/Test 均来自上述 kit。未修改机器全局 PATH；开发脚本为所选 Qt 命令临时添加 Qt bin，结束或失败后恢复调用方 PATH 和 CORDIS_QT5152_ROOT。

本轮使用 VS2022 Professional 的 MSVC 19.44 x64/v143，Debug /MDd、Release /MD；Qt 库来自 msvc2019_64 包。该组合已实际编译、链接、加载和运行探测。微软的二进制兼容约束允许支持版本的较新工具链链接较旧库，但仍要求合适的运行库和链接版本，/GL 等另有限制，不能推广成任意 Qt/插件组合都已验证。[微软兼容性说明](https://learn.microsoft.com/en-us/cpp/porting/binary-compat-2015-2017)

v142 未安装；当前选定并验证的是上述 v143 组合，未把它记为 v142 验收。Ela 页面贡献已通过下述 Q3a 检查，完整 UI 及原生桌面人工交互仍待验收。

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

## 后续工作

Q3b2 推进管理页并验证原生桌面交互；已存在的 Kernel/实例/服务/UI 账目先形成可用诊断，配置候选差异、保存状态及模块引用随 Q4/P4 接入。Q4 推进 Loader/Store、配置计划与持久化。connection、worker 和 watcher 按对应集成资源补充专用屏障。此分解是 P3 的开发顺序，不表示完成了原设计中任何尚未运行的 L/Q/U 场景。
