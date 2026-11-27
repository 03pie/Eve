# Windows 安装包

双击 `Eve-<版本>-windows-x64-setup.exe`，选择简体中文或 English，然后选择安装目录与快捷方式。向导跟随系统明暗模式。安装范围为当前 Windows 用户，默认目录为 `%LOCALAPPDATA%\Programs\Eve`。安装包内含桌面工作台、日志查询工具及所需运行库，可离线安装。

安装后从开始菜单打开 **Eve**。更新时先关闭工作台，再运行新版安装包，向导会识别已有安装。可在 Windows「设置 → 应用 → 安装的应用」卸载。工作区配置、用户文件和运行归档继续保存在原位置。[运行日志](runtime-logging.md)

## 从源码制作

准备 Windows x64、Visual Studio 2022 C++ 工具链、Qt 5.15.2 MSVC x64、CMake 3.21+、Ninja 及 Inno Setup 6.6 或更高的兼容版本。命令会在独立的 Release 预设下构建两个发布入口，再从本次暂存目录编译安装程序。可通过 `EVE_ISCC` 指定 `ISCC.exe` 的完整路径。[开发环境](development.md) · [Inno Setup 下载](https://jrsoftware.org/isdl.php)

```powershell
$env:EVE_QT5152_ROOT = 'C:\Qt\5.15.2\msvc2019_64'
.\tools\desktop.ps1 -Action package
```

安装包和同名的 `.sha256` 文件位于 `build/ela-installer-msvc-release/dist/`。版本来自根 CMake 项目。以下示例用当前版本的产物在普通权限的本机桌面执行验收。

```powershell
$setup = 'build/ela-installer-msvc-release/dist/Eve-0.2.0-windows-x64-setup.exe'
if ((Get-FileHash $setup -Algorithm SHA256).Hash -ne ((Get-Content "$setup.sha256") -split ' ')[0]) {
    throw '安装包 SHA-256 与校验文件不匹配。'
}
.\tools\test-windows-installer.ps1 -Installer $setup
```

验收在临时目录执行安装、同版本覆盖、实际窗口启动、日志查询和卸载，核对当前用户的开始菜单、注册记录以及卸载后的工作区内容。已安装同一产品时，脚本会在开始安装前退出。

欢迎页的插图保留了可编辑的 SVG 母版，浅色和深色 PNG 随源码提供。向导的简体中文翻译固定在上游提交 `1ace6a4`，对应的 MIT 许可随安装包交付。[品牌插图](../packaging/windows/art/welcome.svg) · [翻译来源](https://github.com/kira-96/Inno-Setup-Chinese-Simplified-Translation/tree/1ace6a485174288c7416d0979cc2db1f0990f95a)

## 公开分发

当前构建产出未签名的 EXE。若发布者使用 Authenticode 签名，在签名完成后重新计算随包的 SHA-256。运行时随附的声明位于安装目录的 `licenses`。仓库记录的 Ela 内嵌图标字体与四张图片仍缺公开再分发授权，公开分发前应取得许可或使用权属明确的替代资源。[资源来源记录](../third_party/ela/RESOURCE-PROVENANCE.txt) · [第三方声明](../THIRD_PARTY_NOTICES.md)
