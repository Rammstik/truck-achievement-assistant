#ifndef ProductVersion
  #error Build with installer/build.ps1
#endif

[Setup]
#ifdef AA_TEST
AppId=Rammstik.ETS2AchievementAssistant.TEST
#else
AppId=Rammstik.ETS2AchievementAssistant
#endif
AppName=ETS2 Achievement Assistant
AppVersion={#ProductVersion}
AppPublisher=Rammstik
AppPublisherURL=https://github.com/Rammstik/truck-achievement-assistant
AppSupportURL=https://github.com/Rammstik/truck-achievement-assistant/issues
AppUpdatesURL=https://github.com/Rammstik/truck-achievement-assistant/releases
DefaultDirName={code:FindGame}
DirExistsWarning=no
AppendDefaultDirName=no
DisableDirPage=no
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
WizardStyle=modern dynamic
Compression=lzma2
SolidCompression=yes
#ifdef AA_TEST
OutputDir=test-results
OutputBaseFilename=TEST-ONLY-ETS2-Assistant-Setup
#else
OutputDir=..\outputs\public
OutputBaseFilename=ETS2-Achievement-Assistant-Setup-{#ProductVersion}
#endif
SetupLogging=yes
CloseApplications=no
RestartApplications=no
UninstallDisplayName=ETS2 Achievement Assistant
UninstallDisplayIcon={app}\bin\win_x64\eurotrucks2.exe
#ifdef AA_TEST
UninstallFilesDir={app}\.assistant-uninstall
#else
UninstallFilesDir={localappdata}\Programs\ETS2AchievementAssistant\Uninstall
#endif
#ifdef AA_TEST
CreateUninstallRegKey=no
UsePreviousAppDir=no
#endif

[Languages]
Name: russian; MessagesFile: compiler:Languages\Russian.isl
Name: english; MessagesFile: compiler:Default.isl

[Messages]
russian.SelectDirLabel3=Выберите папку Euro Truck Simulator 2. Установщик проверит совместимость игры перед копированием файлов.
russian.SelectDirBrowseLabel=Нужна корневая папка игры, в которой находится каталог bin.
russian.FinishedLabelNoIcons=Помощник установлен. Запустите ETS2 и нажмите Ctrl+F8. План и настройки сохраняются между запусками.
english.SelectDirLabel3=Select the Euro Truck Simulator 2 folder. Setup will check compatibility before copying files.
english.SelectDirBrowseLabel=Choose the game root folder containing the bin directory.
english.FinishedLabelNoIcons=The assistant is installed. Start ETS2 and press Ctrl+F8. Your plan and preferences are kept between sessions.

[CustomMessages]
russian.InvalidGame=В выбранной папке не найден bin\win_x64\eurotrucks2.exe. Укажите корневую папку Euro Truck Simulator 2.
russian.UnsupportedGame=У выбранного файла нет сведений о версии. Проверьте папку установки ETS2.
russian.CloseGame=Закройте ETS2 перед установкой, обновлением или удалением помощника, затем повторите действие.
russian.ProcessCheckFailed=Не удалось проверить, закрыта ли игра. Закройте ETS2 и повторите действие.
russian.GameReadFailed=Не удалось прочитать файл игры. Проверьте доступ к папке и повторите действие.
english.InvalidGame=The selected folder does not contain bin\win_x64\eurotrucks2.exe. Select the Euro Truck Simulator 2 root folder.
english.UnsupportedGame=The selected executable has no version information. Check your ETS2 installation folder.
english.CloseGame=Close ETS2 before installing, updating or uninstalling the assistant, then try again.
english.ProcessCheckFailed=Setup could not check whether the game is closed. Close ETS2 and try again.
english.GameReadFailed=The game executable could not be read. Check folder permissions and try again.

[Files]
Source: staging\bin\win_x64\plugins\AchievementAssistant.dll; DestDir: {app}\bin\win_x64\plugins; Flags: ignoreversion
Source: staging\bin\win_x64\plugins\AchievementAssistant\*; DestDir: {app}\bin\win_x64\plugins\AchievementAssistant; Flags: ignoreversion recursesubdirs createallsubdirs
; This generic configuration may predate this installer: preserve it on updates/removal.
Source: staging\bin\win_x64\plugins\map-probe.json; DestDir: {app}\bin\win_x64\plugins; Flags: onlyifdoesntexist uninsneveruninstall
Source: staging\licenses\*; DestDir: {app}\bin\win_x64\plugins\AchievementAssistant\licenses; Flags: ignoreversion
Source: staging\README_RU.md; DestDir: {app}\bin\win_x64\plugins\AchievementAssistant; Flags: ignoreversion
Source: staging\README_EN.md; DestDir: {app}\bin\win_x64\plugins\AchievementAssistant; Flags: ignoreversion
Source: staging\manifest-sha256.json; DestDir: {app}\bin\win_x64\plugins\AchievementAssistant; Flags: ignoreversion

[Code]
function VdfValue(Line: String): String;
var I: Integer;
begin
  Result := '';
  Line := Trim(Line);
  if (Length(Line) < 2) or (Line[1] <> '"') then Exit;
  Delete(Line, 1, 1);
  I := Pos('"', Line);
  if I = 0 then Exit;
  Delete(Line, 1, I);
  Line := Trim(Line);
  if (Length(Line) < 2) or (Line[1] <> '"') then Exit;
  Delete(Line, 1, 1);
  I := Pos('"', Line);
  if I > 0 then Result := Copy(Line, 1, I - 1);
  StringChangeEx(Result, '\\', '\', True);
end;

function GameInLibrary(LibraryRoot: String): String;
var Lines: TArrayOfString; I: Integer; InstallName, Candidate: String;
begin
  Result := '';
  InstallName := 'Euro Truck Simulator 2';
  if LoadStringsFromFile(AddBackslash(LibraryRoot) + 'steamapps\appmanifest_227300.acf', Lines) then
    for I := 0 to GetArrayLength(Lines) - 1 do
      if Pos('"installdir"', Trim(Lines[I])) = 1 then InstallName := VdfValue(Lines[I]);
  Candidate := AddBackslash(LibraryRoot) + 'steamapps\common\' + InstallName;
  if FileExists(Candidate + '\bin\win_x64\eurotrucks2.exe') then Result := Candidate;
end;

function FindGame(Param: String): String;
var SteamRoot, Candidate: String; Lines: TArrayOfString; I: Integer;
begin
  Result := ExpandConstant('{sd}\SteamLibrary\steamapps\common\Euro Truck Simulator 2');
  if not RegQueryStringValue(HKCU, 'Software\Valve\Steam', 'SteamPath', SteamRoot) then
    if not RegQueryStringValue(HKLM32, 'Software\Valve\Steam', 'InstallPath', SteamRoot) then Exit;
  Candidate := GameInLibrary(SteamRoot);
  if Candidate <> '' then begin Result := Candidate; Exit; end;
  if LoadStringsFromFile(AddBackslash(SteamRoot) + 'steamapps\libraryfolders.vdf', Lines) then
    for I := 0 to GetArrayLength(Lines) - 1 do
      if Pos('"path"', Trim(Lines[I])) = 1 then begin
        Candidate := GameInLibrary(VdfValue(Lines[I]));
        if Candidate <> '' then begin Result := Candidate; Exit; end;
      end;
end;

function ClosedGameError(): String;
var Locator, Services, Processes: Variant;
begin
  Result := '';
  try
    Locator := CreateOleObject('WbemScripting.SWbemLocator');
    Services := Locator.ConnectServer('.', 'root\CIMV2');
    Processes := Services.ExecQuery('SELECT ProcessId FROM Win32_Process WHERE Name = ''eurotrucks2.exe''');
    if Processes.Count > 0 then Result := CustomMessage('CloseGame');
  except
    Result := CustomMessage('ProcessCheckFailed');
  end;
end;

function ValidateGame(): String;
var ExePath, GameVersion: String;
begin
  Result := '';
  ExePath := AddBackslash(WizardDirValue) + 'bin\win_x64\eurotrucks2.exe';
  if not FileExists(ExePath) then begin Result := CustomMessage('InvalidGame'); Exit; end;
  try
    if not GetVersionNumbersString(ExePath, GameVersion) then
      Result := CustomMessage('UnsupportedGame');
  except
    Result := CustomMessage('GameReadFailed');
  end;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var Error: String;
begin
  Result := True;
  if (CurPageID = wpSelectDir) and (not WizardSilent) then begin
    Error := ValidateGame();
    if Error <> '' then begin MsgBox(Error, mbError, MB_OK); Result := False; end;
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := ClosedGameError();
  if Result = '' then Result := ValidateGame();
end;

function InitializeUninstall(): Boolean;
var Error: String;
begin
  Error := ClosedGameError();
  Result := Error = '';
  if not Result then SuppressibleMsgBox(Error, mbError, MB_OK, IDOK);
end;
