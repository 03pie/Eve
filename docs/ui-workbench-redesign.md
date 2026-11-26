# Cordis 平台工作台布局与交互专项

**本轮优化平台 UI 的布局和操作交互，视觉基线为项目现有 Ela 实现。** 使用者从概览识别实例，经托管窗口管理和配置，再从输出面板追查记录。范围包括壳层导航、概览、实例管理与配置、开发者工具、通知、设置和关于。插件在这里是受管理对象、页面入口或日志来源。插件业务页面另行安排。原生基线为 `f263dfe`，设计日期为 2026-11-25。[可操作平台原型](design/workbench-ui-prototype.html) · [阶段总表](ui-special-stages.md) · [原生验收单](../tests/acceptance/ui-runtime-feedback.md)

## 调研对设计的影响

五个设计技能已逐一核对作者原文，并用于评审本机界面和可操作原型。优先处理概览截断身份、配置结果难以判读、长输出来源和工具区焦点返回。每个决定都有对应的 Ela 控件、实现位置及验收用例。[五个技能的适用性、设计决定与实施顺序](ui-platform-design-review.md)

平台使用者需要识别当前实例、确认配置是否生效；维护者需要从同一对象找到故障及其运行记录。调研围绕桌面工作台怎样安排导航、命令、状态与诊断展开。右栏给出 Cordis 的设计决定。

| 参照 | 在产品或官方指南中看到的做法 | 在 Cordis 中如何取舍 |
|---|---|---|
| VS Code 的工作台与输出 | 活动区组织入口，工具面板在编辑区旁工作，工具栏随当前视图变化。状态栏把工作区信息和当前上下文分放两端。[工作台分区](https://code.visualstudio.com/api/ux-guidelines/overview) · [面板](https://code.visualstudio.com/api/ux-guidelines/panel) · [状态栏](https://code.visualstudio.com/api/ux-guidelines/status-bar) | 左侧点击插件直接打开中央业务页，插件资源树由页面拥有。输出与问题共用下边缘。只有输出页显示通道，状态栏只留摘要和进入详情的入口。 |
| IntelliJ IDEA 的主工具栏与空状态 | 相关命令归组，常用动作靠近其操作对象。空列表提供明确的下一步。[IDE 界面](https://www.jetbrains.com/help/idea/new-ui.html) · [工具栏](https://plugins.jetbrains.com/docs/intellij/toolbar.html) · [空状态](https://plugins.jetbrains.com/docs/intellij/empty-state.html) | 概览空态提供「添加插件实例…」。输出无匹配项提供「重置筛选」。用户主动关完中央页签时保留空画布和外围导航。 |
| Windows 和 JetBrains 的桌面反馈 | Windows 命令栏按优先级收拢动作，持续的故障可用内联条承载恢复动作。JetBrains 按触发位置和是否需要立刻决定选择提示形式。[命令栏](https://learn.microsoft.com/en-us/windows/apps/develop/ui/controls/command-bar) · [InfoBar](https://learn.microsoft.com/en-us/windows/apps/develop/ui/controls/infobar) · [通知选择](https://plugins.jetbrains.com/docs/intellij/notification-types.html) | 在出错字段或编辑区旁显示原因和下一步。后台需关注的事件进铃铛。将丢弃草稿时才由用户作阻断式决定。宿主绘制 Ela 短提示，长期存在的动作放在页内或中心。 |

## 从本机界面到改版

截图来自 Windows 原生 Qt 观察宿主，链接生产 Workbench。设置、实例管理、实例配置、工作区配置四图取自同一次独立进程（PID 45796，Qt 5.15.2，`windows` 平台，正常关窗）。采样时装载的模块贡献了真实实例名、页面名和输出通道。首页截图拍摄时底部面板已展开。[原观察记录](assets/ui-runtime-feedback/observation.json) · [补采窗口、尺寸与可见标签](assets/ui-runtime-feedback/layout-observation.json)

| 屏幕 | 实际看见什么 | 对布局的直接决定 |
|---|---|---|
| 概览 | 1180 × 800 中，横幅至少高 220，容纳品牌文案和三个按钮。下方单卡固定宽 280，插件名按 192 像素截为 `literature.reader · m…`，旁边还有充足空间。[实图](assets/ui-runtime-feedback/home.png) · [首页实现](../src/ui/home.cpp) | 复用同一 ElaImageCard 和原品牌背景，缩短品牌区，将设置与命令收回既有入口。保留 ElaScrollPageArea 独立实例卡片和 ElaFlowLayout 自然折行，优化长名称、身份路径及状态的可辨性。 |
| 实例管理与实例配置 | 同一对象的窗口标题和正文重复技术 ID。管理详情是数行平铺文本，刷新独占一行。配置页同时出现启用、停用和重新协调，以及「已保存 无 · 未保存」。[管理实图](assets/ui-runtime-feedback/management-baseline.png) · [实例配置实图](assets/ui-runtime-feedback/instance-configuration-baseline.png) | 两个入口聚焦同一托管窗口的对应视图。标题写人可辨识的对象，详情按「当前状态」「配置」「技术详情」排列。实例尚无保存版本时写「尚未保存到磁盘」。 |
| 工作区配置 | 1100 × 791 窗口中编辑、运行、磁盘分别占一列，检查、应用、保存与仅冲突时使用的动作散落两端。下半部默认留给一次执行的摘要。[实图](assets/ui-runtime-feedback/configuration-baseline.png) · [现有会话操作](../src/ui/configuration.cpp) | 文件编辑为主体，所选条目的期望和实际状态可查看。差异在检查后展开，冲突操作贴紧冲突原因。三个状态各有独立来源和文案。 |
| 设置 | 标签在内容最左，开关在最右，中间留出近整窗空白。搜索命中整组，说明文字与持久化状态混在同一组。[实图](assets/ui-runtime-feedback/settings-baseline.png) · [分组检索](../src/ui/workbench.cpp) | 限制表单行宽，让控件和标签相邻，按具体设置项命中。保存位置或保存失败在设置页显示，不占日常工作台。 |
| 输出、问题与铃铛 | 来源框截断实例代次，记录没有时间。铃铛打开的窗口中，通知与插件管理计数、刷新和其他页签争用空间。[概览与原生输出](assets/ui-runtime-feedback/platform-output-baseline.png) · [当前铃铛窗口](assets/ui-runtime-feedback/notifications.png) | 输出先选插件来源，详细身份可筛选。通知中心沿用 Ela 的列表、按钮与短提示，入口放在右下铃铛；选「查看日志」后才展开底部并定位。 |

## Ela 视觉基线与布局评审

平台继续复用项目维护的 ElaWindow、ElaMenuBar、ElaMenu、ElaTabBar、ElaImageCard、ElaScrollPageArea、ElaToggleSwitch 和 ElaMessageBar。字号、圆角、绘制、悬停、按下、选中、禁用、焦点及明暗主题均从现有组件和项目封装取得。布局调整通过容器、边距、分组、可见性和动作路由实现。品牌渐变、几何图案、Cordis 标识与彩色实例图标照用。[原生首页与品牌绘制](../src/ui/home.cpp) · [字体初始化](../third_party/ela/ElaWidgetTools/ElaApplication.cpp) · [按钮与焦点](../src/ui/appearance_p.hpp) · [控件与主题规范](ui-design-system.md)

本次使用 Damir Sokolovsky 的 **unslop**（`620163e`，CC BY 4.0）复核首屏空间、字段对齐、提示与触发动作的距离。VS Code、IntelliJ 和 Windows 的参照用于决定面板位置、操作归属及反馈时机。可辨识的对象名贯穿概览、管理窗口和输出，帮助用户在窗口之间继续同一项工作。[技能原文](https://github.com/heydamir/unslop/blob/620163eedb3529d9cd23a3994ed8d287d06f760d/skills/unslop/SKILL.md)

| 审查项 | 本次处理与复核依据 |
|---|---|
| 间距与比例 | 首屏先给实例留空间。页头与卡片区左边缘对齐；各卡内部按相同顺序陈列，横向空间足够时多张并排，余宽不足则下一张自然换行。用相同窗口宽度比较旧图和原型。 |
| 主题与图标 | 沿用 Ela 当前主题角色和项目已有的状态图标。图标与对应名称并排，错误原因靠近恢复操作，键盘焦点与当前选中项均可辨认。 |
| 名称与文字层级 | 以「添加实例」「应用到工作台」「保存到磁盘」「查看此实例日志」说明动作及其作用对象。继续使用现有 ElaText 层级和微软雅黑，将身份、当前状态、技术详情分别排列。 |
| 布局与细节 | 每张首页卡片独立呈现实例身份、实际状态、启用目标和操作。配置页分别呈现编辑、运行与磁盘状态。输出始终能辨认当前来源。忙碌、失效、未读、警告与错误都有文字或可读状态。 |
| 品牌区与状态更新 | 在原品牌图案上重新组织标题和动作。保留 Ela 开关、按钮和菜单的原生反馈。后台新增事件或刷新实例时保持用户当前焦点与滚动位置。 |

原型用合成实例和事件演示平台操作。首页保留品牌区，下面用独立卡片展示实例。卡头是图标、名称和实际状态，卡中是身份路径与开关，卡底是管理、配置。打开「问题」中失败实例的相关日志，可以观察来源、实例筛选和事件定位。外观参照来自本机原生窗口，原型截图记录的是浏览器中的布局。[可操作原型](design/workbench-ui-prototype.html) · [概览布局](assets/ui-runtime-feedback/prototype-home.png) · [定位到输出](assets/ui-runtime-feedback/prototype-output.png) · [Qt/Ela 对照图](assets/ui-runtime-feedback/platform-output-baseline.png)

## 页面如何铺开

尺寸按 Qt **逻辑像素**讨论。常用布局间距为 4、8、12、16、24。页头、卡片及工具区随字体度量和实际内容高度排版，宽度不足时先换行或收拢次要工具。对照同一台 Windows 上相同主题、DPI 及控件状态的原生基线，检查改版后的标签可读性、焦点顺序和点击目标。浏览器原型用于试走布局和操作顺序，最终外观以项目编译出的 Qt/Ela 窗口为准。[项目组件规范](ui-design-system.md) · [Windows 桌面工具布局](https://learn.microsoft.com/en-us/windows/apps/get-started/line-of-business/design-for-lob)

| 区域 | 内容、位置与操作 | 窄窗或特殊状态 |
|---|---|---|
| 标题区 | Cordis 标识旁的 ElaMenuBar 承载文件、视图、转到、帮助，点击标题在其下方展开对应 ElaMenu。后退和前进靠近页面搜索。Ctrl+P 找页面，Ctrl+Shift+P 或顶栏命令入口检索并执行动作。右上保留系统窗口控件。 | 菜单支持相邻标题切换、方向键选项、Enter 执行、Esc 或点击外部收起。单纯展开菜单保留中央页、底部面板、托管窗口和草稿，只有选中命令才执行相应导航。 |
| 活动栏与中央页签 | 概览在上，已发布的插件页面在其下，底部工具区、设置、关于在下。相同图标的不同插件以完整名称和可展开标签区分。中央一份页签代表一个已打开页面。 | 页签横向滚动，活动页可见。关闭当前页回到仍有效的页面。全部关闭保留空画布，用户可以经活动栏或 Ctrl+P 返回。后台贡献变化只更新可用入口。 |
| 概览 | 短页头展示工作台身份、实例数与运行数，右侧单一主动作「添加插件实例…」。每个实例一张独立 Ela 卡片。卡头左侧图标、右侧名称及实际状态，中部左侧身份路径、右侧单个启用开关，底部并列「管理」「配置」。卡面只承载信息。 | 卡片在可用宽度内并排，自然换行直至单列，按阅读顺序保留每张卡的全部操作。同名实例展示可区分的路径末段，完整路径在详情可复制。无实例时把添加入口移入空态，避免同屏重复主按钮。 |
| 添加实例 | 文件菜单和首页进入同一个对话框。依次选择已注册插件、填写唯一实例标识、编辑配置。主按钮为「添加并应用」。无可编辑会话或目录为空时提供具体原因和可用入口。 | 非法配置贴近编辑区，重复标识贴近标识框，失败保留用户输入。成功回到新卡片并说明磁盘状态。「添加实例」沿用当前宿主注册目录；交付模块的装载方式见 P7。 [现有入口约定](adr/0028-home-management-windows.md) · [P7 运行方式](p7-acceptance.md) |
| 实例管理 | 从卡片「管理」打开托管窗口。页头显示对象和实际状态，主体优先显示状态原因、可采取动作及「查看此实例日志」。依赖、服务、贡献、实例号、代次与操作记录在详情。配置拥有的实例走配置路径，手动实例沿用原权限。 | 状态刷新时保留选中对象、滚动和键盘位置。对象退役显示其已失效；从历史事件跳入时保留原身份。关闭窗口返回来源卡片。 |
| 实例配置与工作区配置 | 卡片「配置」聚焦该实例的配置段。文件菜单打开完整配置文件。文件路径、编辑器、运行条目与差异结果各有标题。正文边给出下一步「检查差异」「应用到工作台」或「保存目标到磁盘」，其他合法操作仍可从工具区取得。 | 改窄后在编辑、状态、差异之间切换，共用原草稿。冲突时在冲突摘要下安排「载入磁盘版本」与「采用冲突基线」，各自说明影响。未应用编辑与已应用的待保存目标可同时存在。 |
| 底部输出与问题 | 默认收起。「视图 → 输出」打开特定视图，Ctrl+J 切换底部。页签依次为「输出」「问题」。切页、筛选及后台记录追加只更新当前页内容，底部高度及分隔条位置保持；内容超高时页内滚动。手动拖动或主窗高度受限时再调整分隔条。输出顶部选运行和插件来源，事件行给出时间、级别、消息。实例、代次和事件 ID 可在详情查询。问题页列出当前可处理事项，支持定位实例与关联日志。 | 窄窗优先保留来源、筛选和关闭。「更多」收纳低频动作。已选来源在另一次运行中没有记录时保留其名称并解释空态。后台追加保留当前平台页面、选区和滚动位置。[通道、过滤与返回协议](ui-runtime-feedback-plan.md) |
| 通知与状态栏 | 状态栏左侧显示工作区状态或持续故障，右侧把当前页面、铃铛和未读数作为同一组。文本在一行内垂直居中，长标题可省略并保留完整提示。通知中心锚定右下，列表和空态使用与当前 Ela 主题一致的底色。ElaMessageBar 用于短提醒。 | 普通配置提交在操作处更新状态。后台故障可进通知中心，点击「查看日志」后展开底部并定位。Esc 返回原控件，配置编辑仍保留草稿与选区。 |
| 设置与关于 | 设置为中央页。顶部搜索，页内目录可直达外观、工作区布局、通知、开发者、快捷键。表单区建议最大宽 760，标签和控件按行对齐。「关于」展示产品标识、产品版本和可核对的运行环境。 | 窄窗把目录移到搜索下方，设置值移到标签下方。搜索命中可聚焦控件，无结果可清除搜索。恢复默认只作用于提示的偏好范围。正式宿主接通偏好文件后，使用新进程复验。磁盘写入失败时保留上次有效偏好。 |

四个顶栏标题复用现有 ElaAppBar 左侧的 ElaMenuBar；每个标题由 `addMenu` 创建归属于菜单栏的 ElaMenu，既有 QAction 决定启用状态与命令目的地。菜单打开期间临时接管键盘，收起后继续原任务；「帮助 → 关于」进入中央页，「文件 → 工作区配置」才进入宿主管理的窗口。HTML 原型演示同一位置的下拉，Windows 成品使用项目内的菜单控件。[菜单和动作装配](../src/ui/workbench.cpp) · [ElaMenuBar 创建菜单](../third_party/ela/ElaWidgetTools/ElaMenuBar.cpp)

### 设置页复用 Ela 示例

以项目导入的 ElaWidgetTools 2.0.0 同一提交的 `T_Setting` 为参照。示例用 18 像素分区标题、15 像素设置名和独立的 `ElaScrollPageArea` 设置行，左侧名称、右侧控件。Cordis 继续使用中央滚动页，顶部放设置搜索，正文限制行宽。原生设置行默认高 75 逻辑像素、圆角 6，窄窗或大字时按内容增高、控件移到下一行。颜色和状态由 Ela 主题提供。[Ela 设置示例](https://github.com/Liniyous/ElaWidgetTools/blob/aae58f5d0d98e05eabe10c263aa7da5f2491d702/ElaWidgetToolsExample/ExamplePage/T_Setting.cpp) · [本项目设置行](../third_party/ela/ElaWidgetTools/ElaScrollPageArea.cpp) · [设置页原型](assets/ui-runtime-feedback/prototype-settings.png)

| 设置组 | 用户能做什么 | 原生实施复用点 |
|---|---|---|
| 外观 · 工作台主题 | 浅色、深色两档。顶栏与设置页双向更新；切换主题后沿用已选窗口材质和转场。 | 沿用 `ElaTheme` 的变更信号，控件颜色继续取自对应主题。 |
| 外观 · 窗口材质 | 保留 Ela 示例的六种窗口效果：标准（Normal）、Ela 云母（ElaMica）、Windows 云母（Mica）、Mica Alt、亚克力毛玻璃（Acrylic）和 DWM 模糊（DWMBlur）。默认标准。搜索「窗口效果」或「毛玻璃」即到此项；在当前窗口查看效果。 | 正式工作台沿用 `ElaApplication::setWindowDisplayMode`，材质作用于已接入 Ela 的窗口。浏览器在同一处提供纹理及壳层示意。系统材质能力与实际呈现按下文核对。[Ela 枚举](../third_party/ela/ElaWidgetTools/ElaWidgetToolsDef.h) · [原生材质入口](../third_party/ela/ElaWidgetTools/ElaApplication.cpp) |
| 外观 · 背景图片 | 「窗口材质」行里的缩略图是「选择背景图片」按钮，按下后直接打开系统文件选择器，可选 PNG、JPEG、WebP。有效图片立即成为整个平台壳层的背景。取消后沿用原图，读取失败时在设置项旁提示并保留原图。「恢复默认背景」只清除自选图片。 | 工作台持有一份已解码图片，六种材质共用。切换页面、主题和材质时沿用当前图片，更新窗口时按客户区居中、等比裁切。原型的选择限当前会话，正式宿主按自己的界面偏好保存策略处理。 |
| 外观 · 页面切换效果 | 保留无动画（None）、上移（Popup）、缩放（Scale）、翻转（Flip）、模糊（Blur）。可在原位预览，或在概览、设置与关于之间切换体验。默认沿用 Cordis 的无动画。 | `ElaWindow::setStackSwitchMode` 设置每个工作台的普通中央页动效。源页或目标页受插件呈现条件控制时，沿用原生的同步导航。命令、菜单、底部输出／问题等高频操作保持即时。[转场实现](../third_party/ela/ElaWidgetTools/Development/ElaCentralStackedWidget.cpp) · [工作台初始化](../src/ui/pages.cpp) |
| 工作区布局 | 在紧凑图标和展开名称间切换；设定展开导航宽度（180–360，默认 240）和底部面板高度（120–480，默认 160）；切换面板显隐。手动拖动分隔条后，设置值随之更新。 | 复用工作台已有的导航控件、`ElaSpinBox`、键盘开关和 `WorkbenchPreferences`。高度和宽度以 Qt 逻辑像素计，面板开闭属于本次会话。[工作台绑定](../src/ui/workbench.cpp) · [布局与默认值](../src/ui/preferences.cpp) |
| 外观中的用户卡片 | 「显示用户卡片」默认开启。紧凑导航显示用户头像，展开时显示「未登录」。关闭这一项只收起导航中的用户卡片。顶栏和概览继续展示 Cordis 品牌。 | 现版原生宿主在 Ela 用户卡中填入 Cordis 图标和文案。新设计保留卡片的用户身份用途，显隐复用 `ElaWindow::setUserInfoCardVisible`；账号专项后续接入 `userInfoCardClicked` 和登录状态。本期静态卡片不发起登录。[当前填充](../src/ui/pages.cpp) · [窗口接口与点击信号](../third_party/ela/ElaWidgetTools/ElaWindow.h) · [紧凑与展开时的显隐](../third_party/ela/ElaWidgetTools/ElaNavigationBar.cpp) |
| 通知与开发者 | 免打扰切换短提醒；「打开输出」查看平台和插件记录。 | 开关复用 Ela 交互，免打扰由通知中心消费；日志存储由 LOG 专项负责。示例「启用日志」替换的是整个进程的 Qt 消息处理器。[Ela 日志实现](../third_party/ela/ElaWidgetTools/ElaLog.cpp) |
| 快捷键与恢复默认 | 恢复默认后为浅色、标准窗口材质、默认背景、无切页动画、紧凑导航、默认宽高和显示用户卡片。当前面板开闭、免打扰和插件运行状态保留。 | 在同一条偏好读写路径为材质与转场增加独立的稳定标识。迁移旧文件时保留有效的旧设置，新增项按默认值初始化，单项未知值回到该项默认值。 |

**设置导航**位于中央设置页内。搜索框和目录常驻，设置正文独立纵向滚动。中央区宽度达到 760 逻辑像素时，五组入口纵向列在正文左侧。更窄时，目录移到搜索框下方横排，可横向滚动。当前组沿用 Ela 的选中色与指示条。方向键沿目录移动焦点，Home、End 定位首末项，Enter 或空格跳至对应组标题。标题对齐正文视口起点，最后一组也可对齐。手动滚动正文时，当前标记随位置更新，键盘焦点留在正在操作的控件。[宽窗原型](assets/ui-runtime-feedback/prototype-settings-navigation-wide.png) · [窄窗原型](assets/ui-runtime-feedback/prototype-settings-navigation-narrow.png)

设置页从同一份分组定义生成目录与正文，按稳定组键关联标题、控件和搜索词。Ctrl+F 聚焦搜索，按行过滤后仅保留有命中的分组和目录项。Enter 定位首个匹配控件，Esc 清空后恢复五组，无结果时显示清空入口。搜索词和当前组由设置页持有，临时重绘按当前可见组恢复位置。点击分组仅改变设置正文位置，平台活动栏保留在「设置」，全局页签和前进／后退历史维持原顺序。偏好值继续由工作台管理，正式 P7 宿主在设置页展示实际保存范围。[设置页和检索索引](../src/ui/workbench.cpp) · [P7 入口](../app/plugin_workbench/workbench.cpp)

原生目录可复用 `ElaListView` 的选中样式和滚动条，沿用同一模型切换纵向、横向排列。设置搜索和目录放在正文视口外。若沿用 `ElaScrollPage::addCentralWidget`，正文直接使用它创建的滚动区，滚动位置与组标题坐标统一取自该视口。[Ela 列表](../third_party/ela/ElaWidgetTools/ElaListView.cpp) · [选中样式](../third_party/ela/ElaWidgetTools/Development/ElaListViewStyle.cpp) · [滚动页装配](../third_party/ela/ElaWidgetTools/ElaScrollPage.cpp)

**Windows 材质的应用与回退。** ElaMica 使用随库的 MicaBase 图片生成明暗纹理；Mica、Mica Alt、Acrylic 和 DWMBlur 走 Windows 合成器。库的 Mica 分支处理 Windows 11 初版和新版，微软公开的系统背景类型接口从内部版本 22621 起支持。当前库的材质设置函数返回 `void`，记录的枚举表示请求值。S7d 需要在原生适配层确认系统能力与调用结果，依现有透明效果和高对比度设置呈现。当环境只能显示实色时，设置行就近说明原因、保留所选偏好；恢复可用后再应用。全局主题与材质应在已注册的窗口之间同步。从任一材质回切标准时应完整撤去旧材质。[Ela Windows 分支](../third_party/ela/ElaWidgetTools/Development/ElaWinShadowHelper.cpp) · [微软系统背景类型](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwm_systembackdrop_type)

**原生工作台如何绘制自选图片。** `ElaWindow::paintEvent` 在 `Normal` 窗口材质下支持 `Pixmap`。标准材质优先把同一张有效图片交给浅、深主题的 `setWindowPixmap`，再调用 `setWindowPaintMode(ElaWindowType::Pixmap)`。其他材质需要工作台在材质之上、内容之下自绘一层带透明度的背景图片，同步所有平台表面。Windows 合成器负责系统材质，自选位图由应用绘制。切回标准时仅启用 Ela 的图片绘制路径，清除自选图片时恢复当前材质的默认底图。[Ela 窗口绘制](../third_party/ela/ElaWidgetTools/ElaWindow.cpp) · [Ela 示例的明暗图片配置](https://github.com/Liniyous/ElaWidgetTools/blob/aae58f5d0d98e05eabe10c263aa7da5f2491d702/ElaWidgetToolsExample/mainwindow.cpp#L107-L110)

**各区域怎样跟随材质。** 标题栏、导航、页签、中央画布及底部面板共用背景。概览卡片和设置行沿用 Ela 的半透明表面，滚动视口透出所在页面。菜单、菜单项、下拉列表、搜索建议、通知及托管窗口使用与当前主题和材质协调的独立表面，文字、输入框、按钮和焦点需要足够的衬底。工作区配置根控件目前会用 `QPalette::Window` 的 `WindowBase` 自动填满客户区，原生接入时须消除这层对背景的遮盖，并保留编辑区的可读底色。启用高对比度或减少透明效果时，使用实色表面和清晰的控件边界，关闭该选项后恢复当前背景。[Ela 中央画布](../third_party/ela/ElaWidgetTools/Development/ElaCentralStackedWidget.cpp) · [卡片表面](../third_party/ela/ElaWidgetTools/ElaScrollPageArea.cpp) · [工作区配置背景](../src/ui/configuration.cpp)

Cordis 的状态栏沿用 ElaStatusBar，关闭其 Qt 尺寸握柄以移除项目间的强调色竖线。窗口边框继续处理原生缩放。状态摘要和当前页在控件中保留完整文本，绘制时按可用宽度省略。右侧布局由单一横向容器负责，提醒数量变化只调整铃铛所在项目的宽度。

**切页与焦点。** 原生 Popup 可延迟提交目标页，而工作台的页签、焦点和前进／后退历史都依赖当前稳定页面键。S7d 要求先提交目标页一次，再呈现可取消的视觉过渡；连续切页、关闭目标或减少动态效果时取消旧过渡、显示最后有效页面。源、目标任一是受控插件页，或最后一个页签关闭进入空画布时，直接完成原有同步切换。系统要求减少动态效果时保留用户选值并直接呈现目标页。浏览器原型以模拟转场说明操作，原生验收检查真实窗口、焦点、历史和屏幕合成结果。[Ela 呈现条件](../third_party/ela/ElaWidgetTools/private/ElaWindowPrivate.cpp) · [工作台页面同步](../src/ui/pages.cpp) · [原生取消与显示回归](../tests/qt/ela_presentation.cpp)

本机浏览器已核对六种材质、五种中央页转场、快速导航、减少动态效果及恢复默认。搜索「毛玻璃」并按 Enter 可定位材质下拉框，搜索「背景」并按 Enter 可定位图片按钮。缩略图经键盘和鼠标验证均可打开文件选择器。分别载入 PNG、JPEG 和 WebP 后，比对截图确认缩略图、中央画布、设置卡、侧栏和底部面板同步换图。取消和损坏文件均保留原图。明暗模式下另已核对菜单、通知、对话框和编辑控件的合成颜色。[浅色设置](assets/ui-runtime-feedback/prototype-settings-acrylic.png) · [深色设置](assets/ui-runtime-feedback/prototype-settings-acrylic-dark.png) · [深色概览与输出](assets/ui-runtime-feedback/prototype-material-workbench-dark.png)

原生既有回归另以 Qt offscreen 执行，菜单、页签、历史、偏好往返、设置搜索、受控页动效及中断清理共七项通过。新设置的 Windows 合成效果由 U11 在正式候选包核实。[原生验收单](../tests/acceptance/ui-runtime-feedback.md)

### 工作台在不同宽度下优先显示什么

以**控件所在区域的可用宽度**为判据。中央概览与底部面板在导航栏右边，单独测中央宽度；托管配置窗口按自己的客户区测量。以下断点是布局初设。原生实施按文字度量、DPI 和用户窗口高度复核。

| 可用宽度（逻辑像素） | 实例与配置 | 输出与通知 |
|---|---|---|
| ≥1080 | 概览中多张独立实例卡片并排，每张卡保持自己的标题、开关和操作。工作区配置允许源文件和运行条目并排，差异结果按需展开。 | 可同时显示运行、来源和级别。事件正文占余宽，详情按需打开。 |
| 760–1079 | 放得下的卡片继续并排，其余按原顺序换行。身份路径与开关始终属于同一卡片。配置优先保留编辑区域，较长的状态和差异按组展开。 | 来源和筛选始终可达，减少事件的次要元数据；较长消息自然换行。 |
| <760 | 按实际可用宽度逐张换行，只有一张放得下时显示单列卡片；设置项的值移到标签下，配置编辑、运行和差异按区切换。 | 历史、来源或筛选可从紧凑工具栏调出，关闭按钮保持可达；通知中心限在窗口内。 |

低高度时允许面板内容内部滚动，保持来源选择、错误原因和返回入口可达。配置对话框可滚动至主动作，收起工具区后回到原字段或实例。长日志与长路径可以在详情中完整查看及复制。

## 实现时的数据与状态约束

- **对象名取自工作台已有事实。** 配置实例以插件标识和配置路径定位。显示名优先采用可用的插件/实例声明，可见页面标题只在归属明确时作为回退。停用或无页面的插件仍显示可区分的名称或完整 ID。展示名称、过滤键、重试目标分别取值。概览计「实例」，同插件配置两个实例时显示两个。[首页快照装配](../src/ui/pages.cpp)
- **宿主拥有导航和窗口。** 保留页面贡献的展示与撤销、托管管理/配置窗口和原关闭屏障。菜单、键盘与卡片共用同一动作和权限判断。历史事件以原身份定位。目标实例已退役时禁用旧动作，用户仍可查阅其日志。页面贡献只在宿主入口与页签边界验收。[页面贡献](adr/0006-ela-page-contributions.md)
- **配置交互继续分三步。** 检查生成差异，应用改变运行目标，保存写入已应用目标。采用冲突基线保留当前目标；载入磁盘文本只更新编辑区。视图隐藏与切换不触发任一步。编辑过的对象在用户处理草稿前不能被另一对象自动替换。[配置窗口与会话](adr/0028-home-management-windows.md)
- **布局偏好与日志归档分别保存。** 宿主保存布局、稳定来源与通知偏好。日志专项提供按运行、来源、事件定位的查询。当前 P7 构造 `shell(scheduler)`，界面偏好仅在会话内有效。S7d 为正式入口指定每用户偏好位置，验证旧版兼容和跨进程恢复。启动时底部继续收起。[正式宿主](../app/plugin_workbench/workbench.cpp) · [偏好读写](../src/ui/preferences.cpp) · [独立日志工作单](runtime-logging-plan.md)

## 开工次序与交付

| 工作包 | 先实现的完整用户路径 | 完成判断 |
|---|---|---|
| UI-S7a　壳层、概览、添加 | 从启动页识别实例，经文件菜单或唯一主按钮添加一项，再用活动栏打开页面并返回。验证 Ela 原生下拉、卡片换行和焦点。 | 0、1、多实例，同名长路径，关闭全部页签和纯键盘操作通过 U8。 |
| UI-S7b　管理与配置 | 从卡片定位对象，检查配置差异，应用后查看实际结果，最后显式保存。制造重复标识或磁盘冲突，保留编辑并完成恢复。 | U4、U9 覆盖错误恢复、草稿、待保存、对象退役和修订冲突。 |
| UI-S7c　平台工具区布局与焦点 | 从实例详情、问题或铃铛定位同一次运行中的事件，收起诊断工具并返回原平台控件。 | U6、U10 覆盖工作台和独立配置窗口的宽窄布局、键盘路径与选区恢复。 |
| UI-S7d　设置与关于 | 从页内目录直达分组或搜索设置项，换主题或改布局，重启同一宿主后核对偏好与版本。 | U11 覆盖目录与正文同步、键盘操作、原有偏好、首次运行、损坏与不可写。 |
| UI-S5、S6　开发者工具与反馈 | 可与页面调整并行。先用合成记录连通面板与实例详情，再完成从警告定位到事件、Esc 回编辑的路径。 | U1–U5。U7 待真实日志接口交付后，以正式入口和真实进程单独联调。 |
| UI-S8　桌面复核 | 同主题、同 DPI、同状态并排比较原生前后图，核对 Ela 外观及键盘、焦点、明暗、100%/150% 缩放、长名称和读屏。 | 按 U1–U12 逐项留证。原型使用模拟数据，成品验收在本机 Qt/Ela 运行包执行。 |

原型走查从「概览 → 管理 → 查看此实例日志」开始，再从「文件 → 工作区配置」体验检查、应用和保存的区别。缩小窗口后，用键盘切换来源并打开铃铛，检查进入工具区和返回原位置的路径。浏览器原型使用模拟数据，实际磁盘事务、持久日志和 Windows 辅助功能以本机 Qt/Ela 运行包验收。[打开原型](design/workbench-ui-prototype.html) · [验收步骤](../tests/acceptance/ui-runtime-feedback.md)
