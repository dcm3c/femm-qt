# WinVersion.cmake -- the Windows VERSIONINFO resource for each shipped .exe.
#
# The version a user sees is the one in project(VERSION ...), written out in
# full: "5.0.20261013" for a date-numbered release. VERSIONINFO has two kinds
# of field, and only one of them can hold that:
#
#   * the STRING fields (ProductVersion, FileVersion, FileDescription, ...) are
#     free text, so they carry the version exactly as written. Explorer's
#     Properties > Details shows ProductVersion as "Product version", and Task
#     Manager lists a process by its FileDescription.
#   * the BINARY fields (FILEVERSION, PRODUCTVERSION) are four 16-bit numbers,
#     at most 65535 each, so an 8-digit date cannot be one of them. A patch
#     number of 8 digits is therefore split YYYY.MMDD: 5.0.20261013 becomes
#     5.0.2026.1013, which still sorts in date order. Anything else is
#     major.minor.patch.0. Explorer shows this one as "File version".
#
# Both come from the same project(VERSION), so there is one version number;
# the binary field is only its 16-bit-safe encoding, never maintained by hand.

# femm_win_file_version(<version> <out>) -- the four comma-separated binary
# fields for <version> ("5.0.20261013" -> "5,0,2026,1013").
function(femm_win_file_version VERSION OUT)
    string(REPLACE "." ";" parts "${VERSION}")
    list(LENGTH parts n)
    if(NOT n EQUAL 3)
        message(FATAL_ERROR "femm_win_file_version: '${VERSION}' is not major.minor.patch")
    endif()
    list(GET parts 0 major)
    list(GET parts 1 minor)
    list(GET parts 2 patch)
    string(LENGTH "${patch}" plen)
    if(plen EQUAL 8)
        string(SUBSTRING "${patch}" 0 4 hi)
        string(SUBSTRING "${patch}" 4 4 lo)
        math(EXPR hi "${hi}")      # strips nothing for a year; normalizes "0105" -> 105
        math(EXPR lo "${lo}")
    else()
        set(hi "${patch}")
        set(lo 0)
    endif()
    foreach(v IN ITEMS "${major}" "${minor}" "${hi}" "${lo}")
        if(v GREATER 65535)
            message(FATAL_ERROR "femm_win_file_version: '${VERSION}' does not fit "
                    "VERSIONINFO's 16-bit fields (${v} > 65535)")
        endif()
    endforeach()
    set(${OUT} "${major},${minor},${hi},${lo}" PARENT_SCOPE)
endfunction()

# femm_add_version_resource(<target> <description>) -- generate the target's
# VERSIONINFO from project(VERSION) and compile it in. Windows only; a no-op
# elsewhere.
function(femm_add_version_resource TARGET DESCRIPTION)
    if(NOT WIN32)
        return()
    endif()
    femm_win_file_version("${PROJECT_VERSION}" FEMM_RC_BINARY_VERSION)
    set(FEMM_RC_VERSION "${PROJECT_VERSION}")
    set(FEMM_RC_DESCRIPTION "${DESCRIPTION}")
    get_target_property(outname ${TARGET} OUTPUT_NAME)
    if(NOT outname)
        set(outname "${TARGET}")
    endif()
    set(FEMM_RC_FILENAME "${outname}.exe")
    set(rc "${CMAKE_BINARY_DIR}/generated/${TARGET}_version.rc")
    configure_file("${CMAKE_SOURCE_DIR}/cmake/version_resource.rc.in" "${rc}" @ONLY)
    target_sources(${TARGET} PRIVATE "${rc}")
endfunction()
