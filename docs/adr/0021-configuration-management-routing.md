# ADR-0021：Home 与管理页的配置路由

日期：2026-10-25。范围：Q4e2，继 [配置会话与页面](0020-configuration-session-ui.md) 接入既有工作台。

## 能力与依赖

Cordis::Ui 声明不包含 Loader 类型的宿主协议 IConfigurationManagement，提供路径值快照、期望启停及完整目标重新协调。可选 Cordis::ConfigUi 的 createConfigurationManagement 适配 ConfigurationSession，弱持会话，要求使用同一个 owner scheduler。管理器弱持适配器，宿主必须保活；普通插件的 diagnostics/v1 只能读取值快照，不能取得配置变更能力。Core 与普通 Ui 不增加 Loader 依赖。

宿主先 attach 同一 Kernel，再一次性 attachConfiguration(provider, protectedRuntimes)。保护的是 Runtime 定义的所有实例，宿主必须为配置独占这些定义，并传入驱动使用的全部定义；也可保留内部配置控制台的定义，防止其被手动停止。管理器验证 ID 在绑定 Kernel 中、无重复、无相关在途手动操作；同一绑定幂等，不提供更换或解除入口。协议无法推断任意宿主适配器的实际 Kernel，匹配驱动与保护集合仍是宿主装配责任。

配置拥有实例从手动 Stop/Retry/Load、终结缓存及 Home 页面贡献兜底中排除。该保护独立于适配器是否还存活；来源关闭、读取抛错或失效后保留最近条目并禁用操作，不能降级成手动管理。其他定义的手动实例流程继续使用原有语义。

## 身份、启停与回执

配置路径是管理身份，InstanceId/epoch 只是当前实际状态。快照包含本地期望、父级影响后的有效启用、实际状态、等待父项、修订与保存状态；禁用或尚未挂载的路径不伪造实例，也不会从 Home 消失。配置目标删除路径时才移除对应卡片。

Home 卡片仍只展示状态和一个 Ela 左右开关，不增加点击跳页。配置开关修改 Session 目标，本地启用不意味着实际已经运行；子项受父停用约束时不会偷偷启用祖先。卡片按 configuration:<path> 复用，重建后更新实例号，不重复显示页面贡献或内部控制台。程序刷新屏蔽 toggled 信号。

Home 信号发出时捕获路径和修订，排队后提交管理器；管理器接受及实际派发前分别核对来源、修订、路径权限、busy 与关闭状态。后台编辑使旧请求得到 ConfigConflict，不以新刷新到的修订重新解释旧点击。每个路径最多一个在途管理请求，历史沿用有界管理记录。

管理页增加“配置条目”，显示期望、实际及保存状态，提供启用、停用和“重新协调目标”。后者只在选中有效启用的失败条目时可用，但会重新接受并协调当前完整目标，可能重试其他失败条目；不承诺单项重试，不调用 Instance.retry。此操作同样增加目标修订并使 Store dirty。没有手动实例时隐藏上方空表格，为配置列表留出空间。

回执包含真实目标修订和 Applied/Pending/Failed/Superseded/Cancelled，不把 Pending 解释为成功运行。管理历史保留捕获修订、路径及目标结果；无效修订/枚举回执转为协议错误。运行失败仍保留已接受期望，保存继续经配置页显式执行，关闭仍先完成会话运行与持久化屏障。

## 重入与退出

外部适配器回调可抛错或同步重入关闭，绑定/读取设独立门禁，不在回调后继续使用过期权限。管理模型和 Home 控件写入均在既有窗口访问屏障内，Qt 信号重入关闭并泵送 DeferredDelete 不得销毁当前栈中的控件。关门取消未派发动作；已接受目标继续由独立 Session 收敛及退出处理。适配器不接管会话关闭责任。

cordis_config_workbench 绑定配置及内部控制台定义，使用原 Debug/Release 目录；cordis_ela_demo 仍演示手动模式。启动仍只读取配置编辑区，不自动接受或写入。文件 watcher、后台 I/O 与专用资源屏障另在 Q4d3 接入。

验证及人工复核见 [Q4e2 记录](../p3-development.md#q4e2-home-与管理页配置路由)和 [补充清单](../../tests/acceptance/p3-ui.md#q4e2-home管理路由补充验收)。

## 后续状态

Q4e2 已获本次用户人工确认。上述 Q4d3 待办已在 [ADR-0022](0022-configuration-background-io-watcher.md) 交付配置专用监听/后台 I/O，新增原生体验另列待验。
