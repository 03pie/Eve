# ADR-0042：固定来源与 hash 目录安装事务

日期：2026-10-30。状态：接受。承接 [候选代目录](0041-module-candidate-generations.md)与 [P5 计划](../p5-development.md)。

## 问题与边界

M1 可并存独立目录中的当前/候选代，但调用方仍须自行准备目录。直接让 ModuleManager 复制文件，会把磁盘事务、运行目录和后端执行混在一起；只返回预检快照则会在复制、发布与后续装载之间丢失来源保护。

新增独立 Cordis::ModuleInstall，依赖现有 ModulePreflight 和 QtCore 的路径/摘要设施，不依赖 ModuleNative、ModuleHost、Kernel、Loader 或 UI。同步安装函数可以在宿主 worker 上执行，无 QCoreApplication、QObject、调度器或 owner 线程前提；调用方负责等待 IO 完成再退出。安装器不创建代次、不执行 DLL，也不修改业务运行或配置。

## 选择与所有权

- 来源复用 VerifiedSource 的文件固定、两次声明/摘要核对、祖先保护与 AMD64 PE 检查。路径限定和祖先保护提取为私有共同设施，没有新增另一套验证规则。
- 宿主提供已存在且可写的 store 根；先按句柄解析最终路径，再保护其祖先并核对根的身份。根别名解析为真实根，安装内容不沿缓存目录的重解析点跳转。
- 安装布局为 `<store>/<DLL sha256>/module.json` 和声明中的原 DLL 文件名。只复制当前 schema 声明的这两个文件，保留原始 JSON 字节；不扫描目录或隐式打包依赖、配置、资源旁文件。业务能力、依赖解析、Qt 元数据/工厂及迁移兼容仍由后续阶段验证。
- 同 hash 的安装/复用先以 CREATE_NEW 获取专用锁文件，持有期间不共享，关闭时删除。已有锁只返回忙/不可获取，不推测过期、不清除外部锁。不同 hash 可独立安装。只读缓存复用也要求可获取这把锁。
- 在同一根内创建唯一临时目录，记录目录及两个文件的 128 位原生身份。从固定来源按 64 KiB 分块复制、检查实际写入并 flush；临时声明/摘要完整通过后才发布。
- 用拥有的目录句柄执行不覆盖的重命名。关闭子文件句柄后发布，再按最终名称取得只读保护，重新检查文件内容、声明身份、路径、文件列表、链接数及创建时的原生身份。已有 hash 目录也必须重新验证，声明比较包括库文件名；冲突或篡改均拒绝，不覆盖旧内容。
- InstalledArtifact 的副本共享安装目标的只读文件和目录保护；移动后的凭据无效。它只保护声明产物与路径身份，不是业务授权、可执行引用或永久文件系统访问控制。装载方持有凭据交接给原 stage 路径，stage 仍独立验证并取得自己的保护。
- 发布前失败显式回收已记录身份的临时文件，并通过目录句柄删除空目录。外来文件、路径替身或被占用文件不能被递归删除；清理不完整成为原错误的 causes，路径保留供诊断。发布后验证失败保留已可见目录并报告错误，不对可能已被其他进程观察的目录做撤回。

## 兼容性与实际限制

现有 load/stage、Current/Candidate、默认驻留、Windows PIN、64 代预算和 Core 发布语义不变。安装完成不能代表运行切换成功，Qt 业务及物理卸载策略仍由原边界管理。

事务提供正常进程中的完整目录发布与错误可见性，不宣称断电持久性、崩溃后自动清扫、管理员攻击隔离或未知文件的安全接管。store 应由宿主管理；拿不到创建目录的身份时保留异常路径并报错，不猜测归属。完整 UNC 输入规则沿用预检，但本轮真实文件系统验证为本机 Windows NTFS，未把本地结果写成网络共享的实测证据。

Windows 行为以官方 [FILE_RENAME_INFO](https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-file_rename_info)、[SetFileInformationByHandle](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setfileinformationbyhandle) 和 [CreateFileW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew) 为依据。安装、重命名冲突和清理使用真实 OS 文件与句柄验证；证据见 [M2a 记录](../p5-m2a-development.md)。
