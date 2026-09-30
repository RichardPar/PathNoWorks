add_executable (pnw-sethost main.cc)
target_link_libraries (pnw-sethost PRIVATE pnwclient pnwcterm)
install (TARGETS pnw-sethost)
