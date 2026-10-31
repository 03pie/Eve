# ADR-0047：配置与运行共用不可变定义目录

日期：2026-10-31。状态：已接受。基线 fad636a。接续[模块预约](0046-module-replacement-reservation.md)，完整范围见 [P5](../p5-development.md)。

## 问题

Session 独立保存 PluginDefinition/schema，Kernel 驱动另有 Runtime、来源、可选依赖、可用性与预算五份映射。现有静态装配可以依靠宿主保持一致，但后续换代不能仅替换工厂后继续接受用旧 schema 校验的候选，也不能让可重入预检在目标 CAS 前改变定义来源。

RuntimeId 对应插件定义与模块代次；不采用保持旧 RuntimeId 并修改其内部工厂的方案。新代必须有独立 RuntimeId，实际实例重建后也有新 InstanceId。目录身份与这些运行身份分开，不将文本相同当作同一次定义校验。

## 决定

增加宿主静态库 Cordis::ConfigDefinitions，ConfigurationDefinitions 一次构造并保持不可变；每行 ConfiguredPlugin 同时拥有 schema、模块来源、实际 Runtime、可选类型化依赖、激活门禁及依赖预算。驱动的五份映射收敛为同一个目录，Session 经 IConfiguredReconcileDriver 取得同一目录，不依赖具体 Kernel 驱动。构造只做结构、类型化依赖和来源一致性检查，不执行 schema 或插件；驱动再核对实际 Kernel 的 Runtime 归属。

校验结果只保留无回调的不可伪造共享身份，不持有目录、schema 或 Runtime。完整校验期间先强持目录数据，schema 重入释放最后门面也不提前释放工厂。驱动直接 preflight/plan/apply、Coordinator 的预检前后和 Session 发布草稿前都核对身份；Session 最终核对必须位于驱动快照等所有外部回调之后，再检查关闭状态，失效候选不能进入期望目标及保存账目。

保留旧 vector<KernelPlugin> 构造与独立 validateCandidate 用于静态/协议宿主；旧驱动只接受没有完整目录身份的候选。新驱动采用命名工厂 configured，避免新增 shared_ptr 构造重载破坏旧 `{}` 调用。完整配置 Session 不再接收另一份 schema；向完整驱动传入旧式独立 schema 构造明确拒绝。

ConfigYaml 继续独立负责文本/schema 校验，无 Core、Runtime、Module 或 Qt 依赖。其定义规范化逻辑提取为私有公共实现，ConfigDefinitions 依赖 ConfigYaml/Core；Coordinator、Session 和 Kernel 适配器向下使用它，不形成循环。普通 Core、C ABI 和模块目录账目不改变。

## 边界

本段没有公开可变目录指针或局部工厂切换 API。后续 ReplacementOperation 必须同时持有模块预约、Core 作用域、旧目录/配置及目标版本，授权候选创建并整体提交服务、定义及模块 Current 角色；不能由独立的 setDefinitions 绕过。不同目录的独立 Session 并存测试不等于替换成功。

三个生产示例宿主迁入共同目录。旧源码入口保留，源码 SDK 仍须一起重建；本段不改变 Core/IQt 插件协议或 C ABI，项目 SDK 版本保持 0.1.1。schema 的无副作用约束仍由宿主负责，目录身份不证明外部捕获值不可变。验证与限制见 [M2c4 记录](../p5-m2c4-development.md)。
