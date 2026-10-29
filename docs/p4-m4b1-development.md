# P4-M4b1 共同目录与 Qt root 装载

日期：2026-10-29。基线 99863b8。按用户要求将 M4b 分为可审核提交的装载层与业务工厂层；本段不代替 M4b2、M4c 或整个 P4。

## 架构与实现

修改前检查职责、依赖、状态和所有权。原 manager.cpp 同时维护唯一 Registry/Generation、受限映射及 C ABI 发现，并在四处重复目录查找。现在 catalog.cpp 管理同一份有界目录、代次保留/复用/失败、映像 PIN 和实际文件身份核对；manager.cpp 保留 C ABI 描述握手及原生业务入口。Qt 后端实际使用该目录和映射机制，没有增加第二份注册表。

Generation 的后端驻留所有者只用于保留具体加载器，不提供空业务接口；共同错误信号独立于目录和 Windows/Qt，使原生请求层不因复用目录而依赖装载实现。Core、配置和普通 UI 不反向依赖 ModuleQt。公开 qt_module.hpp 只含宿主值和 loadQtModule，不含 QObject、Windows 或可调用插件指针。

loadQtModule 在应用 owner 上重新固定来源，核对同一产物的 Qt 元数据，然后预留共同代次。两后端复用受限 LoadLibraryExW 搜索、实际映像身份核对和进程 PIN；Qt 再为同一映像取得自己的 loader 引用并创建 root。所有外部入口均在目录锁之外执行，root 完成前保持 loading/Validated，构造重入只能看到未完成状态。

QPluginLoader 与 root 保留在进程目录；root 始终归 loader，不手动 delete 或 unload。创建调用登记 Call，返回后释放；root 的 QObject 租约保留到进程退出，异常或不确定资源也保留。root 必须留在应用线程且没有外部 parent。root 仍存活时 drain 拒绝，不把没有业务实例当作根对象已经析构。后续业务实例清理与进程驻留根工厂是不同边界。

本段仅创建 loader root，尚未 qobject_cast/调用 IQtPluginV1 业务工厂，故 Loaded 不等于可激活，descriptorAccepted 和 businessAccepted 均为 false。NativeModule.open 显式拒绝 qt-ui 代次，避免共享目录后把 Qt 映像送进业务 C ABI。下一段 M4b2 实现真正的工厂契约、独立业务对象与完整清理屏障。

## 审查与验证

由当前代理审查，未使用子代理。复核来源保护不降级、唯一身份/64 代预算、原生状态兼容、锁外调用、root 的真实所有权与不确定失败、Qt 工具链校验复用，以及公开头与依赖方向。预检新增两个异常构造和一个重入夹具依然不映射，证明元数据预检不能替代真实 root 检查。

首轮 Qt 装载/预检 26/26 通过，增加混合后端、外部 Qt loader、无应用及构造重入后继续验证。负向控制临时在 instance 前发布 Loaded，重入检查按预期 0/1；finally 中逐字节恢复生产源码并重新编译，恢复后 1/1。该控制证明提前发布会被测试捕捉，并非声称提交版本曾存在此缺陷。日志为 build/p4-m4b1-negative-{build,tests}.log 和 negative-restore-{build,tests}.log。

真实 DLL 场景包括成功装载、不同安装路径同内容复用且 root 计数为一、相同模块 ID 不同摘要拒绝、root 活跃时拒绝 drain、错误线程/无应用/错误后端、工具链拒绝/预检后篡改不映射、root 错误 parent/抛异常后失败驻留、外部 Windows/Qt 映射不收养、C ABI 与 Qt 同进程共用目录，以及构造时重入和关闭拒绝。所有已映射夹具由既有子进程包装器在进程退出后删除，未新增绕过路径校验的清理工具。

最终原目录 Release 全量 648/648、Debug/ASan P4 各 180/180、无 Qt GCC 全量 245/245；原 Release 关闭示例 P4 180/180，完成后恢复 ON 并重建。reuse/reentry/throw/mixed 在 Debug/ASan 各连续 20 次，共各 80 次通过。冻结的 16 项源码/构建哈希一致，18 个相关 C++ 编译命令含 ASan（包含十二个元数据夹具变体）；公开 qt_module.hpp 在 MSVC/GCC 无 Qt 包含目录独立编译通过。首个手动头探针遗漏项目要求的 /utf-8，引发 C4819，失败日志保留为 headers-missing-utf8；补齐与项目一致的编码选项后通过，未放宽警告或改源码。

最终日志为 build/p4-m4b1-reviewed-{release,debug,asan}-{build,tests}.log、{debug,asan}-repeat.log、no-examples-*.log、restore-*.log、source-sha256.json、asan-commands.json、headers.log 和 audit.log；无 Qt 结果为 build/p4-m4b1-review-gcc-{build,tests}.log。先前 review-release 的 644 项为增加最后四项前的过程证据，不代替最终 648 项矩阵。

复核未发现阻止本段提交的剩余问题，M4b1 随本提交归档；M4b2/M4c 和 P4 整体保持进行中。没有修改 sources/、research/、第三方或桌面页面；本段不重复要求桌面人工确认，M4c 的真实 UI DLL 仍须原生验收。
