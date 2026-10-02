; pathnoworks.iss -- the Windows installer: PathNoWorks, and cppdecnet's
; decnetd to be the DECnet node it drives.  Built by build-installer.ps1,
; which stages the files first; see there.
;
; Installs for the current user, without administrator rights, and asks the
; same questions as cppdecnet's tools\install-decnetd.sh and .ps1: who the
; node is, how it reaches the network, whether it stays connected.  decnetd
; then runs from a Task Scheduler task when the user logs in.

#ifndef Stage
  #define Stage "..\..\build\stage"
#endif
#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif

[Setup]
AppId={{20F824E0-6626-4158-9D97-F2DF81C0F586}
AppName=PathNoWorks
AppVersion={#AppVersion}
AppPublisher=PathNoWorks
AppComments=Pathworks-style DECnet for Windows, with cppdecnet's decnetd
DefaultDirName={localappdata}\Programs\PathNoWorks
DefaultGroupName=PathNoWorks
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
OutputDir=..\..\Windows
OutputBaseFilename=PathNoWorks-{#AppVersion}-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\pathnoworks.exe
UninstallDisplayName=PathNoWorks
ChangesEnvironment=yes
CloseApplications=yes

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "addtopath"; Description: "Add the PathNoWorks tools (pnw-ncp, pnw-dir, ...) to my &PATH"
Name: "desktopicon"; Description: "Put PathNoWorks on the &desktop"; Flags: unchecked

[Files]
Source: "{#Stage}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Dirs]
Name: "{localappdata}\cppdecnet"

[Icons]
Name: "{group}\PathNoWorks"; Filename: "{app}\pathnoworks.exe"
Name: "{group}\DECnet node\Edit the node's configuration"; Filename: "{sys}\notepad.exe"; Parameters: """{localappdata}\cppdecnet\decnetd.conf"""
Name: "{group}\DECnet node\decnetd log"; Filename: "{sys}\notepad.exe"; Parameters: """{localappdata}\cppdecnet\decnetd.log"""
Name: "{group}\DECnet node\Restart decnetd"; Filename: "{sys}\WindowsPowerShell\v1.0\powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File ""{app}\decnetd-task.ps1"" -Action Restart"
Name: "{group}\Documentation"; Filename: "{app}\docs"
Name: "{group}\Uninstall PathNoWorks"; Filename: "{uninstallexe}"
Name: "{userdesktop}\PathNoWorks"; Filename: "{app}\pathnoworks.exe"; Tasks: desktopicon

[Registry]
; The tools on the user's PATH.
Root: HKCU; Subkey: "Environment"; ValueType: expandsz; ValueName: "Path"; \
    ValueData: "{olddata};{app}"; Tasks: addtopath; Check: NeedsAddPath(ExpandConstant('{app}'))

[Run]
Filename: "{app}\pathnoworks.exe"; Description: "Start PathNoWorks"; Flags: postinstall nowait skipifsilent

[UninstallRun]
Filename: "{sys}\WindowsPowerShell\v1.0\powershell.exe"; \
    Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\decnetd-task.ps1"" -Action Remove"; \
    Flags: runhidden waituntilterminated; RunOnceId: "RemoveDecnetdTask"

[UninstallDelete]
Type: files; Name: "{app}\*.log"

[Code]
var
  KeepPage: TInputOptionWizardPage;
  TypePage: TInputOptionWizardPage;
  NodePage: TInputQueryWizardPage;
  ModePage: TInputOptionWizardPage;
  PeerPage: TInputQueryWizardPage;
  ExtrasPage: TInputOptionWizardPage;
  NumbersPage: TInputQueryWizardPage;
  ConfDir, ConfFile, LogFile: String;

{ ------------------------------------------------------------- checks }

function IsDigits(S: String): Boolean;
var I: Integer;
begin
  Result := Length(S) > 0;
  for I := 1 to Length(S) do
    if (S[I] < '0') or (S[I] > '9') then Result := False;
end;

function ValidName(S: String): Boolean;
var I: Integer; Letter: Boolean;
begin
  Result := (Length(S) >= 1) and (Length(S) <= 6);
  Letter := False;
  for I := 1 to Length(S) do begin
    if ((S[I] >= 'A') and (S[I] <= 'Z')) or ((S[I] >= 'a') and (S[I] <= 'z')) then
      Letter := True
    else if (S[I] < '0') or (S[I] > '9') then
      Result := False;
  end;
  Result := Result and Letter;
end;

function ValidAddress(S: String): Boolean;
var P, Area, Node: Integer;
begin
  Result := False;
  P := Pos('.', S);
  if P = 0 then Exit;
  if not IsDigits(Copy(S, 1, P - 1)) or not IsDigits(Copy(S, P + 1, Length(S))) then Exit;
  Area := StrToIntDef(Copy(S, 1, P - 1), 0);
  Node := StrToIntDef(Copy(S, P + 1, Length(S)), 0);
  Result := (Area >= 1) and (Area <= 63) and (Node >= 1) and (Node <= 1023);
end;

function ValidPort(S: String): Boolean;
begin
  Result := IsDigits(S) and (StrToIntDef(S, 0) >= 1) and (StrToIntDef(S, 0) <= 65535);
end;

function ValidHost(S: String): Boolean;
var I: Integer;
begin
  Result := Length(S) > 0;
  for I := 1 to Length(S) do
    if Pos(S[I], 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.:_-') = 0 then
      Result := False;
end;

function NeedsAddPath(Dir: String): Boolean;
var Path: String;
begin
  if not RegQueryStringValue(HKCU, 'Environment', 'Path', Path) then begin
    Result := True;
    Exit;
  end;
  Result := Pos(';' + Uppercase(Dir) + ';', ';' + Uppercase(Path) + ';') = 0;
end;

function Listening: Boolean;
begin
  Result := ModePage.SelectedValueIndex = 1;
end;

function KeepingConfig: Boolean;
begin
  Result := (KeepPage <> nil) and (KeepPage.SelectedValueIndex = 0);
end;

{ ------------------------------------------------------------- pages }

procedure InitializeWizard;
begin
  ConfDir := ExpandConstant('{localappdata}\cppdecnet');
  ConfFile := ConfDir + '\decnetd.conf';
  LogFile := ConfDir + '\decnetd.log';

  KeepPage := nil;
  if FileExists(ConfFile) then begin
    KeepPage := CreateInputOptionPage(wpSelectTasks,
      'DECnet node', 'This PC is a DECnet node already.',
      'There is a node configuration in ' + ConfFile + '. Keep it, or set the node up again? ' +
      'Setting it up again keeps the old one as a .bak file.', True, False);
    KeepPage.Add('Keep it');
    KeepPage.Add('Set the node up again');
    KeepPage.SelectedValueIndex := 0;
  end;

  if KeepPage <> nil then
    TypePage := CreateInputOptionPage(KeepPage.ID, '', '', '', True, False)
  else
    TypePage := CreateInputOptionPage(wpSelectTasks, '', '', '', True, False);
  TypePage.Caption := 'This node';
  TypePage.Description := 'What kind of DECnet node is this PC?';
  TypePage.SubCaptionLabel.Caption :=
    'An endnode is right for a desktop: one circuit, to a router that looks after ' +
    'the rest of the network. A router joins circuits together and carries other ' +
    'nodes'' traffic.';
  TypePage.Add('Endnode');
  TypePage.Add('Level 1 router (routes within its area)');
  TypePage.Add('Level 2 router (an area router)');
  TypePage.SelectedValueIndex := 0;

  NodePage := CreateInputQueryPage(TypePage.ID, 'This node', 'Its name and address',
    'On HECnet, your area''s coordinator gives you an address; elsewhere, pick one ' +
    'nobody on your network is using.');
  NodePage.Add('Node name (one to six letters and digits):', False);
  NodePage.Add('Node address (area.node, like 29.151):', False);

  ModePage := CreateInputOptionPage(NodePage.ID, 'Reaching the network',
    'How does this node reach the rest of DECnet?',
    'Multinet carries DECnet over TCP, the way HECnet nodes link up. (DECnet ' +
    'straight on an Ethernet needs a packet driver, which decnetd doesn''t use on ' +
    'Windows.)', True, False);
  ModePage.Add('Connect to a peer that listens (the usual for an endnode)');
  ModePage.Add('Listen for a peer that connects to us');
  ModePage.SelectedValueIndex := 0;

  PeerPage := CreateInputQueryPage(ModePage.ID, 'Reaching the network', 'The other end',
    'Naming the node at the other end lets you use its name; leave its DECnet ' +
    'address and name empty if you don''t know them.');
  PeerPage.Add('The peer''s IP address or host name:', False);
  PeerPage.Add('Port:', False);
  PeerPage.Add('Its DECnet address (optional):', False);
  PeerPage.Add('Its DECnet name (optional):', False);
  PeerPage.Values[1] := '7100';

  ExtrasPage := CreateInputOptionPage(PeerPage.ID, 'Staying connected, and names',
    'A few choices about how the node behaves',
    'A desktop node can stay off the network until PathNoWorks needs it: the ' +
    'circuit comes up when a program first connects, and goes down once none has ' +
    'been connected for a while.', False, False);
  ExtrasPage.Add('Disconnect when nothing has used it for a while (endnodes)');
  ExtrasPage.Add('Keep HECnet''s list of node names, fetched weekly from MIM');
  ExtrasPage.Add('Learn node names from the network');
  ExtrasPage.Add('Serve the monitoring web pages');
  ExtrasPage.Values[2] := True;

  NumbersPage := CreateInputQueryPage(ExtrasPage.ID, 'Staying connected, and names',
    'The numbers', '');
  NumbersPage.Add('Disconnect after this many idle minutes:', False);
  NumbersPage.Add('Monitoring pages'' port:', False);
  NumbersPage.Values[0] := '120';
  NumbersPage.Values[1] := '8102';
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := False;
  if (PageID = TypePage.ID) or (PageID = NodePage.ID) or (PageID = ModePage.ID) or
     (PageID = PeerPage.ID) or (PageID = ExtrasPage.ID) or (PageID = NumbersPage.ID) then
    Result := KeepingConfig;
  if (PageID = NumbersPage.ID) and not Result then
    Result := not ((ExtrasPage.Values[0] and (TypePage.SelectedValueIndex = 0)) or ExtrasPage.Values[3]);
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID = PeerPage.ID then begin
    if Listening then
      PeerPage.PromptLabels[0].Caption := 'The peer''s IP address (empty: take a connection from anywhere):'
    else
      PeerPage.PromptLabels[0].Caption := 'The peer''s IP address or host name:';
  end;
  if CurPageID = NumbersPage.ID then begin
    NumbersPage.Edits[0].Enabled := ExtrasPage.Values[0] and (TypePage.SelectedValueIndex = 0);
    NumbersPage.Edits[1].Enabled := ExtrasPage.Values[3];
  end;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var S: String;
begin
  Result := True;
  if CurPageID = NodePage.ID then begin
    if not ValidName(Trim(NodePage.Values[0])) then begin
      MsgBox('A node name is one to six letters and digits, with at least one letter.', mbError, MB_OK);
      Result := False;
    end else if not ValidAddress(Trim(NodePage.Values[1])) then begin
      MsgBox('An address is area.node: area 1 to 63, node 1 to 1023, like 29.151.', mbError, MB_OK);
      Result := False;
    end;
  end else if CurPageID = PeerPage.ID then begin
    S := Trim(PeerPage.Values[0]);
    if not ((S = '') and Listening) and not ValidHost(S) then begin
      MsgBox('Give the peer''s IP address or host name.', mbError, MB_OK);
      Result := False;
    end else if not ValidPort(Trim(PeerPage.Values[1])) then begin
      MsgBox('A port is a number from 1 to 65535.', mbError, MB_OK);
      Result := False;
    end else if (Trim(PeerPage.Values[2]) <> '') and not ValidAddress(Trim(PeerPage.Values[2])) then begin
      MsgBox('The DECnet address is area.node, like 29.150, or empty.', mbError, MB_OK);
      Result := False;
    end else if (Trim(PeerPage.Values[3]) <> '') and not ValidName(Trim(PeerPage.Values[3])) then begin
      MsgBox('The DECnet name is one to six letters and digits, or empty.', mbError, MB_OK);
      Result := False;
    end;
  end else if CurPageID = NumbersPage.ID then begin
    if NumbersPage.Edits[0].Enabled and
       (not IsDigits(Trim(NumbersPage.Values[0])) or (StrToIntDef(Trim(NumbersPage.Values[0]), 0) < 1)) then begin
      MsgBox('The idle time is a number of minutes, at least 1.', mbError, MB_OK);
      Result := False;
    end else if NumbersPage.Edits[1].Enabled and not ValidPort(Trim(NumbersPage.Values[1])) then begin
      MsgBox('A port is a number from 1 to 65535.', mbError, MB_OK);
      Result := False;
    end;
  end;
end;

{ ------------------------------------------------------------- the config }

{ A path for the configuration: forward slashes, which decnetd takes on
  Windows too, and quoted, since a profile's path may have a space. }
function ConfPath(P: String): String;
begin
  StringChangeEx(P, '\', '/', True);
  Result := '"' + P + '"';
end;

function NodeType: String;
begin
  case TypePage.SelectedValueIndex of
    0: Result := 'endnode';
    1: Result := 'l1router';
  else Result := 'l2router';
  end;
end;

function ConfigText: String;
var NL, Mode, PeerAddr, PeerName: String;
begin
  NL := #10;
  if Listening then Mode := 'listen' else Mode := 'connect';
  PeerAddr := Trim(PeerPage.Values[2]);
  PeerName := Uppercase(Trim(PeerPage.Values[3]));
  Result :=
    '# decnetd.conf -- written by the PathNoWorks installer, ' +
      GetDateTimeString('yyyy-mm-dd hh:nn', '-', ':') + '.' + NL +
    '# Edit freely (Start menu > PathNoWorks > DECnet node), then restart' + NL +
    '# decnetd from the same menu.' + NL + NL +
    'routing ' + Trim(NodePage.Values[1]) + ' --type ' + NodeType + NL + NL +
    'node ' + Trim(NodePage.Values[1]) + ' ' + Uppercase(Trim(NodePage.Values[0])) + NL;
  if (PeerAddr <> '') and (PeerName <> '') then
    Result := Result + 'node ' + PeerAddr + ' ' + PeerName + NL;
  if ExtrasPage.Values[1] then
    Result := Result + 'node @hecnet --cache ' + ConfPath(ConfDir + '\hecnet.dat') + NL;
  if ExtrasPage.Values[2] then
    Result := Result + 'node @neighbours' + NL;
  Result := Result + NL +
    'circuit mul-0 Multinet ' + Trim(PeerPage.Values[0]) + ':' + Trim(PeerPage.Values[1]) +
      ':' + Mode + ' --t3 15' + NL + NL +
    '# The socket the PathNoWorks tools look for, decnetapi.sock in %TEMP%.' + NL +
    '# Whoever can open it acts as this node.' + NL +
    'api';
  if ExtrasPage.Values[0] and (TypePage.SelectedValueIndex = 0) then
    Result := Result + ' --on-demand --idle ' +
      IntToStr(StrToIntDef(Trim(NumbersPage.Values[0]), 120) * 60);
  Result := Result + NL;
  if ExtrasPage.Values[3] then
    Result := Result + NL + '# Monitoring pages: on all interfaces, with no login.' + NL +
      'http --http-port ' + Trim(NumbersPage.Values[1]) + NL;
end;

function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfo, MemoTypeInfo,
  MemoComponentsInfo, MemoGroupInfo, MemoTasksInfo: String): String;
var C: String;
begin
  Result := MemoDirInfo + NewLine + NewLine;
  if MemoTasksInfo <> '' then Result := Result + MemoTasksInfo + NewLine + NewLine;
  if KeepingConfig then
    Result := Result + 'DECnet node:' + NewLine + Space + 'keep ' + ConfFile + NewLine
  else begin
    C := ConfigText;
    StringChangeEx(C, #10, NewLine + Space, True);
    Result := Result + 'DECnet node, ' + ConfFile + ':' + NewLine + Space + C;
  end;
  Result := Result + NewLine + 'decnetd runs as you, from the task "cppdecnet decnetd", ' +
    'when you log in.';
end;

{ ------------------------------------------------------------- installing }

procedure CurStepChanged(CurStep: TSetupStep);
var Rc: Integer; Rule: String;
begin
  if CurStep <> ssPostInstall then Exit;

  if not KeepingConfig then begin
    ForceDirectories(ConfDir);
    if FileExists(ConfFile) then
      FileCopy(ConfFile, ConfFile + '.' + GetDateTimeString('yyyymmdd-hhnnss', #0, #0) + '.bak', False);
    if not SaveStringToFile(ConfFile, ConfigText, False) then
      MsgBox('Could not write ' + ConfFile + '.', mbError, MB_OK);
  end;

  WizardForm.StatusLabel.Caption := 'Starting decnetd...';
  if not Exec(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'),
       '-NoProfile -ExecutionPolicy Bypass -File "' + ExpandConstant('{app}\decnetd-task.ps1') +
       '" -Action Register -Exe "' + ExpandConstant('{app}\decnetd.exe') +
       '" -Conf "' + ConfFile + '" -Log "' + LogFile + '"',
       '', SW_HIDE, ewWaitUntilTerminated, Rc) or (Rc <> 0) then
    MsgBox('decnetd''s task could not be set up (code ' + IntToStr(Rc) + '). ' +
           'Try Start menu > PathNoWorks > DECnet node > Restart decnetd.', mbError, MB_OK);

  { A listening circuit wants a firewall rule to let its peer in, and that
    needs administrator rights: ask, and let Windows ask for them. }
  if not KeepingConfig and Listening then
    if MsgBox('Windows Firewall will block the peer from connecting to port ' +
              Trim(PeerPage.Values[1]) + ' unless it''s allowed. Allow it? ' +
              '(Windows will ask for administrator rights.)', mbConfirmation, MB_YESNO) = IDYES then begin
      Rule := 'Remove-NetFirewallRule -DisplayName ''cppdecnet decnetd'' -ErrorAction SilentlyContinue; ' +
              'New-NetFirewallRule -DisplayName ''cppdecnet decnetd'' -Direction Inbound -Protocol TCP ' +
              '-LocalPort ' + Trim(PeerPage.Values[1]) + ' -Program ''' +
              ExpandConstant('{app}\decnetd.exe') + ''' -Action Allow -Profile Private,Domain';
      ShellExec('runas', ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'),
                '-NoProfile -WindowStyle Hidden -Command "' + Rule + '"', '', SW_HIDE,
                ewWaitUntilTerminated, Rc);
    end;
end;

{ Take the tools back off the PATH. }
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var Path, Dir: String; P: Integer;
begin
  if CurUninstallStep <> usPostUninstall then Exit;
  if not RegQueryStringValue(HKCU, 'Environment', 'Path', Path) then Exit;
  Dir := ExpandConstant('{app}');
  P := Pos(';' + Uppercase(Dir), Uppercase(Path));
  if P > 0 then begin
    Delete(Path, P, Length(Dir) + 1);
    RegWriteExpandStringValue(HKCU, 'Environment', 'Path', Path);
  end;
end;
