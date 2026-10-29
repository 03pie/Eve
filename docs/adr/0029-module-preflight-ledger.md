# ADR-0029：原生模块预检与宿主驻留账目

日期：2026-10-28。状态：P4-M1 实现；验证结果见 [P4 开发记录](../p4-development.md)。

## 决定

P4 分段推进，先建立实际可用的产物预检和宿主账目，再实现原生装载与 ABI 握手、请求桥及 Runtime 集成。本阶段不提前发布缺少 service/effect 等函数族的 C ABI 表，不把声明中的 ABI 1.0 等同于已验证的二进制接口。

提供两个独立目标：

| 目标 | 责任 | 依赖 |
|---|---|---|
| Cordis::Module | GenerationLedger、唯一 ModuleLease、分类快照和永久驻留原因 | Core，无 Qt/Ela |
| Cordis::ModulePreflight | 严格 JSON 声明、最终路径边界、文件预算及 SHA256 | Module；Windows x64，QtCore 为私有实现依赖 |

公开头仅使用 C++17 源码 SDK 类型，不暴露 Qt、Windows HANDLE、动态库函数指针或 ABI 布局。Core、普通 UI 和配置 Session 的依赖方向不改变。Loader 的外部模块配置在真实集成完成前仍拒绝，顶部“添加插件配置”也仍只选择已注册定义。

## 声明与路径

采用设计说明书 §11.1 的十个字段，全部必需，拒绝未知和重复字段，包括转义后相同的 JSON 键。当前明确支持 manifestVersion 1、c-abi、windows-x64、声明 ABI 1.0 和 resident；其他策略返回不支持。moduleId/pluginId 为有界 ASCII 稳定标识，版本暂限三段无前导零十进制文本，不实现预发布版本或版本比较。

library 限同目录 DLL 基名，拒绝目录分隔符、盘符、设备名和流路径；plugins 为 1–64 项唯一标识。版本数字字段仅接受 uint32 十进制整数原文，拒绝小数、指数和负数，避免 JSON 浮点 DOM 将 1.00000000000000000001 舍入为 1 而误接受。摘要必须为 64 位十六进制，统一为小写。默认声明 64 KiB、产物 256 MiB；声明预算可配置至 1 MiB，容器深度最多 16。

插件根必须显式给出带盘符或完整 UNC 共享的绝对 UTF-8 路径；拒绝 /path、C:path 和不完整网络根，避免依赖当前驱动器/目录。manifest 可相对根目录或使用完全限定路径，也拒绝驱动器相对写法。根、声明和库经真实句柄解析后检查目录边界。Qt 5 canonicalFilePath 在本机的 junction 复现中保留了别名路径，因此不能单独作为此边界依据。改用 [GetFinalPathNameByHandleW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfinalpathnamebyhandlew)；解析失败拒绝，不退回字符串猜测。允许解析后仍在根内的别名，不接受通过 junction 指向根外的声明/库。

预检用同一个只读原生句柄获取最终路径、大小/身份/修改时间和字节；根、声明、库的句柄持续持有到函数返回，不能先检查路径、关闭句柄后再按字符串重开文件。按照 [CreateFileW 共享规则](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew)，只允许读取共享，拒绝已存在的写入/删除访问，阻止预检期间新写入或叶文件替换；读完复核句柄元数据与最终路径。遇到共享冲突返回带 Windows 错误编号的失败，来源恢复后可重试。

PreflightArtifact 是一次快照，移动后原值失效；预检返回后释放文件句柄，不作为未来装载的永久授权。M2 必须重新校验并约束装载产物，解决检查和装载之间的文件变化；本轮不声称消除了所有文件系统竞态、证明 PE 格式有效或认证恶意代码。

## 账目

每个新账目分配宿主进程内唯一 GenerationId；同一句柄副本共享一代账目。十类资源分别登记实例、setup/cleanup task、调用、worker、回调、队列载荷、服务对象、QObject 和外部句柄。记录有独立 id、类别和有界标签；最多 4096 条。

ModuleLease 只能移动。释放/析构幂等，只删除自己持有的宿主记录，不运行插件代码。资源适配器必须在插件对象、回调捕获和析构完整退出后释放租约；当前尚未自动接入已有资源适配器，不能把这些单独账目测试当成 S07 的 DLL 引用安全证明。

beginDraining 与 acquire 由同一互斥锁仲裁，排空开始后不再接收新登记。既有租约仍等待实际持有方释放。快照返回纯值，不能修改账目；永久 pin 独立于引用记录，重复原因幂等，最多 64 项。不提供随意清零计数、替其他租约释放或自动清除 pin 的接口。

drained 仅表示停止接收且记录为空；即使 drained 为真，pin、默认 resident、装载器约束及专项卸载门禁仍可阻止卸载。本阶段没有 unload 方法，不修改 P5 边界。

## 后续

M2：manifest/产物至 ModuleGeneration 的受控装载，ABI 独立 C 头与 C/C++ 布局验证、兼容性及失败驻留。

M3：请求完成协议、ModuleLease 在执行引用上的真实托管、Kernel/Session 注册和服务 DLL 联合闭环。

M4：锁定工具链的 Qt UI DLL、诊断入口和 P4 验收。UI 模块首版固定驻留；P5 才处理受控替换及卸载白名单。
