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
