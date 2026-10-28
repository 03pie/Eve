# UI-S1–S4 结构增量验证

更新：2026-10-28。基于未提交工作区，基线提交 1afd44b。阶段和验收范围见 [阶段计划](ui-special-stages.md)、[VS Code 对照](vscode-ui-structure.md)及 [UI-A21–24](../tests/acceptance/ui-optimization.md#ui-s2--ui-s3-增量)。本记录区分 UI-S0 历史确认与后续新增交互。

本文件记录追加 Home 管理窗口需求之前已验收的 UI 版本及其源快照。后续用户追加的窗口/配置实例添加另见 [Home 管理记录](home-management-development.md)，不以本文历史检查替代新代码验证。

## 交付与边界

UI-S1 已分析标题/菜单/命令中心、活动栏/侧栏、中央页签、面板/状态/通知、检索/命令/设置以及公共视觉和键盘行为；分屏、独立窗口、终端、调试及账号等独立能力明确归于后续业务设计。

UI-S2 增加 Ela 页签右键菜单与输出页局部工具。关闭操作绑定菜单打开时的稳定键和页签代次，保持页面内容、插件身份和生命周期；关闭后重开的同名页不受旧菜单影响。输出复制/清空复用菜单 QAction，命令页隐藏输出工具。底部仍默认收起，分隔条的背景/悬停跟随 Ela。

UI-S3 增加 Ela 设置搜索、无结果说明与 Ctrl+F/Escape，并提供状态栏铃铛和“转到→通知历史”。搜索不改变偏好值，不写文件；通知历史来自独立服务的纯值快照，未连接 Kernel 也能查看，不自动导航或展开底部。Home、管理、配置、关于和笔记沿用 UI-S0 组件、字体、状态及布局，纳入本轮回归。

Core、公开 SDK 依赖、插件配置事务、页面 epoch 与退出屏障保持既有边界；sources/、研究材料和第三方源码未修改。新增可见控件均为 Ela，正文继续使用 Microsoft YaHei，图标使用 ElaAwesome。

## 自动化

新增七个真实场景：tab_context、tab_snapshot、tab_reentry、panel_tools、settings_search、settings_reentry、notifications；另为 tab_context/panel_tools/settings_search 增加 150% 检查。既有 showcase 扩展为设置过滤/空结果、输出工具和页签菜单的明暗截图，并检查工具区域几何。

首次增量完整回归为 Debug/Release 各 454/454、ASan UI 114/114；随后发现原生焦点准备和分隔条主题问题，最终修复后的结果另记下文，不用初次通过替代最新源验证。

最终 Release 完整回归 454/454、ASan UI 114/114 已通过。Debug 完整轮次 453 项通过，新增多截图的 showcase_hidpi 在多套回归同时运行时超过 20 秒；该项独立串行连续五次通过，单次 15.33–18.66 秒。随后只将两个多截图场景的 CTest 预算设为 60 秒，其余检查仍保持既定阈值；该项测试内容未修改，不用延长阈值替代功能复核。对应 build/ui-structure-final-verified-release.log / debug.log / asan.log 及 build/ui-structure-debug-showcase-repeat.log；不把单次超时写成整轮 454/454。源快照 build/ui-structure-source-sha256.json 包含 20 项源文件/构建文件，未覆盖 UI-S0 历史快照。

## 原生与视觉

Windows 平台按 QT_SCALE_FACTOR=1 / 1.5 执行新增七场景与 showcase，共 16 次；设置搜索另外各重复四次，共八次，用于复核窗口激活和焦点；两个入口各以 smoke / smoke-dark 在两档进程缩放下运行，共八次。上述脚本使用真实 Qt/Ela 窗口和 Qt 输入，不写成用户亲自点击。

最终分隔条修复前的 32 次原生检查均通过，历史日志在 build/ui-structure-native；该结果不代填修复后的完整矩阵。最新源码的三个页签场景在进程缩放 1 下通过，随后 panel_tools 的系统剪贴板断言失败，脚本停止。独立系统探针确认 OpenClipboard 返回错误 5（拒绝访问），见 build/ui-structure-desktop-probe.json；其他后台检查结束后重试仍受限，不能再简单归因为测试并发。当时将完整原生矩阵保留待验，没有将环境故障记成通过；本次恢复后的结果见下文。截图检查包含四个完整菜单动作、设置过滤和空结果、输出工具定位、深色分隔条以及 150% 中文文字。菜单截图等待 ElaMenu 的 400ms 展开动画结束。

2026-10-28 04:18 再次复核：两个手动验收进程仍在运行且 Responding=true；进程桌面与输入桌面均为 Default，但 OpenClipboard 仍返回错误 5。证据为 build/ui-structure-desktop-recheck.json，不能仅据 LogonUI 进程存在就判断为锁屏。未收到新增体验回复，未重启或关闭正在等待用户体验的两个入口。

2026-10-28 04:21 已单独执行所有不依赖剪贴板的最新原生检查：两档进程缩放的七类场景共 14 次、设置焦点额外复核八次、两个入口明暗退出八次，合计 30/30 通过。结果在 build/ui-structure-closeout-native/nonclipboard-summary.json 及同目录 final-/repeat- 日志；16 张新增结构截图已归档并复核代表图。没有将 panel_tools/1、panel_tools/1.5 两项受限检查算作通过，未关闭或重启两个用户验收窗口。

本次用户反馈后，OpenClipboard 已恢复成功（错误 0），见 build/ui-structure-accepted-desktop.json；此前两个验收进程均已退出。本次只补跑受限的 panel_tools/1 与 panel_tools/1.5，两项均退出 0，日志为同目录 accepted-panel_tools-1.log / accepted-panel_tools-1.5.log。结合此前最新源码的 30 项通过，原生矩阵最终为 32/32；没有重复运行无关检查，也没有覆盖旧失败日志。

进程缩放与系统显示设置分别记录；UI-S0 用户已明确确认系统 150%，本轮不把进程设置冒充系统切换。只有单屏环境，跨屏未覆盖。

## 审查及失败记录

审查修复旧菜单可能误关重新打开的同名页签：引入每次打开的代次，新增锚点与被操作页重开回归。新菜单关闭旧弹层，窗口关闭也撤销菜单代次；动作只持宿主值和弱引用。设置可见性回调改变查询或重入关闭时，过滤延后收敛，窗口访问屏障保留当前写入栈。通知文字按 plain text 显示。

原生 settings_search 首次在 Escape 断言失败，独立重跑通过；测试随后补齐窗口暴露、激活、事件处理及实际焦点断言，保留 build/ui-structure-native/settings_search-1.log，不声称已复现为产品快捷键缺陷。

最终回归曾跨越约三小时中断，Release 的 guards/expired_kernel 与 Debug 的 invalid/showcase_hidpi 记录为超时，日志保存在 build/ui-structure-interrupted-release.log / debug.log。这次中断不通过放宽阈值或删减检查处理，恢复后完整重跑；中断期间的结果不作为通过证据。后续多截图场景的独立预算调整依据另见自动化段落。原生输出复制另在后台多套回归同时运行时出现一次剪贴板断言失败，保留 build/ui-structure-native-clipboard-failure.log，其他测试结束后独立复核仍失败，随后系统探针确认拒绝访问；当时保留为环境待验项，现已恢复并补验通过。

截图采到 ElaMenu 动画中间帧，改为等动画结束；没有修改第三方菜单动画。深色底部分隔条原本沿用 Windows 浅色外观；第一版把样式写在 QSplitter 容器，management_theme 在两套构建中捕获其对后代文字调色板的影响。最终改为只设置 QSplitterHandle，自身背景和悬停色跟随 Ela，保持页面调色板和原生拖动。失败日志 build/ui-structure-closeout-*.log 与修复定向日志保留，最终结果使用独立日志。

## 新增体验与阶段结论

UI-S0 的“已体验，通过”“系统 150% 也已验证通过”继续保留，适用于基线。2026-10-28，用户在 UI-S4 独立验收问题下明确回复：**“已体验，全部通过”。** 该问题覆盖两个新版 Release 入口的页签右键关闭其他/右侧/全部、输出复制/清空、设置搜索和 Ctrl+F/Escape、状态栏铃铛通知历史，以及正常关闭两个窗口。配置入口使用独立 build/ui-structure-closeout-native/manual.yaml。

据此登记 UI-A21–24 新增体验通过；没有沿用基线回复代填。进程查询同时确认两个验收进程已退出，但没有虚构手动进程退出码或逐次点击记录。系统剪贴板已恢复且两档原生复制检查补验通过，UI-S2/UI-S3 阶段出口满足，UI-S4 阻塞解除，UI-S0–S4 及 UI-00–07 在当前第一方 Windows 支持范围完成。

本轮未提交。原 Release 目录仍为 build/ela-msvc-release，没有更换编译目录。ASan 仅覆盖项目插桩单元，不认证预编译 Qt 或系统库，也不证明 P4/P5 的动态模块卸载与替换能力。

## 当前归档

build/ui-structure-audit.log 已核对 33 项改动文件的 UTF-8/LF 与只读/第三方边界、20 项源快照、全部本地文档链接/锚点、七个 Core 单元的依赖、普通 Ui 与公开 SDK 边界、12 个修改编译单元的 ASan 插桩，以及十组受控文字/动作配色（最低对比 4.93）。三套 CTest 配置中的两个多截图场景均为 60 秒预算；测试正文和其他场景预算未改变。

当前差异另归档为 build/ui-structure-review.zip，包含基线提交号与源快照，用于审阅未提交工作区，不是发布包。最终审计核对最新 30+2 项原生通过、独立用户新增确认、阶段/工作包状态及验收映射；历史阻塞证据另保留为 build/ui-structure-blocked-audit.log / blocked-review.zip，没有用历史通过代替最新结果。

## 完成条件审计

| 工作包 / 验收 | 当前交付与权威证据 | 结论 |
|---|---|---|
| UI-00；UI-A01；UI-S1 | 组件/字体盘点、VS Code 各区结构对照与四项行为约定已交付；当前源审计核对 Ela 控件、应用字体及架构边界 | 完成 |
| UI-01；UI-A02–05、21–22；UI-S2 | pages/workbench 与原有 history/tabs/empty/search/layout 回归；新增 tab_context/snapshot/reentry、panel_tools 的自动化及原生两档通过；用户新增确认 | 完成 |
| UI-02；UI-A06–08 | home/status 辅助、真实启停与状态；cards/power/power_busy/states/showcase、身份保持/空状态/长名称检查；基线用户确认与最终回归 | 完成 |
| UI-03；UI-A09–10 | management/configuration_management 的类型化图标、详情、权限与恢复；最终 guards/deselect/retry/stop/cleanup/theme 和配置管理场景通过 | 完成 |
| UI-04；UI-A11–13 | configuration 的应用/运行/磁盘独立反馈、Ela 字体、路径与滚动；feedback、acceptance_partial、conflict/stale/editor_reentry 与两个入口 smoke/用户确认 | 完成 |
| UI-05；UI-A14–16、23；UI-S3 | preferences 的原子写入/校验/失败恢复与业务隔离；roundtrip/invalid/failure/locked/isolation 及 settings_search/reentry、关于页截图和新增用户确认 | 完成 |
| UI-06；UI-A17–18 | keyboard/hidpi、settings_search 的实际焦点/Ctrl+F/Escape、明暗与宽窄几何、最新原生截图；用户基线系统 150% 和新增体验分别确认；跨屏明确未覆盖 | 完成（当前支持矩阵） |
| UI-07；UI-A19–20、24；UI-S4 | 最终 Release 454/454、ASan UI 114/114；Debug 453 项及独立截图五次通过；原生 32/32；用户确认两个入口正常关闭、通知历史及四项增量；快照/审计/差异包归档 | 完成 |

以上条目覆盖本轮交付和验收条件，不将未实现的分屏/独立窗口、终端/调试、P4/P5、外部任意插件样式或缺少硬件的跨屏检测记入完成范围。产品与测试源码保持已验证的 20 项快照，最后只更新验收/进度文档；本次没有新增产品修改，因此不重跑已经通过且不受影响的完整回归。
