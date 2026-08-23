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
rem ======================================================================
pushd "%~dp0"
call buildsource.bat
"%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe" femm-qt.iss > NUL
popd
