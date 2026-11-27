# 运行日志

底部「输出」面板可按本次或历史运行、来源、等级筛选。来源包括平台及实际写入的插件实例。工作区「问题」和通知中心提供业务与运行告警，可定位关联日志。宿主为本次运行分配独立标识，并在后台分批写入归档。已写入的历史运行可以跨进程查询。

空工作台的默认工作区位于 `%LOCALAPPDATA%/EvePluginWorkbench/platform-workspace`。使用 `--workspace` 或 `tools/desktop.ps1 -Workspace` 可以明确选择目录。归档优先放在工作区的 `.eve/logs`。该目录无法打开时，宿主尝试应用本地数据目录下的 `logs/<工作区绝对路径的 SHA-256>`，并在诊断中给出实际位置。两处均不可写时，持久化状态会报告问题并等待恢复。队列有容量上限，丢弃或未确认的条数会出现在诊断中。

默认事件段按 8 MiB 轮转。保留策略对已关闭的运行执行清理，期限为 14 天，容量软上限为 256 MiB。插件的实时输出快照有单独的容量限制。工作台正常关闭时等待后台写入线程结束，持久化诊断用于区分已接收、已确认及损失情况。

从 Cordis 0.1.x 升级时，如果新的默认位置还没有对应文件，工作台会继续使用旧默认目录中的 `session.yaml` 或 `preferences/workbench.json`。显式指定的工作区仍从原路径打开。输出面板和命令行均可查询旧工作区的 `.cordis/logs` 及旧版备用归档；新运行写入 Eve 的日志目录。

Qt 插件通过宿主的输出服务写入。Native 插件调用 ABI 日志接口时，`EVE_OK` 表示宿主已将记录复制进实例的有界内存。宿主配置日志接收端且实例获准发布后，记录才会异步投递。接收端接入归档时，可按运行和实际来源查询已写入的记录。[Qt SDK](sdk-consumer.md) · [Native ABI](abi-v1.md)

## 命令行查询

使用匹配 Qt kit 的运行环境，从工作台同一构建目录启动 `eve_log_query.exe`。以下示例查询此前通过 `--workspace` 指定的 `build/workspace`。

```powershell
$env:Path = "$(Join-Path $env:EVE_QT5152_ROOT 'bin');$env:Path"
$query = '.\build\ela-msvc-release\bin\eve_log_query.exe'
$workspace = Join-Path $PWD 'build/workspace'
& $query --workspace $workspace
& $query --workspace $workspace --run $runId --sources
& $query --workspace $workspace --run $runId --source $sourceId --level warning
& $query --workspace $workspace --run $runId --event $eventId
```

从第一条命令的输出取得 `$runId`，从来源列表取得 `$sourceId`，从相关日志或问题详情取得 `$eventId`。用 `--help` 查看实例、模块、通道、时间段和文本过滤参数。工具以只读方式查询工作区及其回退归档。若历史分段损坏，`--sources` 仍输出可读来源，在标准错误报告原因并以非零状态退出。脚本可检查 `$LASTEXITCODE` 判断来源列表是否完整。[来源查询契约](adr/0063-log-query-contract.md)
