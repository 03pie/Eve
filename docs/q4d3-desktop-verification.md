# Q4d3 原生桌面代理复核

日期：2026-10-26。构建：当前工作区 Ela/MSVC Release，Qt 5.15.2 msvc2019_64、MSVC 19.44/v143。操作方式：computer-use 原生 Windows 窗口操作与视觉检查，外部文件变化由仓库内专属测试文件注入。

初次应用访问授权超时；用户要求重试后成功。此次为代理原生复核，不登记成用户亲自完成的人工验收。未修改用户默认配置、系统缩放或系统环境变量。

## 已观察结果

| 项目 | 操作与观察 | 结果 |
|---|---|---|
| 初始读取与运行分离 | 独立 retry.yaml 启动；修订 0、未自动挂载 notes。检查差异、应用后修订 1，保存后已保存 1 | 通过 |
| 编辑保护 | 编辑区改为 LOCAL_UNAPPLIED_DRAFT；外部写入 DISK_ONE，通知和磁盘原文更新，编辑及目标修订保持 | 通过 |
| 原子替换与续写 | File.Replace 替换为 DISK_TWO_ATOMIC；随后写入 DISK_THREE_AFTER_REPLACE，两次原文都到达 | 通过 |
| 删除重建 | 仅删除专属 retry.yaml；页面提示删除，编辑未清空。重建 DISK_FOUR_RECREATED 后监听恢复 | 通过 |
| 显式载入 | 为本地草稿预览至“应用”可用；点击载入磁盘后编辑替换、旧候选失效、应用禁用，目标仍修订 1 | 通过 |
| 保存基线 | 再次检查并应用磁盘内容至修订 2；保存报告冲突，原文保留，未自动修改保存基线 | 通过 |
| 关闭冲突与恢复 | 冲突时点击关闭，窗口恢复。外部写入 DISK_FIVE_AFTER_CLOSE_CONFLICT 后仍收到通知；更新冲突、显式采用基线时磁盘未被覆盖，再保存后修订 2 同步 | 通过 |
| 自身保存与外部回退 | 自身保存后无外部变更提示；外部 DISK_SIX_BEFORE_REVERT 到达后，再恢复 own-saved.yaml，仍检测到该变化 | 通过 |
| 布局 | 原生浅/深色及 QT_SCALE_FACTOR=1.5 进程缩放下，新增提示、载入按钮、原文区清晰，无遮挡或深色白底 | 通过；未更改或替代用户系统 DPI 验收 |
| 退出刷新 | tools/desktop.ps1 run 重开已保存文件，预览并应用但不点保存，关闭时刷新已接受目标；入口检查到原生退出码 0，所有工作台进程退出，无线程运行警告 | 通过 |

操作小文件期间窗口持续响应，保存及读取仍区分目标、运行和已保存状态；未作大文件或高磁盘延迟的性能验收。后台线程/关闭的受控等待另由配置 I/O 自动化场景验证。

## 证据与边界

专属文件位于 build/desktop-acceptance：retry.yaml、saved-baseline.yaml、previous-disk.yaml、own-saved.yaml 与 scaled.yaml。entry-exit.txt 为 0，entry-run.log 为桌面入口运行日志；retry-stderr.log、scaled-stderr.log 未出现错误。Get-Process 未发现剩余 cordis_config_workbench 进程。通过 Start-Process 另行追踪的两次 ExitCode 为空，未将空值计为 0；最终退出码以同步桌面入口的实际原生退出码校验为准。

视觉检查使用工具返回的原生截图，过程见会话，不补造持久截图文件。没有借助 offscreen 图像代填此次原生操作。自动回归 Ela Debug/Release 各 401/401；该数量包含新增类型化 worker 场景，不能换算为本表的人工完成比例。

以上为 2026-10-26 代理复核时的记录。2026-10-27 用户明确回复“已手动验证通过”，Q4d3 已按本次确认登记至 [验收单](../tests/acceptance/p3-ui.md)，不补造其环境或逐项操作细节。P3 最终状态见 [收口审计](p3-closeout.md)，模块 ABI 与物理卸载仍属于后续阶段。
