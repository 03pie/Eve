# ADR-0020：配置会话与可选 Ela 配置页

日期：2026-10-25。范围：Q4e1，在 [内置配置子树](0019-configuration-subtrees.md) 和 [配置 Store](0015-configuration-store.md) 之上连接编辑、期望启停、运行及保存。

## 依赖与能力边界

新增不依赖 Qt 的 Cordis::ConfigSession，公开 ConfigurationSession。宿主提供独占的 IReconcileDriver、固定内置定义/schema、IConfigurationWriter 及原始磁盘基线，会话内部拥有 Coordinator 和 Store，不将可变子对象交给 UI。Core 保持无 Loader/Qt/Ela 依赖。

可选 Cordis::ConfigUi 依赖 Cordis::Ui 和 ConfigSession，以 createConfigurationPage 创建宿主页组件；普通 Ui SDK 及现有外壳不依赖配置模块。此页不注册为普通插件可消费的管理服务。新工作台示例只由宿主把页面贡献给现有 ElaShell，笔记 Runtime 从会话的配置驱动启动；未绑定直接 Stop/Load 的手动实例管理器，因此不能绕过配置目标停止配置拥有的实例。

原 cordis_ela_demo 仍演示手动实例管理，新 cordis_config_workbench 演示配置拥有实例的完整链路；两者使用同一既有构建目录。后续统一 Home/管理入口时须按路径路由到配置目标，不能把两种管理权限混用。

## 预览、接受与启停

preview 只解析、结构/schema 校验、驱动能力预检和生成目标差异，不接受、不挂载、不写盘。ConfigurationDraft 的构造私有，绑定来源会话及基准目标修订，完整值可复制/移动但不能部分赋值。apply 拒绝跨会话、已移动或过期候选，接受仍使用 Coordinator 的 CAS 和真实运行规划。

Coordinator 接受后在同一 owner 栈内 track 到 Store，目标修订取自 Coordinator，二者保持同一不可变目标指针。写操作期间拒绝嵌套变更，诊断不发布中间的“目标已变而 Store 未跟踪”快照。schema/driver 关闭重入会及时关闭 Coordinator 接受门禁；若目标已经线性化接受才重入 close，Store 的关闭延迟到该业务栈返回，已接受目标仍会被跟踪并最终保存。

setEnabled 修改目标路径的本地 disabled，而不是调用 Instance.stop。实现复用受限 YAML 值树，精确定位元数据节点，保留其他条目、config 负载、realm 及 inject，再完整校验并接受。父级禁用继续级联；启用一个仍受父禁用的子项不会偷偷启用祖先。负载中的 id/disabled 字段不会被文本替换误改。切换后编辑器显示规范文本，不承诺保留注释和排版。

## 保存、冲突与退出

save 使用当前期望修订，Store 的历史保存、dirty、有界写队列保持独立于运行结果。运行失败不回滚已接受目标，也不阻止用户保存该目标；旧修订保存完成不能把更新目标标为干净。

磁盘冲突保留外部原文，页面展示供用户审阅，不自动采用或覆盖。rebaseExternal 只在用户明确选择后使用已捕获的冲突版本作为新基线，不接受其目标，也不触发写入；之后仍须显式保存。保存执行时继续二次比较磁盘版本，新的外部修改仍会冲突。

Session.close 同步关门，独立等待 Coordinator 的真实 quiescent 和 Store 的最终保存，两者都成功才返回成功。投递丢失、资源残留或写入失败不能冒充关闭成功；错误回执仅跨线程写原子门禁/Promise，不从 worker 改 owner 账目。显式关闭仍是宿主责任。

交互示例先请求保存并检查 dirty，保存冲突时保留可编辑会话和窗口，调用 ElaShell.cancelCloseRequest 恢复关闭按钮，引导用户在配置页处理冲突；干净后才进入不可撤销的 Session/Kernel/Shell/scheduler 关闭链。低层 Session.close 是终结操作，不能在失败后假定可恢复编辑。未应用的编辑不进入 Store；界面明确说明退出刷新的是已应用修改。

## 页面交互与生命周期

编辑变更立即使预览失效；后台刷新不覆盖编辑区。应用按钮检查候选基准修订，外部目标变化使旧候选不可应用。预检期间重入编辑会丢弃旧输入候选；应用/启停期间编辑则保留新文字，已接受目标仍报告真实结果，新文字须重新预览应用。调用外部驱动前固定候选及输入快照，不借用可被编辑信号清除的候选容器。条目表分开显示期望和实际状态，启停用 Ela 左右开关；正在编辑未应用内容时先要求应用或使用已应用目标，避免切换时悄悄丢弃编辑。

开关信号先捕获路径和期望修订，再排队执行；选择变化不能把旧信号作用到另一行。程序同步用 QSignalBlocker，不产生业务请求。保存历史、执行失败、后台错误和原始冲突文本按纯文本呈现；差异使用业务名称，不能把枚举编号当作交互说明。控件采用 ElaText、ElaPlainTextEdit、ElaTableView、ElaPushButton、ElaToggleSwitch，并显式处理主题、字号、表头列宽与 150% 缩放。

模型/属性信号可重入关闭并泵送事件。页面在 UI 写入期间拦住 DeferredDelete，最后一层访问退出后通过应用队列完成销毁，避免 QWidget 栈内被删除；已经接收的 DeferredDelete 不能简单再次 deleteLater，否则 Qt 的一次投递标志可能使销毁永远不再发生。定时刷新仅在值变化时重设只读详情，保留用户滚动/选择。页面弱持会话，销毁页签不结束配置运行。

## 验证与后续

会话专项使用真实 Kernel、Coordinator、Store 和受控写回执，覆盖预览/CAS、跨会话及移动来源、父子启停、旧写入/新 dirty、冲突/rebase、运行失败仍可保存、关闭重入、线程/投递门禁及“保存成功但清理失败”。接受线性化后、调用返回前的投递重入关闭单独验证不会漏 track。

Qt 专项用真实临时文件和真实事件循环检查编辑/启停/保存、外部冲突、过期候选、模型信号中延迟销毁、明暗/缩放，以及取消关闭请求后的再次退出；两项新工作台 smoke 使用临时文件，不写用户默认配置。实测与截图见 [开发记录](../p3-development.md#q4e1-配置会话与-ela-配置页)。

此阶段提供可复用配置会话、配置页及独立工作台入口。统一原 Home/管理页路由、文件 watcher、后台 I/O、专用 worker/connection 屏障与动态模块仍待后续接入；新配置工作台的原生桌面人工补充验收保持待验，不覆盖此前用户确认的 Q3c 结果。

## Q4e2 后续演进

2026-10-25 用户已确认 Q4e1 配置工作台验收。上述未绑定手动管理器及统一路由待办描述的是 Q4e1 交付状态；Q4e2 通过 [ADR-0021](0021-configuration-management-routing.md) 的独立宿主协议统一 Home/管理页，保留配置权限边界，新增路由人工项单列待验。
