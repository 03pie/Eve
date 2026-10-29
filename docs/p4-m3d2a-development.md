# P4-M3d2a 业务 DLL 与真实 Qt 配置链路

日期：2026-10-29。基线 02d2ca9。M3d2a 实现、审查及最终验证完成，随本提交归档；M3d2b/S06/S07 与 M4 待实施。

## 交付与职责

examples/native_services 提供 counter/reporter 两个业务 DLL（源文件为 counter.cpp/reporter.cpp）、公共 C ABI 协商/有界实例辅助代码、配置和真实 Qt 命令行宿主。DLL 不链接 Core/Qt，不导出测试控制入口；STL 对象仅在 DLL 内部使用。宿主沿用远端 NativeServices 及 M3d1 托管 API，没有增加公共 SDK 接口或绕过配置目录。

counter 每实例维护独立 uint64 计数；reporter 的启动和清理经 service_invoke→request_complete→complete 完成，两端请求身份分别管理。启动结果和 Core/effect 共同决定服务发布；清理时读取的是 Core 预提交的旧绑定。32 槽预算、单调实例编号、溢出检查和固定缓冲区限制示例资源；启动和清理故障通过显式配置展示，不用未定义行为制造失败。

配置先经冻结目录和无副作用 schema/preview，再由 Kernel/Session 执行。QtConfigurationWriter 实际提交独立临时文件，重新读取并与 persisted 的规范文本比较；普通启动失败仍保存 desired，运行结果与持久化状态分别报告。正常退出等待 Session→Kernel→NativeModule→generation drain，确认服务键和执行引用清空、DLL Resident/PIN，再确认队列无待处理/丢弃/异常回执并关闭 QtScheduler。清理失败则核对不静止、drain 失败和执行引用保留。

DLL 链接后由 cmake/WriteModuleManifest.cmake 生成真实 SHA256 声明，声明列为 Ninja byproduct；支持原目录 Debug/Release/ASan 和关闭示例的检查入口。没有修改 Core、Loader、Ui、业务 ABI、只读参考或第三方源码。

## 审查与映射

初版只设置 RUNTIME_OUTPUT_DIRECTORY，Windows MODULE 仍输出到构建子目录，导致五项首次检查均找不到稳定声明，见 build/p4-m3d2a-first-{build,tests}.log。已同时指定 LIBRARY_OUTPUT_DIRECTORY，保持宿主根边界校验，修复后五项通过，见 layout-{build,tests}.log（相同 p4-m3d2a- 前缀）。没有通过放宽路径检查解决产物布局问题。

复核 C ABI 查询容量/能力与静态描述寿命、失败空句柄、服务/effect 归属、异步启动/清理与 ACK 身份、有界 JSON 复制、实际 owner 调用、故障驻留以及 Qt 事件回调异常边界。补充保存内容精确比对、调度队列审计、Qt Unicode 参数和链接产物摘要追踪。未发现阻止本段提交的剩余问题。

| 场景 | 自动化依据 |
|---|---|
| 冻结目录预览不执行 DLL、真实配置激活/磁盘保存/完整退出 | p4.native_qt_normal |
| 同代多个报告器共享计数器，结果分别为 1/2，租约各一份 | p4.native_qt_multi |
| 两组 counter/reporter 的精确 realm 隔离，分别从 1 开始 | p4.native_qt_realms |
| 报告器启动失败，计划 Failed、模块未隔离、正常资源排空 | p4.native_qt_start_failure |
| 服务清理失败，Session/Kernel/模块均不假静止、PIN 与引用保留 | p4.native_qt_cleanup_failure |

report/read 方法由 DLL 提供，本轮端到端检查以启动业务日志观察调用结果，不声称已单独覆盖其请求分支。S06 要求 stop PENDING 后取消仍晚到完成，S07 要求失活回调不执行而捕获在 DLL 内析构；以上五项不能替代它们。M3d2b 继续实施这两项专项，M3d 和整个 P4 保持进行中。

## 最终验证

| 范围 | 结果 |
|---|---|
| 原目录 MSVC Release 全量 | 612/612 |
| MSVC Debug / ASan P4 | 各 144/144 |
| 无 Qt GCC Debug 全量 | 245/245；两个 DLL 源另经 GCC C++17 严格语法检查，不作为 GCC/MSVC ABI 互通证明 |
| 原 Release 关闭示例 P4 | 144/144，已恢复 ON 并重新构建 |
| 五个真实 Qt 场景 | Debug/ASan 各连续 20 次，共各 100 次通过 |
| 构建/边界 | 8 项源码/构建快照一致；3 个新增 C++ 编译命令含 ASan；三配置共 6 份 DLL 声明/摘要与实际产物匹配；格式/编码/差异/文档检查通过 |

最终日志前缀为 build/p4-m3d2a-reviewed-，包括 {release,debug,asan,gcc}-{build,tests}.log、{debug,asan}-repeat.log、no-examples-*.log、restore.log、gcc-dll-syntax.log、source-sha256.json、asan-commands.json 和 audit.log。首轮失败与五项初步通过是过程证据，最终矩阵使用同一冻结源码。原生桌面 UI 未修改，本段没有人工桌面验收要求。

使用方法见 [业务示例说明](../examples/native_services/README.md)。
