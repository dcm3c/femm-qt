@echo off
rem buildsource.bat - archive the femm-qt source tree at HEAD into a zip.
rem Lives in femm-qt\packaging\; writes to femm-qt\packaging\output\.
rem
rem git archive includes the subtree of its current directory, so this
rem cd's up into femm-qt\ before archiving; the -o path is absolute so
rem the result still lands in packaging\output\.
rem
rem The zip has no .git, so a build from it (the Mac's) could not work out
rem its own build number. The archive therefore carries a BUILD_NUMBER file
rem at its root holding HEAD's commit count, which cmake/BuildInfo.cmake
rem reads before trying git. It is added to the archive only, never to the
rem tree (git archive --add-virtual-file, git 2.40 or later).
setlocal
set "PKG=%~dp0"
if not exist "%PKG%output" mkdir "%PKG%output"
set "PKG_FS=%PKG:\=/%"
bash -c "cd '%PKG_FS%..' && git archive --prefix=femm-qt/ --add-virtual-file=BUILD_NUMBER:$(git rev-list --count HEAD) -o '%PKG_FS%output/femm-qt-py-src_'$(date +%%d%%b%%Y)'_'$(git rev-parse --short HEAD)'.zip' HEAD"
endlocal
