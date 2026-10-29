# P4-M3d 联合接入

日期：2026-10-29。基线为远端优先合并提交 d6d003f，沿用远端 NativeServices 实现，不恢复已淘汰的另一套服务桥。

| 子段 | 交付 | 状态 |
|---|---|---|
| M3d1 | Core 托管 Runtime、Kernel 登记、冻结模块配置目录、Session/真实 DLL 夹具联合回归 | 已完成（本段审核与最终验证通过）；见 [开发记录](p4-m3d1-development.md) |
| M3d2 | 两个业务服务 DLL、真实 Qt 事件循环、多实例、失败驻留、S06/S07 与完整退出示例 | 已完成：M3d2a 业务/Qt 配置链路与 M3d2b 分层 S06/S07 验证均通过，见 [记录](p4-m3d2b-development.md) |

每段实现后审核 bug、规范和质量，修正并验证后提交，再进入下一段。M3d1 的回归夹具不替代业务模块交付，也不将 Kernel/Session 基础接入等同于 M3d 或 P4 整包完成。M3d2 完成后继续 M4 同工具链 UI DLL 与 P4 收口。

M3d2 按可独立验收的职责分段：M3d2a 交付两个无 Qt/Core 依赖的业务 DLL、真实 Qt/Kernel/Session/磁盘保存、多实例/realm、启动与清理故障、完整正常退出，见 [实现与验证](p4-m3d2a-development.md)。M3d2b 单独验证 stop PENDING 后取消仍晚到 completion（S06）及失活回调与 DLL 内捕获析构（S07）。M3d2a 不将普通退出结果映射为 S06/S07 通过。

2026-10-29 收口：M3d2b 采用真实 DLL/生产 RequestChannel 验证 Stop 取消和晚到完成，并单独验证生产 HostTable 的 PENDING stop；Core 托管/真实 Qt 验证 S07 的失活抑制、DLL 捕获析构及最后宿主副本退役。NativeModule 没有公开 stop-cancel API，未为验收新增接口。M3d/M3 当前范围已完成，下一段 M4；完整分层边界见 [验收记录](p4-m3d2b-development.md)。
