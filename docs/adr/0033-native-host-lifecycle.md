# ADR-0033：稳定原生宿主与真实生命周期

日期：2026-10-29。状态：采用（P4-M3b）。

## 决策

在已固定的 ModuleGeneration 上建立显式 NativeModule 会话。业务 query 每代缓存一次，基础 HostTable 为进程寿命常量；函数指针、原生句柄和借用元数据均留在私有实现。公开 SDK 仅提供复制值、Task 和实例生命周期句柄。析构不隐式调用 DLL，也不宣称清理成功。

每个原生实例绑定 InstanceId、epoch 和不可复用 context。create 为同步句柄契约；start/stop 通过 RequestChannel 合并入口返回和 complete。宿主回调另持 Call 租约，结果发布、请求捕获退役和宿主 shim 返回分别等待。关闭顺序为关闭业务门禁、真实启动完成、stop、seal/drained、回调门禁及计数归零、destroy；只有成功 destroy 后释放实例引用。

模块会话持有 ExternalHandle 租约。Manager.drain 在仍有资源时拒绝，不能先关闭账目再要求清理申请 Call。正常会话关闭后回到 Loaded，显式 drain 后为 Resident；Serving 只表示业务宿主会话已建立，不表示 Kernel 已注册或插件已启动。

异常、协议冲突、调度失败和不确定清理保留实例、模块及诊断。工作线程只更新互斥保护的宿主故障信号；owner 执行生命周期变更，管理快照可投影该故障。普通启动业务失败仍可正常 stop/destroy，不与协议不确定混淆。

## 审查边界

已发布结果但 owner 的启动观察器尚未运行时，关闭不再取消已退役请求。关闭回调门禁后的已知 context 日志属于协议故障，不能只返回 CLOSED 后继续宣布静止。异步错误 JSON 已复制到宿主 ErrorInfo，原生对象销毁不影响诊断。

当前只协商基础能力，可选函数族全部为空。真实 dispatch/service/effect 属于 M3c，Kernel/Session 和服务 DLL 联合验收属于 M3d，同工具链 Qt UI DLL 属于 M4。默认进程驻留保持；不提供物理卸载。
