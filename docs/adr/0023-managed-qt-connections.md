# ADR-0023：托管 Qt 信号触发连接

日期：2026-10-26。状态：已实施，限内置插件和零参数触发。

## 背景

QObject 删除或 disconnect 返回不能单独证明业务异步任务、捕获和原生调用快照都已释放。需要把信号连接接入 Context/epoch 与 effect 的退出协议，同时保持 Core 不依赖 Qt。

## 决定

Cordis::Qt 提供 QtConnection::observe，接收信号成员指针和返回 Task<void> 的回调。注册、close 和 snapshot 在 QtScheduler owner 使用，ready 交付可观察的任务；发送者须在注册返回前存活，不能在 connectNotify 中销毁自身。注册后支持发送者正常销毁。复制或释放 facade 不取消连接，所属 effect 负责退出。

首版只把信号作为触发通知，不复制或转发参数。原生 DirectConnection 的宿主桥仅持有宿主租约并登记序号；业务始终经 scheduler 排队，在入口重查所属 Context/epoch 和取消状态。队列与在途任务共同占用预算，默认 256，上限 65536，溢出拒绝并记账。应用必须将业务使用的数据另行安全交付，不能借此跨线程读取发送者属性。

effect 清理在建立原生连接前同步登记。connectNotify 重入停止时，注册中的资源仍受 Kernel 屏障管理；注册结束重查门禁，失败就退役，不向已停止的代交付连接。

每次业务进入保留回调栈及返回 Task，完成观察者只写宿主结果，再通过私有 Qt 通知推进。插件捕获在 owner 释放，捕获析构及其嵌套事件循环也是受保护的清理栈。直接返回自身 close 任务记 CleanupCycle 并终止该次业务等待；这只识别相同任务状态，不检测间接任务依赖图。调用方仍须避免等待任何依赖本回调结束的整体关闭任务。

关门同步拒绝新触发、撤销未进入业务的票据，在锁外 disconnect；随后等托管 disconnect 调用栈返回、原生 functor 租约的最后一个快照释放、全部业务返回且其 Task 完成，再释放回调和捕获，最后用 QObjectResource 等私有接收对象完整 delete 返回。Qt 5.15.2 的直接激活保留 slot functor 引用，故采用共享租约跟踪释放；functor 也可能先于 disconnectNotify 返回而释放，因此托管断连另设 owner 栈深度保护；具体实现和调用快照行为以固定版本回归验证。[Qt 5.15.2 QObject 实现](https://github.com/qt/qtbase/blob/v5.15.2/src/corelib/kernel/qobject.cpp)

## 关闭与失败边界

close 返回连接自身的 Task<void> 资源屏障，同时请求 effect 退役；它不代替 Kernel.close 的 CleanupReport。已取消票据对应的宿主空通知后续被丢弃，不再记录业务投递故障；仍有有效票据时的丢弃才设置 deliveryBlocked。业务抛错或返回失败记入 snapshot，已真实排空的资源仍可关闭成功。scheduler 提前关闭后，显式连接关闭和业务任务的完成仍通过私有 Qt 通知排空；这不证明 Core 清理账目已经处理。正常退出仍需先等 Kernel，再关闭 scheduler，最后退出 Qt 应用。

应用提前销毁私有宿主或资源确认失败时拒绝屏障，并保留无法证明静止的控制块。不能把这种失败解释成可卸载模块。本次没有实现类型化信号参数转发、任意插件 worker、ModulePin 或物理 DLL 卸载，后者留待各自工作包。

## 验证

16 项真实 Qt 场景覆盖跨线程触发与零参数复制、异步任务、原生发射快照、关闭/捕获析构重入、epoch、参数与线程门禁、发送者销毁、facade 释放、业务失败、有界预算、scheduler 先关闭、连接建立期间停止以及直接关闭等待环、断连通知重入和已取消通知丢弃。headless 示例联合验证连接、QtClock 与 Kernel 退出。构建及 ASan 结果见 [Q2c 开发记录](../p3-development.md#q2c-托管-qt-信号连接)。
