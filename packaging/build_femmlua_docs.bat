@echo off
rem ======================================================================
rem  build_femmlua_docs.bat - regenerate luafemm\femm-lua.pdf from its
rem  markdown source.
rem
rem      packaging\build_femmlua_docs.bat
rem
rem  femm-lua.md is the source; femm-lua.pdf is what the installers ship
rem  ({app}\luafemm on Windows, ~/Documents/FEMM/docs on Unix). Edit the
rem  markdown, run this, and commit both -- the same standing rule the
rem  manual and the build guides have.
rem
rem  Needs pandoc and a pdflatex on PATH.
rem
rem  pushd %~dp0..\luafemm because pandoc is given a RELATIVE filename:
rem  run from packaging\, where this script lives, there is no femm-lua.md
rem  to find and the only symptom is pandoc complaining about a missing
rem  input file.
rem ======================================================================
pushd "%~dp0..\luafemm"
if errorlevel 1 (
    echo ERROR: cannot find the luafemm directory beside packaging\
    exit /b 1
)
pandoc femm-lua.md -o femm-lua.pdf --pdf-engine=pdflatex -V geometry:margin=1in
set RC=%errorlevel%
popd
if not "%RC%"=="0" (
    echo ERROR: pandoc failed ^(exit %RC%^). Is pandoc, and a pdflatex, on PATH?
    exit /b %RC%
)
echo Wrote luafemm\femm-lua.pdf
