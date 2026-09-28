; Isolated harness: no app files, shortcuts, uninstall entries or user config.
[Setup]
AppName=FFKeyLock Installer Language Tests
AppVersion=1.0
PrivilegesRequired=lowest
CreateAppDir=no
Uninstallable=no
OutputDir=artifacts
OutputBaseFilename=InstallerLanguage.Tests
WizardStyle=modern

[Code]
#include "..\installer\InitializeLanguage.iss"

procedure Check(Condition: Boolean; const Reason: String);
begin
  if not Condition then
    RaiseException(Reason);
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  ConfigPath, ResultPath: String;
  Original, After: AnsiString;
begin
  if CurStep <> ssPostInstall then
    Exit;
  ConfigPath := ExpandConstant('{tmp}\language-test.ini');
  Check(InitializeAppLanguage(ConfigPath, 'en'), 'Initialize English');
  Check(GetIniString('Settings', 'Language', '', ConfigPath) = 'en', 'English first launch');
  Check(LoadStringFromFile(ConfigPath, Original), 'Read first launch config');
  Check(InitializeAppLanguage(ConfigPath, 'zh'), 'Upgrade existing English');
  Check(LoadStringFromFile(ConfigPath, After) and (After = Original), 'Upgrade must preserve config bytes');
  Check(DeleteFile(ConfigPath), 'Clear isolated English fixture');
  Check(InitializeAppLanguage(ConfigPath, 'zh'), 'Initialize Chinese');
  Check(GetIniString('Settings', 'Language', '', ConfigPath) = 'zh', 'Chinese first launch');
  Original := '[Settings]' + #13#10 + 'Version=99' + #13#10 + 'Language=zh' + #13#10 + '[Library]' + #13#10 + 'Count=0';
  Check(SaveStringToFile(ConfigPath, Original, False), 'Write unsupported version fixture');
  Check(InitializeAppLanguage(ConfigPath, 'en'), 'Upgrade unsupported config');
  Check(LoadStringFromFile(ConfigPath, After) and (After = Original), 'Preserve unsupported config bytes');
  Original := 'damaged configuration';
  Check(SaveStringToFile(ConfigPath, Original, False), 'Write damaged fixture');
  Check(InitializeAppLanguage(ConfigPath, 'en'), 'Upgrade damaged config');
  Check(LoadStringFromFile(ConfigPath, After) and (After = Original), 'Preserve damaged config bytes');
  Check(not InitializeAppLanguage(ExpandConstant('{tmp}\missing-directory\config.ini'), 'en'), 'Report initialization failure');
  ResultPath := ExpandConstant('{param:ResultFile}');
  Check(ResultPath <> '', 'Supply /ResultFile in the isolated artifact directory');
  Check(SaveStringToFile(ResultPath, 'PASS: installer language initialization and upgrade preservation', False), 'Write test result');
end;
