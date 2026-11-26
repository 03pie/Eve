# ADR-0052：依赖评估的原位只读统计

日期：2026-11-18。状态：已采纳。

## 问题与边界

运行验证需要测量实际依赖评估批次、队列延迟、停机与清理耗时。通知可合并，Active 实例可被空唤醒，Pending 实例可在同一 epoch 内重新评估，因此评估次数需要独立读取。

最初 ServiceRegistry 广播弱观察作业，由 Instance::enqueue 合并排队，绑定的 committed consumers 负责失效传播。统计接口记录实际评估行为。后续的按键通知索引见 [ADR-0053](0053-keyed-activation-notifications.md)。

## 决定

在唯一生产调用链 InstanceControl::start→ServiceRegistry::checkDeclarations 中，找到真实实例声明后、检查图和绑定前累计一次，定义为**实际激活依赖评估批次**。包含零依赖实例；图失败、缺失和版本/能力不匹配的尝试均计入。前置门禁拒绝、通知、入队、空执行以及只读诊断均不计。它不是边访问次数、完整图扫描次数或成功激活次数，不作这些含义的性能宣称。

累计值属于原 ServiceRegistryControl，由 owner 线程修改；复制注册表和发布组视图读取同一统计，独立注册表互不影响。新增 ServiceRegistry::dependencyEvaluationCount() const，返回 Result<uint64_t>，只允许 owner 线程读取；WrongThread 在读计数前拒绝。队列关闭后仍允许诊断。没有 reset、事件、回调、时间采样或 Kernel 重复转发。

累计达到 uint64_t 最大值后，下一次评估锁存溢出并保留数值；getter 明确返回 InvalidArgument，不回绕，不影响原依赖评估、激活或失败路径。无测试专用写入入口；不可自然穷尽的溢出分支以静态审查记录，不伪称动态覆盖。

只增加非虚 out-of-line 成员函数和私有控制块字段，既有公开 ServiceRegistry/快照布局、原签名、C ABI、协议和元数据不变。沿用同工具链源码 SDK 0.1.5；旧调用者不依赖新符号，新调用者必须链接含该符号的当前 Core。这不构成跨编译器或任意旧 Core 的二进制兼容保证。实施前保存原头、导出和实际旧产物，实施后验证原符号、布局与旧 SDK 实物运行。

## 不采用的方案

- 用队列数、revision 或 epoch 替代评估次数；其语义不同。
- 向既有公开 Snapshot 追加字段；会改变实际二进制布局。
- 扩大替换访问桥、给 Kernel 再加转发，或增加通用 metrics 框架；观测点属于服务索引且已有实际直接消费者。
- 每实例保存后汇总；已销毁实例会丢失历史，保留所有实例又会扩大拥有者和压力基线。
- 为观测先实现完整反向索引，或为溢出测试暴露 setter/friend；均超出当前观测问题。

## 验证出口

用真实公开 Kernel、ServiceRegistry 和 FakeScheduler 验证初始值、成功/缺失/失败尝试、合并与空唤醒、无副作用读取、共享视图/独立索引、WrongThread 和关闭后诊断。复验受影响 Core/发布/替换/Loader 与实际业务/UI 路径；验证无 Qt GCC、静态 Core 及共享 Release/Debug/ASan，核对实际插桩对象、公开头、旧导出与旧 SDK 实物。冻结最终输入后审查；记录原失败和真实选择清单，不把专项当全量。审查提交并同步、工作区干净后进入连续替换资格测试。
