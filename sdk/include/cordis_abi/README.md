# C ABI SDK（Windows x64 V1 基线）

本目录包含独立 C11 头：module_query.h 为只读发现，plugin.h 为业务函数表，state.h 为可选同步状态扩展。Windows x64 的调用约定、固定线码、全字段布局及参数所有权见 [ABI V1](../../../docs/abi-v1.md)。

状态扩展使用独立 cordis_query_state_v1 符号和版本，不改变已有 plugin/host V1 表。缺失符号保持旧插件无状态；存在但拒绝或返回坏表则失败。每个实际 PluginId 的 describe 可明确返回 UNSUPPORTED；其他操作的拒绝不能解释成无状态。schema 最多 128 个可见 ASCII 字节，版本非零；JSON 为独立 UTF-8 原文，最多 1 MiB。输入只借用至调用返回，输出写宿主一次提供的缓冲区，只能修改内容和 size；不能等待、泵事件、抛异常或返回 PENDING。

生产 Kernel/请求/服务/effect 桥已在 P4 完成；状态适配属于 [P5-M3-b](../../../docs/p5-m3-development.md)，完整替换/恢复事务由 M3-c 接入。不能把握手或单实例导入成功当作整体状态替换已完成。C++ 源码 SDK 与 C ABI 分开，不承诺任意平台或工具链二进制互通。
