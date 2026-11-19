# P5 最终 Release 桌面验收

状态：2026-11-19已完成最终同产品界面取证与当前B的人工体验；实现基线fdf099c。原计划步骤和早期记录保留，下文结果明确区分自动界面动作、人工回执和失败历史；此前P3/UI人工回执保持其原范围。

## 运行入口

由运行记录器打开最终产品窗口，第一次正常关闭后会用同一配置自动重开。手动入口如下（不与已打开窗口重复运行）：

```powershell
./tools/desktop.ps1 -Action run -Preset ela-msvc-release -Workbench business -QtRoot E:/Qt/5.15.2/msvc2019_64 -ConfigurationFile D:/01_C++/Eve/build/p5-m5e3-native/session.yaml -StoreRoot D:/01_C++/Eve/build/p5-m5e3-native/store
```

这是独立验收配置和存储，不修改原用户工作区。初始Counter、Reporter、Notebook均为A版1.1.0。打开“文件 → 模块管理…”并先保存一次，建立A版基线。

## 升级顺序

模块窗口选择对应行，输入候选module.json的绝对路径，再按“检查影响 → 升级”操作：

| 顺序 | 模块 | 候选声明 | 新版本 |
|---|---|---|---|
| 1 | Counter | D:/01_C++/Eve/build/ela-msvc-release/bin/native-upgrade/counter-b/module.json | B，1.2.0 |
| 2 | Reporter | D:/01_C++/Eve/build/ela-msvc-release/bin/native-upgrade/reporter-b/module.json | B，1.2.0 |
| 3 | Counter | D:/01_C++/Eve/build/ela-msvc-release/bin/native-upgrade/counter-a/module.json | A，1.1.0 |
| 4 | Reporter | D:/01_C++/Eve/build/ela-msvc-release/bin/native-upgrade/reporter-a/module.json | A，1.1.0 |
| 5 | Notebook | D:/01_C++/Eve/build/ela-msvc-release/bin/qt-upgrade/notebook-b/module.json | B，1.2.0 |

## 检查步骤

1. 从左侧插件入口打开“插件笔记”，输入两行草稿，如“P5-e3：中文草稿 📝”和“第二行仍须保留”。用页面按钮输出，再通过Ctrl+Shift+P执行输出笔记命令，确认两种入口可用。
2. 按表完成四次Native升级。每次检查影响时核版本、SHA及影响路径；检查本身不应改变当前版本。点击升级后等Applied及已提交报告，再核本次操作和排空/物理卸载回执。
3. 每次Applied后先观察“配置未保存”，再点“保存已接受配置”，确认Written及保存修订同步。应用与保存应是两个可见动作。中间磁盘保持的精确断言由e2的原D1自动证据承担，本轮记录器只在退出后读文件。
4. Native升级期间笔记草稿、页面及命令应继续可用；Home插件卡“管理”的实例/服务与界面信息能看到消费者恢复运行及当前绑定。Counter替换时其依赖消费者按闭包重建属于预期。
5. 再执行Notebook A→B。必要时重新打开笔记页，两行草稿应恢复；编辑一小段，再分别用按钮和命令输出。当前页面和命令只对应新有效实例/代次，不重复出现旧贡献。
6. 在模块窗口查看旧代和回收结果：旧Native有原排空及物理回收回执；Qt旧A保持进程驻留及实际原因属于预期。不要将“目录映像引用已释放”单独当作全进程映像消失；本轮无需人为制造PIN或外部工厂引用。
7. 隐藏后重新打开模块窗口，确认操作历史、当前版本和保存状态保持。最后保存，关闭辅助窗口，再正常关闭主窗口。
8. 记录器会自动打开第二个进程。确认同一配置保存为Counter A、Reporter A、Notebook B，再正常关闭。这里检查保存定位，运行态草稿不承诺跨进程持久化。

本轮人工观察现有界面和交互。Native精确数值、HMODULE/完整MEM_IMAGE离开和文件独占由[e2验收映射](../tests/acceptance/p5-replacement.md#m5-e2-最终五配置全量与需求映射)承担。两次真实进程退出、最终配置和产物摘要由记录器留档，不能仅凭窗口隐藏判断已正常退出。

## 本次回执

原计划的人工回执与早期保存A记录不一致，原件没有改写为成功；聚焦复核也因保存A被guard拒绝重开。原因未定，详见[原始取证与收口](p5-m5e-development.md#e3-收口结论)。

最终在同一fbece70b…产品、新build/p5-m5e3-ui-repro目录通过实际Windows界面自动操作完成：Notebook A→B的Applied与dirty，显式Save Written2，17032正常退出，再由同文件/store新16824进程加载B1.2.0。随后实际UI自动完成Counter B→Reporter B→Counter A→Reporter A，每次分别Applied/Written，最终表格A/A/B；该自动补充顺序为QtB后Native往返，不冒称原人工序列。

用户在已验证的16824 B窗口体验笔记编辑、输出按钮和Ctrl+Shift+P命令，明确回复“已体验，通过，并已关闭当前窗口”。16824实际exit0，两个退出后的配置及最终文件均SHA8f2d745d6cc2a229ff36a34e066698104233af155219b24e07a243b611273ce0，精确A/A/B定位；四份21实物前后摘要相同。自动动作、用户原话、真实两个退出码及失败历史分别留档，不修改recording-only的原字段。

最终验收出口已齐备，文档审查后随本次提交同步；正式范围、完整SHA及各原件入口见[证据索引](p5-release.md)。
