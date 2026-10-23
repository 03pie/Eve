# 第一阶段内核验收场景

以下是第一阶段的 Given/When/Then 规范。A04/A06 及 A05 的 Effect 层已有自动化证据，见 [W2 验证记录](../../docs/w2-verification.md)；A05 的实例停止与 epoch、其余场景仍待实施。实现时将对应行为转为 CTest 管理的真实单测，沿用设计说明书编号。

## A01：缺依赖与恢复（D01）

Given 消费者声明依赖 storage/v1，当前无提供者；When mount 消费者；Then 返回有效句柄、状态 Pending、activate 次数为 0，awaitTarget 返回 PendingMissingDependency。

When 提供者激活成功；Then 消费者只激活一次，committed 记录对应 BindingId。

## A02：提供者退出（D02）

Given 已激活消费者的 cleanup 被 gate 暂停；When 停止提供者；Then 拒绝新业务调用，但提供者资源仍 open。

When gate 放行，消费者 cleanup 通过旧租约访问服务；Then 时间线必须为 consumer read open → consumer cleaned → provider closed。不能提前销毁提供者。

## A03：换代恢复

Given A02 完成；When 注册同名新提供者；Then 未终结的消费者重新激活，BindingId 改变，旧引用拒绝新业务，配置不因重启被覆盖。

## A04：Effect 重复清理（C01）

Given 组合 effect 取得资源 1、2、3；When 两个 token 副本和 owner 同时 close；Then 每项只清理一次，组内顺序 3→2→1，所有观察者得到同一结果。

## A05：启动中停止（C02/C03）

Given setup 已登记资源 1 且异步等待；When setup 内重入触发 owner.stop；Then close 等待 setup 屏障。setup 随后取得资源 2，必须登记并清理；旧结果不能让实例回到 Active。

## A06：清理错误和挂起（C04）

Given 一项 cleanup 抛错，其他项可正常释放；Then 其他项仍执行且错误汇总。另一项永久等待时，观察超时只能报告 blocked，不能伪报资源归零或 Disposed。

## A07：父子和传递依赖（D03/S02）

Given A 提供服务给 B，B 给 C，父实例拥有三者；When 关闭根；Then 先向所有受影响实例传播停止，再等 C→B→A 清理，不因全局串行 LIFO 形成等待环。

## A08：更新覆盖（S05 的内核部分）

Given epoch1 正在 setup；When 连续接受配置 2、3；Then 仅配置 3 可以发布，旧操作分别得到 Superseded，所有已获得资源均被清理。

## A09：实例隔离（D04/S01）

Given 同一 Runtime 两个实例和不同 realm；When 停止一个；Then 另一实例资源不减少，不读取其他 realm 服务，两个实例的效果树独立。

## A10：事件边界（E01/E02）

Given once listener 内重入 emit；Then 仅调用一次。Given 分发快照后注销 listener；Then 进入回调前 liveness 检查阻止调用；闭包实际释放前仍保持所属资源引用。

## 通用退出断言

正常 stop 后实例、绑定消费者、effect、监听器和在途任务账目归零；没有未完成 Promise。超时或不合作任务必须列明残留，不能通过清空容器使计数虚假归零。测试使用 fake clock 和 gate，不依赖真实 sleep。
