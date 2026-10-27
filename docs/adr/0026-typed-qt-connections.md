# ADR-0026：类型化 Qt 信号的数据冻结与退出屏障

日期：2026-10-27（北京时间）。状态：已实施，限内置插件及受支持的数据值；不构成二进制 SDK。

## 背景

QtConnection 原有 observe 仅把信号作为触发器。类型化投递需要在发射参数失效前取得数据所有权，又不能把业务函数或其捕获放入 Qt 原生连接和消息队列。

Qt 的 raw-data 字符串入口允许借用外部缓冲区；普通隐式共享复制不能作为独立拥有数据的保证。本实现对 QString/QByteArray 使用 constData/size 构造独立副本，保留嵌入零字符及 null/空值区别。此处依据固定版本实现及实际借用缓冲区回归，不外推为任意 Qt 元类型都能安全复制。[Qt 5.15.2 QByteArray 实现及说明](https://github.com/qt/qtbase/blob/v5.15.2/src/corelib/text/qbytearray.cpp)，[QString 实现](https://github.com/qt/qtbase/blob/v5.15.2/src/corelib/text/qstring.cpp)。

## 决定

新增 QtConnection::observeValues，返回与 observe 相同的 QtConnection 资源句柄。参数根据信号成员指针推导并去除引用，支持普通、const/noexcept 成员指针和无参数信号。业务回调返回 Task<void>，始终在 QtScheduler owner 执行，入口沿用 Context/epoch 与取消门禁。

数据仅允许数值、枚举、std::string、QString 和 QByteArray；不接纳 QObject/原始指针、视图、用户自定义结构、元类型或转换器。原生 functor 仅持宿主租约和字节上限，冻结过程中不调用业务函数。发射端须保证源参数在本次同步信号发射期间可读，不能并发修改同一缓冲区。

先保留连接票据，再检查全部参数的内容字节预算并冻结。队列及在途业务上限沿用 maximumPending，默认 256、上限 65536；复制中的票据和待释放票据也占预算。每份数据默认 1 MiB、上限 16 MiB，按 QString 的 UTF-16 内容字节、字节串长度及标量 sizeof 求和，不计容器、终止符和分配器固定开销。边界内存上限仅约束托管副本，不控制调用方自身构造信号参数的分配。

QtScheduler 及私有 Qt 消息仅携带宿主状态和编号，数据只由资源账目保管。owner 将同一个 map 节点从 pending 转到 active，不额外分配入口记录或形成预算空隙。业务参数以 const 引用交付，存储覆盖调用栈及其返回 Task 的寿命；业务必须先停止使用引用再完成 Task，不得在此窗口之后借用。

## 关闭与失败

关闭同步撤销未进入业务的票据。正在冻结的票据必须等冻结结束；已冻结的数据在 owner 锁外释放，调度器从其他线程丢弃消息不会释放业务数据。等待原生快照、所有票据/异步业务和捕获释放之后，才确认私有 QObject 完整删除。close 仍是资源屏障，Core effect 与 Kernel 的整体确认继续独立观察。

释放批次在 owner 实际析构数据完成前仍保留预算；正常完成、撤销及入口门禁失败都遵守此顺序。诊断的 running/payloads 与关闭条件也覆盖释放栈，不把移出 map 当作数据已经释放。提交前审核发现并补修该边界，私有控制块回归不改变公开类型白名单。

超过数据预算或冻结异常会终结该票据，计入 payloadErrors/discarded 并保留诊断，不向业务交付部分值，也不让复制异常越过 Qt 激活栈；原生内存分配耗尽不作为完整故障容错保证。callbackErrors 继续仅记录业务失败，payloads 报告资源账目仍持有的冻结数据数量。业务错误不妨碍真实排空后的关闭成功。

应用先退出时拒绝关闭证明，并保留未知资源和仍被异步业务使用的数据。复制或释放 facade 不取消所属 effect；正常退出仍须保持 Qt 循环、先等 Kernel，再关 scheduler 与应用。直接返回自己的 close Task 继续报告 CleanupCycle；间接等待环仍由调用方避免。

## 范围与验证

本轮不提供任意自定义类型复制、跨 ABI、ModulePin、物理卸载或其他线程后端保证。模板冻结及数据 tuple 的代码仍属于源码 SDK，不能据此声明 DLL 可卸载。

15 项真实 Qt 场景覆盖借用字符串深拷贝、异步参数寿命、取消、旧代、条数及字节预算、null/空值、发送者销毁/facade 释放、业务错误、并发发射、注册门禁、const/无参数入口、原生快照、并发关闭和应用先退出。headless 示例 cordis_qt_value_connection_demo 使用临时 raw-data 缓冲区，异步消费后联合等待连接、时钟及 Kernel 关闭。矩阵及重复检查结果见 [开发记录](../p3-development.md#q2f-类型化-qt-信号连接)。
