# Stamp the build date into the generated common/build_info.cpp.
#
#   cmake -DIN=<template> -DOUT=<generated> -DFEMM_VERSION=5.0.20261013 -P BuildInfo.cmake
#
# Run at configure time so the file exists for a first build, and again as a
# custom target on EVERY build so it cannot name a day the binary was not built
# on. configure_file() rewrites the file only when its content differs, so the
# recompile-and-relink this triggers happens at most once a day rather than
# once a build.
#
# "%d%b%Y" -> 09Sep2026. That is the spelling packaging/buildsource.bat already
# uses for the source-zip name (`date +%d%b%Y`), so the About box and the
# archive a tester downloaded read the same. FEMM 4.2's About box showed
# __DATE__'s own "Sep 28 2024" instead; same information, different order.
string(TIMESTAMP FEMM_BUILD_DATE "%d%b%Y")
configure_file("${IN}" "${OUT}" @ONLY)
