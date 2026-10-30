# P4-M4c3 需求映射与原生验收

日期：2026-10-30（北京时间）。基线 b9b889a。按架构优先原则先核对 P4 的 A01/A02、S06/S07、业务 DLL、同工具链 UI DLL 和资源驻留退出条件，再完成原生确认、产物检查及提交。P4 总审计见 [收口记录](p4-closeout.md)。

## 原生用户验收

运行原 Release 的 cordis_qt_workspace_demo，显式使用独立 build/p4-m4c3-desktop-* 下的临时 session.yaml，Qt 后端为 windows，未改原工作台配置。真实进程为 19728，窗口标题为“Cordis 插件平台”。

本次问题明确覆盖编辑后输出、添加第二实例、修改配置及启停、打开模块诊断、正常关闭。用户回复原文为 **“已全部验证”**；随后核对时进程尚在，要求正常关闭后，用户回复 **“主窗口已经关闭了”**。再次核对进程已退出，exit-code.txt 为 0，应用合并日志为 0 字节，保存的配置可解析且 formatVersion=1。没有通过强制终止获得退出结果。

本轮 Computer Use 技能要求的 JavaScript 会话入口未提供；插件发现也未找到可恢复的匹配接口。原生操作由用户完成，不写成代理执行，不虚构截图、逐次点击、系统 DPI 或跨屏验证。证据为本会话原始回复及 build/p4-m4c3-{desktop-session,manual-evidence}.json、该独立目录中的应用/退出日志。

## 产物复审与修复

发现新工作台 EXE 缺少 Windows Eve 图标资源；原有两个入口匹配正式七帧，新入口不匹配。只读 PE 检查的红结果为 build/p4-m4c3-icon-red.json / icon-red-debug.json（Debug 退出 1）。

第三个桌面入口接入时，将既有两处 RC 打包逻辑提取为单一 cmake/EveBrand.cmake，在共同父目录启用 RC。三个目标统一使用现有 eve.rc.in/eve.ico；没有编辑原始图标、第三方或只读参考，没有修改业务 C++。修复后 Debug/Release/ASan 三个入口的 9 份 EXE 均通过 RT_GROUP_ICON/RT_ICON 解析，七帧分别与正式 ICO 字节一致，合计 63 帧。

## 验收清单审计

新增 [机器可读矩阵](../tests/acceptance/p4-matrix.json)，19 个组件覆盖当前全部 237 个 P4 标签测试，8 个验收条目引用 54 个不同场景，另明确纳入 p3.session_module_add 支持检查。tools/audit_p4_acceptance.py 从实际 CTest JSON 读取清单，拒绝缺失引用、禁用场景、遗漏 P4 标签测试、重复归属及未声明的支持场景；清单校验与测试执行结果分别记录。

四项负向控制验证拼错引用、移除末尾清理组件（包括名称为 p3 的 P4 标签检查）、禁用必需测试、重复组件匹配都被拒绝，见 build/p4-m4c3-audit-negative-controls.json。测试范围不再仅依赖 p4.* 名称前缀。

| 验证 | 结果 | 证据（build/p4-m4c3- 前缀） |
|---|---|---|
| Release / Debug / ASan 重新构建 | 通过；仅 RC/入口重新链接，业务源码保持 | {release,debug,asan}-build.log |
| 三种构建的 P4 全标签与支持检查 | 各 238/238（237+1） | {release,debug,asan}-tests.log |
| 三套清单审计 | 237 项唯一归属、54 项引用、1 项支持检查 | acceptance-{release,debug,asan}.json |
| EXE 图标 | 9 份 EXE 的 63 帧原字节匹配 | icon-green-{release,debug,asan}.json |
| 原生用户确认与退出 | 两次回复、真实退出 0、空日志、合法保存结果 | manual-evidence.json 及独立桌面目录 |

本轮没有业务 C++ 修改。M4c2 的三套全量各 706/706、GCC 258/258 仍作为对应业务版本的完整基线，不记成本轮再次执行的全量；本轮重新验证全部 P4 范围与修改后的 EXE。13 项 M4c2 C++ 文件与用户体验的 b9b889a 基线哈希一致，Core/Qt/Loader/Ui 运行源码、SDK、第三方及只读参考未改。最终审计与源码快照见 build/p4-m4c3-final-{audit.log,source-sha256.json}。

当前 Windows x64 支持矩阵中的 M4c3 已完成，随本提交归档并同步。整个 P4 的结论和未纳入范围见 [P4 收口](p4-closeout.md)；P5 受控替换及物理卸载仍未开始。
