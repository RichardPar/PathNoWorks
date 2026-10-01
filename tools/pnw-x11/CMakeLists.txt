add_executable (pnw-x11 main.cc)
target_link_libraries (pnw-x11 PRIVATE pnwclient)
install (TARGETS pnw-x11)
