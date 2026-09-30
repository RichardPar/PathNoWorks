add_executable (pnw-lat main.cc)
target_link_libraries (pnw-lat PRIVATE pnwlat pnwterm)

# The privileged helper: opens the raw socket and runs pnw-lat with it.
add_executable (pnw-latsock latsock.cc)
install (TARGETS pnw-lat pnw-latsock)
