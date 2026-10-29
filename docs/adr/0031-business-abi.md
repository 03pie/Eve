# ADR-0031：业务 ABI 完整函数表与协商

日期：2026-10-28。状态：M2b 协议和协商层实现，验证见 [开发记录](../p4-m2b-development.md)。

## 决定

在独立 plugin.h 定义 C11 业务入口 cordis_query_v1、固定线码、HostTable 与 PluginApi，不扩张 M2a 的只读描述入口。完整函数族及逐项所有权以 [ABI V1](../abi-v1.md) 为准；不发布只含 create/start 的不完整 HostTable，也不在生产中填充永远成功的占位函数。

基础生命周期与 complete/log/cancel 必需。dispatch、service、effect 以已知能力位协商；启用的整族槽必需，未启用的槽为空。service 提供者归属 effect，因此 SERVICES 依赖 EFFECTS。required_host_capabilities 必须包含在实际协商集合中；未知位拒绝。

首版仅复制有界 JSON/文本，不提供共享 buffer，不允许跨 CRT 分配/释放。整数 handle/request 与 callback token 区分归属；模块自行 create/destroy 实例，宿主拥有上下文、effect、binding 和请求。HostTable 寿命覆盖所有潜在使用者。

采用 Windows x64 自然布局与明确调用约定。同 major 的已知完整前缀可接受新 minor/尾部，但不能静默接受未知能力。不兼容签名/语义变化必须升级 major。C/C++ 共享完整 sizeof/align/offsetof 断言。

协商实现位于独立 Cordis::ModuleAbi，不依赖 Qt/Windows/Ela。私有 queryBusiness 先校验宿主，再调用 query，随后校验插件前缀、函数族和 manifest 身份并复制元数据；不执行生命周期。查询异常或非法状态拒绝，不绕过模块驻留策略。

异步入口允许 complete 与返回值竞态，宿主须在入口返回后核对一次仲裁结果；取消不免除完成义务。API 调用退出、request 完成、线程退出及资源捕获销毁是不同屏障，不能相互替代。生产桥及 ModuleLease 在真实资源上的集成仍属于 M3。

## 验证范围

纯 C 模块与无 Qt 测试检查协商、容量、复制和生命周期约束；Windows 夹具使用 M2a 固定/PIN 的真实 DLL，读取业务导出后走相同协商器。测试中的宿主可选槽是协商哨兵，没有冒充已实现服务/effect 系统。公开 ModuleManager 仍只执行只读发现，不提供可调用指针；M3 将把协商接入宿主内部实例桥。

M2 的装载/ABI 协商层完成不等于 P4 完成。Kernel/Session 接入、生产请求竞态、服务/effect 桥、同工具链 UI DLL 与 P5 替换/卸载继续独立验收。
