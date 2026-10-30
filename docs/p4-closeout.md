# P4 收口审计

日期：2026-10-30（北京时间）。范围：Windows x64 / MSVC 19.44 / Qt 5.15.2 / 当前共享 Core、Qt、Ui 源码 SDK 与默认驻留策略。

**P4 在上述支持矩阵已完成。** ModuleManager、C ABI 请求/服务/effect 桥、Core 托管与 Kernel/Session、两个业务服务 DLL、同工具链 Qt/Ela 业务页面 DLL、只读模块诊断、正常退出和失败驻留均有实现及验收依据。没有将描述握手、占位接口或单纯编译通过当作完成。

## 完成条件

对照 [开发指南的 A01/A02 与 P4 出口](../Cordis-Cpp17-Qt5152-开发指南.md)及 [设计说明书 §18–19](../Cordis插件平台-软件设计说明书.md#18-测试与需求追踪)：

| 条件 | 实际交付与证据 | 结论 |
|---|---|---|
| 模块唯一目录与固定来源 | 共同 VerifiedSource/进程目录、受限 Windows 映像映射、摘要与 PE 验证、固定文件与父目录；C ABI/Qt 共用 | 完成；manifest/native/qt_load 回归 |
| A01：ABI 与工具链拒绝 | C11/C++17 布局、major/size/保留位/能力表；Qt 编译器、CRT、Qt/SDK、指针宽度、Debug/迭代器与实际 Core 身份 | 完成；非法业务进入前拒绝，不能将 C ABI 与同工具链 C++ 契约混为一谈 |
| A02：可执行引用与驻留 | Instance/Call/任务/回调/数据/服务/QObject/外部引用真实租约；根与映像 PIN，错误路径保留诊断 | 完成（默认驻留）；不提供清理前的物理卸载 |
| S06：取消后晚到 stop | 生产 RequestChannel 的一次取消与真实 DLL 晚到完成；生产 HostTable 等待 PENDING stop 后再 destroy | 完成；不宣称 NativeModule.close 具有公开取消接口，见 [分层验收](p4-m3d2b-development.md) |
| S07：失活与 DLL 捕获 | Core 失活抑制业务主体、DLL 内捕获析构、最后宿主副本跨线程退役；活动/失败对照 | 完成；结果发布、捕获退役、静止分别验证 |
| 两个业务服务 DLL | counter/reporter 仅使用 C ABI，真实 Qt/Kernel/Session、共享计数、多实例/realm、磁盘保存与失败退出 | 完成，见 [业务服务](p4-m3d2a-development.md) |
| 同工具链 UI DLL | 共享运行库、真实工厂和独立 QObject；笔记页面/命令/输出、多实例、换代、启停、失败和模块诊断 | 完成，见 [工作台](p4-m4c2-development.md) |
| 原生用户验收与正常退出 | 本次用户确认全部操作及主窗口关闭；真实退出码 0、空应用日志、合法保存结果；业务源码与已验版本一致 | 完成，见 [原始范围与回执](p4-m4c3-development.md) |
| 品牌与产物复审 | 正式 Eve ICO 统一接入三个入口；9 份 EXE 的七尺寸资源逐字节校验，未改原稿 | 完成；新入口缺图标已真实复现并修复 |
| 审核、验证、提交与同步 | 各子段审查/修复/验证后提交；本轮清单、产物和收口审查归档 | 随本次收口提交同步；历史证据按阶段区分 |

## 最终证据与复现

[机器可读矩阵](../tests/acceptance/p4-matrix.json) 与 [详细验收表](../tests/acceptance/p4-module.md)共同维护：19 个组件覆盖当前 237 项 P4 标签检查，8 个出口条目引用 54 个独立场景，另纳入 1 项 Session 支持检查。三套 MSVC 当前 P4 复核各 238/238；缺引用、禁用、遗漏标签及重复匹配的负向控制均拒绝。清单工具只证明测试范围完整，不伪报其执行结果。

在配置好的构建环境中：

```powershell
python tools/audit_p4_acceptance.py --build-dir build/ela-msvc-release --output build/p4-acceptance.json
if ($LASTEXITCODE -ne 0) { throw 'P4 acceptance inventory failed' }
$scope = (Get-Content build/p4-acceptance.json -Raw -Encoding UTF8 | ConvertFrom-Json).selectionRegex
ctest --test-dir build/ela-msvc-release -R $scope --output-on-failure --no-tests=error
python tools/check_eve_icons.py build/ela-msvc-release/bin/cordis_ela_demo.exe build/ela-msvc-release/bin/cordis_config_workbench.exe build/ela-msvc-release/bin/cordis_qt_workspace_demo.exe
```

可用 --ctest 指定 CTest 绝对路径。业务版本 b9b889a 的完整基线为 MSVC Debug/Release/ASan 各 706/706、无 Qt GCC 258/258，关闭示例和静态兼容亦有独立证据；本次不把该历史基线冒记为再次跑完的全量。当前重建与 238 项复核、原生回执、图标红绿结果及源码/边界审计完整索引见 [M4c3](p4-m4c3-development.md)。

## 支持边界

本结论支持当前工具链下的默认驻留演示与集成。Qt C++ SDK 要求相同源码/工具链及共享运行库，不是稳定公共二进制 ABI；无 Qt GCC 检查不声明 GCC DLL 与 MSVC 宿主互通。

逻辑退出与释放执行引用不等于卸载 loader root 或 DLL。物理卸载、在线替换、状态迁移、旧代恢复及卸载白名单属于 P5，尚未开始。自动化和本次用户确认没有新增系统 DPI/跨屏验证，也未生成原生点击或截图记录；先前 UI 专项的范围按其独立收口文档保留。
