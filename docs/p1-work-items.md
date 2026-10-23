# 第一阶段工作单

状态：构建骨架可用，业务运行时尚未实现。此工作单按依赖顺序推进，不要求先完成整个平台。

| 顺序 | 工作包 | 具体交付 | 完成条件 |
|---|---|---|---|
| W0 | 基础工程 | CMake、预设、类型、编译链接检查 | 当前基础检查通过 |
| W1 | 结果与调度 | Result<T>/void、IScheduler、FakeScheduler、Task/Promise、取消源 | 异步回调不内联、重复完成仲裁、错误及关闭路径可测 |
| W2 | EffectScope | owner 控制块、setup/cleanup 屏障、组合、重复 close | C01–C04、S03 的核心场景通过 |
| W3 | 实例与 Context | Runtime/Instance 分层、状态收敛、epoch、stop/retry | 旧结果不发布；重入停止；多实例独立 |
| W4 | 服务依赖 | Binding、Realm、committed lease、失效传播 | D01/D02/D03、传递依赖清理及冲突诊断通过 |
| W5 | 可选依赖与环 | inject 子实例、所有权/依赖联合诊断 | 子实例清理、强依赖环输出完整路径 |
| W6 | EventBus | 五种分发、once、作用域、失活快照 | E01/E02 的无 Qt 场景通过 |
| W7 | 内核示例与收尾 | 提供者/消费者控制台示例、资源快照、关闭报告 | 完整闭环及所有内核验收通过 |

## 首次编码任务

从 W1 开始：先完成 Result 和 FakeScheduler，再实现 Task/Promise。新增真实测试验证“已完成 Task 的 thenOn 也必须排队”“两个完成者竞争仅一方成功”“失败 continuation 可观测”“scheduler 拒绝不悬空”。不要在 Task 的基础语义未确定时同时推进 DLL 和 GUI。

## 每个工作包的交付要求

- 公共头文件独立可包含，core 不包含 Qt/Ela。
- 修改接口时更新契约和错误说明。
- 对已实现行为提供单测；不以空测试或返回成功的 stub 代表待实现能力。
- Debug/Release 当前可用工具链检查通过；目标 MSVC 尚未验证时在记录中明确。
- 保留一个可运行示例，示例不得暗示未实现功能已可用。

## 不纳入本轮

Qt/Ela 页面、YAML Loader、DLL 装载、二进制 ABI、物理卸载和热替换按设计说明书 P3–P5 推进。C ABI 在内核语义稳定后再冻结布局。
