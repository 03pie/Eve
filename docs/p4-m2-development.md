# P4-M2 原生装载与描述握手

日期：2026-10-28。当前 **M2a 已完成，M2 整包进行中，M2b 尚未开始**。上一提交 59b7c76 为 M1；本轮实现与审查修复按用户要求随本提交归档。

## 本轮交付

- Cordis::ModuleNative / ModuleManager：固定并复验来源、PE 检查、真实 DLL 装载、进程目录和保守驻留。
- 独立 C11 描述 ABI：cordis_module_query_v1，与未来业务 HostTable 分开，结构和所有权见 [ADR-0030](adr/0030-native-module-discovery.md)。
- 宿主快照：区分已映射、PIN、描述通过与 Failed；不输出函数指针，不注册或激活插件。
- 诊断工具新增显式 --load；默认行为继续只读预检。
- 真实 C DLL 与违规样例、C/C++ 布局、装载/目录/异常/搜索测试，映射见 [P4 验收单](../tests/acceptance/p4-module.md)。

M2b 的业务 ABI 完整签名/能力表和生命周期协议还未实施。没有提前开放安装外部插件、Home 添加未注册 DLL、服务调用或 UI DLL。

## 当前行为

| 输入/操作 | 结果 |
|---|---|
| 正常声明、PE 和描述 | Loaded；nativeMapped/nativePinned/descriptorAccepted 为真，尚无业务 Runtime |
| 同模块内容重复请求 | 返回同一 GenerationId 和 reused=true，query 只调用一次 |
| 同 ID 不同版本/hash/插件目录 | 拒绝，不替换旧代 |
| 预览后篡改产物 | 重新校验失败，不执行 DLL |
| 非 PE、改名 EXE 或其他架构 | 进入装载器前拒绝 |
| 缺入口、错误描述或违规 C++ 异常 | Failed；若已映射则仍驻留，不自动重试 |
| drain 或管理器析构 | 不物理卸载；已排空代次不再接收装载/激活引用 |
| 当前目录中的同名依赖 | 不搜索；显式 DLL 目录中的依赖可解析 |
| 目录达到 64 代 | 拒绝继续保留新代，已有诊断仍可读取 |

M1 文件句柄类提取到私有 native_file_p.hpp 共用，公开 SDK 没有 Qt/Windows 类型。ModuleLoad 为新的源码 SDK 错误分类，描述 ABI 使用自己的显式状态值。

## 验证与修复记录

首轮编译发现 /EHsc 会把 extern C 函数视为不抛出，故意违规的样例触发 C4297。模块边界和 ABI 测试目录改用 /EHs，保留防御性 C++ 展开，未关闭警告、未使用 /EHa；初版覆盖选项的 D9025 也通过目录配置消除。

首轮 P4 专项 24/26，通过项已包含真实 C DLL 和失败驻留；两个依赖场景因 Windows MODULE 目标输出目录属性不同而找不到辅助 DLL。统一库/运行输出路径后 26/26。随后补充目录上限、EXE/架构拒绝，并明确使用严格 C11 模式。

进一步审查收紧了物理身份与加载器归属：使用卷标识/128 位 FileId，拒绝不可用身份和外部预先映射；找到相同逻辑 ID 后仍扫描其他记录的物理归属，防止绕过冲突。新增 aliases/untracked 两项真实 DLL 场景。

首轮 ASan 26/27，预算场景 64 次被拒绝描述的 DLL 初始化超出原 15 秒预算，失败保留在 build/p4-m2-asan-tests.log。目录限额本来就包括原生加载失败的代次，预算检查现用 64 个真实缺依赖失败填充，并额外断言其错误阶段；query/描述失败另由 incompatible 覆盖。不改生产上限、不增加超时，也不把失败写成通过。

随后重跑 ASan 的预算场景出现文件复制失败，原夹具采用固定 case 目录；清理后同一二进制独立通过，耗时 2.85 秒。外层驱动改为每轮唯一目录，拒绝复用既存目录，并检查子进程退出后的删除结果，避免超时残留影响后续运行。末次只有测试驱动变化，C/C++ 实现不变；之后补验完整 P4 专项。

提交前审查发现 Loaded 在 query 之前就对其他管理器可见，与公开契约冲突。改为握手期间保持 Validated，成功后在同一锁中发布 Loaded、descriptorAccepted 和完成标志。新增真实 C DLL 事件门禁回归，验证并发快照、临时 Call 账目、重复/排空拒绝和最终成功；修复前失败，修复后通过，见 build/p4-m2-review-red-tests.log / green-tests.log。

异常选项原先直接替换 /EHsc 子串，会把合法 /EHsc- 改为关闭展开的 /EHs-，也遗漏 Debug 等配置专属选项。现在模块和 ABI 测试共用目录级函数，只替换完整 token，保留负修饰和其他选项，覆盖标准及自定义配置，不改缓存或 Core/Qt/UI 的模型；脚本回归验证选项及作用域。

关闭示例的首轮审查验证 30/31，预算场景在并行构建期间触及原 15 秒超时；日志保留为 build/p4-m2-review-no-examples-tests.log。随后用相同源码、选项和测试预算单独重跑完整专项，31/31 通过，预算场景 7.21 秒；没有放宽超时、减少目录上限或修改生产行为。

## 提交前最终验证

| 范围 | 结果 | 证据 |
|---|---|---|
| 原目录 MSVC Release / Debug 全量 | 各 490/490 | build/p4-m2-review-release-tests.log / debug-tests.log |
| 无 Qt GCC Debug 全量 | 217/217；独立 C11/C++17 布局及源码 SDK 头包含通过 | build/p4-m2-review-gcc-tests.log |
| ASan P4 专项 | 31/31 | build/p4-m2-review-asan-tests.log |
| 关闭示例及自定义异常选项 | P4 专项 31/31，含异常拒绝、并发发布和独立探针 junction | build/p4-m2-review-no-examples-isolated-tests.log |
| 新增审查回归 | 并发发布修复前 0/1；修复后发布与选项检查 2/2 | build/p4-m2-review-red-tests.log / green-tests.log |
| CLI | 默认预检、显式 --load 成功；缺参数退出 2，来源错误退出 1 | build/p4-m2-review-cli.log |
| 源/依赖/格式 | 22 项源/构建/脚本快照一致；Core/Ui 与公开头边界、纯 C 头、本地文档链接和 diff 检查通过 | build/p4-m2-review-source-sha256.json / audit.log |
| ASan 编译范围 | 23 个受影响编译单元，含纯 C DLL 样例 | build/p4-m2-review-asan-commands.json；不包含预编译 Qt/系统 DLL |

源码在审查修复后冻结，全量与专项验证期间逐项保持一致；最后仅更新文档。自定义组合中 Core 原样保留 /EHsc- /EHsc，模块/ABI 使用 /EHsc- /EHs，实际编译命令保存在 build/p4-m2-review-custom-flags-commands.json。关闭示例检查后恢复原开关和编译选项，原生子进程退出后的夹具均已删除。审查前的 488/216 及 29 项开发轮次保留为历史日志，不覆盖上表最新结果。

当前证据为 Windows x64、MSVC 19.44/v143、Qt 5.15.2 及本机支持 128 位文件身份的文件系统；GCC 验证无 Qt 路径和布局，没有验证 GCC DLL 与 MSVC 宿主的二进制组合。所有数量只对应本轮 M2a，不继承为完整业务 ABI、DLL 卸载或恶意代码隔离认证。

## 本机运行

沿用原 Release 目录；Qt PATH 按 [开发环境](development.md) 配置。默认只读预检：

```powershell
./build/ela-msvc-release/bin/cordis_module_inspect.exe D:/plugins example/module.json
```

明确执行真实装载和只读描述握手：

```powershell
./build/ela-msvc-release/bin/cordis_module_inspect.exe --load D:/plugins example/module.json
```

--load 会执行 DLL 的正常装载/静态初始化，成功仍不激活业务插件；当前产物必须提供独立的 cordis_module_query_v1 描述入口。成功输出代次、模块和实际产物路径，默认驻留至这个工具进程退出。
