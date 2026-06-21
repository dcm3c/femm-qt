# SPDX-License-Identifier: MIT
# Copyright (C) 2026 David Meeker and contributors
#
# stamp_mpconfig.cmake - record which mpconfigport.h the committed
# micropython_embed package was generated against.
#
# Writes the line-ending-normalized SHA256 of mpconfigport.h to
# mpconfigport.sha256. Normalizing (stripping CR) makes the stamp identical on
# Windows (CRLF) and Linux/macOS (LF), so the committed stamp matches on every
# platform. Run by regen.sh / the regen-micropython target; CMakeLists.txt
# computes the same hash and compares, warning only when the config has actually
# changed since the package was last regenerated.
#
# Usage:  cmake -P micropython/stamp_mpconfig.cmake
file(READ "${CMAKE_CURRENT_LIST_DIR}/mpconfigport.h" _cfg)
string(REGEX REPLACE "\r" "" _cfg "${_cfg}")
string(SHA256 _hash "${_cfg}")
file(WRITE "${CMAKE_CURRENT_LIST_DIR}/mpconfigport.sha256" "${_hash}\n")
message(STATUS "Wrote mpconfigport.sha256 = ${_hash}")
