# The desktop: needs Qt 6 (qt6-base-dev); skipped without it.
find_package (Qt6 QUIET COMPONENTS Widgets Concurrent)
if (NOT Qt6_FOUND)
    message (STATUS "Qt 6 not found: not building the pathnoworks desktop "
                    "(install qt6-base-dev)")
    return ()
endif ()

set (CMAKE_AUTOMOC ON)

# Everything but main, so the tests can drive the windows.
add_library (pnwgui STATIC
    src/common.cc
    src/decwindows.cc
    src/filewindow.cc
    src/logindialog.cc
    src/maildialog.cc
    src/mainwindow.cc)
target_include_directories (pnwgui PUBLIC src)
target_link_libraries (pnwgui PUBLIC Qt6::Widgets Qt6::Concurrent
                                     pnwdap pnwmail pnwnice)

add_executable (pathnoworks src/main.cc)
target_link_libraries (pathnoworks PRIVATE pnwgui)
install (TARGETS pathnoworks)

set (PNW_HAVE_GUI ON PARENT_SCOPE)
install (FILES pathnoworks.desktop DESTINATION share/applications)
