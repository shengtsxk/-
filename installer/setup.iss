; 灵境 Lingjing 安装脚本 (Inno Setup)
#define MyAppName "灵境Lingjing"
#define MyAppVersion "1.3.8"
#define MyAppPublisher "LingjingProject"
#define MyAppExeName "Lingjing.exe"
#define MyAppIcon "D:\lingjing\assets\icons\app.ico"
#define PackageDir "D:\lingjing\package"
#define VcRedist "C:\Qt\vcredist\vc14.50.35719_VC_redist.x64.exe"
#define OutputBase "灵境Lingjing安装程序"

[Setup]
AppId={{F9AF3F3E-F00D-4B6E-A737-41DBDECD2455}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={localappdata}\Programs\灵境Lingjing
DefaultGroupName=灵境Lingjing
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
OutputDir=C:\Users\Asgard\Desktop
OutputBaseFilename={#OutputBase}
SetupIconFile={#MyAppIcon}
UninstallDisplayIcon={app}\{#MyAppExeName}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible

[Languages]
Name: "chinesesimplified"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "{#PackageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#VcRedist}"; DestDir: "{tmp}"; Flags: deleteafterinstall

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{tmp}\vc14.50.35719_VC_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "正在安装 Microsoft Visual C++ 运行库..."; Flags: waituntilterminated
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{app}"
