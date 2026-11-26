# ADR-0055：真实工作台有限动作轨迹与独立进程回放

日期：2026-11-19。状态：已采纳。

## 问题与边界

d1 的固定三实例、四相位验证不能覆盖配置撤回、多 realm、排队修订与取消。保留其判据，不向 sampleCatalog 添加压力模式。只提取其中原模块目录的纯值复制，供 d1 与新动作观察两个实际消费者使用，删除原来的重复定义。锁及临时 Generation 强引用在采样返回前释放；不持有 Runtime、Instance、Scope 或插件回调跨动作。

动作沿唯一 Workspace→Session→Coordinator→Driver/Store 与原 ReplacementOperation 执行。测试不直接调用 Driver.apply/close 或 Instance.stop，不增加生产 API、测试业务导出、第二事务/目录/保存器，不放宽目录预算或清除 PIN。实际 desired、原任务回执及快照是事实来源，测试只维护有限动作前置条件与本次异步关联。

## 动作执行与回放

有限动作运行器使用固定 seed 生成、逐动作保存和原状态核验。回放与失败缩减在各自独立的子进程中复用同一份严格保存格式。

## 数据与职责

测试私有目录 helper 返回原 generation/resources/origins/reservation/readers 的纯值。动作执行文件负责合法配置编辑、原任务及指定时序窗口；观察文件核对实际 desired 路径、Core/Driver/Native/UI/realm 与来源资源；轨迹文件负责严格字段、文件读写、完整错误树与产物摘要；入口仅负责生成/顺序执行/退出状态。避免把这些职责追加到 d1 的固定 validator 或生产 Workspace。

动作参数独立于执行证据。每条动作先保存并 flush 再执行，记录前后纯值、原 revision/OperationId/phase、Plan/Replacement/Save 报告及递归 ErrorInfo。seed、revision、ID、计数等 uint64 一律规范十进制字符串；严格拒绝超界、非规范值、未知字段/动作/版本。动作文件仅接受本工具的规范 JSON 序列化，读取后与规范字节比较，拒绝 QJson 会折叠的重复键，不另造通用 JSON 解析器。PRNG 算法与版本固定，执行器不接收随机源；实际seed生成标splitmix64/1，手工资格标authored/1，缩减保留真实来源。固定产物 locator 与 SHA 是轨迹输入，回放不允许替换或忽略产物变化。

使用确定必达骨架加有界随机组合：Notebook 挂载/更新/停止启用/撤回、第二 realm 的 Counter/Reporter 成对往返、Native A/B 升级、明确 Prepared/Building 取消、相关路径拒绝与无关排队修订、显式保存冲突/rebase。普通动作后等待原收敛；排队组合内明确不 pump。合法 Native 配置只有 null 与现有故障字段，不伪造业务 schema。第二 realm 不在默认域重复发布提供者；撤回/realm 改动从当前目标保留 module/inject/children/local disabled/config 重建，再走原 preview/apply。config 原 JSON 数字片段不得经 QJson double 往返。

Native 仅在实际 oldUnloaded 后重用 A/B；Qt 每进程最多一次尝试，保留 Resident 谱系。观察器允许原 Pending/Failed 的真实状态，不能假定任意目标 Applied。取消引用由某动作创建的本进程真实操作，不使用上次进程数值充当能力。指定窗口未出现或前置条件失效属于 InvalidTrace，不能静默跳过或重新随机。

## 回放、缩减与故障语义

d2b 每次回放/候选使用新 OS 子进程与独立 scratch/Scene，区分 Passed、真实 Failed(signature)、InvalidTrace 与启动/超时故障。只有同一原错误及状态事实签名才保留缩减；原运行 ID 仅作证据，不要求新进程数值相同。

固定初始三业务 Applied 是 bootstrap，不是可缩减动作。真实目标是噪声中的 Draft(65537 ASCII 字节)→Notebook B 原升级，capture 应原生拒绝 InvalidArgument；核未停止/提交及原实例、epoch、页面、全文不变。删除式缩减需留下两动作；各删一个后新进程不再复现，65536 字节另作通过对照，完整轨迹重放两次。只宣称所证明的删除最小，不声称全局最短。故障不通过伪造错误或清理原不确定状态生成。

## 验证与兼容性

目录提取后重跑 d1 原 8+100 判据；d2a 两个固定 seed 和真实覆盖/文件/错误树/精度核验，d2b 严格回放、无效输入/窗口及真实缩减。使用原共享 Qt/Ela Release、Debug、ASan；无 Qt/无 Ela 不声称运行本入口。SDK、生产二进制与业务导出无预期变化，实际输入/对象/产物核验后据实记录。任何观察持有、真实故障或失败验证均保留原始证据，修复后重新冻结。
