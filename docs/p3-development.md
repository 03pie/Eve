# P3 桌面适配开发记录

日期：2026-10-24。状态：进行中；Q0–Q3c 自动化基线、Q4a/Q4b 配置校验和 Q4c1 目标接受/串行协调基础已实现；原生桌面人工验收待办，下一步 Kernel 驱动及 Store，其他专用资源按集成补齐。

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
| p3.management_workbench | 中央页签/资源树同步、面板隐藏恢复、纯文本输出和撤页后的模型同步删除 |
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

## 后续工作

完成 Q3c 原生 Windows 桌面人工验收，推进 Q4c2 Kernel 运行树驱动及真实依赖/子树协调，再接 Q4d Store 冲突/原子保存；管理页随之接入候选差异、条目启停和保存状态。模块引用/驻留/卸载详情随 P4 接入；connection、worker 和 watcher 按对应集成资源补充专用屏障。此分解不表示完成了原设计中任何尚未运行的 L/Q/U 场景。
