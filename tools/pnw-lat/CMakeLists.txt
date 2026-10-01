if (WIN32)
    # LAT runs on raw Ethernet, which needs a packet driver on Windows;
    # decnetd there links over Multinet only.
    message (STATUS "Windows: not building pnw-lat (LAT needs raw Ethernet)")
    return ()
endif ()

add_executable (pnw-lat main.cc)
target_link_libraries (pnw-lat PRIVATE pnwlat pnwterm)

# The privileged helper: opens the raw socket and runs pnw-lat with it.
add_executable (pnw-latsock latsock.cc)
install (TARGETS pnw-lat pnw-latsock)
