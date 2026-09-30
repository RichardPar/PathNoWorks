add_executable (pnw-ncp main.cc)
target_link_libraries (pnw-ncp PRIVATE pnwclient)
install (TARGETS pnw-ncp)
