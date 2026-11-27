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
#define Workbench "eve_plugin_workbench.exe"

[Setup]
AppId={{7677F0BE-D8C1-4491-96A7-D16C47B32E38}
AppName=Eve
AppVersion={#AppVersion}
AppPublisher=Eve
VersionInfoVersion={#AppVersion}
VersionInfoDescription=Eve Setup
MinVersion=10.0
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
PrivilegesRequired=lowest
DefaultDirName={localappdata}\Programs\Eve
DefaultGroupName=Eve
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
OutputBaseFilename=Eve-{#AppVersion}-windows-x64-setup
UninstallDisplayIcon={app}\{#Workbench}
RestartApplications=no

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "zhCN"; MessagesFile: "inno-zh-CN\ChineseSimplified.isl"

[Messages]
en.WelcomeLabel1=Install Eve
en.WelcomeLabel2=This will install Eve {#AppVersion} for your Windows account.%n%nThe installer includes the plugin workbench and the command-line log viewer.
zhCN.WelcomeLabel1=安装 Eve
zhCN.WelcomeLabel2=即将为当前 Windows 账户安装 Eve {#AppVersion}。%n%n安装内容包括插件工作台和命令行日志查询工具。

[CustomMessages]
en.LegacyChanged=A previous program file or shortcut was modified. Move it to a safe location and run Setup again:%n%1
zhCN.LegacyChanged=旧版程序或快捷方式已被修改。请先将该文件另存至其他位置，再运行安装程序：%n%1
en.LegacyPath=Upgrade in the original installation directory:%n%1
zhCN.LegacyPath=请在原安装目录中执行升级：%n%1

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
Name: "{group}\Eve"; Filename: "{app}\{#Workbench}"; WorkingDir: "{app}"
Name: "{userdesktop}\Eve"; Filename: "{app}\{#Workbench}"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#Workbench}"; WorkingDir: "{app}"; Description: "{cm:LaunchProgram,Eve}"; Flags: postinstall nowait skipifsilent

[Code]
const
  UninstallKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{#Copy(SetupSetting("AppId"), 2)}_is1';
  LegacyWorkbench = 'cordis_plugin_workbench.exe';
var
  LegacyFiles, LegacyDigests: TArrayOfString;
  LegacyGroupDirectory: String;

function PreviousValue(Name: String): String;
begin
  if not RegQueryStringValue(HKCU64, UninstallKey, Name, Result) then Result := '';
end;

function IsLegacyInstall: Boolean;
begin
  Result := (PreviousValue('Publisher') = 'Cordis') and (PreviousValue('DisplayVersion') = '0.1.5');
end;

procedure InitializeWizard;
begin
  if IsLegacyInstall and (WizardForm.GroupEdit.Text = 'Cordis') then WizardForm.GroupEdit.Text := 'Eve';
end;

procedure RememberFile(Path, Digest: String);
var I: Integer;
begin
  if not FileExists(Path) then Exit;
  I := GetArrayLength(LegacyFiles);
  SetArrayLength(LegacyFiles, I + 1);
  SetArrayLength(LegacyDigests, I + 1);
  LegacyFiles[I] := Path;
  LegacyDigests[I] := Digest;
end;

function VerifyShortcut(Path, Target: String): Boolean;
var Link, Shell: Variant;
begin
  Result := True;
  if not FileExists(Path) then Exit;
  Shell := CreateOleObject('WScript.Shell');
  Link := Shell.CreateShortcut(Path);
  Result := (CompareText(Link.TargetPath, Target) = 0) and (String(Link.Arguments) = '');
  if Result then RememberFile(Path, GetSHA256OfFile(Path));
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var Root, Programs, GroupName, Link: String;
    I: Integer;
begin
  SetArrayLength(LegacyFiles, 0);
  SetArrayLength(LegacyDigests, 0);
  LegacyGroupDirectory := '';
  Result := '';
  if not IsLegacyInstall then Exit;
  Root := AddBackslash(PreviousValue('InstallLocation'));
  if CompareText(Root, AddBackslash(ExpandConstant('{app}'))) <> 0 then begin
    Result := FmtMessage(CustomMessage('LegacyPath'), [Root]);
    Exit;
  end;
  { 此组摘要取自正式的 0.1.5 x64 安装包，拒绝清理用户放入的同名文件。 }
  RememberFile(Root + LegacyWorkbench, 'D38CA07EF12B995316EBB5BED41D72E572EA17B43C8B3183067CD37802755186');
  RememberFile(Root + 'cordis_log_query.exe', '7FCEC439E25D8070144CCB86CD536F71014FFBF7D3D6C3C7D8EF27EDF3D32E4B');
  RememberFile(Root + 'cordis_core.dll', 'E3AD6FA4BAF04C0C6D9CEDB798679BE7697400CFF8F31E597E3D464EEDCC974B');
  RememberFile(Root + 'cordis_qt.dll', 'CEF7BD8FDE9D96F78FEF592FB79DF40A2031860B8282FAAD8BA06175A1DB7572');
  RememberFile(Root + 'cordis_ui.dll', 'E91E80E84BDC406818529FFBDBD7BF09B88E4C1A8EF2991C33D690F17000AAA9');
  for I := 0 to GetArrayLength(LegacyFiles) - 1 do
    if CompareText(GetSHA256OfFile(LegacyFiles[I]), LegacyDigests[I]) <> 0 then begin
      Result := FmtMessage(CustomMessage('LegacyChanged'), [LegacyFiles[I]]);
      Exit;
    end;
  Programs := AddBackslash(ExpandConstant('{userprograms}'));
  GroupName := PreviousValue('Inno Setup: Icon Group');
  if GroupName <> '' then begin
    LegacyGroupDirectory := ExpandFileName(Programs + GroupName);
    if Pos(Lowercase(Programs), Lowercase(LegacyGroupDirectory)) <> 1 then begin
      Result := FmtMessage(CustomMessage('LegacyChanged'), [LegacyGroupDirectory]);
      Exit;
    end;
    Link := AddBackslash(LegacyGroupDirectory) + 'Cordis.lnk';
    if not FileExists(Link) then
      LegacyGroupDirectory := ''
    else if not VerifyShortcut(Link, Root + LegacyWorkbench) then begin
      Result := FmtMessage(CustomMessage('LegacyChanged'), [Link]);
      Exit;
    end;
  end;
  if PreviousValue('Inno Setup: Selected Tasks') = 'desktopicon' then begin
    Link := ExpandConstant('{userdesktop}\Cordis.lnk');
    if not VerifyShortcut(Link, Root + LegacyWorkbench) then
      Result := FmtMessage(CustomMessage('LegacyChanged'), [Link]);
  end;
end;

procedure RegisterExtraCloseApplicationsResources;
var I: Integer;
begin
  for I := 0 to GetArrayLength(LegacyFiles) - 1 do
    RegisterExtraCloseApplicationsResource(False, LegacyFiles[I]);
end;

procedure CurStepChanged(CurStep: TSetupStep);
var I: Integer;
begin
  if CurStep <> ssPostInstall then Exit;
  { 先安装完整的 Eve，再清理已核验且未改变的旧件。只移除空的快捷方式目录。 }
  for I := 0 to GetArrayLength(LegacyFiles) - 1 do
    if FileExists(LegacyFiles[I]) and (CompareText(GetSHA256OfFile(LegacyFiles[I]), LegacyDigests[I]) = 0) then
      if not DeleteFile(LegacyFiles[I]) then Log('Could not remove previous file: ' + LegacyFiles[I]);
  if LegacyGroupDirectory <> '' then RemoveDir(LegacyGroupDirectory);
end;
