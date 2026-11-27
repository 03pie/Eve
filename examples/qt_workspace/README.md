# 配置驱动的笔记样例

此样例用一个独立 Qt DLL 演示页面、实例命令和输出通道。可以创建多个实例，修改配置和启停实例，并通过模块管理查看代次、占用资源、驻留原因和启动错误。

## 构建和启动

在 Windows x64 的 MSVC 开发环境中设置 `EVE_QT5152_ROOT`，指向 Qt 5.15.2 的 MSVC x64 安装目录。从仓库根目录运行以下命令。

```powershell
cmake --preset ela-msvc-release
cmake --build --preset ela-msvc-release --target eve_qt_workspace_demo
$env:Path = "$env:EVE_QT5152_ROOT/bin;$env:Path"
./build/ela-msvc-release/bin/eve_qt_workspace_demo.exe
```

默认将 `session.yaml` 保存在 `EveDllNotebook` 的应用数据目录。也可在启动时传入独立的 `session.yaml` 路径。首次启动使用 [workspace.yaml](workspace.yaml) 的默认配置。样例 DLL 与 `module.json` 位于同一构建的 `bin/qt-workspace/notebook` 下。

## 使用样例

- 在笔记页编辑后点击“输出当前笔记”，或执行对应实例的输出命令。在“输出”面板选择该实例的通道查看结果。
- 插件卡片的配置入口可修改标题（`title`）和启动文本（`text`）。编辑页中的草稿在该实例中保留，保存配置时写入配置界面中的启动文本。
- 通过“文件 → 模块管理”查看实际运行状态和资源。退出时如遇磁盘保存冲突，处理冲突后可重试关闭。
- 在独立会话中可设置布尔选项 `failStart` 或 `failCleanup`，分别演示启动失败时的回滚和清理失败时的诊断。

## 集成检查

默认笔记 DLL 采用驻留策略。构建测试会另外生成支持草稿迁移的 `notebook-a`、`notebook-b` 夹具。构建全部目标后可运行以下检查。

```powershell
ctest --preset ela-msvc-release -R '^(p4\.qt_workspace_|p5\.(notebook_state_|platform_workspace_|platform_upgrade_))'
```
