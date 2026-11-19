Cordis 0.1.5 - Windows x64 内部技术预览

此包用于内部测试，暂不公开发布。准确交付修订见 build-manifest.json。
第一方代码尚未选择开源许可证；第三方材料见 licenses/，不据此变更第一方许可。

运行环境
  Windows 10/11 x64；实际通过的系统构建见随交付的验收记录。
  无需安装 Visual Studio、CMake、Python 或 Qt 开发环境。
  完整解压后运行，不在 ZIP 内直接启动，不删改随包 DLL 或模块声明。

两个入口
  cordis_config_workbench.exe
    配置工作台：Home 插件卡片、实例启停、配置检查/应用/保存及磁盘冲突处理。
    默认数据：%LOCALAPPDATA%\CordisConfigWorkbench\cordis.yaml
    指定配置：cordis_config_workbench.exe --file "D:\测试数据\cordis.yaml"
    外观与布局保存到同一应用数据目录的 ui-preferences.json。

  cordis_platform_workbench_demo.exe
    固定 Counter/Reporter/Notebook 三模块升级工作台；不是任意插件安装宿主。
    默认配置：%APPDATA%\CordisBusinessPlatform\session.yaml
    默认存储：%APPDATA%\CordisBusinessPlatform\plugins
    指定位置：cordis_platform_workbench_demo.exe "D:\测试数据\session.yaml" "D:\测试数据\plugins"
    六份随包 A/B 模块必须全部保留。候选声明位于 native-upgrade/ 和 qt-upgrade/。
    升级 Applied 表示运行目标已接受；仍需“保存已接受配置”才写入配置文件。
    Qt 模块在进程中驻留。Notebook 草稿支持同进程升级迁移，未承诺跨进程文档保存。
    业务工作台当前外观偏好只在本次会话内保留。

完整性检查
  从 PowerShell 执行：
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\verification\verify-package.ps1" -PackageRoot "."
  脚本比较实际文件、清单和摘要，不修改运行包或插件批准。
  外部 ZIP/清单摘要应来自这次交付记录；包内自带摘要只证明文件一致性。

数据与退出
  应用目录可以只读；配置和插件存储目录必须可写。
  正常关闭主窗口并等待进程退出，之后才能移动或替换版本目录。
  新旧版本并列保留；首次更新前备份配置及所用插件存储，按交付更新说明操作。
  删除应用目录不会自动删除用户数据；不要手工清理仍在用或用于恢复的 hash 目录。
  输出与通知历史有界且只在内存中保留；需要记录时使用输出区“复制”，关闭后不保留。

验收与问题记录
  verification/ 提供独立验包工具。保留交付版本、ZIP摘要、Windows构建及错误原文。
  配置工作台与业务工作台使用独立应用数据目录，测试时可指定独立配置/store。
  完整桌面记录使用 record-desktop.ps1，会串行打开两个入口及各自重开窗口；
  请按交付说明操作并正常关闭。它只记录，不代替人工操作或体验结论。
  只读副本验收使用 preview-acl.ps1 的 Prepare、Protect、Probe、Restore；
  它只改自己创建的测试副本，不改原包。权限恢复前保留原脚本所在目录。
  这些工具均使用 Windows 自带 PowerShell，不需要 Python 或管理员权限。
  使用包旁“验收说明.txt”中的本次准确摘要及命令，不把包内自算摘要当外部锚点。
  最终跨机器验收、更新恢复和支持范围以本次交付证据为准。
