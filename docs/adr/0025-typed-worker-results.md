# ADR-0025：托管工作线程的类型化数据结果

日期：2026-10-26。状态：已实施，继承 QtWorker 的 Windows/MSVC、一次工作与内置插件边界。

## 背景

QtWorker 的 Result<void> 已具备原生线程/TLS、捕获、私有宿主与 effect 退役屏障。若业务自行用共享变量传递计算结果，容易在函数返回后、原生线程退出前读取，或在旧代取消后发布成功。

## 决定

新增源码 SDK `QtValueWorker<T>`，通过同一个 QtWorker 管理资源。工作接收 WorkerCancellation 并同步返回 Result<T>，后台将结果冻结为 Task<T>::Snapshot，然后只返回 void 成败给底层资源。数据必须自己拥有存储，不借用 Context、GUI 或线程约束服务。

底层 result 成功后，在 owner 将冻结快照原样交给类型化任务。内部 TaskAccess::completeSnapshot 沿用一次完成仲裁和观察者协议，不复制或移动业务值，因此支持不可复制结果，也不在发布期间执行其构造函数。冻结结果时的移动异常由既有 worker 异常边界转为 CallbackFailed。

底层 result 失败时仅转发宿主错误，不读取或清空工作数据。应用提前退出时，底层可能在原生线程仍运行期间拒绝结果，正在运行的闭包继续拥有共享数据；未知资源保留，不能据此宣称 join 或可卸载。

## 寿命与范围

- ready、close、snapshot、requestCancel 直接沿用 QtWorker 的语义。
- result 返回 Task<T>；成功仍等待原生线程/TLS、捕获释放、宿主删除及自身 effect 账目确认，并通过当前代门禁。取消和旧代结果不能绕过底层判定。
- 结果快照可比 worker、Context 和 Kernel 存活更久。最后一个 Task/快照所在的线程负责数据最终析构；T 的析构必须适合该线程。close 不证明外部仍持有的数据已经销毁。
- 仅传递数据，不承载插件回调、Qt 对象或模块资源所有权。不新增 ModulePin、ABI、其他线程后端、多任务队列或强制取消保证。

## 验证

真实 Qt/原生线程场景覆盖不可复制数据在发布时禁止 owner 移动、TLS 阻塞期间不发布、本地取消、旧代更新、业务失败、抛错、冻结移动失败、facade 释放、scheduler 先关及应用先销毁。示例 `cordis_qt_value_worker_demo` 输出 sum(1..100)=5050，之后确认 Kernel 与 worker 完整关闭。

无 Qt 单测验证快照身份不变、不可复制值、完成仲裁、排队观察者以及空快照和失败快照。实际矩阵与日志见 [P3 开发记录](../p3-development.md)。
