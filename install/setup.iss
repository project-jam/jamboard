[Setup]
AppName=JamBoard
AppVersion=3.3
AppPublisher=Project Jam
DefaultDirName={autopf}\JamBoard
DefaultGroupName=JamBoard
UninstallDisplayIcon={app}\jamboard.exe
OutputDir=.
OutputBaseFilename=JamBoard_Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern dynamic
WizardResizable=yes
DisableWelcomePage=no
; Uncomment and provide path when you have the icon:
; SetupIconFile=..\jamboard.ico

[Dirs]
Name: "{app}\sounds"

[Files]
Source: "..\jamboard.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\*.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\yt-dlp.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\zpix.ttf"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\JamBoard\JamBoard"; Filename: "{app}\jamboard.exe"
Name: "{autodesktop}\JamBoard"; Filename: "{app}\jamboard.exe"
Name: "{autoprograms}\JamBoard\Uninstall JamBoard"; Filename: "{uninstallexe}"

[Run]
Filename: "{app}\jamboard.exe"; Description: "Launch JamBoard"; Flags: postinstall nowait skipifsilent

[Code]
var
  InfoPage: TWizardPage;
  InfoLabel: TNewStaticText;

procedure InitializeWizard;
begin
  InfoPage := CreateCustomPage(wpWelcome, 'Important Notice', 'Requirements for JamBoard');

  InfoLabel := TNewStaticText.Create(InfoPage);
  InfoLabel.Parent := InfoPage.Surface;
  InfoLabel.Left := 8;
  InfoLabel.Top := 8;
  InfoLabel.Width := InfoPage.SurfaceWidth - 16;
  InfoLabel.AutoSize := False;
  InfoLabel.WordWrap := True;
  InfoLabel.Caption :=
    'A virtual audio cable (e.g. VB-Cable) is required for the ' +
    'secondary output feature (streaming/recording).'#13#10#13#10 +
    'If you don'#39't have one, continue installing Jamboard, ' +
    'then install a virtual audio cable after that as it is required to have one.';
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID = InfoPage.ID then
  begin
    InfoLabel.Width := InfoPage.SurfaceWidth - 16;
    InfoLabel.Height := InfoPage.SurfaceHeight - 16;
  end;
end;
