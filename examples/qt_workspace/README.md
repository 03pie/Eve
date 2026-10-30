# 配置驱动的笔记 UI DLL

这是 M4c2 的实际业务示例：一个独立 Qt/Ela DLL 贡献笔记编辑页、注册“输出笔记”命令，并写入当前实例的真实输出通道。支持同一插件多个配置实例、配置换代、启停、保存、失败回滚和清理失败诊断；DLL 不链接 Loader/Session/模块目录，也没有测试控制导出。

在当前 Windows x64 / MSVC / Qt 5.15.2 / 共享 Core 构建中运行：

```powershell
./tools/desktop.ps1 -Action build -Preset ela-msvc-release
$env:PATH = "$PWD/build/Qt/5.15.2/msvc2019_64/bin;" + $env:PATH
./build/ela-msvc-release/bin/cordis_qt_workspace_demo.exe
```

可用唯一参数指定自己的 session.yaml；默认使用 CordisDllNotebook 的独立应用数据目录。首次读取 [workspace.yaml](workspace.yaml)，不会修改原配置工作台的文件。DLL 和 manifest 在同一构建的 bin/qt-workspace/notebook 下；元数据、Debug/Release 产物及 SHA256 不可混用。

- 编辑后点击“输出当前笔记”，或从底部命令面板执行对应实例的命令，在“输出”选择该实例的 notebook 通道查看。编辑框为本次页面草稿；启动文本通过配置中的 text 字段保存。
- 顶部配置入口打开现有配置文件界面；插件卡片中的配置界面可修改 title/text。添加实例使用 example.notebook，宿主 Session 自动带入冻结的 notebook 模块归属。
- “模块诊断”入口只读显示代次、业务入口状态、真实引用、驻留原因及配置失败原因。业务入口开放与某个配置实例激活成功是分别记录的状态。
- 输出过长会给出失败反馈，保留现有输出。退出前保存冲突会保留窗口和 Session，显式处理磁盘冲突后再关闭。

配置可使用可选的布尔 failStart 或 failCleanup 演示失败。前者验证已创建资源回滚；后者故意阻止业务所有者最终释放，显示不静止并保留 DLL/实例引用，不能解释为成功退出。正常退出同样不卸载 loader root 或映像。

12 项真实 Qt/DLL 联合场景和无 Qt 的 Session 追加检查见 [开发记录](../../docs/p4-m4c2-development.md)。关闭示例选项时，实际 DLL、宿主装配和检查仍构建；关闭共享 Core 时该同工具链示例不提供。M4c3 的原生桌面验收尚待完成。
