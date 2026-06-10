#ifndef AppVersion
#define AppVersion "0.3.0"
#endif

#define AppName "FFKeyLock"
#define AppPublisher "brealin"
#define AppExeName "FFKeyLock.exe"

[Setup]
AppId={{8F90D3C9-2E57-47D7-92A7-5CF40F6B9332}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} v{#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
LicenseFile=..\LICENSE
OutputDir=output
OutputBaseFilename=FFKeyLock-Setup-v{#AppVersion}
SetupIconFile=..\FFKeyLock\FFKeyLock.ico
Compression=lzma
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=lowest
UninstallDisplayIcon={app}\{#AppExeName}
VersionInfoVersion={#AppVersion}
VersionInfoCompany={#AppPublisher}
VersionInfoDescription={#AppName} Installer
VersionInfoProductName={#AppName}
VersionInfoProductVersion={#AppVersion}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "chinesesimp"; MessagesFile: "compiler:Default.isl,Languages\ChineseSimplified.isl"

[CustomMessages]
english.AppLanguageTitle=Application display language
english.AppLanguageDescription=Choose the language FFKeyLock should use on first launch.
english.AppLanguageSubCaption=You can change this later from Settings -> Language.
english.AppLanguageChinese=Simplified Chinese
english.AppLanguageEnglish=English
english.DesktopIconTask=Create a desktop shortcut
english.AdditionalIconTasks=Additional icons:
english.LaunchApp=Launch %1
chinesesimp.AppLanguageTitle=软件显示语言
chinesesimp.AppLanguageDescription=选择 FFKeyLock 首次启动时使用的显示语言。
chinesesimp.AppLanguageSubCaption=之后也可以在“设置 -> 语言”中修改。
chinesesimp.AppLanguageChinese=简体中文
chinesesimp.AppLanguageEnglish=English
chinesesimp.DesktopIconTask=创建桌面快捷方式
chinesesimp.AdditionalIconTasks=附加图标：
chinesesimp.LaunchApp=启动 %1

[Files]
Source: "..\x64\Release\FFKeyLock.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\FFKeyLock\FFKeyLock.ico"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.zh-CN.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\CHANGELOG.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\CHANGELOG-en.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion

[Dirs]
Name: "{userappdata}\FFKeyLock"

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExeName}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "{cm:DesktopIconTask}"; GroupDescription: "{cm:AdditionalIconTasks}"

[Run]
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchApp,{#StringChange(AppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[Code]
var
  AppLanguagePage: TInputOptionWizardPage;

procedure InitializeWizard;
begin
  AppLanguagePage :=
    CreateInputOptionPage(
      wpSelectDir,
      CustomMessage('AppLanguageTitle'),
      CustomMessage('AppLanguageDescription'),
      CustomMessage('AppLanguageSubCaption'),
      True,
      False);

  AppLanguagePage.Add(CustomMessage('AppLanguageChinese'));
  AppLanguagePage.Add(CustomMessage('AppLanguageEnglish'));

  if ActiveLanguage = 'english' then
    AppLanguagePage.Values[1] := True
  else
    AppLanguagePage.Values[0] := True;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  ConfigPath: string;
  AppLanguage: string;
begin
  if CurStep = ssPostInstall then
  begin
    ConfigPath := ExpandConstant('{userappdata}\FFKeyLock\config.ini');
    if AppLanguagePage.Values[1] then
      AppLanguage := 'en'
    else
      AppLanguage := 'zh';

    SetIniString('Settings', 'Language', AppLanguage, ConfigPath);
  end;
end;
