# P7 文献阅读插件：数据与持久化

日期：2026-11-24。实现范围为 `plugins/literature-reader` 自有业务数据；不修改平台、公开 SDK 或既有运行包。PDF 是首要导入格式。

## 职责与依赖

`literature::ReaderLibrary` 只依赖公开 Qt Core。它拥有文献目录、阅读位置、按页批注、书签、笔记和最后选中文献；页面通过同一模型读写，不能另建一套持久化状态。插件装配层提供用户明确指定的可写 workspace 根，不能使用 DLL 目录、平台 Session/Store 或宿主个人偏好作为文献库。

公开接口位于 `reader_library.hpp`：`open`、`reloadDiscardingChanges`、`importDocument`、`verifiedFilePath`、`documents/document`、位置/批注/书签/笔记更新和显式 `save`。文献 ID 与批注/书签 ID 是独立 UUID；正文身份是实际复制字节的 SHA256。阅读位置与附注使用零基页号，UI 负责按实际 PDF 页数限制导航。

大文件采用两阶段：静态 `prepareImport(root, source, title, cancelled, error)` 在受控工作线程中流式处理，只返回私有构造的 `PreparedImport`；UI 线程以 `adoptPreparedImport` 核 workspace、普通文件长度及准备时修改时间，再加入内存目录。静态 `verifiedFilePathFor` 接收 root 与 Document 值快照，可独立线程校验阅读文件；实例便利接口复用同一逻辑。工作线程不能访问活动模型，UI 必须持有并等其自然返回；取消在每块复制/校验间检查。取消或页面关闭不自动删除已经导入的对象。

## 文件与所有权

用户 workspace 下只有 `library.json`、短时 `library.lock` 与 `objects/<sha256>.pdf`。源 PDF 只读，记录原文件名，不保留源绝对路径。导入用同目录 `QTemporaryFile` 分块复制并计算 SHA，检查读取错误、大小与源元数据变化后原子重命名；目标已存在则核对实际长度/SHA，绝不覆盖不同字节。最大单文件 512 MiB；导入不把整份 PDF 放进 JSON 或内存。

对象不可变。索引写失败或用户放弃未保存导入时，对象可以暂时没有索引引用；本轮不自动删除对象，以免误删另一实例尚未提交的文献。正文损坏/丢失会明确拒绝阅读，不回源路径偷偷替换。`verifiedFilePath` 分块核对正文后才交给 PDF 渲染层；模型不渲染 PDF，不虚构解码成功。导入先核扩展名及 PDF 头，完整解析错误由实际渲染器报告。

## 状态、并发与失败

`open` 在临时快照完整校验后才替换模型；失败保持原内存状态和原文件。未知 schema、重复身份、非法对象路径/长度/字段类型与超限元数据明确失败，不按空库重建。已有未保存修改时 `open` 拒绝，只有明确的 `reloadDiscardingChanges` 才丢弃草稿重载。

所有修改先进入内存，标记 dirty。`save` 只短时持有 `QLockFile`，比较从 `open/save` 获得的索引 SHA 与存在状态，避免不同实例静默覆盖。冲突返回错误并保留 dirty 和原 revision；用户可以重新加载，或在独立 workspace 中工作。锁不持有整个实例寿命，不自动合并用户笔记。

索引通过 `QSaveFile` 写入且关闭 direct-write fallback，只有实际 `commit` 成功后才更新 revision、磁盘基线与 dirty。保存核对象仍是约定长度的普通文件，不在 UI 线程重哈希整馆；真正打开阅读时走独立正文 SHA 校验。写入/权限/冲突失败不截断旧索引，不把失败当保存成功。模型析构没有隐式保存、删除或权限修改。业务 workspace 属于用户；这不是对恶意同权限进程的文件系统沙箱。

## 验证与边界

插件内数据回归使用真实临时目录和文件，覆盖 Unicode 流式导入/原件不变、位置与附注/书签/笔记保存重开、短锁失败后重试、双实例冲突、坏 schema/索引和对象损坏的失败保护。测试 CMake 由插件 UI/装配负责人统一接入。数据层不引入私有 SDK 头、平台内部链接或第二个数据库框架。

## 已执行验证

1.1 的实际页面需求增加 `exportNotes`：导出 UTF-8 Markdown，包含文献标题、当前笔记和按页书签/附注。导出使用同一模型和 `QSaveFile`，拒绝库内路径、链接目录/目标、PDF 扩展及已有 PDF 头，不修改原文、索引、revision 或 dirty；UI 不再复制存储算法。

独立新测试树 `build/p7-reader-data-check-20261124-1` 使用 Qt 5.15.2 / MSVC 19.44.35222、C++17、`/W4 /WX` 构建。`round-3` 中实际宿主进程 28736 自然退出 0，初始九项数据回归全部通过，源码前后 SHA 相同：Unicode/多块导入和重开、附注等状态重开、双实例冲突、短锁失败后重试、坏元数据保护、坏正文/导入保护、真实 `std::async` 准备与取消、非法编辑拒绝、临时索引真实只读保存失败及恢复后同草稿成功。1.1 导出接口完成后的最终 `round-5` 实际进程 9624 自然退出 0，十项全部通过，新增覆盖 Unicode Markdown 内容、库内/PDF/相对路径拒绝与原文/索引/dirty/revision 不变；源码前后 SHA 相同。

`round-2` 原编译失败保留：真实工作线程结果传输暴露 `PreparedImport` 缺少赋值操作，已补默认值赋值，不增加票据构造入口。最终命令、启动、退出、原始 stdout/stderr 和源码索引保存在 `round-3`；初始八项通过只作开发历史。

这组数据测试不宣称 PDF 解码/渲染或 UI/插件生命周期验收通过；这些由实际阅读后端和独立插件验收覆盖。插件 1.0/1.1 在本轮共享业务 schema 1，持久数据重开不等同跨 DLL 的热替代状态迁移。
