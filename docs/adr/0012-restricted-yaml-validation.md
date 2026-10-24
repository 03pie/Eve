# ADR-0012：受限 YAML 与内置插件候选校验

日期：2026-10-24。范围：P3 Q4b。承接 [ADR-0011](0011-configuration-preview.md) 的结构候选，在进入执行/保存之前补齐文本输入和内置插件定义/schema 校验。

## 解析依赖及构建边界

选用 [LibYAML 0.2.5](https://github.com/yaml/libyaml/releases/tag/0.2.5)，通过 [yaml_parser_parse 事件接口](https://github.com/yaml/libyaml/blob/0.2.5/include/yaml.h)逐事件读取；不使用 yaml_parser_load 的文档组合和别名展开。库的公开 API、事件中 anchor/tag/style/mark 的含义及 MIT 许可均按官方固定源码核对。

源码与许可导入 third_party/libyaml；[UPSTREAM.md](../../third_party/libyaml/UPSTREAM.md) 记录归档及 SHA256。保留库实现原文，项目只增加静态库构建入口，不执行上游安装/测试，不在构建时联网。许可另复制到 build/<preset>/licenses/LibYAML.LICENSE。

Cordis::Loader 仍只提供无解析器依赖的结构模型；新增 Cordis::ConfigYaml 公开链接 Loader、私有链接固定 LibYAML，不依赖 Core/Qt/Ela，也不把 yaml.h 暴露到 SDK。根工程启用 C/C++，预设明确 GCC 使用 gcc/g++，MSVC 使用 cl；Core 目标本身仍不链接解析器。

## 输入子集与预算

只接受单份 UTF-8 文档。formatVersion 与 plugins 必填，空文档不能隐式表达“删除全部条目”；清空目标必须显式 plugins: []。可使用块/流式容器、引号、块字符串、注释及 YAML 1.2 版本指令。

所有显式 tag、tag 指令、anchor、alias、merge key、多文档及非字符串映射键拒绝；所有映射（含 config）拒绝重复键，转义后同名也算重复。固定结构字段使用白名单，插件 config 子树不套用平台字段白名单，而由插件 schema 决定。modules 使用 `{id: {manifest: path}}`，inject 支持 name/vN 简写和完整 name/major/minimumMinor/capabilities 映射。

disabled 仅接受未加引号的小写 true/false；结构整数要求未加引号、无前导零的 uint32 十进制数。负数、溢出、浮点数、字符串到布尔/数字的隐式转换均拒绝。标识、模块引用和依赖的剩余约束继续由 Q4a 结构校验负责。

默认预算：原文 1 MiB、65536 个节点（映射键计数）、64 层、所有解码标量合计 1 MiB、规范输出 8 MiB；解析深度可设置为 1–128，其他解析预算须为正。事件开始前检查节点/深度预算，标量复制前检查累积字节；进入配置条目时另检查结构条目/深度预算，完整结构仍检查 Q4a 字段预算。解析器与事件由 RAII 释放，失败不返回部分候选。

这些是宿主事件树与文本的预算，不声称限制 C 库每次分配的精确大小或 CPU 时间。原文有界且所有引用拒绝，不产生别名展开。当前没有文件 watcher 或后台解析调度。

## 配置值和规范文本

ParsedConfiguration 封闭构造，拥有 DocumentCandidate、条目来源位置和规范文本；不保留库节点。移动后原来源 isValid=false，validateCandidate 拒绝把该空对象当成清空目标的有效输入。config 转为确定性 JSON 字符串，缺省为 `{}`，供当前 Runtime 字符串协议及 schema 使用。映射按键排序，列表保持顺序；JSON 字符串转义控制字符并保留 UTF-8。普通 null/Null/NULL/~ 转 null，true/false 转布尔，JSON 文法的数字保留字面形式，其余普通标量（如 yes、on、.inf、01）是字符串；带引号或块样式的标量始终是字符串。

规范 JSON 本身是有效 YAML 子集，normalizedYaml 可重新解析，不保留注释或原排版。不承诺把数字 1e1 与 10、默认字段省略与显式填写、不同列表顺序归为相同文本；结构预览仍按规范后的依赖和 EntryPath 判断动作。Typed Document 的 config 仍是不透明字符串；只有文本解析入口保证 config 是上述 JSON。

语法/类型/未知字段错误使用 ConfigInvalid 和 loader.yaml 阶段；结构错误保留 loader.structure。错误信息提供一基行列，条目错误尽可能带 EntryPath；positions 记录条目起始位置。

## 内置定义与 schema

validateCandidate 按值保留解析来源及宿主 PluginDefinition，先检查定义合法性、唯一性、schema 存在性和所有条目的插件归属，再调用 schema。未知插件使整个预检失败，不会在发现后部未知条目前就执行前项 schema。禁用条目同样检查 schema，避免保存以后启用必然失败的配置。

schema 是宿主提供的纯检查函数，接收规范 JSON config；返回失败时外层错误为 ConfigInvalid/loader.schema，保留原始原因；抛出异常也转为配置错误。不得在 schema 中注册资源或执行插件激活。ValidatedCandidate 不保留 schema、Runtime 或实例，校验完成即释放局部定义捕获。

有效 inject 是定义依赖与用户声明的联合：按 name/major 取并集，minimumMinor 取较大值，capability 取并集。用户省略或给出更弱声明不会删除定义要求；原始目标仍保留用户声明，有效要求单独作为值快照提供。

本阶段只发布内置插件的 ValidatedCandidate：任何非空 modules 声明都拒绝（包括未使用声明），避免把尚未检查的路径、manifest、产物或 ABI 当成已验证目标。parseConfiguration 仍可解析外部模块结构供未来 P4 使用。外部模块完整验证由后续模块适配器接入。

## 示例与后续

cordis_configuration_demo 有界读取两份文件，完成解析、内置 schema 校验，再打印目标差异。demo.profile 的 config 必须为 work/personal 字符串，demo.empty 必须为空映射；示例只是预览，不挂载插件、不改写文件。真实 YAML 样例位于 examples/configuration。

Q4c 继续 Desired 接受/CAS、基于实际运行树的单队列协调和 Superseded 报告；之后实现 Store 冲突检查及原子保存。Q3c 原生桌面人工验收保持待办，实际运行树及管理页接入前完成，不由本轮命令行测试代替。
