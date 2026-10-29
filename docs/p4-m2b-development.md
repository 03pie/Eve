# P4-M2b 业务 ABI 开发

日期：2026-10-28。基线 249a425。M2b 的完整签名、所有权/能力协议和协商层已实现；M2 装载/协商层完成，M3 生产桥尚未开始。本轮经代码审查，按用户要求随本提交归档，再进入下一阶段。

## 交付

- [plugin.h](../sdk/include/cordis_abi/plugin.h)：独立 C11 业务表，包含生命周期、双向完成/取消、日志、dispatch、service、effect；没有共享 buffer 或 Qt 类型。
- [ABI V1](abi-v1.md) 与 [ADR-0031](adr/0031-business-abi.md)：完整参数所有权、状态线码、能力依赖、容量/版本和请求终结协议。
- Cordis::ModuleAbi：无 Qt 的宿主私有协商器，拒绝不完整函数族、错误保留位/能力、非法 query 返回和 manifest 身份不匹配；复制元数据，保留受模块寿命约束的函数表。
- 纯 C 样例模块：自行拥有实例与复制配置，异步完成前拒绝销毁，取消后仍发送完成，支持有界 echo 函数槽；不是已注册的生产服务。
- C11/C++17 共享全字段布局断言，以及真实驻留 DLL 的业务入口检查。

ModuleManager.load 的含义保持 M2a 只读发现；本轮没有给 Loader 开放外部模块、注册 Runtime 或修改 Home。测试中的可选宿主槽仅用于协商，未把返回占位码当成真实生产服务/effect。原生测试直接在已 PIN 的 DLL 上验证协商与 C 边界，不视作已建立 M3 的业务租约账目。

## 验证

首轮 MSVC Release 新增 7/7、GCC 新增 6/6 通过。随后补充测试宿主稳定寿命、取消不提前完成、宿主复制结果在模块复用/销毁后仍有效、完整字段偏移和 C 样例自身的能力检查，再冻结最终源码运行回归。

首轮完整矩阵的 Release/Debug 各 496/497：既有 p3.workbench_power 和 ui.optimization_showcase_hidpi 分别超时，记录耗时约 1721/1728 秒；原因未确定，不将其写成正常性能数据。日志保留为 build/p4-m2b-final-release-tests.log / debug-tests.log。Release 工作台项在同一二进制上独立复核 1/1，耗时 2.18 秒；随后串行重跑两套完整矩阵，均 497/497。未改 UI 源码、断言或超时，最终结果以下表为准。

| 范围 | 最终结果 | 证据 |
|---|---|---|
| 原目录 MSVC Release 全量 | 497/497 | build/p4-m2b-isolated-release-tests.log |
| 原目录 MSVC Debug 全量 | 497/497 | build/p4-m2b-isolated-debug-tests.log |
| 无 Qt GCC Debug 全量 | 223/223 | build/p4-m2b-final-gcc-tests.log |
| ASan P4 专项 | 38/38 | build/p4-m2b-final-asan-tests.log |
| 原 Release 关闭示例 | P4 专项 38/38，含独立探针 junction；结束后恢复 ON | build/p4-m2b-no-examples-tests.log / restore.log |
| 公开 C 头隔离 | MSVC/GCC 的 C11/C++17 同时包含两个 ABI 头，严格语法检查通过 | build/p4-m2b-header-*.log |
| 源/依赖/编码/文档 | 12 项源/构建文件快照不变；C ABI 仅含 stdint.h，Core/UI/Loader 依赖方向保持 | build/p4-m2b-source-sha256.json / audit.log |
| ASan 编译范围 | 8 个新增/受影响编译行含纯 C DLL，不包含预编译 Qt/系统库 | build/p4-m2b-asan-commands.json |

最终源码在完整验证、专项与头文件检查期间保持一致，随后仅更新文档。没有修改 sources/、research/ 或 third_party/；原生夹具在子进程退出后删除。沿用原 Release 目录，未新增 Release 构建目录。

当前首版支持 Windows x64；GCC 仅验证无 Qt 源码及布局，不推断与 MSVC DLL 二进制互通。默认 DLL 驻留，没有物理卸载、安全隔离或生产请求桥的验收声明。

## 下一步 M3

将稳定寿命 HostTable、私有业务表和 ModuleLease 接入 ModuleGeneration；实现宿主请求控制块、一次完成/返回值竞态、取消及旧 epoch 拒绝，再接 create/start/stop/destroy 与 Kernel/Session。服务和 effect 必须使用真实资源屏障，不能把本轮测试宿主提升为生产实现。

按依赖顺序实施：请求仲裁与投递屏障 → 真实生命周期适配 → service/effect 资源桥 → Kernel/Session 联合 DLL 验收。M4 同工具链 UI DLL 和 P5 替换不提前并入 M3。
