{ Keep existing files byte-for-byte, including unsupported or damaged configs. }
function InitializeAppLanguage(const ConfigPath, AppLanguage: String): Boolean;
begin
  if FileExists(ConfigPath) then
    Result := True
  else
    Result := SetIniString('Settings', 'Language', AppLanguage, ConfigPath);
end;
