# ADR-0051：UI 贡献复用原发布组

日期：2026-11-18。状态：设计基线 4ca3b48、页面/命令 M5-a1 d936580 已审计提交同步；值输出/通知 M5-a2 实现、阶段审计及五配置验证通过，随收口提交归档。原设计源码基线 cff1bac（P5-M4-c 已审计、提交同步）。具体预算、序号、纯值结算及关闭取舍见 [M5-a2 实施设计](../p5-m5-development.md#m5-a2-实施设计)与[收口结论](../p5-m5-development.md#m5-a2-收口结论)。

## 问题与实际边界

M1–M4 已有唯一 Core/Module/Driver 共同提交，服务 Binding 的 Held/Published 由原 PublicationControl 决定。候选 Context 必须能初始化组内业务，因此 business/eventGuard 在开放候选组中合法；这不代表候选已经可以向用户公开 UI。

当前 UiPagesService::add 在 effect setup 直接登记 Ela 导航、堆栈和工作台；CommandService::add 直接加入命令模型；OutputService/NotificationService 直接写可见历史。它们仅检查本代业务有效，未检查外部发布状态。仅在管理页追加升级按钮会暴露候选页面、命令或日志，并让失败候选改变当前 UI。先补该边界，再接业务升级入口。

已有撤销、Context/epoch 输入门禁、QObjectResource 完整析构、命令在途调用与清理屏障继续使用。Core 不依赖 Qt/Ela、Module 或 Loader；第三方 Ela 不认识 Instance、Kernel 或 ReplacementOperation。

## 决定

### 原状态与私有适配桥

以原 Context 所属 ServiceRegistry/PublicationControl 为唯一发布来源，不另建 UI 事务、发布组或可变状态目录。沿现有 UI friend/eventGuard 模式提供私有适配桥：登记时验证真实 owner、epoch 和同一 QtScheduler；纯呈现查询只读原发布状态，不执行插件回调、调度器虚调用或资源登记。实际导航、输入和命令执行仍重新检查本代业务门禁。

普通非发布组上下文保持原行为。开放候选/恢复组为 Held；Aborted 或失效上下文不可进入；Published 只有在原 notifying/deliveryBlocked 屏障解除后对外开放。候选内部初始化仍可运行，不能把 eventGuard 全局改成“候选不可调用”。

失效通知复用原 ServiceRegistry::watch/notifyRegistry：UI 原贡献记录保活实际 ScheduledJob，注册表只存弱索引，job 只捕获弱 UI 记录；撤销或不再需要观察后释放。不给插件公开观察、发布或解除预约能力，不建立新的观察者目录。真实 QtScheduler 排队且不嵌套推进当前任务；共同提交完成并清理发布标志后才处理呈现刷新。私有桥和拒投路径须有实际验证，不能靠轮询或一次 processEvents 猜测已发布。

现 PublicationGroup::abort 依赖退役 Binding 或后续 forgetInstance 间接发通知，无输出且清理失败时两者都可能不发生。引入 UI 观察后，须让 Aborted 终态本身沿原 notifyRegistry 发通知并正确记录拒投，不能依赖无关服务事件触发清理。仍只改变原状态及通知路径，不新增终态目录。

### 准备完整，呈现受同一状态控制

PageHandle/CommandHandle 的 ready 必须在准备阶段完成真实工厂、控件、所有权、注册和清理安装，不能等到 Published：插件 activate 会等待 ready，而原提交又等待所有实例 Active，延迟 ready 会死锁。

页面、分类、命令、工作台元数据使用原记录/模型。准备时即设置独立呈现门禁；不得“先正常插入，再隐藏”，否则插入信号重入时已泄露。原 Ela IsVisible 是展开/折叠派生位，会被 setChildVisible 改写，不能拿它表示发布权限。为真实调用者补充的通用 Ela 呈现能力须在原 node/stack 上实现，默认开放，不保存平台类型，不复制一套页面目录。

原生导航树、紧凑菜单、搜索建议、首项自动选择、路由/前进后退、中央页签、Home 贡献与独立窗口均读取同一门禁。命令可复用原过滤模型，在 appendRow 前设置准备状态，外部 execute 同时受门禁约束。候选页面可以完整创建并隐藏，失败后仍按原资源屏障确认 destroyed、工厂释放；移除 Held 节点也必须正确处理可见索引、分类和已排队路由。

当前 PageControl/Group 依赖“刚插入的最后一行”保存持久索引，Ela getRow 依赖原 children 序号；引入可见索引后须替换这些假设，按稳定节点 key 查找并遵守 Qt 模型行通知。不能保留旧索引算法，再追加隐藏行补丁。恢复活动页面依据真实旧/新实例映射与页面逻辑 id，重新定位新贡献；不迁移 QWidget、原 Ela key 或失效路由记录。

可见索引只是原节点的派生投影，刷新须在正确的模型插入/删除或 reset 窗口内兑现；不能让 rowCount/getRow 随外部 predicate 裸变而没有模型通知。原节点、稳定 key 和资源拥有者仍是同一份，派生行缓存不授予发布权，也不成为第二目录。

提交只改变原发布来源；通知只刷新已准备对象的可见投影，不能在提交后重新调用可能返回普通 NodeResult 失败的 addPageNode 或执行页面工厂。所有记录读取同一发布来源，不能通过逐页独立批准形成第二次提交。模型通知仍可能重入关闭，呈现与撤销均须保活宿主访问栈、重查记录及窗口门禁。

业务/元数据已经提交后的窗口关闭、投递拒绝或不可恢复呈现故障要保留准确的 delivery/资源诊断，不能二次回滚或伪装候选初始化失败；可失败的工厂/登记必须尽量在原 ready 前兑现。不得让正常可预见的注册失败滑到 Applied 之后才被发现。

### 值输出与通知

Held 输出/通知只能保存复制的有界宿主值，不创建可见 channel、弹出提示、刷新控件或淘汰已发布历史。pending 值保存在各自原控制块，使用独立且明确的条数/字节/目标限额；超限显式拒绝，不能驱逐当前可见记录来接纳未提交候选。实施前在工作单确定限额及序号规则，复用原 UTF-8、长度和级别校验，不复制两套追加算法。

发布后才按原可见历史规则接纳，锁存为纯历史并释放弱 guard/watch；实例退出不应隐藏已经发布的历史。取消/失败候选的 pending 值丢弃，不能留下通道或改变现有选择。保留有界历史不等于保留插件工厂、服务租约或 DLL 捕获。

值记录的终态判断与页面输入门禁不同：提交后、watch 刷新前实例可能已经 stop/dispose，弱 owner/epoch 失效不能把已提交值误判成失败候选。pending 值须临时保留原只读发布来源的终态观察，区分已 Published 与从未发布的 Aborted；不强持 Instance、Runtime、完整 Context 或 DLL 捕获，不新建并行“提交回执”账目。接纳/丢弃后释放来源和观察，纯历史只剩宿主值。

## 不采用的方案

- 在 settled 回调或升级按钮中再发布一遍 UI；这会形成第二个提交决定，恢复和拒投也会分叉。
- 只在提交后调用 addPageNode，或令 ready 等待 Published；前者把可失败登记移到提交后，后者与 Active 门禁互相等待。
- 只隐藏 QTreeView 行；Ela 的紧凑菜单、建议、自动首选与工作台仍可能公开候选。
- 给 Core 增加 Qt/UI 类型、独立发布控制器或公开通用 Task API，仅为绕过现有私有适配边界。

## 验证与兼容

至少覆盖普通贡献回归；候选已 ready 但页面/命令/搜索/菜单/输出不可见不可执行；完整提交后一起进入；候选失败、取消和完整恢复；真实 S09 消费者失败；隐藏页工厂/登记失败；注册/呈现/撤销中重入关闭；通知拒投；Held 阶段关闭；完整 QObject/工厂/命令调用释放；输出限额、历史淘汰、序号和已发布历史保留。特别覆盖无服务输出且清理失败时 abort 仍通知，以及提交后、刷新前立即停止/析构仍正确接纳已提交值。保留真实状态/角色/共同提交与物理卸载回归。

M5-0 只记录方案，不修改 SDK 0.1.5、ABI、Ela 或生产行为。实现阶段若改变公开诊断布局/契约，须评估并更新 SDK 门禁、重建宿主与同工具链插件；不能声称旧 C++ 二进制稳定兼容。原业务 C ABI、Qt 固定驻留及无 Qt Core 依赖方向保持。详细分段和产品验收见 [M5 工作单](../p5-m5-development.md)。
