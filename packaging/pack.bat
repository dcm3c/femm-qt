@echo off
rem ======================================================================
rem  pack.bat - build both distributables from the CURRENT COMMIT.
rem
rem      packaging\pack.bat
rem
rem  Produces, in packaging\output\:
rem    femm-qt-py-src_<date>_<hash>.zip   the source archive (buildsource.bat)
rem    femm-qt-py_<date>.exe              the Windows installer (femm-qt.iss)
rem
rem  The zip comes from `git archive HEAD`, so it contains what is COMMITTED,
rem  not what is in the working tree. An edit you have not committed will not
rem  be in it, and neither will an untracked file -- which is how the Python
rem  examples went years without shipping, and how coilgun.fem reached nobody.
rem
rem  The installer, by contrast, packages build\bin as it stands right now.
rem  So a stale build ships stale binaries: run ninja first.
rem
rem  ---------------------------------------------------------------------
rem  CODE SIGNING (Azure Trusted Signing) - optional, off by default.
rem
rem  Set these four and the build signs itself; leave any of them unset and
rem  it builds exactly as it always has, unsigned, with no Azure account:
rem
rem    FEMM_SIGN_ENDPOINT   region URI, e.g. https://eus.codesigning.azure.net
rem    FEMM_SIGN_ACCOUNT    Trusted Signing account name
rem    FEMM_SIGN_PROFILE    certificate profile name
rem    FEMM_SIGN_DLIB       path to Azure.CodeSigning.Dlib.dll
rem
rem  Optional: FEMM_SIGNTOOL to point at a specific signtool.exe.
rem
rem  Nothing account-specific is committed: the three identifiers are read
rem  from the environment into output\trusted-signing.json at build time.
rem  Azure credentials are NOT read here -- the dlib uses
rem  DefaultAzureCredential, so authenticate however you prefer (az login,
rem  or AZURE_TENANT_ID / AZURE_CLIENT_ID / AZURE_CLIENT_SECRET).
rem
rem  Trusted Signing mints a short-lived certificate per operation rather
rem  than issuing one you hold, so signing needs network and credentials AT
rem  BUILD TIME. It cannot be done offline or bolted on afterwards.
rem
rem  Two things get signed, for two different reasons:
rem    * the executables in build\bin, before ISCC packages them -- this does
rem      nothing for SmartScreen (the installer strips Mark-of-the-Web) and
rem      is about antivirus heuristics on MinGW-built solvers;
rem    * the installer and its uninstaller, via ISCC -- this is what removes
rem      the SmartScreen "Unknown publisher" prompt on download.
rem  The Qt and MinGW DLLs are deliberately NOT signed: they are third-party
rem  redistributables and are not ours to re-sign.
rem
rem  Signing removes "Unknown publisher"; it does not grant reputation.
rem  SmartScreen still accrues that over downloads, so a brand-new publisher
rem  identity can warn for a while. That is not a broken setup.
rem ======================================================================
setlocal
pushd "%~dp0"

rem --- decide whether to sign, and say so out loud either way -------------
set "DOSIGN=1"
if "%FEMM_SIGN_ENDPOINT%"=="" set "DOSIGN="
if "%FEMM_SIGN_ACCOUNT%"==""  set "DOSIGN="
if "%FEMM_SIGN_PROFILE%"==""  set "DOSIGN="
if "%FEMM_SIGN_DLIB%"==""     set "DOSIGN="

set "ISCCSIGN="
if defined DOSIGN (
    call :setup_signing || goto :error
) else (
    echo pack: signing disabled ^(FEMM_SIGN_* not set^) - building unsigned
)

rem Call by full path. Git Bash sets NoDefaultCurrentDirectoryInExePath=1, which
rem removes "." from the command search path, so a bare `call buildsource.bat`
rem is "not recognized" when pack.bat is driven from a bash shell even though it
rem works from a cmd prompt.
call "%~dp0buildsource.bat"
"%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe" %ISCCSIGN% "%~dp0femm-qt.iss" > NUL
if errorlevel 1 goto :error

popd
endlocal
exit /b 0

rem ----------------------------------------------------------------------
:setup_signing
if not defined FEMM_SIGNTOOL (
    for /f "delims=" %%I in ('dir /b /s /a-d "%ProgramFiles(x86)%\Windows Kits\10\bin\*\x64\signtool.exe" 2^>nul') do set "FEMM_SIGNTOOL=%%I"
)
if not defined FEMM_SIGNTOOL (
    echo pack: signing requested but no signtool.exe found. Install the Windows
    echo       SDK signing tools, or set FEMM_SIGNTOOL to its full path.
    exit /b 1
)
if not exist "%FEMM_SIGN_DLIB%" (
    echo pack: FEMM_SIGN_DLIB does not exist: "%FEMM_SIGN_DLIB%"
    exit /b 1
)

rem The dlib takes the three identifiers as a JSON file, not as arguments.
rem Written per build from the environment so nothing account-specific is
rem committed. output\ is gitignored.
if not exist "output" mkdir "output"
set "SIGNJSON=%CD%\output\trusted-signing.json"
> "%SIGNJSON%" echo {
>>"%SIGNJSON%" echo   "Endpoint": "%FEMM_SIGN_ENDPOINT%",
>>"%SIGNJSON%" echo   "CodeSigningAccountName": "%FEMM_SIGN_ACCOUNT%",
>>"%SIGNJSON%" echo   "CertificateProfileName": "%FEMM_SIGN_PROFILE%"
>>"%SIGNJSON%" echo }

set "SIGNARGS=sign /v /fd SHA256 /tr http://timestamp.acs.microsoft.com /td SHA256 /dlib"

echo pack: signing build\bin executables as %FEMM_SIGN_ACCOUNT%/%FEMM_SIGN_PROFILE%
for %%E in (femm_gui femm_headless femm_headlessw fkn belasolv hsolv csolv lua4) do (
    if exist "..\build\bin\%%E.exe" (
        "%FEMM_SIGNTOOL%" %SIGNARGS% "%FEMM_SIGN_DLIB%" /dmdf "%SIGNJSON%" "..\build\bin\%%E.exe" || exit /b 1
    )
)

rem Hand ISCC the same command for the installer and uninstaller. $q is Inno's
rem own quote escape -- using it instead of literal quotes keeps this a single
rem batch argument without nested-quote grief. $f is the file being signed.
set "ISCCSIGN=/DSIGN "/Ssigntool=$q%FEMM_SIGNTOOL%$q %SIGNARGS% $q%FEMM_SIGN_DLIB%$q /dmdf $q%SIGNJSON%$q $f""
exit /b 0

:error
echo pack: FAILED
popd
endlocal
exit /b 1
