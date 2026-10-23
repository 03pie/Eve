# ADR-0003：清理结果与资源释放确认分离

状态：W2 已实施。日期：2026-10-23。

## 决定

1. owner 在任何 setup 代码运行前登记 EffectControl，并保留独立资源所有权。EffectToken 析构不关闭。
2. 关闭先广播取消，再等 setup 屏障和 cleanup；返回同一 close 任务，组内逆序、组间允许并行等待。
3. 清理任务返回错误后继续尝试其余清理，但不能据此推断失败项的资源已释放。失败闭包、控制块及 scope pin 保留，结果 quiescent=false。
4. cleanups 仍 pending 或调度器不再投递时，不完成底层 close；诊断快照与独立超时观察报告等待原因。
5. setup 失败但回滚成功可得到 quiescent=true 与非空错误列表，两者分别描述释放安全与运行结果。
6. 显式 adopt 建立清理依赖，检查收养图环。隐式、任意 Task 等待环留到更完整的实例/服务模型分析，不能假称已全部检测。
7. scope 存在异步 cleanup 时暂时拒绝新建 effect，避免清理期间新增资源逃逸；setup 自身仍可登记已取得的迟到资源。
8. 超时仅结束观察，不伪造底层完成、不清空资源账目。FakeClock 用于确定性测试，生产 Qt timer 后续适配。

## 影响

调用方必须检查 quiescent，不能只检查 close.isReady()。当前没有强制释放不确定资源的入口；如果 owner 未显式关闭即被丢弃，会保守保留资源控制块，属于需诊断的生命周期错误而不是自动清理成功。W3 将这些结果映射到实例 Unloading/Failed/Disposed，W4 再绑定 ModuleLease 与卸载门禁。

EffectId 当前在 scope 内分配；W3 诊断应使用 InstanceId + EffectId 组合，不能把单个 scope 内 id 当作全局唯一标识。
