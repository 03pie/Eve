# Cordis 平台原生 UI 本机验收

2026-11-25，在 Windows 11 专业版本机运行 Qt 5.15.2、MSVC x64 和仓库内的 ElaWidgetTools。核对对象包括正式 P7 工作台、本机链接同一宿主实现的测试进程，以及注册了可控测试实例的原生工作台。插件只提供实例或事件。各项操作与预期见[验收工作单](../tests/acceptance/ui-runtime-feedback.md)。

## 设置导航

按 **Ctrl+,** 或点击左侧齿轮打开设置。宽窗左侧依次列出外观、工作区布局、通知、开发者、快捷键。设置页实际可用宽度低于 760 个逻辑像素时，同一目录改到搜索框下方。点击分类直接定位正文，滚动正文同步高亮；搜索只保留有匹配项的分类。宽窗使用上下键、窄窗使用左右键选分类，Enter 或空格进入该组，Ctrl+F 搜索设置。

在独立的本机工作台可执行文件中，实际点击「工作区布局」后核对标题和设置项，再点「外观」返回。点击「窗口材质」左边的缩略图，正式 EXE 打开 Windows 系统图片选择器，取消后仍停在原分类，背景保留。另在原生验收程序中完成宽窄切换、五分类点击及键盘定位、搜索「面板高度」后跳到对应编辑框。[正式程序实图](assets/ui-runtime-feedback/native/settings-navigation-window.jpg) · [宽窗](assets/ui-runtime-feedback/native/settings-navigation-wide.png) · [窄窗](assets/ui-runtime-feedback/native/settings-navigation-narrow.png)

## 各用例覆盖

| 用例 | 已核对的原生行为与依据 |
| --- | --- |
| U1、U2 | 真实来源之间切换、跨实例发布时保留选区。独立归档运行支持历史选择、分页、已知事件定位和筛选导出。实例选项来自整次运行，支持在旧实例与其他实例之间直接切换。七页共 1,311 条的导出与预期集合逐条相符，连续翻页时保留的正文上限为 1,000 条。 |
| U3、U4 | 实际通知归并与免打扰、重开后恢复已读、关联历史事件定位。从独立实例管理窗口查看通知，Esc 返回原按钮和选中行，进入相关日志后同样可返回。配置冲突可保留草稿、显式处理并保存；重复实例标识显示字段错误。[通知中心](assets/ui-runtime-feedback/native/notification-center.png) · [跨窗口通知](assets/ui-runtime-feedback/native/management-notification-center.png) · [冲突界面](assets/ui-runtime-feedback/native/configuration-conflict.png) |
| U5 | 本机 qwindows 供给 10 万条合成记录，两次实测输入至同步重绘 p95 分别为 9 ms 和 8 ms。末次 10,138 ms 完成，604 次显示变更，峰值工作集 84,103,168 字节。性能审核使用独立进程，数据范围为 UI 输出路径。 |
| U6、U8 | 实例卡使用实际状态和单独的目标开关；零实例和文件菜单共享添加动作；连续提交只创建一次。新增实例成功后其管理按钮得到焦点。四个 Ela 菜单可原位展开并按 Esc 取消。正式工作台用 Alt+H、方向键和 Enter 从帮助进入关于页。用 Alt+F 展开文件后点击菜单外空白处，仍停在关于页。本机微软拼音在添加表单、命令面板中取消预编辑、确认候选时均未提前提交或执行。[正式工作台文件菜单](assets/ui-runtime-feedback/native/native-file-menu.jpg) |
| U7 | 两个独立 Windows 进程加载真实阅读器 1.1.0。进程 A（5984）通过工作台添加第二个实例，并在工作区配置中制造和恢复保存冲突。进程 B（43404）从历史分别定位平台及阅读器实例 #2、#3 的事件。已将事件 2、3、6、8 的显示数据与原始 JSONL 核对，并确认保存冲突从当前问题列表移除。另在多插件验收宿主中，两个真实 Qt 插件通过命令交错向同名通道写入四条记录，分别选源并验证归档。[历史输出](assets/ui-runtime-feedback/native/feedback-history.png) · [双插件实图](assets/ui-runtime-feedback/native/feedback-interleaved.png) · [双插件事件原件](assets/ui-runtime-feedback/native/feedback-interleaved.json) |
| U9、U10 | 实例与工作区配置在窄窗仍可编辑、切换和处理冲突；输出与问题页签共用面板高度。原生覆盖 759、760、1079、1080 四个内容宽度，浅深主题、100% 与 150% 两种缩放。[窄窗配置](assets/ui-runtime-feedback/native/configuration-narrow-dark.png) |
| U11 | 导航、搜索、两个主题、六种材质、五种转场、PNG、JPEG、WebP 背景选择及恢复已在本机实测。P7 在两个独立进程间恢复了主题、导航宽度、免打扰、用户卡片显隐、窗口材质、切页效果、背景图和输出来源，底部面板启动时收起。对高对比度和关闭透明效果分别传入受限状态后，平台采用标准外观并隐藏壁纸，DWM 背景属性从 2 变为 0，解除限制后恢复到 2，原壁纸和偏好保留。透明度另用进程私有注册表和同一窗口的原生消息验证了正式读取与刷新路径。[设置实测](../tests/acceptance/ui-runtime-feedback.md#u11u12-设置导航与外观实测2026-11-25) · [策略六阶段记录](assets/ui-runtime-feedback/native/system-surface-policy.json) · [透明度原生消息记录](assets/ui-runtime-feedback/native/system-transparency-private-hive.json) |
| U12 | 从基线提交 `f263dfe` 独立构建同一截图程序，分别用 qwindows 在 100%、150% 捕获改版前后的浅深主题。开关已验证可访问名称、可勾选角色、当前值、禁用和键盘动作。Windows 讲述人的语音回顾实际读出搜索框、已选中开关、背景图片按钮与通知未读数，并读出当前输出来源、真实配置冲突和恢复动作。同一原生窗口运行中关闭系统动画后，转场从 Popup 切到 None，消息条收起。恢复后 Popup 和通知提示均恢复，原选值保留。[三阶段动画记录](assets/ui-runtime-feedback/native/system-animation.json) · [设置控件转录](assets/ui-runtime-feedback/native/narrator-platform-controls.png) · [输出与冲突转录](assets/ui-runtime-feedback/native/narrator-output-conflict.png) · [首页改版前](assets/ui-runtime-feedback/native/home-before.png) · [首页改版后](assets/ui-runtime-feedback/native/home-after.png) |

## 可复验的本机记录

- Release 的工作台、面板、配置管理及界面优化回归为 78/78；加上 UI 服务、页面、发布门禁、配置窗口等回归后合计 167/167。P7 模块加载、持久化、升级、关闭、模板、反馈与双插件联调回归 9/9。单项目的 CTest 名称与运行方式见[原生测试工作单](../tests/acceptance/ui-runtime-feedback.md)。
- 扩展执行 Release 的全部 259 个 `ui` 标签用例。使用本次编译的插件产物时通过 255 项，4 项 P5 产品测试拒绝了 SHA-256 与冻结名单不符的 Counter 和 Reporter DLL。同样的拒绝在基线 `f263dfe` 重现。二进制对比显示每个新 DLL 仅有 6 个时间戳字节差异。保持名单和宿主构建不变，另用受审原 DLL 和清单复验 P5 产品测试，6 项全部通过。完整结果存于 `build/design-tools/ui-release-all-tests.log`、`build/design-tools/ui-base-platform-tests.log` 和 `build/ui-special-evidence/p5-qualified-c6xdv3of/receipt.json`。
- 两种主题与两种缩放的四个组合的完整截图位于 `build/ui-special-evidence/visual/{before,after}`。设置专项另保留每种材质的系统调用结果与原图。
- Release 再次通过私有注册表透明度测试。独立进程采样了 23 次实际桌面透明度，均为开启。各阶段数值、窗口截图及与本机交付匹配的 UI 库散列，见 `build/ui-special-evidence/private-hive-release-20261125-175457`。
- U7 正式宿主联调的进程回执、原始日志、偏好文件与截图位于 `build/ui-special-evidence/feedback-tQfSdj`。两次运行 ID 分别是 `6b2b7eee-bdb0-4a68-b2a4-ea284726ac4e`、`50acf297-207d-4923-890c-5809238ee33b`。双插件原始日志存于 `build/ui-special-evidence/interleaved-BrubgG`。
- 日志归档的全量运维、保留和压力指标归入[日志系统专项](runtime-logging-plan.md)。本次 UI 核对涵盖工作台真实使用到的发布、回看、定位和反馈。

### 本机可运行版本

本次 Release 工作台位于 `build/cordis-ui-workbench/cordis_plugin_workbench.exe`。该目录包含平台和 Qt 运行库、Windows 平台及 WebP 图像插件、真实阅读器 1.1.0。2026-11-26 更新后的工作台和 UI 库 SHA-256 分别为 `8072227774022CF23112F7CBDDD0E6C0A1766CA86F6BB595D0E159CA4A2AE5AD`、`A32FC29AB1026F6612CD6B42768A3CCEC24DC667ED4E6C1F35F4EA59204B1494`。完整清单存于 `build/ui-special-evidence/local-build-sha256.json`。独立工作区已验证真实阅读器加载、正常关窗退出 0、归档完结和锁释放。

## Windows 系统外观现场验收

2026-11-26 从 Windows 设置分别关闭透明效果、启用「夜空」对比度主题。每个用例的同一 HWND 都完成正常、受限、恢复三阶段。两次 DWM 背景属性均为 2、0、2，壁纸可见、隐藏、恢复，图片像素摘要和选中材质始终一致。设置页显示与限制对应的原因。已恢复测试前的透明效果、对比度和系统主题，原主题文件 SHA-256 一致。[Windows 实测原始读数](assets/ui-runtime-feedback/native/system-surfaces-windows.json)

## 窗口主题与状态栏复验

2026-11-26 根据通知中心白底和底栏排版的截图，复查主窗口、设置、输出、问题、命令搜索、通知空态与列表，以及插件管理、实例配置、工作区配置、添加实例和模块管理的托管窗口。列表、滚动区和多行编辑器沿用控件类型的 Ela 样式。Qt 原生部件使用同一组 Ela 颜色角色，下拉弹层保留 Ela 自绘的边缘和阴影。配置分隔线保留原有可拖动区域，线条随主题更新。[深色通知空态](assets/ui-runtime-feedback/native/notification-dark-empty.png) · [深色通知列表](assets/ui-runtime-feedback/native/notification-dark-populated.png) · [深色问题面板](assets/ui-runtime-feedback/native/tools-dark-problems-after.png) · [设置材质下拉](assets/ui-runtime-feedback/native/settings-material-menu-dark.png)

状态栏移除了强调色竖线，左侧消息和右侧当前页面各保留一行。长文字按可用宽度省略，保留完整提示。铃铛及未读数有独立空间和键盘焦点。用 700、1180 逻辑像素两种宽度，浅色、深色，以及 100%、150% 缩放检验边界、间距、垂直对齐和临时状态消息。原件保存在 `build/ui-special-evidence/status-bar`。[深色窄窗与长文字](assets/ui-runtime-feedback/native/status-bar-dark-narrow.png)

最终 Release 构建通过 258 项常规 UI 回归和 9 项 P7 联调。P5 产品用例另用受审模块复跑，6 项全部通过，其中 2 项也在常规回归中使用本轮模块验证。压力复测曾捕获 Windows 拒绝原子替换配置文件。配置页保留失败提示及草稿，可再次保存。本次回归的配置冲突及保存用例通过。[窗口矩阵与保存诊断](../tests/acceptance/ui-runtime-feedback.md#通知中心与平台工具窗口主题复验--2026-11-26)
