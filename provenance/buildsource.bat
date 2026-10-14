@echo off
rem SPDX-License-Identifier: MIT
rem buildsource.bat - archive the provenance tree at HEAD into a zip.
rem Lives at the top of provenance\; writes to provenance\output\.
rem
rem git archive takes only what is committed at HEAD: uncommitted edits and
rem everything .gitignore excludes (logs, figures, fit parameters) are left
rem out.  The zip unpacks into a provenance\ folder, since the package
rem documents all the material libraries, not only the B-H curves.
setlocal
set "PKG=%~dp0"
if not exist "%PKG%output" mkdir "%PKG%output"
set "PKG_FS=%PKG:\=/%"
bash -c "cd '%PKG_FS%' && git archive --prefix=provenance/ -o '%PKG_FS%output/provenance_'$(date +%%d%%b%%Y)'_'$(git rev-parse --short HEAD)'.zip' HEAD"
endlocal
