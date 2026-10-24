# ADR-0014：配置文件事务基础

日期：2026-10-24。范围：Q4d1。Q3c 原生桌面人工验收仍待完成，Q4c2 Kernel 运行树/管理页接入门禁保持不变；本轮独立实现文件事务，为后续 Store 提供真实磁盘读写能力。

本页保留 Q4d1 的文件层边界；后续目标/保存状态、单写队列和退出刷新见 [ADR-0015](0015-configuration-store.md)。文件值类型现位于 file.hpp，qt_file.hpp 保持原包含入口。

## 构建及接口

启用 CORDIS_REQUIRE_QT5152 时构建 Cordis::ConfigFileQt，公开头 cordis/loader/qt_file.hpp，依赖 ConfigYaml 和 Qt5::Core。基础 Loader、解析层、通用协调器和 Core 不反向依赖它，未增加 Ela 或 GUI 依赖。Qt5::Core 已是现有工具链依赖，没有导入新的第三方源码。

readConfigurationFile 同步返回磁盘原文及 ConfigurationFileVersion；commitConfigurationFile 接收有效 ValidatedCandidate 与期望磁盘版本，写入其 normalizedYaml。方法不运行事件循环、不执行用户回调，不需要 QCoreApplication 或 scheduler，可由宿主安排到专用线程。同路径写事务使用协作锁串行化；完整 Store 的异步写队列另行接入。

## 路径、读取和版本

仅接受本地绝对路径；父目录必须存在，解析其 canonicalPath 后绑定最终文件名，不自动创建目录。拒绝空路径、相对路径、资源路径、NUL、冒号文件名以及最终符号链接/Windows 快捷方式和非普通文件。Windows 要求盘符限定的本地路径，拒绝 UNC/设备命名空间及尾部空格/点、设备保留名，按 [Windows 文件名规则](https://learn.microsoft.com/windows/win32/fileio/naming-a-file)处理。父目录本身可以包含已解析的链接；这些检查不构成文件系统安全沙箱。

默认文件预算 8 MiB，限额范围为 1..INT_MAX-1。读取前检查文件大小，分块读取时再次检查剩余预算，防止文件增长绕过上限；原文不做换行或编码转换，SHA256 覆盖实际读取字节。打开失败时用带 error_code 的文件状态查询确认真正缺失，权限等错误不能当作空配置。缺失与存在的零字节文件拥有不同版本。

版本包含规范目标路径、exists 和小写 SHA256；不依赖 mtime，外部只改注释/换行也产生冲突。文件读取不负责 YAML/schema 校验；无效外部文本同样保留。文件预算、YamlLimits 和结构预算相互独立，宿主需按文件策略设置解析限额，读取成功不代表可接受配置。

## 写入与错误

目标移动失效、输出超限、基线绑定路径错误或 hash 格式错误均在创建临时文件前拒绝。持 `<目标>.cordis.lock`，使用 tryLock(0)，不忙等；setStaleLockTime(0) 禁用按年龄判定过期，不主动强制移除锁。锁被占用返回 Result 失败，错误为 ConfigConflict/loader.file.lock；其他锁 I/O 失败为 ConfigIo。

持锁后读取完整磁盘版本并比较期望基线。不同则返回 ConfigurationFileCommit 的 Conflict，包含 ConfigConflict/loader.file.compare 和被观察到的外部原文；输入目标继续由调用方持有，两份来源都不会被自动采用。相同且目标字节已在磁盘时返回 Unchanged。调用方必须检查 outcome，不能仅以 Result.hasValue() 推断已经保存。

需要写入时，使用 QSaveFile 的同目录临时文件并显式 setDirectWriteFallback(false)，完整写入后再次读取/比较磁盘基线，最后 commit。失败或冲突通过 RAII 丢弃临时文件；成功报告在提交前准备，提交后只移动结果值。Qt 5.15.2 的临时文件、取消和直接写回退行为已按[固定 QSaveFile 源码](https://github.com/qt/qtbase/blob/v5.15.2/src/corelib/io/qsavefile.cpp)核对；协作锁只保护遵守同路径约定的参与者，见[固定 QLockFile 源码](https://github.com/qt/qtbase/blob/v5.15.2/src/corelib/io/qlockfile.cpp)。

新增源代码错误分类 ConfigIo，附加在 ErrorCode 尾部；loader.file.path/read/write/commit 区分故障阶段，不在诊断中输出配置负载。它不表示新建了稳定二进制 ABI。

## 验证及限制

真实临时目录测试覆盖缺失/空文件、已知 SHA256、CRLF/NUL 原文和精确预算、创建/更新/无变化、注释及无效 YAML 冲突、外部新建/删除、无效目标/路径/基线、I/O 错误、链接拒绝、协作锁与两线程竞争、没有应用对象时读写。Windows 专项使用真实不共享删除的文件句柄使重命名提交失败，确认旧字节、旧 hash 和可重试基线保留，且没有残留锁/临时文件。所有文件均在 QTemporaryDir，不使用 sleep 推测时序。

QSaveFile 提供文件替换机制，不等于任意外部编辑器之间的文件系统 CAS。两次 hash 检查会缩小非协作编辑窗口，但最后一次检查与重命名之间仍有竞争；未覆盖恶意目录替换、路径别名/硬链接、网络文件系统、断电或磁盘耗尽专项故障。这些边界不能被协作锁测试替代。

本轮未维护 desiredRevision/persistedRevision、dirty、写队列、watcher 或退出刷新，也未接入配置 UI、运行树或实例。后续 Q4d2 接完整 Store 状态与串行提交、Q4d3 接 watcher；Q4c2 的原生桌面验收前置条件继续保留。
