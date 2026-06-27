; -- femm-qt.iss --
; Inno Setup script for FEMM 5.0 (Qt port)

#define MyDate GetDateTimeString('ddmmmyyyy', '', '')
#define MyYear GetDateTimeString('yyyy', '', '')

[Setup]
AppName           = FEMM
AppVerName        = femm 5.0 {#MyDate}
AppCopyright      = Copyright © 1998-{#MyYear}, DC Meeker
AppPublisher      = D. Meeker
DefaultDirName    = c:\femm-qt
DefaultGroupName  = femm 5.0
UsePreviousAppDir = no
UsePreviousGroup  = no
LicenseFile       = ..\packaging\license.txt
OutputBaseFilename= femm-qt-py_{#MyDate}
; This script lives in femm-qt\packaging\.  Relative [Files]/[Dirs] paths
; are resolved against SourceDir; "bin\..." -> build\bin, "..\foo" ->
; femm-qt\foo, exactly as when the script lived in build\.  The finished
; installer is written to femm-qt\packaging\output\.
SourceDir         = ..\build
OutputDir         = ..\packaging\output
ChangesEnvironment= yes
PrivilegesRequired= lowest
PrivilegesRequiredOverridesAllowed= dialog

[Tasks]
; Optional, OFF by default. Adds {app}\bin to PATH so femm-lua, the solvers,
; and femm_gui can be run by bare name from any folder. FEMM_DIR (set
; unconditionally in [Registry] below) is what the GUI and the language
; bindings actually use to locate the executables -- this task is purely
; command-line convenience, and it is removed again on uninstall (see [Code]).
Name: "addtopath"; Description: "Add femm-qt to the PATH (run femm-lua and the CLI solvers from any folder)"; Flags: unchecked

[Dirs]
Name: "{app}\bin"
Name: "{app}\bin\platforms"
Name: "{app}\bin\styles"
Name: "{app}\bin\imageformats"
Name: "{app}\bin\iconengines"
Name: "{app}\examples"
Name: "{app}\mfiles"
Name: "{app}\mathfemm"

[Files]
; FEMM executables
Source: "bin\femm_gui.exe";                 DestDir: "{app}\bin"; Flags: ignoreversion
Source: "bin\femm_headless.exe";            DestDir: "{app}\bin"; Flags: ignoreversion
Source: "bin\femm_headlessw.exe";            DestDir: "{app}\bin"; Flags: ignoreversion
Source: "bin\fkn.exe";                      DestDir: "{app}\bin"; Flags: ignoreversion
Source: "bin\belasolv.exe";                 DestDir: "{app}\bin"; Flags: ignoreversion
Source: "bin\hsolv.exe";                    DestDir: "{app}\bin"; Flags: ignoreversion
Source: "bin\csolv.exe";                    DestDir: "{app}\bin"; Flags: ignoreversion

; Lua 4 legacy scripting client (interpreter)
Source: "bin\lua4.exe";                     DestDir: "{app}\bin"; Flags: ignoreversion

; Qt runtime DLLs (in bin alongside the exe)
Source: "C:\Qt\6.10.3\mingw_64\bin\Qt6Core.dll";          DestDir: "{app}\bin"; Flags: ignoreversion
Source: "C:\Qt\6.10.3\mingw_64\bin\Qt6Gui.dll";           DestDir: "{app}\bin"; Flags: ignoreversion
Source: "C:\Qt\6.10.3\mingw_64\bin\Qt6Widgets.dll";       DestDir: "{app}\bin"; Flags: ignoreversion

; MinGW runtime DLLs
Source: "C:\Qt\6.10.3\mingw_64\bin\libgcc_s_seh-1.dll";   DestDir: "{app}\bin"; Flags: ignoreversion
Source: "C:\Qt\6.10.3\mingw_64\bin\libstdc++-6.dll";      DestDir: "{app}\bin"; Flags: ignoreversion
Source: "C:\Qt\6.10.3\mingw_64\bin\libwinpthread-1.dll";  DestDir: "{app}\bin"; Flags: ignoreversion
Source: "C:\Qt\Tools\mingw1310_64\bin\libgomp-1.dll";     DestDir: "{app}\bin"; Flags: ignoreversion

; Qt platform plugin (required for any Qt GUI app on Windows)
Source: "C:\Qt\6.10.3\mingw_64\plugins\platforms\qwindows.dll";          DestDir: "{app}\bin\platforms"; Flags: ignoreversion

; Qt style plugin (native Windows look)
Source: "C:\Qt\6.10.3\mingw_64\plugins\styles\qmodernwindowsstyle.dll";  DestDir: "{app}\bin\styles"; Flags: ignoreversion

; Qt image format plugins (for icons, bitmaps)
Source: "C:\Qt\6.10.3\mingw_64\plugins\imageformats\qico.dll";          DestDir: "{app}\bin\imageformats"; Flags: ignoreversion
Source: "C:\Qt\6.10.3\mingw_64\plugins\imageformats\qjpeg.dll";         DestDir: "{app}\bin\imageformats"; Flags: ignoreversion
Source: "C:\Qt\6.10.3\mingw_64\plugins\imageformats\qgif.dll";          DestDir: "{app}\bin\imageformats"; Flags: ignoreversion
Source: "C:\Qt\6.10.3\mingw_64\plugins\imageformats\qsvg.dll";          DestDir: "{app}\bin\imageformats"; Flags: ignoreversion

; Qt SVG icon engine (if icons use SVG)
Source: "C:\Qt\6.10.3\mingw_64\plugins\iconengines\qsvgicon.dll";       DestDir: "{app}\bin\iconengines"; Flags: ignoreversion
Source: "C:\Qt\6.10.3\mingw_64\bin\Qt6Svg.dll";                         DestDir: "{app}\bin"; Flags: ignoreversion

; Material libraries (don't overwrite user-modified copies)
Source: "..\matlibs\matlib.dat";                   DestDir: "{app}\bin"; Flags: confirmoverwrite
Source: "..\matlibs\statlib.dat";                  DestDir: "{app}\bin"; Flags: confirmoverwrite
Source: "..\matlibs\heatlib.dat";                  DestDir: "{app}\bin"; Flags: confirmoverwrite
Source: "..\matlibs\condlib.dat";                  DestDir: "{app}\bin"; Flags: confirmoverwrite

; Lua 4 legacy scripting client (binding, launcher, third-party notices)
Source: "..\luafemm\femm.lua";                     DestDir: "{app}\bin"; Flags: ignoreversion
Source: "..\luafemm\femm-lua.bat";                 DestDir: "{app}\bin"; Flags: ignoreversion
Source: "..\lua\liblua\COPYRIGHT";                 DestDir: "{app}\bin"; DestName: "Lua-COPYRIGHT.txt";       Flags: ignoreversion
Source: "..\lua\luasocket-1.0\COPYRIGHT";          DestDir: "{app}\bin"; DestName: "LuaSocket-COPYRIGHT.txt"; Flags: ignoreversion

; Documentation
Source: "..\manual\manual.pdf";                DestDir: "{app}\bin"; Flags: ignoreversion
Source: "..\LICENSE-MIT";                   DestDir: "{app}\bin"; Flags: ignoreversion
Source: "..\LICENSE-GPL";                   DestDir: "{app}\bin"; Flags: ignoreversion
Source: "..\NOTICE-Qt.txt";                 DestDir: "{app}\bin"; Flags: ignoreversion
; Third-party MIT notices (compiled into the binaries)
Source: "..\micropython\LICENSE.micropython"; DestDir: "{app}\bin"; DestName: "LICENSE-MicroPython.txt"; Flags: ignoreversion
Source: "..\micropython\ulab\LICENSE";      DestDir: "{app}\bin"; DestName: "LICENSE-ulab.txt"; Flags: ignoreversion
Source: "..\LICENSE-melihaltun.txt";        DestDir: "{app}\bin"; Flags: ignoreversion
Source: "..\readme.txt";                    DestDir: "{app}"    ; Flags: ignoreversion isreadme

; Example files
Source: "..\examples\RadialMagneticBearing.fem";    DestDir: "{app}\examples"; Flags: ignoreversion
Source: "..\examples\CoggingTorque.fem";            DestDir: "{app}\examples"; Flags: ignoreversion
Source: "..\examples\SpeakerMotor.fem";             DestDir: "{app}\examples"; Flags: ignoreversion
Source: "..\examples\Roters-Ch9Fig6.fem";           DestDir: "{app}\examples"; Flags: ignoreversion
Source: "..\examples\Force-vs-Position.lua";        DestDir: "{app}\examples"; Flags: ignoreversion
Source: "..\examples\DoubleSidedLIM.fem";           DestDir: "{app}\examples"; Flags: ignoreversion
Source: "..\examples\ThrustBearing.fem";            DestDir: "{app}\examples"; Flags: ignoreversion
Source: "..\examples\TEAMProblem28.fem";            DestDir: "{app}\examples"; Flags: ignoreversion
Source: "..\examples\KelvinTransformation.fem";     DestDir: "{app}\examples"; Flags: ignoreversion
Source: "..\examples\tutorial.fem" ;                DestDir: "{app}\examples"; Flags: ignoreversion
Source: "..\examples\ProximityExact.fem" ;          DestDir: "{app}\examples"; Flags: ignoreversion
Source: "..\examples\ProximityApprox.fem"  ;        DestDir: "{app}\examples"; Flags: ignoreversion
Source: "..\examples\bdemo1.fee";                   DestDir: "{app}\examples"; Flags: ignoreversion
Source: "..\examples\bdemo2.fee";                   DestDir: "{app}\examples"; Flags: ignoreversion
Source: "..\examples\htutor.feh";                   DestDir: "{app}\examples"; Flags: ignoreversion
Source: "..\examples\ACElec2.fec";                  DestDir: "{app}\examples"; Flags: ignoreversion
Source: "..\examples\ACElec2a.fec";                 DestDir: "{app}\examples"; Flags: ignoreversion
Source: "..\examples\ACElec3.fec";                  DestDir: "{app}\examples"; Flags: ignoreversion
Source: "..\examples\cond_layer.FEC";               DestDir: "{app}\examples"; Flags: ignoreversion
Source: "..\examples\lrk.fem";                      DestDir: "{app}\examples"; Flags: ignoreversion
Source: "..\examples\IABC.fem";                     DestDir: "{app}\examples"; Flags: ignoreversion

; OctaveFEMM / MATLAB interface
Source: "..\octavefemm\manual\octavefemm.pdf";              DestDir: "{app}"; Flags: ignoreversion
Source: "..\octavefemm\examples\demo2.m";                   DestDir: "{app}\examples"; Flags: ignoreversion
Source: "..\octavefemm\examples\roters1b.m";                DestDir: "{app}\examples"; Flags: ignoreversion
Source: "..\octavefemm\examples\roters1b.fem";              DestDir: "{app}\examples"; Flags: ignoreversion
Source: "..\octavefemm\examples\strips.m";                  DestDir: "{app}\examples"; Flags: ignoreversion
Source: "..\octavefemm\examples\htutor.m";                  DestDir: "{app}\examples"; Flags: ignoreversion
Source: "..\octavefemm\examples\ACElec2.m";                 DestDir: "{app}\examples"; Flags: ignoreversion
Source: "..\octavefemm\*.m";                                DestDir: "{app}\mfiles"; Flags: ignoreversion

; MathFEMM (Mathematica interface)
Source: "..\mathfemm\mathfemm.m";                    DestDir: "{app}\mathfemm"; Flags: ignoreversion;
Source: "..\mathfemm\usage.nb";                      DestDir: "{app}";          Flags: ignoreversion;
Source: "..\mathfemm\examples\MathFEMM-Demo2.nb";    DestDir: "{app}\examples"; Flags: ignoreversion;
Source: "..\mathfemm\examples\roters1b.nb";          DestDir: "{app}\examples"; Flags: ignoreversion;
Source: "..\mathfemm\examples\strips.nb";            DestDir: "{app}\examples"; Flags: ignoreversion;
Source: "..\mathfemm\examples\htutor.nb";            DestDir: "{app}\examples"; Flags: ignoreversion;

; Scilab interface
Source: "..\scifemm\scifemm.sci";                    DestDir: "{app}\scifemm";  Flags: ignoreversion
Source: "..\scifemm\examples\demo2.sce";             DestDir: "{app}\examples"; Flags: ignoreversion
Source: "..\scifemm\examples\roters1b.sce";          DestDir: "{app}\examples"; Flags: ignoreversion
Source: "..\scifemm\examples\strips.sce";            DestDir: "{app}\examples"; Flags: ignoreversion
Source: "..\scifemm\examples\htutor.sce";            DestDir: "{app}\examples"; Flags: ignoreversion
Source: "..\scifemm\examples\ACElec2.sce";           DestDir: "{app}\examples"; Flags: ignoreversion

; Julia interface
Source: "..\juliafemm\juliafemm.jl";                  DestDir: "{app}\juliafemm";  Flags: ignoreversion
Source: "..\juliafemm\examples\demo2.jl";             DestDir: "{app}\examples"; Flags: ignoreversion
Source: "..\juliafemm\examples\coilgun.jl";           DestDir: "{app}\examples"; Flags: ignoreversion
Source: "..\juliafemm\examples\roters1b.jl";          DestDir: "{app}\examples"; Flags: ignoreversion
Source: "..\juliafemm\examples\strips.jl";            DestDir: "{app}\examples"; Flags: ignoreversion
Source: "..\juliafemm\examples\htutor.jl";            DestDir: "{app}\examples"; Flags: ignoreversion
Source: "..\juliafemm\examples\ACElec2.jl";           DestDir: "{app}\examples"; Flags: ignoreversion

; Lua 4 interface. The functional client (lua4.exe, femm.lua, femm-lua.bat)
; ships in {app}\bin above, because femm-lua.bat loads femm.lua / lua4.exe from
; its own directory and points FEMM_DIR there — so they must all sit together in
; bin. Here we only add the docs and examples (no second femm.lua copy in
; {app}\luafemm, which would be orphaned and could drift from the bin copy).
Source: "..\luafemm\README.md";                       DestDir: "{app}\luafemm";  Flags: ignoreversion
Source: "..\luafemm\femm-lua.pdf";                    DestDir: "{app}\luafemm";  Flags: ignoreversion
Source: "..\luafemm\examples\ac_magnetics.lua";       DestDir: "{app}\examples"; Flags: ignoreversion
Source: "..\luafemm\examples\coilgun.lua";            DestDir: "{app}\examples"; Flags: ignoreversion

[Icons]
Name: "{group}\FEMM 5.0"; Filename: "{app}\bin\femm_gui.exe";       WorkingDir: "{app}\examples"
Name: "{group}\Manual";   Filename: "{app}\bin\manual.pdf"
Name: "{group}\Tutorial-Magnetic";       Filename: "http://www.femm.info/wiki/MagneticsTutorial"
Name: "{group}\Tutorial-Electrostatic";  Filename: "http://www.femm.info/wiki/ElectrostaticsTutorial"
Name: "{group}\Tutorial-Heat Flow";      Filename: "http://www.femm.info/wiki/HeatFlowTutorial"
Name: "{group}\OctaveFEMM Manual"; Filename: "{app}\octavefemm.pdf"; WorkingDir: "{app}"
Name: "{group}\MathFEMM Manual";   Filename: "{app}\usage.nb";       WorkingDir: "{app}"
Name: "{group}\Lua Scripting Guide"; Filename: "{app}\luafemm\femm-lua.pdf"; WorkingDir: "{app}\luafemm"
Name: "{group}\femm-lua (Lua console)"; Filename: "{app}\bin\femm-lua.bat"; WorkingDir: "{app}\examples"

[Registry]
; Set FEMM_DIR so octavefemm/mathfemm/pyfemm can find the executables.
; Points to the bin directory where femm_headless.exe lives.
; "Install for all users" -> HKLM system environment (admin)
; "Install for me only"   -> HKCU user environment (no admin)
Root: HKLM; Subkey: "SYSTEM\CurrentControlSet\Control\Session Manager\Environment"; \
    ValueType: string; ValueName: "FEMM_DIR"; ValueData: "{app}\bin"; \
    Flags: uninsdeletevalue; Check: IsAdminInstallMode
Root: HKCU; Subkey: "Environment"; \
    ValueType: string; ValueName: "FEMM_DIR"; ValueData: "{app}\bin"; \
    Flags: uninsdeletevalue; Check: not IsAdminInstallMode

[Code]
// Notify Windows that environment variables have changed so new shells pick them up
const
    WM_SETTINGCHANGE = $001A;
    SMTO_ABORTIFHUNG = $0002;
    // PATH lives in the system environment for an all-users install and the
    // per-user environment otherwise -- the same split the FEMM_DIR entries use.
    PathKeyHKLM = 'SYSTEM\CurrentControlSet\Control\Session Manager\Environment';
    PathKeyHKCU = 'Environment';

function SendMessageTimeout(hWnd: HWND; Msg: UINT; wParam: LongInt; lParam: string;
    fuFlags: UINT; uTimeout: UINT; out lpdwResult: DWORD): LongInt;
    external 'SendMessageTimeoutW@user32.dll stdcall';

// Pick the PATH registry location for this install's scope.
procedure PathLocation(var RootKey: Integer; var SubKey: string);
begin
    if IsAdminInstallMode then begin
        RootKey := HKEY_LOCAL_MACHINE;
        SubKey := PathKeyHKLM;
    end else begin
        RootKey := HKEY_CURRENT_USER;
        SubKey := PathKeyHKCU;
    end;
end;

// Append Dir to PATH if not already present (case-insensitive). Reads and
// rewrites as REG_EXPAND_SZ so existing %VAR% references are preserved.
procedure EnvAddPath(Dir: string);
var
    RootKey: Integer;
    SubKey, Paths: string;
begin
    PathLocation(RootKey, SubKey);
    if not RegQueryStringValue(RootKey, SubKey, 'Path', Paths) then
        Paths := '';
    // Already there? Bracket both sides with ';' so we match whole entries only.
    if Pos(';' + Uppercase(Dir) + ';', ';' + Uppercase(Paths) + ';') > 0 then
        exit;
    if Paths = '' then
        Paths := Dir
    else
        Paths := Paths + ';' + Dir;
    RegWriteExpandStringValue(RootKey, SubKey, 'Path', Paths);
end;

// Remove Dir from PATH if present -- only our own segment, never the whole
// variable (so uninsdeletevalue must NOT be used for PATH). We always append at
// the end, so on uninstall the entry is normally the last one.
procedure EnvRemovePath(Dir: string);
var
    RootKey: Integer;
    SubKey, Paths: string;
    P: Integer;
begin
    PathLocation(RootKey, SubKey);
    if not RegQueryStringValue(RootKey, SubKey, 'Path', Paths) then
        exit;
    P := Pos(';' + Uppercase(Dir) + ';', ';' + Uppercase(Paths) + ';');
    if P = 0 then exit;
    if P = 1 then
        Delete(Paths, 1, Length(Dir) + 1)       // Dir was the first entry: drop "Dir;"
    else
        Delete(Paths, P - 1, Length(Dir) + 1);  // drop ";Dir"
    RegWriteExpandStringValue(RootKey, SubKey, 'Path', Paths);
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
    Res: DWORD_PTR;
begin
    if (CurStep = ssPostInstall) and WizardIsTaskSelected('addtopath') then
        EnvAddPath(ExpandConstant('{app}') + '\bin');
    if CurStep = ssDone then
        SendMessageTimeout(HWND_BROADCAST, WM_SETTINGCHANGE, 0, 'Environment', SMTO_ABORTIFHUNG, 5000, Res);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
    Res: DWORD_PTR;
begin
    if CurUninstallStep = usPostUninstall then begin
        EnvRemovePath(ExpandConstant('{app}') + '\bin');
        SendMessageTimeout(HWND_BROADCAST, WM_SETTINGCHANGE, 0, 'Environment', SMTO_ABORTIFHUNG, 5000, Res);
    end;
end;
