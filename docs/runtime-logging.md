# 运行日志

底部「输出」面板可按本次或历史运行、来源、等级筛选。来源包括平台及实际写入的插件实例。工作区「问题」和通知中心提供业务与运行告警，可定位关联日志。平台把接受的运行事件持续写入工作区的 `.cordis/logs`，正常关闭可在下一次启动后继续查询。

空工作台的默认工作区位于 `%LOCALAPPDATA%/CordisPluginWorkbench/platform-workspace`。使用 `--workspace` 或 `tools/desktop.ps1 -Workspace` 可以明确选择目录。每次运行分配独立标识，源目录与日志分段位于归档中。当前保留策略通常清理超过 14 天的归档，以 256 MiB 为软容量上限，事件段按 8 MiB 轮转。保留清理按运行状态协调。

Qt 插件通过受宿主管理的输出服务写入，Native 插件通过 ABI 的宿主日志接口提交。平台归档记录实际来源和运行身份，宿主生命周期诊断与插件输出可以在同一运行内筛选。[Qt SDK](sdk-consumer.md) · [Native ABI](abi-v1.md)

## 命令行查询

使用匹配 Qt kit 的运行环境，从工作台同一构建目录启动 `cordis_log_query.exe`。以使用上述本地工作区和 Release 构建为例。

```powershell
$env:Path = "$(Join-Path $env:CORDIS_QT5152_ROOT 'bin');$env:Path"
$query = '.\build\ela-msvc-release\bin\cordis_log_query.exe'
$workspace = Join-Path $PWD 'build/workspace'
& $query --workspace $workspace
& $query --workspace $workspace --run $runId --sources
& $query --workspace $workspace --run $runId --source $sourceId --level warning
& $query --workspace $workspace --run $runId --event $eventId
```

从第一条命令的输出取得 `$runId`，从来源列表取得 `$sourceId`，从相关日志或问题详情取得 `$eventId`。用 `--help` 查看实例、模块、通道、时间段和文本过滤参数。查询只读已有归档。
