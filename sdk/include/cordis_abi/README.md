# C ABI SDK（Windows x64 V1 基线）

本目录包含 P4 独立 C11 头：module_query.h 为只读发现，plugin.h 为完整业务函数表。Windows x64 的调用约定、固定线码、全字段布局及参数所有权见 [ABI V1](../../../docs/abi-v1.md)。

M2 的协商层已有纯 C 模块和 C/C++ 检查；生产 Kernel/请求/服务/effect 桥仍在 M3 实施，不能把握手成功当作业务插件已激活。C++ 源码 SDK 与 C ABI 分开，不承诺任意平台或工具链二进制互通。
