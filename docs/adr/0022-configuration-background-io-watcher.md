# ADR-0022：配置后台 I/O 与文件监听

日期：2026-10-25。范围：Q4d3，连接已有文件事务、Store、会话及 [配置管理路由](0021-configuration-management-routing.md)。

## 后台执行与边界

可选 Cordis::ConfigIoQt 提供 QtConfigurationIo 与 QtConfigurationWatcher，依赖 ConfigStore、ConfigFileQt 和 Cordis::Qt，仅 Qt 预设启用。Core、无 Qt 的 Session/Store 与普通 Ui 不引入本模块。原 QtConfigurationWriter 保留为同步适配器；新工作台使用后台适配器，不把“Task 先返回”等同于已经后台化。

QtConfigurationIo 固定路径和文件预算，专用 QThread 惰性启动后保持到关闭，通过有界 FIFO 和条件变量串行处理任务。构造不读写文件；owner 排队有界请求，pending 包含运行中的任务，超过容量明确失败。worker 只处理不可变候选及文件值，不运行 schema、插件逻辑、GUI 或会话账目。解析/schema/目标接受继续在 owner 线程执行。

读取返回原文、存在性/hash 与该次读取时配对的自身提交序号/版本。元数据在同一串行 worker 中获取，不能在 owner 晚到回执时查询“现在的最近写入”来解释过去的读取。提交复用原协作锁、原文基线比较与 QSaveFile 事务，不弱化冲突检测。成功物理提交才增加自身提交序号，冲突、失败、Unchanged 不冒充自身写入。

Promise 可以在 worker 完成，业务延续仍通过 thenOn 回到 owner；close 同步关门并排空已接受任务。值任务完成不能证明线程退出：私有 Qt 上下文使用 QThread.wait(0) 检查专用线程结束，再销毁上下文，才完成关闭。关闭不依赖业务 scheduler 仍开放，但需要 Qt 循环继续运行。遗漏显式 close 时应用析构会同步排空，避免销毁运行中的线程；这只是析构保障，不作为正常桌面退出路径。

## 原生监听与内容版本

watcher 同时监听文件和父目录。QSaveFile/编辑器原子替换可能移除原文件监听，目录事件及维护检查重新注册；父目录删除后重建也会恢复；维护检查核对实际目录存在性而不只看原生登记表，重新登记后再复读覆盖读取期间重建的窗口。初始参数必须使用同一后端 read 返回的 ConfigurationDiskObservation，沿用读取时配对的提交游标；首轮后台复读覆盖“初次读入”和“注册监听”之间的窗口，不在构造时借用更新后的提交序号解释旧基线。注册失败和读取错误保留可见诊断。

原生通知先合并，再通过共享 I/O 后端读取；至多一个在途读取，加一个后续复读标记。合并窗口有界，更新通知到来时不发布已经过期的读取。observations 统计 owner 已处理的观察，不把物理通知次数当作内容修订。仅路径、存在性或原文 hash 的改变形成新的外部通知；同内容重复写和目录噪声不重复发布。

自身提交消重使用读取时配对的新提交序号和版本，不维护永久 hash 白名单。自己的新提交不作为外部修改；随后外部修改并回退到先前相同内容仍是新变化。新提交若在复读前被外部改回旧观察内容，也须通知，不能仅凭等于旧观察 hash 忽略这次覆盖。watcher 只保留最近外部原文及通知序号，有界覆盖；acknowledge 按序号确认通知，不改磁盘、Store 基线或会话期望。

监听是提示与复读机制，不是任意文件系统 CAS；多次外部修改可以合并，读写的非协作竞争边界沿用 [文件事务](0014-configuration-file-transactions.md)。最终保存始终再比较真实磁盘版本。

## 配置页与宿主退出

配置页可选弱持 watcher，显示监听状态及原文，提供“将磁盘版本载入编辑区”。后台刷新不覆盖编辑。点击时再核对通知序号，确认后只载入原文并使旧预览失效；删除通知载入为空文本。不自动解析、应用、启停、改变 Store 基线或保存；用户仍须明确预览/应用。监听提示与 Store 保存冲突分别保留。

工作台启动后台读取成功后才装配会话和 watcher，仍只把原文放入编辑区。关闭先做可恢复的保存尝试，冲突保留窗口且 watcher 继续工作；确认可关闭后依次等待 watcher.close、Session.close、Io.close、Kernel.close、Shell.close，最后停 scheduler。watcher 关闭先断连原生入口并等待 owner 读取回执；投递失败终结为失败，不宣称静止。I/O 的线程 join 是后续独立屏障，不由 watcher 的关闭替代。

## 验证与剩余工作

真实临时文件专项覆盖读取、串行提交/读取元数据、外部冲突、锁失败、关门排空、facade 释放、线程/预算门禁、内容消重、原子替换/原地续写、删除重建、父目录恢复、自身写入及外部回退、scheduler 关闭与待合并请求取消。UI 专项验证外部原文不覆盖未应用编辑、显式载入不推进期望、保存继续冲突及明确 rebase。

本模块只提供配置专用后台 I/O 与 watcher 屏障，不替代任意插件 worker、connection 或动态模块卸载协议。证据见 [Q4d3 开发记录](../p3-development.md#q4d3-配置后台-io-与文件监听)；新增原生体验单列 [专项清单](../../tests/acceptance/p3-ui.md#q4d3-文件监听与后台读写补充验收)。

## 提交前审查修正

初版线程池检查使用 waitForDone(0)，但 [Qt 5.15.2 的 reset 实现](https://github.com/qt/qtbase/blob/v5.15.2/src/corelib/thread/qthreadpool.cpp#L261) 在清理时仍可能执行无超时 wait。提交前改为单个长驻文件线程和显式 FIFO，避免 owner 隐式等待及每个请求反复启动线程；读取/提交的值协议保持不变。这是配置专用 Qt 线程结束屏障，不作为 P4 任意模块或系统 TLS 卸载证明。
