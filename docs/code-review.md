# 提交前代码审查记录

## Q2e/Q2f 与桌面环境增量提交前审核

日期：2026-10-27。范围：145c6c1 之后全部未提交文件，包括类型化 worker、信号数据、Task 快照转发、桌面入口、测试/示例及文档。按用户要求审核、修复并提交；以下为当前提交归档结论，之前自检的未提交描述保留为历史。

发现并修复 P2 预算一致性问题：正常 drain 和入口门禁拒绝路径先从 pending/active 移除票据，再释放数据，并发发射可在旧数据仍存活时重用容量。新增 Retirement 批次在锁内接管数据并保留条数/数据账目，在 owner 锁外析构完成后才归还预算；snapshot 和关闭判定也覆盖释放栈。新增私有控制块回归，不扩展公开类型白名单或生产测试钩子，在真实释放栈中重入 reserve，验证释放期间拒绝、释放后可恢复。修复前 0/1（drain 路径实际失败），修复后同时覆盖 drain/门禁失败两条路径，连接专项 34/34；日志为 build/review-data-before.log 和 build/review-data-after.log。

其余审核核对复制/取消/旧代/原生快照、异步参数及 worker 结果寿命、任务完成仲裁、错误退出保留、头文件依赖与构建隔离；未发现其他阻止本次提交的问题。README 原启动命令仍引用旧 E:/Qt，已改为桌面入口，直接运行示例也明确临时 Qt PATH。入口成功/缺 kit 失败时逐项比对调用方环境保持；原生 stderr 警告退出 0 被接受，退出 7 被拒绝。

最终 Ela/MSVC Debug、Release 各 418/418，GCC Debug、Release 各 209/209，ASan connection/worker/快照专项 68/68；17 项信号/释放预算检查及示例在 Debug、ASan 各连续 20 次通过。日志为 build/review-data-<debug|release|asan>.log、build/review-data-core-<debug|release>.log、build/review-data-repeat-<debug|asan>.log；头/工厂正向与拒绝检查沿用本轮记录。文档本地链接及 git diff --check 通过。未改 sources/research/第三方代码或用户配置，未补记人工确认；Qt/系统预编译库未插桩，未把这些证据外推到模块 ABI/物理卸载。

## P3 Q2f 类型化 Qt 信号工作区自检

日期：2026-10-27。范围：本轮 observeValues、票据/数据寿命、类型约束、测试及示例；本轮自检，不声称独立审查或已提交。

核对原生桥仅持宿主租约及上限，不捕获业务函数；白名单排除指针/视图/自定义类型，Qt 字符串独立冻结，且保留空值及嵌入零字符。先登记票据再复制，复制中和待释放记录也占预算；节点转移保持 pending/active 的原子容量边界，消息丢弃只标记，数据在 owner 锁外释放，Task 观察者跨线程只写宿主结果。关闭等待复制完成、原生租约、全部业务及数据，再删除 QObject；应用先退出继续保守保留。模板 thunks 和 tuple 不提供 ModulePin 或 ABI 卸载保证。

验证中修正测试把资源 close 与 effect 退役混用，以及空参数包的 GCC 未使用变量警告。最终 Ela Debug/Release 各 417/417，GCC 各 209/209，ASan connection/worker/快照专项 67/67；16 项新增信号检查/示例 Debug、ASan 各连续 20 次通过。独立 GCC/MSVC 允许工厂语法检查及指针载荷编译期拒绝通过；不混称 Qt/GCC 运行或 ABI 验收。依赖 Qt/系统库未插桩，未注入分配器耗尽，也未改变 UI 或人工验收状态。完整证据见 [Q2f](p3-development.md#q2f-类型化-qt-信号连接)。

## P3 Q2e 与本机桌面入口工作区自检

日期：2026-10-26。范围：145c6c1 后本轮类型化结果、内部快照转发、测试/示例、桌面环境入口和文档；本轮自检，不声称独立审查或已经提交。

确认类型化成功仅由 QtWorker 完整 result 门禁触发；后台只写一次冻结快照，owner 发布时不调用 T 的构造函数；退出协议提前失败不读取仍在执行的工作数据。任务快照与模块资源寿命分别约束，支持不可复制值，但不为借用 GUI、跨 ABI 或物理卸载背书。新增冻结移动失败和发布禁止移动场景，普通失败/取消/旧代/应用先退出均终结数据任务；失败保留路径沿用原生 worker。

环境自检发现并修复：Windows PowerShell 重定向 stderr 导致 CMake 警告误报失败、UTF-8 无 BOM 中文注释吞行、MSVC 仅装中文资源时错误使用英文依赖前缀。对旧依赖账目为 0 的对象执行 Ela 两配置干净重建；新编译器依赖记录已包含实际头文件。VS 开发环境加载器还会改写空变量，桌面入口改为隔离子进程，成功与失败时均逐项比对调用方环境保持。run 通过原生退出码报告失败，原生窗口验证已应用未保存目标的退出刷新返回 0。

最终 Ela/MSVC Debug、Release 各 401/401，GCC Debug、Release 各 209/209；ASan Core/Qt/worker/快照专项 34/34，新类型化场景及示例 Debug、ASan 各连续 20 次通过。GCC C++17 无 Qt include 路径独立实例化新头与不可复制工厂通过；源 SDK/内核仍不引入 Qt 类型。Qt 及系统预编译库未插桩。原生桌面代理复核证据见 [Q4d3 记录](q4d3-desktop-verification.md)，用户人工确认仍单独待办。

## P3 Q2d 托管 worker 提交前审核

日期：2026-10-26。范围：ef088eb 之后全部未提交的 QtWorker、取消/诊断 SDK、Core 友元、示例、构建、测试及文档，包含新增文件；sources/research 和第三方组件源码未修改。

发现并修复两类 P2 时序问题：

- 自动退役阻断业务延续。快速工作在当前 owner 任务内完成，原实现从初始化观察者直接调用 effect.close，Core 清理先于已登记的 ready 延续进入队列；已完成 result 的晚订阅延续也可能排在本次清理窗口内。两种延续使用仍有效的 Context 都得到 ContextClosing。现在资源完成后先排队宿主退役通知，Core 再排队清理，ready 延续先运行；正常业务结果等待本 effect 清理确认，消除自身清理窗口对结果消费的干扰。
- 过早保留暂态门禁错误。原生完成时其他 effect 正在清理，原实现把此刻的 ContextClosing 固定为业务失败，即使真正交付结果前清理已经结束。现在原生完成只选定工作错误/协作取消，Context 门禁在真正发布时重新判断；旧代仍不能交付成功。

ready_context/result_context 修复前 0/2（build/review-worker-before.log）；cleanup_overlap 在初版修复后的旧门禁选定路径仍真实失败（build/review-worker-overlap-before.log）。修正后新增场景与既有 worker 专项 22/22，见 build/review-worker-targeted.log。另增 retirement_delivery，确定性在资源已关闭、Core 退役尚未确认时关闭 scheduler，确认等待中的结果会失败终结。所有时序来自实际私有 Qt 循环和 owner FIFO，不依靠 sleep。

资源和账目分别保留：close 继续是可独立于业务 scheduler 排空的物理资源屏障；正常 result 还等待本 effect 退役；retired/retirementBlocked 明确记录 Core 确认及交付故障。退役期间只保持一个可丢弃的宿主通知，投递失败不永久挂起结果，也不伪报 Kernel 静止。工作原有错误优先保留，Core 清理失败保留原因链。worker 内部通知不调用插件，原生线程/TLS、捕获 owner 析构、错误退出保留和 Core 无 Qt 边界保持。

规范与质量：核对每个新增具名类型/函数的中文 Doxygen、直接标准头依赖、原生句柄不外泄、跨线程只读/锁下字段、初始化/关闭重入及最后句柄异线程释放；删除未使用的 cstdint，统一新增代码及第一方改动行格式。SDK 和 ADR 补齐 ready/result/close 及物理/账目状态的区别。Windows/MSVC、一次工作、void 结果的范围保持，不扩大到其他原生线程后端、跨 ABI 或物理卸载。

审查后原目录 Ela/MSVC Debug、Release 各 389/389，无 Qt GCC Debug、Release 各 208/208，ASan UI/Loader/connection/worker 236/236；Qt-only Debug worker 专项 22/22，22 项 worker 检查在 Debug、ASan 中均各连续 20 次通过。完整日志为 build/review-worker-final-<debug|release|asan>.log、build/review-worker-core-gcc-<mode>.log 和 build/review-worker-qt-only.log；重复见 build/review-worker-repeat-<debug|asan>.log。

2 个公共头在 GCC/MSVC C++17 独立包含通过，不需要 Qt 头路径。14 个改动文件 UTF-8/LF、第一方改动范围格式、Core 7 个单元/普通 Ui 依赖隔离、3 个相关实现/测试/示例单元 ASan 插桩、本地链接/锚点及差异空白检查通过，见 build/review-worker-audit.log。预编译 Qt/系统库未插桩。未发现其他阻止本次提交的问题，本次将 Q2d 与审查修复一并提交。Q4d3 新增原生人工体验、其他线程后端、类型化结果和跨 ABI/物理卸载保持后续范围。

## P3 Q2c 托管 Qt 连接提交前审核

日期：2026-10-26。范围：64c0a4f 之后全部未提交的 QtConnection、Core 友元/内部任务身份访问、Qt 示例、构建、测试与文档，包含新增文件；复核中补修既有 Home 测试。sources/research 及第三方源码未修改。

发现并修复：

- P2，断连重入提前释放捕获。Qt 5.15.2 在调用 disconnectNotify 前可能已经释放 slot functor；通知再次调用资源 close，原实现只检查 nativeRetired，即会在外层原生断连仍运行时释放业务捕获。如果捕获拥有发送者或其协作资源，会缩短这些资源的有效窗口。现在托管原生断连统一经过 owner 栈深度保护，嵌套关闭不能推进捕获/接收对象销毁，最后一个断连调用返回后才释放。[固定版本实现](https://github.com/qt/qtbase/blob/v5.15.2/src/corelib/kernel/qobject.cpp#L4679)
- P3，正常取消误报投递故障。close 已撤销票据后，scheduler 清空宿主空通知仍设置 deliveryBlocked，导致正常取消呈现错误诊断。drop 现在仅在确实移除有效票据时记丢弃和故障；真实未取消投递失败保持原语义。
- P2，既有 Home 启停测试使用失效控件。加载会以新身份重建卡片，原测试跨事件循环借用旧按钮裸指针并等待瞬态 disabled，完整 Debug 和单独重复均触发 SegFault。QPointer 诊断在同一位置真实确认旧按钮失效。改为守卫指针、同一输入窗口同步发送真实鼠标按下/释放，并等待新实例/新卡片稳定结果；原有 busy/gate 专项继续验证加载中的禁用状态，产品 Home 逻辑未修改。

两项新增连接回归修复前 0/2（build/review-connection-before.log），修复后连接专项 17/17（build/review-connection-fixed.log）。Home 原始失败保留于 build/review-connection-verified-debug.log、build/review-connection-power-debug.log；守卫诊断见 build/review-connection-power-probe2.log（button at 467）。此前 ASan 单独重复 30 次通过，不把它误写成复现或排除依据。

规范和质量：审查跨线程发射、所有权固定、注册期间关闭、旧代门禁、异步 Task 完成、回调/捕获析构重入、发送者销毁、容量、投递失败及应用提前退出；长快照聚合改为具名字段赋值，降低布尔/计数字段位置误配风险；用 exception 直接包含对应标准类型，补齐中文 Doxygen，只格式化第一方改动行及新增文件。Qt-only 示例不依赖 Ela，Core 不包含 Qt 头，连接资源屏障不代替 Kernel 总体静止。

审查后原目录 Ela/MSVC Debug、Release 各 367/367，无 Qt GCC Debug、Release 各 208/208，MSVC ASan UI/Loader/connection 214/214；Qt-only Debug 连接专项 17/17。日志为 build/review-connection-final-<debug|release|asan>.log、build/review-connection-core-gcc-<mode>.log 和 build/review-connection-qt-only.log。三项问题回归在 Debug、ASan 中均各连续 20 次通过；见 build/review-connection-repeat-<debug|asan>.log。

3 个公共头在 GCC/MSVC C++17 独立包含通过，其中 instance.hpp/task.hpp 不需要 Qt 头路径；GCC 对 connection.hpp 仅做语法检查，不声明与 MSVC Qt 库二进制兼容。17 个改动文件 UTF-8/LF、改动范围格式、Core 7 个单元/普通 Ui 依赖隔离、4 个相关实现/示例/测试单元 ASan 插桩、本地文档链接与差异空白通过，见 build/review-connection-audit.log。预编译 Qt 未插桩。信号仍限零参数触发，任意插件 worker、跨 ABI/物理卸载及 Q4d3 新增原生体验另验。本次将 Q2c 开发与本节审查修复一并提交。

## P3 配置子树、会话与后台读写提交前审核

日期：2026-10-25。范围：02a2ed7 之后全部未提交的 Q4c2c2 配置子树、Q4e1 会话/配置页、Q4e2 Home/管理路由、Q4d3 后台 I/O/监听、SDK、示例、构建、测试及文档，包含新增文件。sources/research 与第三方组件源码未修改。

发现并修复五项 P2 问题：

- 编辑重入错误采用旧候选：preview 的同步驱动预检重入编辑时，页面仍将旧输入的候选设置为可应用；apply 期间编辑被错误标记为已应用，启停期间编辑会被 useDesired 覆盖。现在各入口固定输入快照，预览期间变化则丢弃旧候选；应用/启停接受的目标保持真实结果，同时保留期间的新编辑并禁止基于它进行旧目标启停。局部候选副本保证不跨外部调用借用可被信号清除的 optional。
- 监听注册窗口误报自身提交：初始原文读入后、watcher 构造前完成一次自身提交，构造时查询的最新提交序号会使首轮复读把自身保存当作外部编辑。watcher 初始参数收紧为同一后端 read 返回的 ConfigurationDiskObservation，直接使用与初次读取配对的提交游标；历史自身内容的外部回退仍须通知，不能用永久 hash 白名单。
- 未观察提交的外部回退漏报：后台保存新内容后，外部在 watcher 复读前改回上次已观察的旧内容，原实现仅比较旧观察 hash，漏掉这次覆盖。现在新自身提交若与实际磁盘不符，即使磁盘等于旧观察，也发布外部通知。重复复读仍消重，不改变 Store 基线或目标。

- 退出检查隐式阻塞：初版把 QThreadPool.waitForDone(0) 当作非阻塞 join 检查，但 [Qt 5.15.2 源码](https://github.com/qt/qtbase/blob/v5.15.2/src/corelib/thread/qthreadpool.cpp#L261) 的 reset 仍可能无超时等待线程。改为单个惰性启动、保持至关闭的 QThread，加显式 FIFO/条件变量和 wait(0) 检查；不反复启动线程，不使用全局池、terminate 或 detach。
- 目录重建后丢失观察：读取期间父目录删除并重建，旧读取失败返回后虽然重新登记监听，却未再次读取已存在的新文件；若该修改发生在断连窗口，可能一直等不到下一次通知。现在维护检查同时核对实际存在性，重新登记后再复读。既有 directory 在重复/全量检查中真实失败，修正后 Debug 连续 100 次通过，见 build/review-config-directory-repeat.log。

新增 editor_reentry、late_watch、own_revert 三项真实回归，修复前 0/3，见 build/review-config-before.log；三项初始修复后 3/3，见 build/review-config-fixed.log。编辑测试通过真实 Kernel 驱动预检窗口注入 owner 重入；文件测试以 worker 完成条件暂停 owner 泵送，确定性覆盖提交/观察间隙，不靠 sleep。测试等待门改为共享状态，超时退出后晚到观察也不借用已销毁栈。

规范与质量：仅格式化第一方改动行和新增文件，核对参数对齐、直接 include、中文 Doxygen 与 UTF-8/LF。配置管理表复用原单元格，仅值变化时更新，减少周期性重建及模型信号；更新经模型接口，不跨信号保留可失效 item 指针。删除后台执行器未使用的头依赖。配置初始元数据、内容观察、外部通知和 Store 保存基线职责分开；普通 Ui/Core 不引入 Loader 依赖。复核父子清理、自动重建、CAS/关闭重入、弱来源、保护 Runtime、队列容量、读取/提交串行、线程 join、监听恢复与投递失败路径。

第一次全量还曾发生一次 late_watch 超时，保留于 build/review-config-ela-msvc-debug.log；原执行器的三项新增回归随后连续 100 次通过，因此不把该超时根因单独认定为线程池问题。提交前仍依据上述实现不符修正执行器，随后继续完整回归与重复测试。目录恢复失败另保留于 build/review-config-worker-repeat.log 和 build/review-config-final-asan.log。

审查后原目录 Ela/MSVC Debug、Release 各 350/350，无 Qt GCC Debug、Release 各 208/208，MSVC ASan UI/Loader 197/197（UI 88、Loader 133，其中 24 项重叠）。日志为 build/review-config-verified-ela-msvc-<mode>.log、build/review-config-core-gcc-<mode>.log 和 build/review-config-verified-asan.log。修正后的三个新增场景及目录恢复在 Debug/ASan 中各连续 20 次复核，另有目录恢复 Debug 100 次证据。

5 个公共头在 GCC/MSVC C++17 独立包含通过，其中 3 个无需 Qt 头路径。47 个变更文件 UTF-8/LF、Core 7 个编译单元/普通 Ui 的依赖隔离、15 个相关单元 ASan 插桩、本地文档链接/锚点及差异空白通过，见 build/review-config-audit.log。预编译 Qt 未插桩。未发现其他阻止提交的问题。本次将上述四轮开发及审查修复一并提交。Q4e1/Q4e2 的用户人工确认保留；Q4d3 新增原生体验、任意插件 connection/worker、ABI 和物理卸载仍按各自计划验证，不由本轮自动化代填。

## P3 父代上下文与子实例桥提交前审核

日期：2026-10-25。范围：8befcbd 之后全部未提交的 whenActiveAfter、类型化 Context.inject、示例、构建、测试及文档，包括新增文件。sources/research 和第三方组件源码未修改。

- P2，通知重入停止遗漏取消：发布成功上下文前把全部合格 Promise 从实例登记中移走，首项完成向调度器投递回调时重入 stop，后面的未完成观察不在取消集合中，stop 返回后仍可成功。现在完成通知期间保留原登记；stop 能同步取消其余任务，完成后只清除已终结记录。首项历史成功保持，不因后续停止改写。
- P2，新观察回放退役代次：在发布 ready/active/target 后才收集上下文观察，目标通知投递重入 update 并在退役期间登记新观察时，它被混入旧成功代次的完成集合，收到旧配置和已失效 Context。现在激活成功时先冻结观察集合及上下文快照，新登记只等待后续成功代次。
- 规范与质量：仅格式化第一方改动行和新增文件，整理参数对齐、直接 include、中文 Doxygen 及测试调度边界说明。冻结集合与保留可取消登记各自承担通知/生命周期职责，遍历快照不持有可被重入修改的容器引用，完成后统一清除。Runtime 不增加配置变体，Context 仍只弱持实例；Core 无 Loader/Qt/Ela 依赖。

新增 reentry_stop/reentry_update 使用真实 FakeScheduler FIFO，虚拟 post 边界只重入宿主，不提前执行任务。修复前 0/2，见 build/review-activation-before.log；修复后含既有观察及示例 7/7，见 build/review-activation-fixes.log。验证既有成功不改写、stop 返回前取消、退役期间登记不回放及新代配置/身份，不以业务回调被执行替代任务完成证据。

修复后原目录 Ela/MSVC Debug、Release 各 294/294，无 Qt GCC Debug、Release 各 190/190，MSVC ASan 相关回归 47/47。日志为 build/review-activation-<ela-msvc|core-gcc>-<mode>.log、build/review-activation-asan.log。instance.hpp 在两套 C++17 编译器中独立包含通过；12 个改动/新增文件 UTF-8/LF、Core 的 7 个编译单元依赖隔离、新增/修改单元 ASan 插桩、文档本地链接/锚点及差异空白通过，见 build/review-activation-audit.log。预编译 Qt 未插桩。

未发现其他阻止本次提交的问题。Q4c2c1、示例及上述修复已提交为 02a2ed7。提交时显式配置子树协调、管理页保存装配和其余 P3 工作仍待完成，本次不修改 UI 交互或人工验收记录。

## P3 Kernel 配置驱动与 Home 生命周期操作提交前审核

日期：2026-10-25。范围：4f743ed 之后全部未提交的 Q4c2a/b、实例附加依赖/重新加载、Home 状态卡片/左右开关、管理控制器、示例、测试及文档，包括新增文件。sources/research 和第三方组件源码未修改。

- P1，Core 内部投递失败可能永远等待：IScheduler 仍开放时，单次拒绝内部激活任务会使实例标记 deliveryBlocked，但驱动只观察未完成的稳定目标，动作和关闭可能一直等待，快照还显示正常 Pending。现在统一检查自有实例及 EffectScope 投递状态，挂载时发现阻断立即终结动作，业务和快照入口返回明确错误；关闭仍向其他自有实例广播停止，再返回失败并保守保留账目。internal_delivery 同时覆盖首次投递被拒绝、后续激活回执被拒绝和健康实例仍收到停止。
- P2，首页忙碌状态误显示为关闭：开关位置绑定 canStop，管理加载期间 busy 会使其变为 false，实际 Loading 实例显示关闭。现在把真实启用状态与操作权限分离，Loading/Active/Pending 显示开启，操作期间仍禁用；刷新屏蔽业务信号。power_busy 使用显式激活 gate 验证等待中的真实状态及没有多余管理请求。
- P2，重新加载已移动句柄崩溃：Kernel.remount 在读取来源快照前没有检查句柄，移动后空控制块导致空指针访问。现在返回 InvalidArgument；diagnostics_remount 增加已移动来源和保留副本验证。
- 规范与质量：仅格式化第一方改动行及新增文件，展开过度压缩语句，整理 include、直接依赖和中文 Doxygen。复用 Core 描述符校验、驱动健康检查及 Qt 状态等待辅助函数；明确停止缓存和操作历史各自的边界，不强行释放非静止实例。保留固定契约、有效依赖比较、部分状态再规划和真实稳定任务的分离。

三项回归在修复前 0/3，含真实崩溃；修复后 3/3。证据为 build/review-kernel-home-before.log、build/review-kernel-home-fixes.log。既有 delivery 回归同步要求阻断快照返回错误，并独立检查 Kernel 中资源仍然保留，不能把诊断错误解释成排空。

修复后原目录 Ela/MSVC Debug/Release 各 287/287，无 Qt GCC Debug/Release 各 183/183；MSVC ASan 的 UI/Loader 141/141（64 项 UI、77 项 Loader），Core 契约/remount 10/10。日志为 build/review-kernel-home-<ela-msvc|core-gcc>-<mode>.log、build/review-kernel-home-asan.log、build/review-kernel-home-core-asan.log。Kernel、ConfigKernel、管理 SDK 的无 Qt 独立包含、189 项本地文档链接、第一方 UTF-8/LF、差异空白及 Core 依赖隔离检查通过；修改单元的 ASan 插桩已核对，预编译 Qt 未插桩。

未发现其他阻止本次提交的问题。Q4c2a/b、Home 生命周期操作及上述修复已一并提交为 8befcbd；此前桌面人工验收确认保留，Home 变更仍待专项人工复核。显式配置子树、管理页保存装配、watcher/worker、ABI 与物理卸载继续按计划推进，ASan 不构成泄漏或物理卸载证明。

## P3 Ela 工作台与 Home 提交前审核

日期：2026-10-25。范围：88edada 之后全部未提交的工作台菜单/搜索/导航历史、插件直达与空工作区、Home 卡片、插件内部布局、Ela 本地修正、示例、测试及文档，包括新增文件。Core/Qt 适配实现、sources/research 未修改；公开 UI SDK 只更新页面贡献注释，未新增 Ela 类型。

- P1，首页刷新重入关闭后继续写控件：500 ms 刷新会发送卡片属性信号，回调可关闭外壳并推进事件循环，使根窗口/卡片先销毁，随后继续读取卡片详情。home_refresh_reentry 修复前被 ASan 报告 access-violation，栈位于 ElaPopularCard::getDetailedText。现在首页刷新与菜单操作通过宿主窗口访问屏障执行，关闭等待本轮写入栈返回；访问桥先复制再调用，刷新嵌套不重复进入。
- P2，关闭页签回落到失效页面：只尝试相邻页，导航门禁拒绝后仍删除当前页签，中央还显示已关闭视图。tab_fallback 修复前确认没有进入空工作区。现在按快照依次尝试已打开页，只有实际选择改变才删除原页签；均不可用时显示空画布，重入仍保持原页时保留其页签。
- P2，重入查询结果与输入不一致：textChanged 或模型复位回调规范化输入后，外层仍按旧文本发布建议，回车可打开错误页面。search_normalization 修复前真实失败。现在程序查询读取规范化后的实际文本，模型复位返回后再核对当前输入并收敛结果；平面建议模型对有效父索引返回零行。
- P2，卡片迁移后保留失效悬浮区域：卡片移到新容器、旧区域销毁后仍返回旧区域裸指针，按钮更新和绘制还依赖已销毁浮层。card_area_lifetime 修复前失败于悬空区域检查。现在区域/浮层均用 QPointer，缺少浮层时仍可更新/绘制卡片，重新指定区域可恢复浮层，悬浮定时器跳过失效区域。
- 规范与质量：展开新第一方文件中过度压缩的语句，统一缩进与 include 次序，补齐直接标准库/Qt 依赖，移除无用 include 和重复历史状态更新。卡片固定文案仅初始化一次，未增删卡片时不重复使流布局失效；提示文本转义，避免插件名称被当作富文本。第三方目录只修改相关本地补丁，不整体格式化上游文件。

前三项修复前 0/3 通过，证据为 build/review-ui-before.log；第四项见 build/review-ui-card-before.log。前三项定向修复验证见 build/review-ui-fixes.log。新增回归均检查真实 Qt 循环和完整关闭屏障，卡片迁移检查同时验证重新显示浮层。

修复后原目录 Ela/MSVC Debug/Release 各 259/259，无 Qt GCC Debug/Release 各 158/158，UI ASan 61/61 通过。日志为 build/review-workbench-ela-msvc-<mode>.log、build/review-workbench-core-gcc-<mode>.log、build/review-workbench-asan.log。新增首页/工作台、搜索及卡片/浮层等修改单元已核对 ASan 插桩，预编译 Qt 未插桩；pages.hpp 的无 Qt 独立包含检查及差异格式检查通过。

未发现其他阻止本次提交的问题。明暗/150% 缩放布局、空画布及示例自动化继续通过；原生 Windows 桌面人工清单保持待验，未代填为完成。Kernel 配置运行树、watcher、后台 I/O、管理页保存与 ABI 仍按原计划推进，ASan 不构成泄漏或物理卸载证明。

## P3 Q4d1/Q4d2 提交前审核

日期：2026-10-24。范围：7864de9 之后全部未提交的文件事务、Store、Qt 写后端、SDK 值类型/错误分类、构建、测试与文档，包括新增文件。sources/research 及 Core/Qt/UI/Ela 实现未改动。

- P2，Unchanged 回执缺少基线一致性校验：通用 Store 只检查磁盘值形状及目标文本，会接受“缺失文件变为存在”或 hash/原文与基线不一致的 Unchanged，错误推进 persistedRevision 并清除 dirty。现在要求此类回执的路径、存在性、hash 和原文均与提交前基线一致；非法回执形成 Failed，保留保存历史和 dirty，后续项 Skipped，关闭不能虚报刷新成功。
- 新增 p3.store_unchanged_protocol，覆盖缺失基线、hash 漂移、原文漂移、队列跳过及失败关闭。修复前实际失败于保存结果断言，见 build/review-q4d-before.log；修复后 13 项无 Qt Store 场景通过。缺陷位于通用 Store 对后端协议的校验边界，测试使用受控回执，不冒充真实磁盘写入证据。
- 规范与质量：集中基线快照比较；文件大小只查询一次，查询失败归 ConfigIo、超限归 ConfigInvalid；补齐 Qt 写后端空目标错误的 loader.file.target 阶段和标准库直接依赖。核对 UTF-8/LF、中文 Doxygen、公共头独立包含、队列容量、同修订共享、修订单调性、错误原因链、历史任务稳定性、关闭刷新与保守保留。
- 文件及依赖边界：核对原始字节/hash、缺失/空文件、路径绑定/链接拒绝、协作锁、二次基线比较、QSaveFile 禁止直接写回退及临时文件清理；真实文件/锁失败和 Windows 句柄导致的提交失败均保留旧文件。ConfigStore 保持无 Qt；ConfigStoreQt 复用文件事务，不新增第三方库或修改其源码。

修复后 Ela/MSVC Debug、Release 各 243/243；无 Qt GCC Debug、Release 各 158/158；MSVC ASan 56/56 Loader 场景通过，含 17 项 Store 场景。日志为 build/review-q4d-ela-msvc-<mode>.log、build/review-q4d-core-gcc-<mode>.log、build/review-q4d-asan.log。新库和测试均按现有配置插桩，预编译 Qt 未插桩。第一方暂存差异及新文件格式检查通过，未发现其他阻止本次提交的问题。

本次一并提交 Q4d1/Q4d2。Qt 写后端当前同步执行 I/O；后台 I/O、watcher、Kernel 运行树、管理页保存及全应用装配仍待后续实现。原生桌面人工验收未代填；非协作编辑窗口、网络/断电等文件边界沿用 ADR-0014，投递阻断保守保留资源，ASan 不构成泄漏或物理卸载证明。

## P3 Q4a/Q4b/Q4c1 提交前审核

日期：2026-10-24。范围：4b6139b 之后全部未提交的配置结构/预览、受限 YAML、内置定义/schema、通用协调器、SDK、示例、固定 LibYAML、构建、测试及文档，包含新增文件。sources/research、Core/Qt/UI/Ela 实现均未改动。

- P1，规划返回后的执行门禁缺失：驱动 plan 可同步重入并关闭 scheduler，原实现返回后仍调用 apply，导致无法投递回执时继续启动资源。规划抛错路径还可能先发布正常的失败报告，掩盖投递故障。现在集中检查投递能力，在同步规划返回、发布规划失败及下一步执行前重新核对；调度器已关闭时冻结协调，向未完成任务发布 SchedulerClosed。完成旧工作项后也重新检查关闭入口。
- 回归 p3.coordinator_planning_gate 在修复前真实失败（build/review-q4-before.log），确认关闭后仍产生驱动调用；修复后覆盖规划正常返回、抛错及重入协调器关闭，检查不启动动作、目标任务错误/取消结果及真实关闭证据。
- 规范与质量：修正公共类型和 safeToContinue 的过期/不准确注释，复用投递检查，保持 owner 状态与跨线程 Promise 门禁分离。为新增 C/YAML 类型及 LibYAML 许可固定 LF，避免 Windows 检出改变上游字节。核对 YAML 类型/重复键/预算、候选移动失效、schema 捕获释放、有效依赖联合、CAS 重入、最新目标、部分成功、Pending 历史及保守资源保留；核对公开头独立包含、C/C++ 工具链与静态库边界。
- 依赖来源：LibYAML 0.2.5 固定归档 SHA256 与 UPSTREAM.md 一致，10 个源码文件与本地原始解压目录逐一比对，均未修改；MIT 许可保留，构建不联网。导入原文已有空白不重排，第一方改动单独执行暂存格式检查。

修复后 Ela/MSVC Debug、Release 各 215/215；无 Qt GCC Debug、Release 各 145/145；MSVC ASan 28/28 Loader 场景通过，第一方库和 LibYAML C 库均插桩。日志为 build/review-q4-ela-msvc-<mode>.log、build/review-q4-core-gcc-<mode>.log、build/review-q4-asan.log。未发现其他阻止本次提交的问题。

本次完成 Q4a/Q4b/Q4c1，通用协议测试不代表 Kernel 运行树驱动或完整 L01–L04/S05 已验收。配置执行/保存、原生 Windows 人工交互、外部模块及 ABI 继续待办；非静止与投递阻断按契约保留资源，ASan 不证明泄漏或物理卸载安全。

## P3 Q3c 提交前审核

日期：2026-10-24。范围：8f15edd 之后全部未提交的 Ela 组件迁移、项目内基础库、原生导航与工作台布局、宿主模型、演示、测试和文档，包括新增文件。核对固定上游库与本地修改，保留 MIT 许可和来源；sources/research、Core 和公开 SDK 未改动。

- P1，紧凑菜单残留：菜单动作捕获节点裸指针，撤页未移除动作，分组菜单还以可复用的节点地址作键。compact_removal 在修复前确认旧动作仍存在。现在菜单/动作使用稳定 key，触发时重新查找节点，撤页同步删除旧动作、空菜单及已撤销分组的菜单；同组其他页面仍可导航。
- P1，恢复模式访问已删除分组：紧凑模式缓存展开节点裸指针，插件停止后恢复展开模式会解引用旧分组。removed_expanded_group 在修复前被 ASan 确认为 heap-use-after-free。现在只缓存分组 key，恢复时从当前模型解析，跳过已删除分组。
- P1，原生导航关闭重入：原生切页信号中可关闭外壳并处理 DeferredDelete，返回后仍访问导航私有对象和旧节点；程序导航还会用旧节点安排滚动。native_close_reentry 在修复前被 ASan 确认为 heap-use-after-free。现在切页/页脚信号返回后检查 QPointer 和模型归属，程序导航保存持久索引，窗口或节点消失时直接结束；鼠标释放路径也检查视图存活，宿主选择同步使用导航访问屏障。
- P2，通知返回序号：输出 textChanged 信号中重入发布通知会更新共享 sequence，外层返回后一个通知的序号。增强 notifications 在修复前实际失败。现在提交历史后保存本次序号，展示回调重入不会改变返回值，历史仍有界且为纯文本。
- 规范与质量：合并两个带分组的 addPageNode 重载中重复的四条菜单创建分支，集中处理动作创建与清理；拆开新增选择同步中的嵌套单行控制流，补齐 QMouseEvent/QPointer/QStringList 直接依赖并移除无用包含。核对模型项所有权、分组隔离、导航门禁、固定标签归属、主题适配、输出限额、关闭顺序、CMake 可选构建、原始许可与资源分发。

修复前证据：build/review-q3c-before.log（菜单残留和分组 UAF）、build/review-q3c-native-before.log（原生关闭 UAF）、build/review-q3c-notification-before.log（序号重入）。修复后 Ela/MSVC Debug、Release 各 187/187；无 Qt GCC Debug、Release 各 117/117；MSVC ASan 的 45/45 UI 场景通过，第一方与项目内 Ela 插桩，预编译 Qt 未插桩。完整日志为 build/review-q3c-ela-msvc-<mode>.log、build/review-q3c-core-gcc-<mode>.log、build/review-q3c-asan.log。未发现其他阻止本次提交的问题。

格式检查区分导入原文和本地改动：`git diff --cached --check -- . ':(exclude)third_party/ela/ElaWidgetTools/**'` 通过；Ela 相对固定上游目录的本地补丁没有新增空白告警。整个导入目录的暂存检查会报告上游已有的文件尾空行及少量行尾空格，保留原文以便后续比较，不为这些告警重排库源码；记录为 build/review-q3c-staged-check.log、build/review-q3c-vendor-check.log。

原生 Windows 缩放、主题动画和完整键鼠交互仍按 [桌面验收单](../tests/acceptance/p3-ui.md) 人工执行，未代填验收结果。清理失败/投递阻断按设计保留资源；ASan 结果不构成泄漏或 DLL 物理卸载证明。Loader/Store、模块管理和专用资源适配仍按后续工作包推进。

## P3 Q3b2 提交前审核

日期：2026-10-24。范围：4f3b808 之后全部未提交的管理页、诊断 SDK、Kernel/Instance 查询、外壳集成、示例、构建、测试与文档，包括新增文件。sources/research 未改动。

- P1，导航关闭重入：showManagement 切换堆栈会同步发出 Qt 信号；信号中关闭外壳并处理 DeferredDelete，可在切页调用返回前删除窗口，随后继续访问导航对象。navigation_reentry 在修复前实际 SegFault。现在程序入口与导航信号共用 selectManagement，以导航访问计数阻止窗口提前关闭；切页/选中信号返回后重查门禁，重入关闭返回 ContextClosing。回归验证信号栈内窗口仍存活、关闭未完成，调用返回后可完整删除。
- P2，取消选择仍可操作：QTableWidget.clearSelection 会保留 currentRow；原实现把当前单元格当作已选行，停止按钮仍可用，下一次刷新还会恢复旧选择。deselect 在修复前实际失败。现在只从 selectionModel 的唯一已选行解析 InstanceId，清除选择立即禁用操作，刷新保持无选择，重新选中后才开放操作。
- 规范与质量：管理页导航收敛到单一入口，补充 QItemSelectionModel 直接依赖及中文 Doxygen。核对 SDK 的值语义和无 Qt 头隔离、弱 Kernel 生命周期、同 scheduler/owner 门禁、依赖诊断不增加租约、请求去重/真实操作号/有界历史、纯文本展示、清理失败与投递阻断保留、UI 访问屏障和正常关闭顺序。未修改第三方代码。

修复前证据为 `build/review-q3b2-before.log` 和 `build/review-q3b2-navigation-before.log`；修复后 Ela/MSVC Debug、Release 各 178/178，无 Qt GCC Debug、Release 各 117/117。management.hpp、ela_shell.hpp、kernel.hpp 的 GCC C++17 独立包含及 git diff --check 通过。完整日志为 `build/review-q3b2-ela-msvc-<mode>.log`、`build/review-q3b2-core-gcc-<mode>.log`。

MSVC ASan 的 36/36 UI 场景通过，第一方与固定 Ela 库插桩，Qt 预编译库未插桩；日志 `build/review-q3b2-asan.log`。清理失败/投递阻断测试按设计保留未静止资源，该结果不构成泄漏或物理卸载证明。未发现其他阻止本次提交的问题；Loader/Store、配置/模块管理、完整子树、其他专用资源及原生 Windows 人工交互沿计划另验。

## P3 Q3b1 提交前审核

日期：2026-10-24。范围：0d9a5ac 之后全部未提交的命令/通知 SDK、宿主后端、effect 集成、窗口展示、示例、测试和文档，包括新增文件。sources/research 未改动。

- 注册门禁：MSVC std::function 的小对象移动可执行用户代码。旧 add 在检查宿主状态后移动 CommandSpec；若移动中重入 shell.close，随后仍创建命令 effect/列表项，此时宿主服务的关闭结果可能已经完成。现在先保留 Context 值副本，接管回调后重查宿主及本代 guard，再修改索引和创建 effect。新增 registration_reentry，固定在第二次移动触发关闭，确认请求被拒绝、索引和 effect 数量不变。
- 初始化屏障：列表项原先在 effect setup 返回后创建。Qt model 的 rowsInserted 信号中重入关闭并推进 owner 事件，清理会在列表构造返回前释放命令捕获。现在列表创建/登记全部纳入 effect setup，窗口及闭包必须等待初始化返回；token 返回后再补发宿主关闭请求。新增 row_reentry 验证嵌套循环期间捕获仍在、外壳未完成关闭，随后可完整排空。此问题通过生命周期断言复现，未声称已复现物理模块卸载或堆损坏。
- 错误展示：命令失败携带空 message 时，宿主将空文本转给通知服务，被空文本校验拒绝，界面没有失败提示。现在使用“命令执行失败”作为缺省文本。增强 interaction，确认禁用错误和空错误消息都形成可见错误通知。
- 质量和规范：补全 functional/memory/string 直接依赖；投递失败观察者只弱持 CommandControl 并写其原子标志，不再间接访问可在完成时移动清空的 CommandCall.record。增强 inflight，以两个独立 gate 逐个完成并发调用，确认第一个结束后停止仍 pending、最后一项结束后才释放捕获。核对中文 Doxygen、同 scheduler/epoch、错误线程关闭、谓词重入、排队撤销、worker 完成、关闭顺序、有界纯文本历史和无 Qt 构建隔离。

修复前两项失败见 `build/review-q3b-before.log`，列表初始化失败见 `build/review-q3b-row-before.log`。修复后 Ela/MSVC Debug、Release 各 164/164；无 Qt GCC Debug、Release 各 115/115。三个公共头 GCC C++17 独立包含、git diff --check 通过。日志为 `build/review-q3b-ela-msvc-<mode>.log`、`build/review-q3b-core-gcc-<mode>.log`。

MSVC ASan 专项 24/24 UI 场景通过，第一方与固定 Ela 库均插桩，Qt 预编译库未插桩；日志 `build/review-q3b-asan.log`。投递阻断场景按设计保留未静止资源，本检查不构成泄漏检查或任意等待环证明。未发现其他阻止本次提交的问题；管理页、原生 Windows 人工交互、Loader/Store、专用 connection/worker/watcher 及模块 ABI/物理卸载沿原计划另验。

## P3 Q3a 提交前审核

日期：2026-10-24。范围：b5b8bab 之后全部未提交的 Ela 构建、页面服务、外壳、Context.identity、测试、示例和文档，包括新增文件。sources/research 未改动。

- 正确性：PageHandle.close 原先只请求 effect 异步关闭，cleanup 排队期间仍可 navigate 和点击页面。现在 owner 关闭请求同步标记 revoked，输入及导航立即拒绝，retiring 快照也立即体现撤销；实际 QWidget 删除和 effect 完成仍沿原异步屏障。错误线程关闭不改变门禁。lifetime/input_gate 回归在修复前均失败。
- 输入边界：QShortcutEvent 的接收者是 QShortcut，旧过滤器仅识别 QWidget，会漏过停用页面的快捷键。现在沿 QObject parent 找到 QWidget 后判断页面归属。新增 shortcut_gate 验证 Active 能触发、停止后但删除前被阻断。中文输入法 QInputMethodEvent 也会绕过 KeyPress；input_gate 新增真实输入法提交验证，修复前确认修改了已关闭页面。过滤器补齐 InputMethod、MouseMove 和触摸后续事件，保留任意程序信号/外部 queued callback 仍需专用契约的边界。
- 导航：原先只连接 itemClicked，方向键改变选中行却不切换页面。改用 currentItemChanged，让键盘、鼠标与句柄使用同一导航门禁；新增 keyboard_navigation 检查方向键切页及关闭当前页后回安全页。
- 规范与质量：补全 utility/vector 直接依赖、移除未使用头；事件类型判断改为 switch；navigate 按值保留页面控制块，避免 Qt 调用重入时仅借用句柄成员；窗口关闭错误保留原 effect 的 causes；修正工厂释放与记录移除的注释。核对 Qt parent 所有权、工厂失败/关闭重入、同名贡献隔离、服务恢复、线程/epoch、窗口退出顺序、第三方固定 hash/许可、可选构建和脚本环境恢复。

修复前四项失败证据见 `build/review-q3-before.log`；输入法独立失败见 `build/review-q3-input-method-before.log`。修复后 Ela/MSVC Debug、Release 各 151/151；无 Qt GCC Debug、Release 各 115/115；两份新公共头 GCC C++17 独立包含、git diff --check 通过。日志为 `build/review-q3-ela-msvc-<mode>.log`、`build/review-q3-core-gcc-<mode>.log`。

MSVC ASan 专项 11/11 UI 场景通过，第一方和固定 Ela 库均插桩，Qt 预编译库未插桩；日志 `build/review-q3-asan.log`。开发阶段已经复现并规避的上游原生导航 UAF 见 [ADR-0006](adr/0006-ela-page-contributions.md)，本轮未修改第三方源码。未发现其他阻止提交的问题；命令/通知/管理页、原生 Windows 人工交互、connection/worker/watcher 及插件 ABI/卸载仍需后续验收。

## P3 Q2 提交前审核

日期：2026-10-24。范围：ce43cc6 之后全部未提交的 QObjectResource、QtClock、SDK、资源测试、构建登记与文档，包括新增文件。sources/research 未改动。

- 测试正确性：clock_close 原先在预期抛错的 discarded 回调内使用 CHECK。断言失败同样被隔离并计入 callbackErrors，可能被预期异常数量掩盖。现在回调只记录“重入 close 仍 pending”和“新计时被拒绝”的观察值，回调外独立断言，保留有意抛错以验证异常隔离。没有把这项测试缺陷记作生产关闭逻辑 bug。
- 回调所有权：waitTask 的通知标志改为共享值捕获，避免等待超时后栈已展开而排队通知仍访问局部变量。新增 p3.qt_resources_close_releases_handles，覆盖丢弃回调释放最后一个 QtClock facade 后继续完成整批通知，以及子对象析构释放最后一个 QObjectResource facade 后仍保持完整删除屏障。
- 规范与质量：补全直接使用的 memory/utility 标准头，拆开计时账目初始化；公共构造契约明确只有前置条件错误抛 invalid_argument，其他构造失败仍不转移所有权，并与 ADR 一致列出外部 deleteLater/WA_DeleteOnClose 限制。核对 owner 入口、令牌原子取消、已到期队列载荷、析构重入、异常隔离、退出兜底、Kernel 清理集成与无 Qt 构建边界。

Qt 5.15.2 msvc2019_64 + MSVC 19.44/v143 x64 Debug/Release 各 140/140；无 Qt GCC 13.1 Debug/Release 各 115/115。两份新公共头独立包含及 git diff --check 通过。完整日志：`build/review-q2-qt-msvc-<mode>.log`、`build/review-q2-core-gcc-<mode>.log`。

另以 MSVC /fsanitize=address、RelWithDebInfo、/MD、/O1 和 /EHsc 单独构建第一方 core/qt 与资源测试，14/14 资源场景通过，日志 `build/review-q2-asan.log`，重现命令见 [P3 Q2 记录](p3-development.md#q2-提交前审查)。Qt 5.15.2 预编译库未插桩，该结果不构成全程序内存安全或泄漏证明；未运行 TSan、connection 排空、worker join、Ela UI、ABI 或物理卸载验证。未发现其他阻止提交的问题。

## P3 Q0/Q1 提交前审核

日期：2026-10-24。范围：2e6efa2 之后全部未提交的 Qt 工具链配置、QtScheduler、SDK、测试与文档；并核对 FakeScheduler 的同类队列操作。sources/research 未改动。

- 正确性：QtScheduler 和 FakeScheduler 原先在队列锁内移动 ScheduledJob。MSVC std::function 的小对象存储会调用用户可调用对象的移动构造及移动后析构；若这些函数查询 isOpen，则重入同一把锁。现在在锁外创建 unique_ptr<ScheduledJob>，队列内只移动指针，执行和丢弃路径也不在锁内销毁用户对象。新增 p3.qt_scheduler_callable_reentry 与 w1.scheduler_callable_reentry，在修复前 MSVC Debug 两项均真实失败，见 `build/review-p3-before.log`；修复后均通过。
- 重入边界：FakeScheduler::runOne 在任务捕获析构前清除了 pumping，捕获的析构函数可提前运行后续任务。现在由后析构的 PumpGuard 恢复门禁，覆盖成功及异常返回。新增 w1.scheduler_destructor_guard；修复前 GCC Release 明确违反“后项仍排队”断言，见 `build/review-p3-guard-before.log`，修复后两个分支均通过。
- 所有权：QtScheduler 的 post/close 在进入用户代码前持有本地控制块，避免移动/丢弃回调释放 facade 与 Qt lifetime 后继续访问已释放控制块。新增 p3.qt_scheduler_close_releases_owner，验证丢弃回调释放全部所有者后抛错，仍完成后续通知；该场景为生命周期防护，未声称完成消毒器验证。
- 规范和质量：新增文件、类型及具名函数使用中文 Doxygen；重入探针由 Qt/Core 测试共用，避免只依赖编译器恰好使用堆存储。核对 owner 限制、锁外析构、关闭/投递竞争、异常隔离、账目与队列一致性，以及 CMake 可选目标、QT_NO_KEYWORDS 传播和开发脚本环境恢复。同步 ADR、接口契约和进度，保留初次验证与本轮审核的独立证据。

验证：Qt 5.15.2 msvc2019_64 + MSVC 19.44/v143 x64 Debug/Release 各 126/126；无 Qt GCC 13.1 Debug/Release 各 115/115。日志为 `build/review-p3-qt-msvc-<mode>.log`、`build/review-p3-core-gcc-<mode>.log`；QtScheduler/FakeScheduler 公共头仅使用 SDK include 的 GCC C++17 独立包含检查、git diff --check 通过。未发现其他阻止提交的问题。

本轮仍限 Q0 工具链与 Q1 调度；Q2 Timer/QObject 资源屏障、Ela UI、原生 ABI 与 DLL 卸载未实现或验收，未运行 ASan/TSan。

## W7 提交前审核

日期：2026-10-24。范围：25adfcc 之后全部未提交的联合示例、CTest 登记与验收/进度文档，包括新增文件。未改动核心 SDK 行为。

- 正确性与证据：原 recovery 只检查新 epoch/BindingId，没有实际分发恢复后的事件，无法证明新监听器连接到新服务。现在更换提供者前后均发送保存事件，检查恰好调用一次当前监听器、当前 Store 的实际文档内容和确认结果；旧 Store 必须在替换前过期，新监听器恰好一个。增强现有场景而非增加重复测试入口。
- 防御性检查：绑定读取集中到先验证数量的 binding()，避免空数组越界；清理失败快照先判断 optional 是否存在，弱引用只 lock 一次后读取，避免缺失诊断时空解引用。关闭检查复用不可变快照，并核对根、消费者及提供者的 Disposed、effects 和 committed 数量。
- 状态准确性：失败路径不再记录 consumer cleaned，改为 consumer cleanup failed，并断言提供者及根未运行清理。failure 示例返回成功只表示预期残留得到验证，不表示内核关闭成功。
- 规范与质量：核对所有新增文件、类型、具名函数的中文 Doxygen、标准头依赖和回调捕获所有权；统一资源查询与断言，整理进度工作记录表的空行，避免 Markdown 表格断裂。验收报告引用的 24 个唯一测试名均与 CTest 实际登记核对。

GCC 13.1、MSVC 19.44 x64 Debug/Release 各 113/113 通过，日志为 `build/review-w7-core-<compiler>-<mode>.log`；git diff --check 通过。未发现其他阻止提交的问题。未运行 ASan/TSan、Qt、ABI 或物理卸载验证，首阶段结论仅适用于文档明确的内置范围。

## W2/W3 提交前审核

日期：2026-10-23。范围：全部未提交的 W2 收尾、W3 Kernel/实例实现、SDK、测试及文档。

## 已修复问题

1. 移动后的 Runtime 保留原编号，但激活函数已为空。Kernel 仅核对编号会接受该句柄，随后访问空回调。现在移动后原句柄 id() 返回无效编号，Kernel 拒绝挂载；直接 Instance::mount 在登记资源前抛 invalid_argument。新增回归在修复前实际失败，日志为 `build/review-before-fix.log`。
2. 调度器关闭后 update/retry 可能先替换目标、分配操作，再投递失败或构造 EffectScope 抛错。现在在修改账目前返回 SchedulerClosed。回归同时检查 Active 更新和 Failed 重试被拒绝，操作编号、配置修订和状态保持原值。
3. 本机中文 MSVC 的 /showIncludes 前缀被 CMake 错误解码，Ninja 未记录头文件依赖。Release 增量构建因此混用旧 Kernel 目标和新 SDK。新增可选 CMake 缓存项 CORDIS_MSVC_SHOWINCLUDES_PREFIX，默认仍自动检测；本机使用 `注意: 包含文件:`。清洁构建后已核对依赖表，触碰 instance.hpp 会触发 instance.cpp、kernel.cpp 与 w3.cpp 重编译。

## 验证与边界

GCC 13.1、MSVC 19.44 x64 的 Debug/Release 各 61/61 通过。W3 共十组行为，共用一个 CTest 入口；新增测试不会增加 CTest 入口数量。日志为 `build/review-core-<compiler>-<mode>.log`；MSVC 清洁构建日志为 `build/review-build-core-msvc-<mode>.log`。`git diff --check` 通过。

本机 MSVC 配置需使用 W2 记录中的开发者环境及 TEMP/TMP 设置，再执行：

```powershell
cmake --preset core-msvc-debug '-DCORDIS_MSVC_SHOWINCLUDES_PREFIX=注意: 包含文件:'
cmake --preset core-msvc-release '-DCORDIS_MSVC_SHOWINCLUDES_PREFIX=注意: 包含文件:'
```

修复旧依赖数据库后需使用 `cmake --build --preset <preset> --clean-first` 重建一次。不要只修前缀而继续使用缺失依赖记录的旧目标。此覆盖参数仅在检测错误时使用，值应与实际编译器输出匹配。

未发现其他阻止本次增量提交的问题。W3 仍未完成独立 CleanupContext，服务/realm 未实现；历史操作未裁剪，非合作清理及投递阻断会保守保留资源。上述已知边界见 W3 记录；未运行 ASan/TSan、Qt 或 DLL 验证。

## W4 提交前审核

日期：2026-10-23。范围：c3f3661 之后全部未提交的 CleanupContext、服务注册表、实例依赖收敛、输出声明、realm、异步调用、测试及文档；包含新增未跟踪文件。

发现并修复一项高优先级问题：`Instance::change` 在实例已退役时通过 `scope.close().tryResult()` 查询清理失败。提供者此时可能仍在等待消费者退出，调用 close 会提前启动提供者自身清理，破坏消费者优先清理的约束。现在只在快照表明 scope 已开始关闭时查询其结果，等待下游期间更新仅替换目标配置。

新增 `w4.dependency_update_draining`：消费者清理由 gate 阻塞，提供者连续更新两次，验证 gate 完成前提供者不执行清理、不启动新代，旧操作为 Superseded；消费者仍能通过 CleanupContext 访问旧绑定，之后才完成提供者更新及依赖恢复。修复前真实复现，GCC Debug 85 项通过、该项失败，见 `build/review-w4-gcc-debug-before.log`。

修复后 GCC 13.1、MSVC 19.44 x64 Debug/Release 各 86/86 通过，日志为 `build/review-w4-core-<compiler>-<mode>.log`。同时修正文档中仍称服务依赖/realm 未实现的过期状态。`git diff --check` 通过，未发现其他阻止本次提交的问题。

已知范围：W5 可选依赖、父子所有权与联合等待图未实现；清理失败或投递阻断继续保守保留资源。未运行 ASan/TSan、Qt 或 DLL 验证；这些结果不构成物理卸载安全证明。

## W5 提交前审核

日期：2026-10-24。范围：748c8fb 之后全部未提交的子实例、父代清理屏障、实际/声明联合图、SDK、测试及文档，包括新增文件。

发现并修复一项操作结果一致性问题：实例已经返回 PendingMissingDependency 后，新的声明环或不兼容服务会使实例 Failed，但旧 target 已完成，再次 resolve(Failed) 无效，当前 operation 仍错误显示 Pending。现在激活前失败统一通过 failPreflight 收敛；若旧操作已稳定 Pending，先建立新 OperationId，再发布 Failed，配置修订和 epoch 不变，旧操作继续返回其历史 Pending 结果。

增强 `w5.children_declared_cycle` 检查自动失败操作与历史结果，新增 `w5.children_pending_failure` 检查晚到类型不兼容。修复前两项均真实失败（94/96 通过），见 `build/review-w5-before.log`。

修复后 GCC 13.1、MSVC 19.44 x64 Debug/Release 各 96/96 通过，见 [开发进度](progress.md)，日志为 `build/review-w5-core-<compiler>-<mode>.log`。`git diff --check` 通过，未发现其他阻止本次提交的问题。审查涵盖父代广播停止、子清理失败保留、更新重建、realm 继承和环路径。已知图边界仍为同一托管注册表，候选提供者多义时不猜测连边；不覆盖任意 Task 等待。未运行 ASan/TSan、Qt 或 DLL 验证。

## W6 提交前审核

日期：2026-10-24。范围：462c594 之后全部未提交的 EventBus、Context/EffectScope 适配、测试、示例及文档，包括新增文件。

正确性：复制成功的源结果至 serial 返回任务或 parallel 汇总时，用户负载的复制构造可能抛错。原实现会使内部 continuation 失败，返回任务可能永久 pending、自保留控制块不能解除；parallel 还可能在已经减少 pending 后漏记该监听器结果。现在 serial 将复制异常转换为 CallbackFailed，parallel 每项复制失败记入对应 Result，组装报告本身失败则拒绝返回任务；这些路径都释放已完成的分发屏障。新增 `w6.events_result_copy` 使用可抛复制负载，同时验证 serial/parallel 结果、activeDispatches 归零与所属作用域清理成功。

规范与质量：补全标准库直接依赖，移除未使用的 algorithm；补充负载可移动的编译期约束。快照先成功存入记录再增加保留数，避免 vector 扩容失败留下未记账的保留；析构使用已有记录逐项释放并通知，不再额外分配 Promise 容器。修正“最后快照销毁闭包”的过强注释，明确最终回调所有权仍由 effect 清理闭包释放；拆开多个成员赋值，便于逐项检查。

审查同时核对 once/过滤重入、Next 窗口、注销快照、epoch 门禁、scheduler 关闭、异步投递阻断及实例清理路径。GCC 13.1、MSVC 19.44 x64 Debug/Release 各 110/110 通过；event.hpp 独立包含及 git diff --check 通过，未发现其他阻止提交的问题。矩阵结果见 [开发进度](progress.md)，日志为 `build/review-w6-core-<compiler>-<mode>.log`。未运行 ASan/TSan、Qt 或 DLL 验证；任意用户等待环、跨 ABI、全局 schema 与 post 仍不在本轮范围。
