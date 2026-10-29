# P4-M4a 共同产物验证与 Qt 只读预检

日期：2026-10-29。代码基线 8cd2f60；架构原则已单独记录提交为 0213414。本段按架构审查先收敛边界，交付预检基础；尚未创建 Qt root、业务页面或 Qt 模块代次。

## 结构调整

原生 manager.cpp 同时负责固定文件、二次核对、PE、装载、目录和发现。先把已存在的固定来源/目录保护/二次摘要确认/PE 逻辑移到私有 VerifiedSource，C ABI 装载与 Qt 预检实际复用；原生管理器继续负责自己的装载/发现和唯一代次目录。VerifiedSource 持有声明、映像、祖先目录的原始句柄，返回后由原生代次接管映像句柄，其余保护覆盖本次装载调用。没有增加第二套 Registry、裸映像公开接口或后端空实现。

ModuleManifest 追加明确的 ModuleLoader（缺省 CAbi，兼容既有聚合初始化），公共解析允许 c-abi/qt-ui；C ABI 后端显式拒绝 Qt loader。相同声明比较包括装载种类。Qt 预检独立为 Cordis::ModuleQtPreflight，依赖共同预检与私有 QtCore；Core、Loader 和普通 Ui 不反向依赖它。公开 qt_manifest.hpp 只含宿主值，不引入 Qt/Windows 类型。

Qt inspector 在应用 owner 上固定产物、核对 loader，然后读取嵌入元数据。Qt 自带 major/minor/debug、接口 IID、moduleId/version、无顺序插件集合，以及自定义 SDK/Qt 完整版本、编译器/版本、动态 CRT、迭代器调试级别、指针宽度和 C++ 标准必须匹配当前宿主；运行时 qVersion 也须与编译版本一致。Qt 自带元数据仅编码 major/minor，patch 由自定义完整版本核对。该信息是声明预检，不构成任意 C++ ABI 互通或 Qt 工厂虚表验证。

读取阶段不调用 QPluginLoader.load/instance，不映射测试 DLL、不登记代次。QtModuleInspection 只是一次快照，释放保护后文件若改变必须重新预检；后续 M4b 必须重新固定来源，不能把本结果作为永久授权。QObject root 所有权与页面完整析构复用方向见 [ADR-0037](adr/0037-qt-module-boundaries.md)。

## 测试组织与审查

新增独立 Qt 测试入口，没有向原生业务入口增加 Qt 分支。提取测试文件读写/安装设施给既有原生和新 Qt 检查共用，移除原入口中的重复声明构建。原生来源检查也复用同一个生产组件，既有路径/junction/篡改/共享/PE/代次回归覆盖结构调整。

首轮 159/160，唯一失败为把 Qt 元数据 major/minor 误当完整 patch 版本；核对本机 5.15.2 moc 生成格式后，修正为框架 major/minor 与自定义完整版本分层比较。随后目录无序语义与非 Qt DLL 拒绝也加入独立入口。

增量构建检查复现 JSON 改动未进入 DLL：修复前修改 SDK 元数据后，good 检查仍错误通过，见 build/p4-m4a-incremental-negative-tests.log。根因是该生成路径的 moc 依赖未可靠进入产物，现显式建立 JSON→moc→DLL 构建依赖，不靠清理目录或碰源文件时间戳。修复后修改同一 JSON 真实重新生成/编译/链接，预检按预期 0/1 拒绝；finally 恢复 JSON 并重建后 good/order 为 2/2，见 incremental-fixed-{negative,restore}-*.log（均沿用 p4-m4a- 前缀）。修复前矩阵保存为 pre-moc-*，不替代最终矩阵。

审查确认原有来源保护未降级、loader 不串用、工具链字段类型/完整字段严格比较、目录按集合核对、元数据不创建对象/代次、线程拒绝及文件变更后重新验证。未发现阻止本段提交的剩余问题。SDK/头/测试编码与修改区域格式审计通过；没有改 Core、Loader、Ui、只读参考或第三方代码。

## 验证映射

| 场景 | 自动化依据 |
|---|---|
| 合法 Qt 元数据、宿主构建契约、文件变更后重验 | p4.qt_preflight_good |
| 编译器、Qt patch、CRT、迭代器调试、指针宽度、SDK 不同或缺失 | p4.qt_preflight_compiler / qt / runtime / iterator / pointer / sdk / missing |
| 插件目录不匹配、目录顺序不影响语义 | p4.qt_preflight_catalog / order |
| C ABI/Qt 后端双向拒绝、错误线程、真实非 Qt DLL | p4.qt_preflight_backend / thread / not_qt |
| 共同来源提取后的原生兼容 | 既有 P4 native/manifest 全部场景 |

全部 Qt 检查均确认 GetModuleHandle 为空、ModuleManager 目录数量不变。这里只验证只读基础；M4b 的 Qt 后端/工厂/租约、M4c 的页面与原生 UI 验收仍待实施，P5 和物理卸载未开始。

## 最终验证

原目录 MSVC Release 全量 630/630、Debug/ASan P4 各 162/162、无 Qt GCC 全量 245/245；原 Release 关闭示例 P4 162/162 并恢复 ON。good/order/backend/thread 在 Debug/ASan 各连续 20 次，共各 80 次通过。16 项源码/构建快照一致，17 个变更源对应 C++ 编译命令含 ASan（包含九个元数据变体）；两个公开头在 MSVC/GCC 不带 Qt 包含目录独立编译通过。

日志前缀为 build/p4-m4a-reviewed-，包含 {release,debug,asan,gcc}-{build,tests}.log、{debug,asan}-repeat.log、no-examples-*.log、restore.log、source-sha256.json、asan-commands.json、headers.log 和 audit.log。新构建关系修正后重新冻结源码；之前数量相同的 pre-moc 轮次仅为过程历史。

Qt 元数据与 root 所有权依据 [Qt 官方 QPluginLoader 文档](https://doc.qt.io/qt-5/qpluginloader.html)，并以本机固定 Qt 5.15.2 头和 moc 输出核对。无 UI 页面改动，本段不要求人工桌面验收。
