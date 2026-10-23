# P3 桌面适配开发记录

日期：2026-10-24。状态：进行中；Q0 工具链、Q1 QtScheduler、Q2 Timer/QObject 已完成；connection/worker/watcher、Ela UI 与 Loader 尚未实现。

## 已安装与验证

用户授权安装后，以 build/qt-tools 下的 Python 3.10 虚拟环境运行 aqtinstall 3.3.0，从 Qt 公共仓库安装 Qt 5.15.2 win64_msvc2019_64 的 qtbase 归档到 `E:/Qt/5.15.2/msvc2019_64`，包含 Core、Gui、Widgets、Test 与相应工具、Debug/Release 库。仅安装本阶段所需 qtbase，不声称安装了全部 Qt 模块或 Creator。原有 `E:/Qt/6.9.1` kit 保留。

```powershell
py -3.10 -m venv build/qt-tools
./build/qt-tools/Scripts/python.exe -m pip install aqtinstall==3.3.0
./build/qt-tools/Scripts/python.exe -m aqt install-qt windows desktop 5.15.2 win64_msvc2019_64 -O E:/Qt --archives qtbase
```

安装日志：`build/qt5152-install.log`。qmake 实测 5.15.2；CMake 实际解析 Core/Widgets/Test 均来自上述 kit。未修改机器全局 PATH；开发脚本为所选 Qt 命令临时添加 Qt bin，结束或失败后恢复调用方 PATH 和 CORDIS_QT5152_ROOT。

本轮使用 VS2022 Professional 的 MSVC 19.44 x64/v143，Debug /MDd、Release /MD；Qt 库来自 msvc2019_64 包。该组合已实际编译、链接、加载和运行探测。微软的二进制兼容约束允许支持版本的较新工具链链接较旧库，但仍要求合适的运行库和链接版本，/GL 等另有限制，不能推广成任意 Qt/插件组合都已验证。[微软兼容性说明](https://learn.microsoft.com/en-us/cpp/porting/binary-compat-2015-2017)

v142 未安装；当前选定并验证的是上述 v143 组合，未把它记为 v142 验收。Qt/Ela GUI 整体仍待实现和验证。

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

## 后续工作

Q3 接入 Ela UI 服务；Q4 推进 Loader/Store、配置计划与持久化。connection、worker 和 watcher 按对应集成资源补充专用屏障。此分解是 P3 的开发顺序，不表示完成了原设计中任何尚未运行的 L/Q/U 场景。
