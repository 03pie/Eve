; Runtime payload and version are supplied by the Windows release build.
#ifndef AppVersion
  #error AppVersion is required.
#endif
#ifndef PayloadDir
  #error PayloadDir is required.
#endif
#if Ver < EncodeVer(6, 6, 0)
  #error Inno Setup 6.6 or later is required for the dynamic wizard style.
#endif
#define BrandDir "..\..\app\shell\resources"
#define Workbench "cordis_plugin_workbench.exe"

[Setup]
AppId={{7677F0BE-D8C1-4491-96A7-D16C47B32E38}
AppName=Cordis
AppVersion={#AppVersion}
AppPublisher=Cordis
VersionInfoVersion={#AppVersion}
VersionInfoDescription=Cordis Setup
MinVersion=10.0
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
PrivilegesRequired=lowest
DefaultDirName={localappdata}\Programs\Cordis
DefaultGroupName=Cordis
AllowNoIcons=yes
DisableWelcomePage=no
WizardStyle=modern dynamic windows11
SetupIconFile={#BrandDir}\eve.ico
WizardImageFile=art\welcome.png
WizardImageFileDynamicDark=art\welcome-dark.png
WizardSmallImageFile={#BrandDir}\eve-128.png
WizardSmallImageFileDynamicDark={#BrandDir}\eve-128.png
Compression=lzma2
SolidCompression=yes
OutputBaseFilename=Cordis-{#AppVersion}-windows-x64-setup
UninstallDisplayIcon={app}\{#Workbench}
RestartApplications=no

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "zhCN"; MessagesFile: "inno-zh-CN\ChineseSimplified.isl"

[Messages]
en.WelcomeLabel1=Install Cordis
en.WelcomeLabel2=This will install Cordis {#AppVersion} for your Windows account.%n%nThe installer includes the plugin workbench and the command-line log viewer.
zhCN.WelcomeLabel1=安装 Cordis
zhCN.WelcomeLabel2=即将为当前 Windows 账户安装 Cordis {#AppVersion}。%n%n安装内容包括插件工作台和命令行日志查询工具。

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; Each private runtime file is tracked in the uninstall log. When a future
; release retires a DLL, use its exact relative name in [InstallDelete].
Source: "{#PayloadDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs
; MIT translation, upstream tag 6.5.0+ (1ace6a485174288c7416d0979cc2db1f0990f95a).
; https://github.com/kira-96/Inno-Setup-Chinese-Simplified-Translation
Source: "inno-zh-CN\LICENSE.txt"; DestDir: "{app}\licenses\inno-zh-CN"; Flags: ignoreversion

[Icons]
Name: "{group}\Cordis"; Filename: "{app}\{#Workbench}"; WorkingDir: "{app}"
Name: "{userdesktop}\Cordis"; Filename: "{app}\{#Workbench}"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#Workbench}"; WorkingDir: "{app}"; Description: "{cm:LaunchProgram,Cordis}"; Flags: postinstall nowait skipifsilent
