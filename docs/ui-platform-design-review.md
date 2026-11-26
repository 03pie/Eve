# Cordis 平台 UI 与交互专项 · 设计评审与实施决定

2026-11-25。适用范围是 Windows 上的 Cordis 平台工作台。视觉基线取自本地 Qt 5.15.2、项目内 ElaWidgetTools 和 Cordis 品牌资源。专项交付是概览、托管窗口、输出及通知之间的一套连续操作。各页布局见[平台设计图与说明](ui-workbench-redesign.md)，开发入口见[实施工作单](ui-runtime-feedback-plan.md)，测试入口见[U1–U12 原生验收](../tests/acceptance/ui-runtime-feedback.md)。

## 五个技能实际用于哪些决定

| 作者与技能 | 本次采用的方法 | 在 Cordis 上的设计决定与检查 |
|---|---|---|
| Anthropic · Frontend Design | 先写出使用者来此要完成的事，按既定视觉方向安排层级，再单独审一次首屏。 | 概览首先回答「有哪些实例、哪个需要我处理」。缩短现有 ElaImageCard 品牌区，页头放实例摘要和「添加插件实例…」，移走重复的设置、命令入口。每个实例使用独立卡片，顶部是名称与实际状态，下方是路径、开关和操作。用零实例、同名实例、启动失败三种首屏评审。 [技能原文](https://github.com/anthropics/skills/blob/e4bb5d960d87126ff3b3a8af1a104b7db67560ba/skills/frontend-design/SKILL.md) |
| NextLevelBuilder · UI UX Pro Max 2.15.0 | 查 UX 规则库，用真实失败、长文本和键盘输入检验表单及布局。其技术栈目录列有 WPF、WinUI 3 等，Qt/Ela 以项目实现为准。 | 「添加并应用」遇到重复标识时，在标识字段显示原因，保留已填配置；配置格式错误在编辑器旁说明；改正后清除对应错误。中文输入法确认候选词和执行命令分别处理。长实例名在窄窗也有键盘可达的全文入口。 [技能原文](https://github.com/nextlevelbuilder/ui-ux-pro-max-skill/blob/8db36338810d0942567b97ce439affd1619fcf31/.claude/skills/ui-ux-pro-max/SKILL.md) · [UX 数据库](https://github.com/nextlevelbuilder/ui-ux-pro-max-skill/blob/8db36338810d0942567b97ce439affd1619fcf31/src/ui-ux-pro-max/data/ux-guidelines.csv) |
| Paul Bakaus · Impeccable 4.10.0 | 用 critique 找出会误判的地方，用 clarify 改动作和结果的措辞，用 adapt 审宽窄布局，最后用 polish 对齐同级内容。 | 「添加实例」选择宿主已注册的模块。「检查差异」「应用到工作台」「保存到磁盘」各自对应一次操作和一种可核对的结果。管理详情先放失败原因与恢复动作，技术 ID、服务和贡献收进详情。以错误恢复前后的对象、草稿、实际运行和文件内容复核术语。 [技能原文](https://github.com/pbakaus/impeccable/blob/b49177d32788f2dc585c42831d845dacf1ebed2c/.agents/skills/impeccable/SKILL.md) · [文案评审方法](https://github.com/pbakaus/impeccable/blob/b49177d32788f2dc585c42831d845dacf1ebed2c/.agents/skills/impeccable/reference/clarify.md) |
| Vercel · Web Design Guidelines 1.1.0 | 对 HTML 原型逐项查字段标签、键盘、可见焦点、动态错误和可访问名称，定位到具体控件。 | 原生交付另测 Qt 控件角色、名称、值和状态。用键盘从设置搜索框进入输出或通知，返回时读到原控件且选区可继续编辑。对仅用图标的铃铛、跟随按钮和启停开关，检查 Windows 讲述人实际读出的对象和状态。 [技能原文](https://github.com/vercel-labs/agent-skills/blob/5a37a6329fbad3666c3207392a6a7c0110e92efb/skills/web-design-guidelines/SKILL.md) · [逐项规则](https://github.com/vercel-labs/web-interface-guidelines/blob/434b7f91364665f2f733b310ec54809bf8f37937/command.md) · [本项目 Qt 版本的可访问性接口](https://github.com/qt/qtbase/blob/v5.15.2/src/gui/accessible/qaccessible.h) |
| Emil Kowalski · Emil Design Engineering | 按操作频率和动效目的设计反馈，连续输入可中断，业务状态独立于动画结束时刻。 | Ctrl+P、Ctrl+J、来源切换即时响应。保留 Ela 原生开关及消息条反馈。按住空格只提交一次，连续两次明确切换应按顺序得到各自结果。通知回位时仍能编辑或关闭工具区。系统关闭动态效果时，用同一文字、图标与通知历史传达结果。 [作者技能 emil-design-eng](https://github.com/emilkowalski/skills/blob/64eb3deb9a5fbfa041faf783e0bc3fd635872e9c/skills/emil-design-eng/SKILL.md) |

本次查阅 UI UX Pro Max 固定版本的 141 条 UX 规则，在 `Issue` 列检索 `empty states`、`essential text truncation`、`ime composition enter`、`preserve input after failed submit`、`persistent error messages`、`actionable error messages`、`modal dialog focus`，分别命中 CSV 第 80、114、121、134、139、140、142 行。输入法和模态焦点两条在上游标为 Web，这里把可观察行为转成 Windows 测试。配色、字族、图标以及悬停、焦点和选中状态的绘制均沿用现有 Ela。之前采用的 unslop 继续用于删去重复文案、复核首屏空间和动作位置。[固定版原始 CSV](https://raw.githubusercontent.com/nextlevelbuilder/ui-ux-pro-max-skill/8db36338810d0942567b97ce439affd1619fcf31/src/ui-ux-pro-max/data/ux-guidelines.csv) · [项目 Ela 规范](ui-design-system.md) · [既有 unslop 评审](ui-workbench-redesign.md)

## 从本机问题排开发顺序

| 顺序与工作包 | 原界面或实现依据 | 交付出口 |
|---|---|---|
| 第一批 · S7a 概览与创建 | 本机 1180 × 800 的概览，品牌区高至少 220。实例卡片宽 280，标题截到 192；一个实例也难辨全名。「添加插件」容易被理解为安装新模块。[原生实图](assets/ui-runtime-feedback/home.png) · [首页实现](../src/ui/home.cpp) | 沿用品牌原图，原型页头高 116；原生高度依字体和缩放调整。沿用 ElaFlowLayout 排布独立的 ElaScrollPageArea 卡片，宽时并排、窄时换行。文件、视图、转到、帮助复用顶栏 ElaMenuBar 的 ElaMenu，直接在对应标题下展开。失败但目标开启的实例能一眼识别。添加成功后回到新卡片，交代运行目标和保存入口。U8。 |
| 接续 S7a · S7b 配置事务 | 实例窗口并列多项动作和状态，工作区配置有单独的编辑、运行目标、磁盘修订。用户必须能看出修改到了哪一步。[实例配置实图](assets/ui-runtime-feedback/instance-configuration-baseline.png) · [工作区配置实图](assets/ui-runtime-feedback/configuration-baseline.png) | 配置主区域围绕正在编辑的对象和当前下一步安排。提交后按实际返回值写明结果。冲突说明保留了什么，列出「载入磁盘版本到编辑区」「采用当前磁盘为比较基线」的影响；均由用户再显式应用或保存。U4、U9。 |
| 可与第一批并行 · S5 输出、S6 反馈 | 输出用固定 220 宽的来源框，在 1180 × 800 本机窗口已截断实例和代次。现有输出以全文刷新；铃铛把用户带进包含多个管理页签的窗口。[原生输出](assets/ui-runtime-feedback/platform-output-baseline.png) · [原铃铛窗口](assets/ui-runtime-feedback/notifications.png) · [输出实现](../src/ui/output.cpp) | 底部只设输出和问题页签，命令由顶栏或 Ctrl+Shift+P 检索。切页、筛选与数据追加保留面板高度及分隔条位置，超高内容在页内滚动。输出提供运行、来源和实例筛选，事件正文占余宽。新来源到达时维持当前选择与阅读位置。铃铛直达通知；当前故障出现在问题页，普通成功在操作处反馈，需要稍后处理的故障进中心。从通知可定位同一事件。U1–U5。 |
| 串起窗口 · S7c 工具区往返 | 管理窗口约每 250 ms 刷新，问题面板约每 500 ms 检查；从活跃对象跳到记录时可能遇到对象已退役或问题已恢复。[管理窗口](../src/ui/management.cpp) · [问题面板](../src/ui/panel.cpp) | 用户主动定位时保存触发控件、选中对象和选区，工具区打开后 Esc 逐层返回；目标失效则回到所属列表并说明原因。在可用宽 759、760、1079、1080 逻辑像素各走一遍，窄档的长来源仍可区分。U6、U10。 |
| 最后整合 · S7d 设置与关于、S8 原生复核 | 正式宿主当前使用会话偏好；主题、导航和面板有现成控件。消息条已有入场、淡出和其余通知回位的不同动效。[宿主装配](../app/plugin_workbench/workbench.cpp) · [偏好实现](../src/ui/preferences.cpp) · [消息条动效](../third_party/ela/ElaWidgetTools/private/ElaMessageBarPrivate.cpp) | 窗口材质与切页效果均有独立选择和恢复策略。设置搜索定位到字段，说明偏好的保存范围，负责持久化的正式入口须通过新进程恢复。以同主题、缩放和客户区取原生前后图，检查外观、输入法、讲述人和减少动态效果；U11、U12。U7 单独接入真实日志。 |

## 评审时实际走一遍

1. 启动到概览，找出「已启用但启动失败」的实例。打开管理，读失败原因，选「查看此实例日志」。记录当前运行、来源、实例、代次和事件。向另一来源追加一条，继续选择和复制眼前记录。
2. 回到同一实例，编辑配置并故意留下错误。提交后应定位错误且保留草稿；修正再提交。另在工作区配置依次检查差异、应用、保存，对照操作前后运行目标和磁盘文件。模拟外部改写文件，检查判读和恢复。
3. 在设置搜索框选中一段文字，产生后台警告；随后主动打开通知、查看关联事件、逐层返回。再次键入应替换原选区。在目标退役、问题恢复、系统关闭动画的情况下重复一次。历史事件保持原身份。

UI 工作由宿主消费快照、定位事件、管理焦点与展示。详情和动作沿用稳定对象标识，执行时重新核对实例和代次；可见名称只用于展示。托管窗口复用现有所有权与关闭屏障。Ela 开关在动画开始时发出状态信号，业务执行只经既有动作入口；消息条退场和列表重排只影响显示。实现减少动态效果时，在平台视图侧读取 Windows 客户区动画偏好并响应设置变更，必要时以状态栏与通知中心承接同一事件。日志采集、落盘、轮转和跨进程查询按 LOG 工作单交付。[开关实现](../third_party/ela/ElaWidgetTools/private/ElaToggleSwitchPrivate.cpp) · [平台键盘开关](../src/ui/appearance_p.hpp) · [Windows 客户区动画偏好](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-systemparametersinfow) · [Windows 设置变更](https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-settingchange) · [日志工作单](runtime-logging-plan.md)

本机无界面 Chrome 在 1280、960、720、500 像素视口复验，实例卡片分别排为三、二、二、一列，均无横向溢出。帮助菜单在标题下方展开；页面和命令检索共用顶栏输入框，候选项在框下显示。底部默认高 160 像素，用鼠标拖到 218、再用键盘调到 234 后，在空问题页与超过 60 行的输出间反复切换，顶部坐标和面板高度均保持。项目已有 Qt 菜单动作用例在 offscreen 下通过。原型操作和基线用例各自记录，候选版原生验收见 U8、U10。[帮助菜单](assets/ui-runtime-feedback/prototype-menu.png) · [实例卡片](assets/ui-runtime-feedback/prototype-home.png) · [输出与问题](assets/ui-runtime-feedback/prototype-output.png)

浏览器走查还覆盖了字段纠错与中文组合输入。连续两次输入不同的无效 JSON 或重复标识时，原型保留错误和草稿；原因相同时提示区内容保持稳定。修正后解除错误。组合输入期间的方向键、Enter 和 Esc 留给输入法。原型使用 ElaAwesome 图标字形，品牌背景与从原生 ElaImageCard 导出的图片字节一致。[原生品牌资源](assets/ui-runtime-feedback/ela-home-banner.png)

本设计评审形成布局依据、可操作原型和验收工作单。原生版本的实施证据已单列在[本机原生验收](ui-native-acceptance.md)。原型使用模拟实例、模拟运行与模拟磁盘状态。原生实施完成后，实施人员在本地 Windows 登记版本、操作、前后事实和证据。U7 用独立进程及真实归档定位同一事件；U12 用 Windows 输入法、讲述人、动画偏好、明暗主题和实际 DPI 形成原生结论。[打开原型](design/workbench-ui-prototype.html) · [原生验收单](../tests/acceptance/ui-runtime-feedback.md)
