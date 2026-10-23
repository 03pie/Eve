# 研究证据索引

核实日期：2026-10-23。配套正文：[开发指南](Cordis-Cpp17-Qt5152-开发指南.md)。本表表示实际阅读定位，不表示运行测试或审计整个仓库。源码保留在 `research/` 的固定提交检出中，以下网络链接使用不可变提交。

## 论文

**P1**：[论文 v1 全文](https://arxiv.org/pdf/2608.25512v1)，[版本与作者信息](https://arxiv.org/abs/2608.25512v1)。作者 Yifan Shi、Wei Zhang、Tianyi Cui；提交于 2026-08-26。重点定位：§3 pp.9–30；§4 pp.31–56；§5.1 pp.57–64；§5.2 pp.64–68；§6 pp.70–76。页数以论文印刷页码为准。本文核实的是概念、章节与实现对应关系，没有逐项复核形式证明。正文中的工程契约不是从论文自动推出的保证。

## 上游 Cordis

根目录：`research/upstream`。SHA：`35fdc969e4b6c48c48ed44717c7a018324e4dd88`。

| 编号 | 固定源码 / 测试链接 | 核对符号或断言 |
|---|---|---|
| U1 | [fiber.ts](https://github.com/cordiverse/cordis/blob/35fdc969e4b6c48c48ed44717c7a018324e4dd88/packages/core/src/fiber.ts) | FiberState、effect、_refresh、_reload、_unload、update |
| U2 | [registry.ts](https://github.com/cordiverse/cordis/blob/35fdc969e4b6c48c48ed44717c7a018324e4dd88/packages/core/src/registry.ts) | Runtime.fibers；RegistryService.plugin/inject；PromiseLike 包装 |
| U3 | [dispose.spec.ts](https://github.com/cordiverse/cordis/blob/35fdc969e4b6c48c48ed44717c7a018324e4dd88/packages/core/tests/dispose.spec.ts) | dispose by plugin、dispose manually、yield dispose、async return |
| U4 | [reflect.ts](https://github.com/cordiverse/cordis/blob/35fdc969e4b6c48c48ed44717c7a018324e4dd88/packages/core/src/reflect.ts) | _getImpl、provide、notify；uid、isolate、dependents |
| U5 | [service-dispose.spec.ts](https://github.com/cordiverse/cordis/blob/35fdc969e4b6c48c48ed44717c7a018324e4dd88/packages/core/tests/service-dispose.spec.ts) | retains resources during consumer cleanup；retains transitive dependencies；in-flight setup and rollback |
| U6 | [isolate.spec.ts](https://github.com/cordiverse/cordis/blob/35fdc969e4b6c48c48ed44717c7a018324e4dd88/packages/core/tests/isolate.spec.ts) | 不暴露跨域 own provisions；隔离事件；shared label |
| U7 | [events.ts](https://github.com/cordiverse/cordis/blob/35fdc969e4b6c48c48ed44717c7a018324e4dd88/packages/core/src/events.ts) | isBailed、parallel、serial、bail、waterfall、register、once |
| U8 | [events.spec.ts](https://github.com/cordiverse/cordis/blob/35fdc969e4b6c48c48ed44717c7a018324e4dd88/packages/core/tests/events.spec.ts) | 本次抽查 on / once、原型属性名称与空 bucket 清理；五种分发的说明主要依据 U7 实现 |
| U9 | [rebuild.spec.ts](https://github.com/cordiverse/cordis/blob/35fdc969e4b6c48c48ed44717c7a018324e4dd88/packages/loader/tests/rebuild.spec.ts) | 改名后重建 active entry；导入或 patch 期间改目标 |

## DSH vendored 实现

根目录：`research/dsh`（sparse checkout 的 vendor）。SHA：`687f282e8625c6b8d11ff58211a90524be2e4317`。这里直接研究的是该提交的实际 vendored 文件；vendor README 中引用的另一个仓库和历史提交没有全部下载验证。

| 编号 | 固定链接 | 核对内容 |
|---|---|---|
| D1 | [vendor/README.md](https://github.com/deepseek-ai/deepseek-harness/blob/687f282e8625c6b8d11ff58211a90524be2e4317/vendor/README.md) | Manifest；Local modifications 6、8、14、15、19、20、22、23 等。日志陈述与独立逐行核对要区分 |
| D2 | [cordis/src/fiber.ts](https://github.com/deepseek-ai/deepseek-harness/blob/687f282e8625c6b8d11ff58211a90524be2e4317/vendor/cordis/src/fiber.ts) | effect 中的 setupBarrier、finalizeDisposal、effectInertia、先注册 wrapper、UNLOADING 拒绝 |
| D3 | [loader/src/config/entry.ts](https://github.com/deepseek-ai/deepseek-harness/blob/687f282e8625c6b8d11ff58211a90524be2e4317/vendor/loader/src/config/entry.ts) | EntryOptions、moduleNamespace、update、_commitVolatile、init |
| D4 | [hmr/src/index.ts](https://github.com/deepseek-ai/deepseek-harness/blob/687f282e8625c6b8d11ff58211a90524be2e4317/vendor/hmr/src/index.ts) | 构造函数 internals 要求；partialReload；缓存备份、导入、替换、恢复分支 |

## Qt 5.15.2 与 ElaWidgetTools

Qt 网络归档文档当前页面部分标为 5.15.19，故默认卸载 hint 和延迟销毁的精确版本依据另取 qtbase 的 v5.15.2 源码。Qt checkout 只取 plugin/kernel 目录。

| 编号 | 固定源码链接 | 核对内容 |
|---|---|---|
| Q1 | [qpluginloader.cpp](https://github.com/qt/qtbase/blob/40143c189b7c1bf3c2058b77d00ea5c4e3be8b28/src/corelib/plugin/qpluginloader.cpp#L158) | 构造函数默认 PreventUnloadHint；unload 文档与函数；loadHints 默认说明 |
| Q2 | [qobject.cpp](https://github.com/qt/qtbase/blob/40143c189b7c1bf3c2058b77d00ea5c4e3be8b28/src/corelib/kernel/qobject.cpp#L2270) | deleteLater 文档与实现，事件循环退出时机；disconnect 文档的排队事件限制 |
| Q3 | [Threads and QObjects](https://doc.qt.io/archives/qt-5.15/threads-qobject.html) | GUI 主线程、事件驱动对象与 QObject 亲和性；该文档不是本项目运行测试 |
| E1 | [CMakeLists.txt](https://github.com/Liniyous/ElaWidgetTools/blob/aae58f5d0d98e05eabe10c263aa7da5f2491d702/CMakeLists.txt#L9) | C++17、Qt 路径 FORCE、版本范围与推荐版本 |
| E2 | [ElaWindow.h](https://github.com/Liniyous/ElaWidgetTools/blob/aae58f5d0d98e05eabe10c263aa7da5f2491d702/ElaWidgetTools/ElaWindow.h#L62) | addPageNode、removeNavigationNode 等声明；未完成页面销毁所有权的端到端验证 |

## 复核方式

先对各 `research/<repo>` 执行 `git rev-parse HEAD`，应与本表一致；用表中符号搜索可复核实现。不要对研究副本执行来历不明的脚本。测试复现需单独准备受控环境：上游 package.json 声明 yarn 4.14.1，并通过 yakumo/Vitest 运行；DSH 的部分回归位于 vendor 以外的 boot、scripts、应用测试目录，本次 sparse checkout 不包含这些完整依赖，也未运行。

下一轮若更换 SHA，应新建证据基线并重新比较关键实现，不能只改本表的日期。本文没有使用社区文章作为核心行为的证据。

## 关键符号行号定位

以下行号由本地固定版本搜索生成，链接指向同一 SHA。

- U1: [packages/core/src/fiber.ts:90](https://github.com/cordiverse/cordis/blob/35fdc969e4b6c48c48ed44717c7a018324e4dd88/packages/core/src/fiber.ts#L90)
- U1: [packages/core/src/fiber.ts:285](https://github.com/cordiverse/cordis/blob/35fdc969e4b6c48c48ed44717c7a018324e4dd88/packages/core/src/fiber.ts#L285)
- U1: [packages/core/src/fiber.ts:286](https://github.com/cordiverse/cordis/blob/35fdc969e4b6c48c48ed44717c7a018324e4dd88/packages/core/src/fiber.ts#L286)
- U1: [packages/core/src/fiber.ts:287](https://github.com/cordiverse/cordis/blob/35fdc969e4b6c48c48ed44717c7a018324e4dd88/packages/core/src/fiber.ts#L287)
- U1: [packages/core/src/fiber.ts:457](https://github.com/cordiverse/cordis/blob/35fdc969e4b6c48c48ed44717c7a018324e4dd88/packages/core/src/fiber.ts#L457)
- U1: [packages/core/src/fiber.ts:509](https://github.com/cordiverse/cordis/blob/35fdc969e4b6c48c48ed44717c7a018324e4dd88/packages/core/src/fiber.ts#L509)
- U2: [packages/core/src/registry.ts:193](https://github.com/cordiverse/cordis/blob/35fdc969e4b6c48c48ed44717c7a018324e4dd88/packages/core/src/registry.ts#L193)
- U3: [packages/core/tests/dispose.spec.ts:37](https://github.com/cordiverse/cordis/blob/35fdc969e4b6c48c48ed44717c7a018324e4dd88/packages/core/tests/dispose.spec.ts#L37)
- U4: [packages/core/src/reflect.ts:13](https://github.com/cordiverse/cordis/blob/35fdc969e4b6c48c48ed44717c7a018324e4dd88/packages/core/src/reflect.ts#L13)
- U4: [packages/core/src/reflect.ts:202](https://github.com/cordiverse/cordis/blob/35fdc969e4b6c48c48ed44717c7a018324e4dd88/packages/core/src/reflect.ts#L202)
- U4: [packages/core/src/reflect.ts:240](https://github.com/cordiverse/cordis/blob/35fdc969e4b6c48c48ed44717c7a018324e4dd88/packages/core/src/reflect.ts#L240)
- U5: [packages/core/tests/service-dispose.spec.ts:96](https://github.com/cordiverse/cordis/blob/35fdc969e4b6c48c48ed44717c7a018324e4dd88/packages/core/tests/service-dispose.spec.ts#L96)
- U5: [packages/core/tests/service-dispose.spec.ts:131](https://github.com/cordiverse/cordis/blob/35fdc969e4b6c48c48ed44717c7a018324e4dd88/packages/core/tests/service-dispose.spec.ts#L131)
- U7: [packages/core/src/events.ts:6](https://github.com/cordiverse/cordis/blob/35fdc969e4b6c48c48ed44717c7a018324e4dd88/packages/core/src/events.ts#L6)
- U7: [packages/core/src/events.ts:27](https://github.com/cordiverse/cordis/blob/35fdc969e4b6c48c48ed44717c7a018324e4dd88/packages/core/src/events.ts#L27)
- U7: [packages/core/src/events.ts:28](https://github.com/cordiverse/cordis/blob/35fdc969e4b6c48c48ed44717c7a018324e4dd88/packages/core/src/events.ts#L28)
- U7: [packages/core/src/events.ts:95](https://github.com/cordiverse/cordis/blob/35fdc969e4b6c48c48ed44717c7a018324e4dd88/packages/core/src/events.ts#L95)
- U7: [packages/core/src/events.ts:125](https://github.com/cordiverse/cordis/blob/35fdc969e4b6c48c48ed44717c7a018324e4dd88/packages/core/src/events.ts#L125)
- D2: [vendor/cordis/src/fiber.ts:420](https://github.com/deepseek-ai/deepseek-harness/blob/687f282e8625c6b8d11ff58211a90524be2e4317/vendor/cordis/src/fiber.ts#L420)
- D2: [vendor/cordis/src/fiber.ts:520](https://github.com/deepseek-ai/deepseek-harness/blob/687f282e8625c6b8d11ff58211a90524be2e4317/vendor/cordis/src/fiber.ts#L520)
- D3: [vendor/loader/src/config/entry.ts:118](https://github.com/deepseek-ai/deepseek-harness/blob/687f282e8625c6b8d11ff58211a90524be2e4317/vendor/loader/src/config/entry.ts#L118)
- D3: [vendor/loader/src/config/entry.ts:164](https://github.com/deepseek-ai/deepseek-harness/blob/687f282e8625c6b8d11ff58211a90524be2e4317/vendor/loader/src/config/entry.ts#L164)
- D4: [vendor/hmr/src/index.ts:251](https://github.com/deepseek-ai/deepseek-harness/blob/687f282e8625c6b8d11ff58211a90524be2e4317/vendor/hmr/src/index.ts#L251)
- D4: [vendor/hmr/src/index.ts:333](https://github.com/deepseek-ai/deepseek-harness/blob/687f282e8625c6b8d11ff58211a90524be2e4317/vendor/hmr/src/index.ts#L333)
