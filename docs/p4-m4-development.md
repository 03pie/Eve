# P4-M4 Qt UI 模块

日期：2026-10-29。M3 已完成；M4 进行中，不据此标记整个 P4 完成。开发遵循 [仓库架构原则](../AGENTS.md)，边界见 [ADR-0037](adr/0037-qt-module-boundaries.md)。

| 子段 | 职责与完成条件 | 状态 |
|---|---|---|
| M4a | 从原生管理器提取共同产物验证；Qt 元数据/工具链只读预检；独立测试入口及复用产物设施 | 已实现，审查及验证见 [记录](p4-m4a-development.md) |
| M4b1 | 提取共同目录/受限映像映射，实际接入 Qt root 装载、一次创建、失败驻留及 root 租约 | 已完成审查及验证，见 [记录](p4-m4b1-development.md) |
| M4b2 | IQtPluginV1 工厂协商、共享 Core、独立业务对象、末尾清理屏障与模块租约 | 已完成审查及验证，见 [记录](p4-m4b2-development.md) |
| M4c | 通过既有页面资源服务装配 UI DLL 示例，多实例/配置/完整析构/诊断及原生验收，收口 P4 | 待实施 |

每段修改前确认职责、依赖方向、状态与资源所有权；结构不能承载需求时先做必要重构。复用已有的 GenerationLedger、Core Runtime、UiPagesService 和 QObjectResource，不因新增后端而复制整套生命周期或页面系统。只在实际接入第二个后端时提取公共目录接口。
