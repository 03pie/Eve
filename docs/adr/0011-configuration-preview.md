# ADR-0011：配置结构候选与差异预览

日期：2026-10-24。范围：P3 Q4a 前置工作。Q3c 原生桌面人工验收继续待办；本轮并行建设无运行及持久化副作用的配置基础，不把 Q3 验收记为完成。

本页保留 Q4a 的实现边界；后续受限文本解析及内置定义/schema 已在 Q4b 接入，见 [ADR-0012](0012-restricted-yaml-validation.md)，外部模块和实际执行仍待后续阶段。

## 边界

新增独立 C++17 静态库 Cordis::Loader，不依赖 Core、Qt、Ela 或 YAML 库；其公共类型在 cordis/loader/configuration.hpp。Core 不反向引用 Loader。Document 是后续文本解析器的输出结构，不表示输入文本或插件配置已经有效。

validateDocument 只产生 DocumentCandidate：结构校验通过的宿主值。不能称其为设计 §10 的 ValidatedCandidate，因为插件是否存在、schema、inject 不得削弱定义依赖、路径规范化和模块产物等检查仍未实现。候选不含解析器节点、Runtime、实例、插件函数或 QObject，不授予运行能力；构造入口封闭，复制/移动构造完整值，不开放可能部分赋值失败的原地替换。

YAML 解析库尚未选定，本轮不添加网络下载或依赖；原始文本大小、未知字段、严格布尔、tag、别名展开及重复映射键必须由后续解析器额外检查，不能由 typed Document 的验证结果代替。

## 结构与预算

支持 formatVersion=1、显式 modules 映射、plugins/children 树、disabled、config、realm 映射及 inject 的 name/major/minimumMinor/capabilities。config 沿用当前 Runtime 不透明字符串协议，按字节比较，不宣称 YAML 语义相等或保留注释。module=builtin 是隐含模块，显式 modules 不能覆盖；非 builtin 引用必须已经声明，但本阶段不加载或检查对应文件。

EntryPath 使用以斜杠连接的 id，兄弟 id 不可重复，不同父节点下允许同名。标识限 1–256 字节 ASCII 字母、数字、点、下划线、横线，拒绝 .、..，避免路径歧义；UI 显示名称不受此内部 id 规则影响。inject 按 name/major 排序，同键重复拒绝，capability 排序去重，输入排列不触发重建。

默认最多 2048 个条目、32 层、256 个模块、每条 128 项 inject、每项 64 个 capability；标识、manifest、config、realm 及依赖文本字段总预算为 1 MiB，manifest 字符串不超过 4096 字节且不含 NUL。该预算不是 YAML 原文大小。条目数和字节预算必须为正，深度限额允许 1–128；其他数量限额可以设为 0 表示禁止该类内容。显式栈遍历校验树，加入工作栈前计入剩余条目预算，累计字节先比较剩余额度再相加。错误用 ConfigInvalid，带 loader.structure 阶段及可定位条目的规范路径。

父 disabled 向子树计算 effectiveDisabled，不改写子条目自己的 disabled。父重新启用时，原本显式禁用的子条目仍禁用。

## 预览语义

previewChanges 比较两份结构候选，保留调用方传入的 baseDesiredRevision，按 EntryPath 稳定排序输出所有条目行。只做预览，不修改修订号、不执行 CAS、不排序真实执行操作、不计算服务消费者闭包，也不把目标差异冒充 RunningSnapshot。

| 前后条件 | 预览动作 |
|---|---|
| 新增启用条目或禁用转启用 | Add |
| 删除原本启用条目 | Remove |
| 启用转为自身或祖先禁用 | Disable |
| 插件、模块/manifest、realm 或 inject 改变 | Recreate |
| 仅 config 改变 | Restart |
| 启用祖先 Restart/Recreate | 启用后代 Recreate |
| 两边都禁用或完全相同 | NoOp |

desiredChanged 与运行意图区分：禁用条目改 config 虽是 NoOp，仍是目标变更；祖先重建导致后代 Recreate 时，后代自身目标可以没变。未使用模块的 manifest 变化通过 modulesChanged/desiredChanged 保留，不因没有条目行而消失。预览行不得直接作为执行队列，尤其不能对父子重建重复清理；后续执行器须基于真实实例归属和依赖图构建操作。

## 后续

Q4b 选定受限 YAML 解析及序列化，接入插件定义/schema，构建完整 ValidatedCandidate；后续再实现 Desired/Running/Persisted 分离、CAS、单队列执行、Superseded、部分成功报告及冲突保存。Qt 文件存储和 watcher 独立适配。进入运行树和管理页接入前仍完成 Q3c 原生桌面验收。
