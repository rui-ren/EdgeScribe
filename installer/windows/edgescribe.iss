; EDGESCRIBE Installer Script (Inno Setup)
; Compiles to EDGESCRIBESetup.exe
; Download Inno Setup: https://jrsoftware.org/isinfo.php

#define MyAppName "EDGESCRIBE"
#define MyAppVersion "0.1.0"
#define MyAppPublisher "EDGESCRIBE"
#define MyAppURL "https://github.com/EDGESCRIBE/EDGESCRIBE"
#define MyAppExeName "EDGESCRIBE.exe"

[Setup]
AppId={{A1B2C3D4-E5F6-7890-ABCD-EF1234567890}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}/releases
DefaultDirName={localappdata}\Programs\{#MyAppName}
DefaultGroupName={#MyAppName}
; No admin required — installs to user's AppData
PrivilegesRequired=lowest
OutputBaseFilename=EDGESCRIBESetup
Compression=lzma2
SolidCompression=yes
; Modern installer UI
WizardStyle=modern
; Auto-add to user PATH
ChangesEnvironment=yes
; Uninstaller
UninstallDisplayName={#MyAppName}
; Minimum Windows 10
MinVersion=10.0

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
; Main executable and DLLs
Source: "build\Release\EDGESCRIBE.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "build\Release\onnxruntime-genai.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "build\Release\onnxruntime.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "build\Release\onnxruntime_providers_shared.dll"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
; Docs
Source: "README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "LICENSE"; DestDir: "{app}"; Flags: ignoreversion

[Registry]
; Add install dir to user PATH (no admin needed)
Root: HKCU; Subkey: "Environment"; ValueType: expandsz; ValueName: "Path"; \
  ValueData: "{olddata};{app}"; Check: NeedsAddPath(ExpandConstant('{app}'))

[Run]
; After install, show a quick message
Filename: "{app}\{#MyAppExeName}"; Parameters: "--version"; \
  Flags: nowait postinstall skipifsilent runhidden

[UninstallDelete]
Type: filesandordirs; Name: "{app}"

[Messages]
FinishedLabel=EDGESCRIBE has been installed. Open any terminal and run:%n%n  EDGESCRIBE pull nemotron%n  EDGESCRIBE run --live%n%nYou may need to restart your terminal for PATH changes to take effect.

[Code]
// Check if the path already contains our directory
function NeedsAddPath(Param: string): boolean;
var
  OrigPath: string;
begin
  if not RegQueryStringValue(HKEY_CURRENT_USER,
    'Environment', 'Path', OrigPath) then
  begin
    Result := True;
    exit;
  end;
  // Look for the path in the existing PATH (case-insensitive)
  Result := Pos(';' + Uppercase(Param) + ';',
    ';' + Uppercase(OrigPath) + ';') = 0;
end;

// Remove from PATH on uninstall
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  OrigPath: string;
  AppDir: string;
  P: Integer;
begin
  if CurUninstallStep = usPostUninstall then
  begin
    if RegQueryStringValue(HKEY_CURRENT_USER,
      'Environment', 'Path', OrigPath) then
    begin
      AppDir := ExpandConstant('{app}');
      P := Pos(';' + Uppercase(AppDir), ';' + Uppercase(OrigPath));
      if P > 0 then
      begin
        Delete(OrigPath, P - 1, Length(AppDir) + 1);
        RegWriteStringValue(HKEY_CURRENT_USER,
          'Environment', 'Path', OrigPath);
      end;
    end;
  end;
end;
