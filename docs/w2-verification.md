# W2 交付与验证记录

日期：2026-10-23。工作目录：`D:/01_C++/Eve`。状态：W2 工作单范围完成；下一工作包为 W3 实例与 Context。

## 实际交付

- `effect.hpp` / `effect.cpp`：owner 先登记、setup/cleanup 屏障、协作取消、重复 close 共享结果、组内逆序清理、子组收养及环检查。
- `clock.hpp`、`testing/fake_clock.hpp` / `fake_clock.cpp`：虚拟时间、同截止时间稳定顺序、取消、关闭、重入和步数预算。
- `observation.hpp`：超时只终结观察，底层任务继续保留；实际完成与截止竞争只产生一个观察结果。
- `examples/builtin/effect_demo.cpp`：异步清理超时后继续等待、最终释放的可运行示例。
- `tests/unit/w2.cpp`：33 个行为场景。此次在已有实现上增加 timeout_barriers、adopted_failure、clock_budget_errors，并修复步数耗尽丢失既有回调错误的问题。

## 验证矩阵

| 配置 | 配置、构建 | CTest |
|---|---|---|
| GCC 13.1 Debug | 通过 | 60/60 |
| GCC 13.1 Release | 通过 | 60/60 |
| MSVC 19.44 x64 Debug | 通过 | 60/60 |
| MSVC 19.44 x64 Release | 通过 | 60/60 |

每个配置包含原有 26 项检查、33 项 W2 行为测试及 1 项 Effect 示例；不是 240 个独立场景。保持 C++17、警告即错误，Release 测试不依赖 assert。另用 GCC 对 effect.hpp、clock.hpp、observation.hpp、testing/fake_clock.hpp 分别生成仅包含该头文件的翻译单元，独立语法检查通过。

日志位于 `build/w2-<preset>.log`，CTest 记录位于各构建目录 `Testing/Temporary/LastTest.log`。未运行 ASan/TSan、Qt 或 DLL 测试。

## 验收对应

| 契约或风险 | 自动化证据（w2. 前缀） |
|---|---|
| C01 / A04：owner 登记、重复关闭、逆序、句柄析构不关闭 | owner_once、reverse_order、dropped_token |
| C02/C03/S03 的 Effect 层：启动重入关闭、晚到资源回收、不发布 Live | reentrant_setup、async_setup_failure、timeout_barriers |
| C04 / A06：错误隔离汇总，挂起时保留资源 | cleanup_errors、retains_failed_closure、timeout_barriers |
| 子组屏障、环和失败传播 | adopted_group、adoption_cycle、adopted_failure |
| 清理期间拒绝新业务及析构重入 | cleanup_registration、async_registration_guard、cleanup_destructor_guard |
| 调度器关闭不伪报静止 | delivery_blocked、blocked_setup_delivery、blocked_cleanup_delivery |
| 虚拟时钟步数耗尽保留错误、允许继续推进 | clock_step_budget、clock_budget_errors |
| 完成、失败、关闭、超时和截止竞争 | observation_* |

A05 的完整 Instance.stop / epoch 验收留给 W3；W2 只验证其 EffectScope 底座，不将 scope.close 等同于实例停止。A01–A03、A07–A10 的实例、服务、事件部分仍待实现。

## 本机复现与限制

GCC：`./tools/dev.ps1 -Action verify -Preset core-gcc-debug`，Release 替换预设名。

MSVC 首次在默认用户临时目录下配置失败，编译器报 D8050。将 TEMP/TMP 设为仓库内临时目录后，四配置矩阵中的 MSVC 检查通过；仅确认此绕过有效，未确定错误根因。可在新的 PowerShell 进程内执行：

```powershell
New-Item -ItemType Directory -Force build/msvc-tmp | Out-Null
$env:TEMP = (Resolve-Path build/msvc-tmp).Path
$env:TMP = $env:TEMP
& 'C:/Program Files/Microsoft Visual Studio/2022/Professional/Common7/Tools/Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
./tools/dev.ps1 -Action verify -Preset core-msvc-debug
./tools/dev.ps1 -Action verify -Preset core-msvc-release
```

未修改系统环境或安装工具。本机 VS 安装元数据标为不完整，但现存 C++ 工具在上述条件下实际完成编译、链接和运行；不能由此认证整个 VS 安装完整性或目标 Qt kit。

清理失败仍保留闭包与 scope pin，quiescent=false；投递被阻断或任务永久等待时 close 保持 pending。没有自动重试、强制释放或析构时异步关闭；宿主必须显式 close、检查报告并在之后关闭 scheduler。FakeClock 仅供测试。实例/服务等待图、生产 Qt 计时适配与模块引用属于后续阶段。
