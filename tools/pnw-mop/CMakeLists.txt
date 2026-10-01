add_executable (pnw-mop main.cc)
target_link_libraries (pnw-mop PRIVATE pnwclient)
install (TARGETS pnw-mop)
