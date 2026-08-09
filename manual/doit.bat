rem The manual builds with pdflatex (PNG/PDF figures) as of bd7716e; the old
rem latex -> dvips -> ps2pdf chain only accepted EPS and no longer works here.
pdflatex manual
pdflatex manual
pdflatex manual
copy manual.pdf ..\build\bin\manual.pdf
