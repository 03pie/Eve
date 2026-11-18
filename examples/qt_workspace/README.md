# 配置驱动的笔记 UI DLL

这是 M4c2 的实际业务示例：一个独立 Qt/Ela DLL 贡献笔记编辑页、注册“输出笔记”命令，并写入当前实例的真实输出通道。支持同一插件多个配置实例、配置换代、启停、保存、失败回滚和清理失败诊断；DLL 不链接 Loader/Session/模块目录，也没有测试控制导出。

在当前 Windows x64 / MSVC / Qt 5.15.2 / 共享 Core 构建中运行：

```powershell
./tools/desktop.ps1 -Action build -Preset ela-msvc-release
$env:PATH = "$PWD/build/Qt/5.15.2/msvc2019_64/bin;" + $env:PATH
./build/ela-msvc-release/bin/cordis_qt_workspace_demo.exe
```

可用唯一参数指定自己的 session.yaml；默认使用 CordisDllNotebook 的独立应用数据目录。首次读取 [workspace.yaml](workspace.yaml)，不会修改原配置工作台的文件。DLL 和 manifest 在同一构建的 bin/qt-workspace/notebook 下；元数据、Debug/Release 产物及 SHA256 不可混用。

- 编辑后点击“输出当前笔记”，或从底部命令面板执行对应实例的命令，在“输出”选择该实例的 notebook 通道查看。编辑框投影本次实例草稿；启动文本通过配置中的 text 字段保存，编辑草稿不会自动写回配置。
- 顶部配置入口打开现有配置文件界面；插件卡片中的配置界面可修改 title/text。添加实例使用 example.notebook，宿主 Session 自动带入冻结的 notebook 模块归属。
- 顶部“文件→模块管理…”显示代次、业务入口状态、真实引用、驻留原因及配置失败原因；原 1.0.0 Notebook 不提供代码状态升级，但仍可保存配置。业务入口开放与某个配置实例激活成功是分别记录的状态。
- 输出过长会给出失败反馈，保留现有输出。退出前保存冲突会保留窗口和 Session，显式处理磁盘冲突后再关闭。

配置可使用可选的布尔 failStart 或 failCleanup 演示失败。前者验证已创建资源回滚；后者故意阻止业务所有者最终释放，显示不静止并保留 DLL/实例引用，不能解释为成功退出。正常退出同样不卸载 loader root 或映像。

原 12 项真实 Qt/DLL 联合场景和无 Qt 的 Session 追加检查见 [开发记录](../../docs/p4-m4c2-development.md)。关闭示例选项时，实际 DLL、宿主装配和检查仍构建；关闭共享 Core 时该同工具链示例不提供。

P5-M5-b3 另生成 `bin/qt-upgrade/notebook-a`（1.1.0）和 `notebook-b`（1.2.0）实际产物，使用现有独立状态 IID 保存实例当前草稿，全部继续驻留。原 demo 默认仍使用 `qt-workspace/notebook` 的 1.0.0 无状态入口；不能把它的普通编辑能力等同于支持草稿迁移。

A/B 共用 `org.cordis.example.notebook.draft` v1 和规范 JSON 格式，迁移至多 65536 个 UTF-8 字节；正常编辑不截断，超限捕获明确拒绝。新增十一项实际协议/草稿/输入冻结/完整销毁检查和原十二项场景已在 Release/Debug/ASan 各专项 23/23 通过，见 [收口记录](../../docs/p5-m5b-development.md#b3-收口结论)。同进程代码替换、管理入口和最终桌面升级验收继续由 b4 与 M5-c/d/e 完成，本段不提供手动绕过代次门禁的装载路径。

b4-1 增加明确的固定业务资格入口，和原 demo 共用同一 Workspace 与启动/退出实现：

```powershell
./build/ela-msvc-release/bin/cordis_platform_workbench_demo.exe
```

它在独立 `CordisBusinessPlatform` 应用数据目录中保存 session 和 hash 安装 store；也可依次传自己的 session 文件与 store 目录。单一 Kernel/manager/Driver/Session 同时装配 counter、reporter 和 Notebook。新 session 选择三个 A，已有文件按保存的定位及原身份/后端/固定 Native 批准策略加载，不强限为 A/B。仅固定四个 Native 资格产物的实际身份/摘要临时获准，生产名单仍空，Qt 固定驻留。

保存冲突保留原窗口/业务以便处理后重试关闭；不静止/未知回执保留原拥有链，正常关闭先释放实际工厂再记录 drain，调用方额外工厂引用的拒绝与物理卸载分开。九项新场景加原回归在三配置各专项 50/50、静态 Native 18/18 通过，见 [b4-1 收口](../../docs/p5-m5b4-development.md#b4-1-收口结论)。b4-1 当时仅交付统一运行/关闭；当前升级接线见下段，卡片启停仍不等同代码替换。

[b4-2](../../docs/p5-m5b42-development.md#收口结论)已增加 Workspace.previewUpgrade / upgrade / upgradeSnapshot 的宿主实际接线；当前能力查询为 owner 的 Result，并按原事务真实共同提交同步 Current。预览不装载/安装，执行重新验证来源、修订与闭包。三类实际 A/B、Notebook 故障恢复、通知/回执故障及 Native 真实回收通过有限集成场景；正常 Qt root 继续驻留。b4-2 当时未交付产品按钮；当前 c2 交互见后，Home 开关仍仅是配置启停。

可在已有 Release 构建后运行受控替换集成检查：

    ctest --test-dir build/ela-msvc-release -R "^p5.platform_upgrade_" --output-on-failure -j 1

需先加载项目 MSVC/Qt 开发环境。十二项是有限同平台事务验证，不代替 100 次压力或原生桌面人工验收。完整本段受影响检查在三配置各 219/219，通过的实际旧 SDK .5 DLL 仍为原始保存实物。

M5-c1 接通顶部“文件→模块管理…”及命令面板的平台命令，打开同一个托管模块窗口，显示当前版本、策略、配置实例与资源/错误。关闭该窗口只隐藏；整个平台保存确认后等待当前模块视图更新退出，再关闭会话与内核。SDK 0.1.5 的旧宿主与旧插件已实际配当前 Ui/Core 运行；精确验证与最终文案复验范围见 [c1 结论](../../docs/p5-m5c-development.md#c1-收口结论)。c1 当时未提供升级按钮；c2 实际操作见下段。

M5-c2 使用同一模块窗口，具体边界及自动验证见 [收口](../../docs/p5-m5c-development.md#c2-收口结论)。测试升级请启动上面的 `cordis_platform_workbench_demo.exe`，使用独立 session/store，避免覆盖自己的配置：

1. 顶部“文件→模块管理…”选择 counter、reporter 或 notebook。
2. 输入/选择候选 `module.json` 的绝对路径。例如当前是 A 时，选择同一 Release 的 `bin/native-upgrade/counter-b/module.json` 或 `bin/qt-upgrade/notebook-b/module.json`。本段示例验证 A→B；已卸载 Native 回切仍走原资格重验，Qt 旧代驻留内容不承诺在同进程重复回切。
3. 点击“检查影响”，核对旧新版本、SHA256、策略和受影响配置/闭包；这里尚未安装或装载，接口/状态能力在实际准备时校验。更改输入、模块或修订需重新检查。
4. 点击“升级”，查看原 OperationId、阶段和结果；可以请求取消。关闭模块窗口只隐藏，业务操作仍继续；重新打开查看同一历史，不会再启动一遍。
5. Applied 表示业务与已接受配置定位共同提交；点击“保存已接受配置”才请求写文件。保存冲突独立报告，在现有配置窗口明确采用外部基线后重试，不因冲突回滚业务。Native 物理结果和 Qt 驻留也分别显示。

本段十项 `p5.upgrade_view_` 为真实控件有限自动检查，三配置各相关 236/236；不是 100 次压力或原生桌面人工验收。c3 的缺符号/消费者故障及高级重试见下段，生产卸载名单仍空。

M5-c3 在同一模块窗口的“资源与诊断”中增加高级回收：选中明确 GenerationId，查看真实策略/PIN/引用，再点击“尝试物理回收”。它只重试原排空/卸载，不替持有者释放资源；Current、Qt 和在途/保留的升级不会被强行回收。普通外借 Runtime 释放后可再试，永久 PIN 不可撤销；最后尝试与第一次升级报告分别显示，Applied 不因回收拒绝改变。

构建测试选项还提供 `bin/native-faults` 四份 resident 故障样例，仅用于独立故障资格检查，不加入生产或正常四份 explicit 批准。真实控件九项 `p5.upgrade_fault_` 覆盖两类缺符号、Notebook 恢复/超限/停止失败、S09 消费者失败及恢复失败、外借工厂后的物理回收。永久失败须保留原链，不把隐藏窗口或进程结束当作成功业务退出。运行步骤和精确验证见 [c3 收口](../../docs/p5-m5c-development.md#c3-收口结论)。
