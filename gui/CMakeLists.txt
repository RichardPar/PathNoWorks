# The desktop: needs Qt 6 (qt6-base-dev); skipped without it.
if (WIN32 AND NOT Qt6_DIR)
    # Qt's own installer and aqtinstall both put it in C:\Qt\<version>\
    # msvc2022_64; take the newest there unless told otherwise.
    file (GLOB PNW_QT_DIRS "C:/Qt/6.*/msvc2022_64")
    if (PNW_QT_DIRS)
        list (SORT PNW_QT_DIRS COMPARE NATURAL ORDER DESCENDING)
        list (GET PNW_QT_DIRS 0 PNW_QT_DIR)
        list (APPEND CMAKE_PREFIX_PATH "${PNW_QT_DIR}")
    endif ()
endif ()
find_package (Qt6 QUIET COMPONENTS Widgets Concurrent)
if (NOT Qt6_FOUND)
    if (WIN32)
        message (STATUS "Qt 6 not found: not building the pathnoworks desktop "
                        "(install Qt 6 for MSVC 2022 in C:\\Qt, or set "
                        "CMAKE_PREFIX_PATH)")
    else ()
        message (STATUS "Qt 6 not found: not building the pathnoworks desktop "
                        "(install qt6-base-dev)")
    endif ()
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

# The icons it asks for by freedesktop name, from Tango (see icons/README.md):
# the theme on Windows, which has none, and the fallback elsewhere.
file (GLOB_RECURSE PNW_ICONS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
      CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/icons/tango/*")
qt_add_resources (pathnoworks "icons" PREFIX "/" FILES ${PNW_ICONS})

if (WIN32)
    # A windowed program: no console window behind it.
    set_target_properties (pathnoworks PROPERTIES WIN32_EXECUTABLE ON)
    # Qt's DLLs and plugins next to it, so it runs from the build tree.
    if (TARGET Qt6::windeployqt)
        add_custom_command (TARGET pathnoworks POST_BUILD
            COMMAND Qt6::windeployqt --no-translations --no-compiler-runtime
                    --no-system-d3d-compiler --no-opengl-sw
                    "$<TARGET_FILE:pathnoworks>"
            COMMENT "Copying Qt beside pathnoworks.exe"
            VERBATIM)
    endif ()
    # Where the tests find Qt's DLLs.
    get_target_property (PNW_QT_CORE Qt6::Core IMPORTED_LOCATION_RELEASE)
    get_filename_component (PNW_QT_BIN "${PNW_QT_CORE}" DIRECTORY)
    set (PNW_QT_BIN "${PNW_QT_BIN}" PARENT_SCOPE)
else ()
    install (FILES pathnoworks.desktop DESTINATION share/applications)
endif ()

set (PNW_HAVE_GUI ON PARENT_SCOPE)
