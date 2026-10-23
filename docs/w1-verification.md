# W1 交付与验证记录

日期：2026-10-23。状态：W1 工作单范围完成。下一工作包：W2 EffectScope。

## 实际交付

| 文件 | 能力 |
|---|---|
| `sdk/include/cordis/result.hpp` | Result<T>/void、受检查访问、移动独占值、右值按值取出 |
| `sdk/include/cordis/scheduler.hpp` | IScheduler 与 run/discarded 任务协议 |
| `sdk/include/cordis/testing/fake_scheduler.hpp`、`src/core/fake_scheduler.cpp` | 手动 FIFO 调度、线程检查、关闭、步数限制、异常恢复 |
| `sdk/include/cordis/task.hpp` | Task/Promise、一次完成仲裁、多观察者、异步投递回执和 BrokenPromise |
| `sdk/include/cordis/cancellation.hpp` | 可跨线程请求及查询的协作取消 |
| `examples/builtin/task_demo.cpp` | worker 完成任务、owner 排队处理结果的可运行示例 |
| `tests/unit/w1.cpp` | 24 个独立行为场景 |

core 继续只依赖 C++17 与线程标准库，没有引入 Qt/Ela 或第三方异步框架。示例不是插件生命周期演示，W2–W7 尚未实现。

## 验证矩阵

| 配置 | 构建 | CTest |
|---|---|---|
| GCC 13.1 Debug | 通过 | 26/26 |
| GCC 13.1 Release | 通过 | 26/26 |
| MSVC 19.44 x64 Debug | 通过 | 26/26 |
| MSVC 19.44 x64 Release | 通过 | 26/26 |

每个配置含 1 项既有基础检查、24 项 W1 行为用例及 1 项异步示例。不是 104 个不同功能用例。所有配置保持 C++17 和警告即错误，Release 检查不依赖 assert。

另外，GCC Debug 下的完成竞争、注册竞争和关闭竞争各连续运行 20 次，60 次均通过。该重复测试增强竞态覆盖，但不是线程消毒器或形式证明；本轮未运行 ASan/TSan。

原始输出在 `build/w1-<preset>.log`、`build/w1-races.log` 和各构建目录的 CTest 临时记录中；构建日志不提交，持久结果以本文件为准。

## 关键行为证据

| 契约 | CTest 名称（均有 w1. 前缀） |
|---|---|
| 成功/错误分支、void、错误访问、移动独占值 | result_values、result_move_only |
| FIFO、不内联、工作线程投递和 owner-only pump | scheduler_fifo、scheduler_thread |
| 循环步数限制、拒绝嵌套 pump、异常后继续使用 | scheduler_limit、scheduler_recovery |
| 已完成任务的回调仍排队，失败源可观察 | task_ready_queued、task_failure_queued |
| 独占 payload 被多个观察者共享且不重复 move | task_move_only_observers |
| resolve/reject 竞争仅一方成功，注册不丢回调 | task_completion_race、task_registration_race |
| 关闭前注册拒绝、已入队丢弃、完成前关闭 | task_closed_registration、task_discard_queued、task_closed_completion |
| 调度器销毁、回调抛错、post 抛错可观察 | task_destroyed_scheduler、task_callback_failure、task_throwing_scheduler |
| 最后生产者退出及共享生产者语义 | task_broken_promise、task_producer_copies |
| 投递回执也异步通知，空回调/空 scheduler 有错误 | task_receipt_chain、task_invalid_observer |
| 取消幂等、跨线程可见且不终结任务 | cancellation_independent |
| 关闭与完成并发、丢弃通知重入/抛错 | scheduler_close_race、scheduler_close_reentrant |

## 明确边界

- 本轮完成的是手动测试调度器；生产 QtScheduler 未实现。
- 计时器、FakeClock 与超时观察将在 W2 使用清理屏障时补齐，不在当前 W1 退出条件中。
- scheduler 在源任务仍 pending 时关闭，不主动终结源任务；源完成或生产者消失后，投递回执报告失败。此行为已记录到接口契约和 ADR-0002。
- 普通闭包仍为源码 SDK 的 std::function；跨 DLL 模块 lease 和线程相关析构属于后续模块/Qt 适配范围。
- 内核资源清理、实例状态机、依赖服务和 EventBus 尚无实现，不借用这些基础测试宣称其通过。

复现：按 [开发环境报告](environment-verification.md) 加载相应工具链，运行 `tools/dev.ps1 -Action verify -Preset <preset>`。
