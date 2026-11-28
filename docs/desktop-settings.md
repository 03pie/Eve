# 桌面设置和语言扩展

从 **File → Open User Settings (JSON)**、设置页或命令面板打开用户设置。Windows 工作台将它放在 `%LOCALAPPDATA%\EvePluginWorkbench\preferences\settings.json`。文件使用 UTF-8 JSON，可以只保留需要自定义的键，删除某个键会恢复该项默认值。界面和文件共用同一份设置，外部编辑保存后会重新读取。发现格式错误时，桌面保留当前设置和文件内容，待修复后再应用。

```json
{
  "workbench.theme": "dark",
  "workbench.windowMaterial": "acrylic",
  "workbench.navigationMode": "compact",
  "workbench.navigationWidth": 240,
  "notifications.doNotDisturb": false
}
```

| 设置 | 可选值 |
| --- | --- |
| `workbench.theme` | `light`、`dark` |
| `workbench.windowMaterial` | `normal`、`elaMica`、`mica`、`micaAlt`、`acrylic`、`dwmBlur` |
| `workbench.pageTransition` | `none`、`popup`、`scale`、`flip`、`blur` |
| `workbench.navigationMode` | `compact`、`expanded` |
| `workbench.navigationWidth` | 180–360，单位为逻辑像素 |
| `workbench.panelHeight` | 120–480，单位为逻辑像素 |
| `workbench.showUserCard` | 布尔值 |
| `workbench.wallpaper` | 背景图的绝对路径 |
| `notifications.doNotDisturb` | 布尔值 |
| `output.source` | 输出面板所选来源的标识 |

工作区的插件实例、启用状态和业务配置由 `session.yaml` 管理。

## 语言扩展

在插件管理中选择 **Install Extension** 打开本地 `.evelang`，核对包信息后确认安装。在该语言扩展的详情中选择 **Use Display Language**，重启 Eve 后生效。通过 **Switch to English** 或卸载当前语言扩展可恢复英文。缺少扩展、扩展无法读取或某条文案没有翻译时，使用内置英文。

`.evelang` 是最多 512 KiB 的 UTF-8 JSON 对象。下例演示两个翻译条目：

```json
{
  "formatVersion": 1,
  "locale": "fr",
  "name": "Français",
  "translations": {
    "settings.title": "Paramètres",
    "workbench.title": "Plateforme de plugins Eve"
  }
}
```

译文键采用默认资源中的稳定消息 ID。翻译保留 `%1` 等格式占位符，带助记键的菜单文案保留一个合适的 `&` 助记标记。安装文件存放在用户设置旁的 `language-packs/` 目录，文件名为 `locale-<语言标记>.evelang`。启用状态写入独立的 `locale.json`。语言扩展只提供数据，可独立开发和分发，无需编译 DLL。

源资源位于 `plugins/builtin/desktop-ela/i18n/messages.en.json`，运行时读取程序目录下的 `system/desktop/messages.en.json`。直接修改运行目录的资源并重启即可更新文案。修改源码目录的文件并构建时，CMake 更新资源副本。修改一条文案时保留原消息 ID。
