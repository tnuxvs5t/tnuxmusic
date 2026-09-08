[Setup]
AppId={{6A68A2DA-7ED0-4BB9-9368-71B16E23E40F}}
AppName=tnuxmusic
AppVersion={#AppVersion}
AppPublisher=tnuxmusic
DefaultDirName={autopf}\tnuxmusic
DefaultGroupName=tnuxmusic
OutputDir=..\..\dist
OutputBaseFilename=tnuxmusic-{#ReleaseTag}-windows-x64-setup
SetupIconFile=..\icons\tnuxmusic.ico
Compression=lzma
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional icons:"; Flags: unchecked

[Files]
Source: "{#StageBin}\*"; DestDir: "{app}\bin"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\tnuxmusic"; Filename: "{app}\bin\tnuxmusic.exe"
Name: "{commondesktop}\tnuxmusic"; Filename: "{app}\bin\tnuxmusic.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\bin\tnuxmusic.exe"; Description: "Launch tnuxmusic"; Flags: nowait postinstall skipifsilent
