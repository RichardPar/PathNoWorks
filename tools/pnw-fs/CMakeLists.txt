if (WIN32)
    # WinFsp's FUSE 3 layer (WinFsp with its developer files).
    set (WINFSP_DIR "$ENV{ProgramFiles\(x86\)}/WinFsp"
         CACHE PATH "WinFsp installation, with its developer files")
    if (EXISTS "${WINFSP_DIR}/inc/fuse3/fuse.h")
        add_executable (pnw-fs main.cc)
        target_include_directories (pnw-fs PRIVATE "${WINFSP_DIR}/inc/fuse3"
                                                   "${WINFSP_DIR}/inc")
        # The DLL lives in WinFsp's own bin directory, not on PATH, so it is
        # delay loaded and found with FspLoad.
        target_link_libraries (pnw-fs PRIVATE pnwdap
                               "${WINFSP_DIR}/lib/winfsp-x64.lib" delayimp)
        target_link_options (pnw-fs PRIVATE /DELAYLOAD:winfsp-x64.dll)
        install (TARGETS pnw-fs)
        set (PNW_HAVE_FUSE ON PARENT_SCOPE)
    else ()
        message (STATUS "WinFsp not found at ${WINFSP_DIR}: not building "
                        "pnw-fs (install WinFsp with its developer files)")
    endif ()
    return ()
endif ()

# Needs libfuse 3 (libfuse3-dev); skipped without it.
find_package (PkgConfig)
if (PkgConfig_FOUND)
    pkg_check_modules (FUSE3 IMPORTED_TARGET fuse3)
endif ()
if (FUSE3_FOUND)
    add_executable (pnw-fs main.cc)
    target_link_libraries (pnw-fs PRIVATE pnwdap PkgConfig::FUSE3)
    install (TARGETS pnw-fs)
    set (PNW_HAVE_FUSE ON PARENT_SCOPE)
else ()
    message (STATUS "libfuse3 not found: not building pnw-fs "
                    "(install libfuse3-dev)")
endif ()
