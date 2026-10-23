# W7 首阶段内核联合验收

日期：2026-10-24。状态：首阶段工作单范围已完成。范围：W0–W7 同工具链、无 Qt 内置内核；不包含跨 ABI、物理卸载、配置 Loader 或 GUI。

## 可运行闭环

[platform_demo.cpp](../examples/builtin/platform_demo.cpp) 将真实 Kernel、父子实例、ServiceRegistry 和 EventBus 组合在一起。根先注入强依赖缺失的消费者，再注入存储提供者；消费者保持配置恢复，并拥有异步保存事件监听器。每一步打印真实资源快照，协议断言失败则返回非零退出码。示例回调捕获共享宿主状态，不捕获会在失败残留前销毁的栈对象。

默认启用 `CORDIS_BUILD_EXAMPLES=ON`，构建与运行：

```powershell
./tools/dev.ps1 -Action verify -Preset core-gcc-debug
./build/core-gcc-debug/examples/builtin/cordis_platform_demo.exe shutdown
./build/core-gcc-debug/examples/builtin/cordis_platform_demo.exe recovery
./build/core-gcc-debug/examples/builtin/cordis_platform_demo.exe failure
```

| 场景 / CTest | 真实行为与退出断言 |
|---|---|
| shutdown / w7.platform_shutdown | 事件进入保存确认阶段后关闭根；新业务拒绝，监听器失活但在途快照、旧服务及租约保留；确认后消费者→提供者→根清理，关闭报告 quiescent=true |
| recovery / w7.platform_recovery | 缺依赖不运行消费者；提供者出现后激活；退出使消费者再次 Pending，旧监听器注销且旧对象释放；新 BindingId 驱动新 epoch，配置仍为 kept，旧引用失效；更换前后均实际分发事件并核对当前服务内容及确认结果，最终正常关闭 |
| failure / w7.platform_failure | 消费者清理失败报告 quiescent=false；committed=1、服务仍 open；提供者与根仍等待，内核关闭任务 pending，不清空资源伪报成功 |

保存示例先同步写入服务，再等待异步确认；不声称实现生产存储或磁盘 I/O。在途确认固定整个消费者资源作用域，服务保留由 committed 账目提供；异步 ServiceRef::invoke 的独立调用屏障仍由 W4 测试覆盖。

正常关闭时检查：根 Disposed、直接子记录清空、消费者 Disposed、消费者及提供者 effects 为空、Kernel 直接实例表为空、注册表无绑定、服务弱引用过期、监听器/在途分发/调度队列均归零，关闭报告无错误。恢复后新提供者也必须 Disposed。失败场景的成功退出仅表示上述“保留并报告”的断言成立，绝不表示内核已成功关闭。

## A01–A10 证据汇总

以下各项均有真实自动化断言。部分既有行为共用 `w3.single_activation` 入口，测试入口数不代表行为数。

| 验收 | 主要证据 |
|---|---|
| A01 缺依赖与恢复 | w4.dependency_recovery、w7.platform_recovery |
| A02 提供者退出与旧绑定清理 | w4.dependency_recovery、w4.committed_retirement、w7.platform_shutdown |
| A03 同名新绑定恢复 | w4.dependency_recovery、w7.platform_recovery |
| A04 重复清理、组内逆序 | w2.owner_once、w2.reverse_order、w2.broadcast_cancel |
| A05 setup 屏障及重入停止 | w2.reentrant_setup、w3.single_activation、w5.children_reentrant_stop |
| A06 清理异常、非合作任务与超时观察 | w2.cleanup_errors、w2.timeout_barriers、w4.dependency_blocked_cleanup、w7.platform_failure |
| A07 父子与传递依赖 | w4.dependency_transitive、w5.children_ordered、w5.children_cycle |
| A08 配置更新覆盖与旧代失活 | w3.single_activation、w4.dependency_update_draining、w5.children_update_realms |
| A09 多实例与精确域隔离 | w3.single_activation、w4.contract_realms、w5.children_update_realms |
| A10 once、注销快照与闭包屏障 | w6.events_once_filter、w6.events_snapshot_lifetime、w6.events_lifetime、w6.events_next |

附加契约证据：W5 的声明/所有权联合环路径；W6 的五类分发、Observer 错误隔离、复制异常及投递阻断。各包的细节与限制保留在原记录。

## 验证结果与边界

GCC 13.1、MSVC 19.44 x64 Debug/Release 四配置各 113/113 通过，统一见 [开发进度](progress.md)，提交前复核日志为 `build/review-w7-core-<compiler>-<mode>.log`，审查结论见 [代码审查记录](code-review.md)。新增三项 CTest 直接运行示例的三个完整场景，标签为 `w7;acceptance;example`。所有场景使用 FakeScheduler 和可控 gate；没有真实 sleep 或忙循环等待。

W0–W7 的首阶段工作单与 A01–A10 在上述内置范围收口。仍需后续验证：Qt/Ela 适配与生产调度器、全局事件 schema/post、跨 ABI/ModuleLease、物理卸载、Loader 与受控替换。未运行 ASan/TSan、真实线程池压力或 DLL 验证；不能将本轮结论解释为完整平台或物理卸载安全已经实现。

后续从 P3 前置条件开始：重新核实 Qt 5.15.2 kit 与 MSVC 组合，先冻结 Qt scheduler/事件循环关闭契约，再进入桌面适配；旧 Qt 环境报告仅是历史证据。本轮未因未来 GUI 需求修改只读参考材料。
