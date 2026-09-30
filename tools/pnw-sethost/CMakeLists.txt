add_executable (pnw-sethost main.cc)
target_link_libraries (pnw-sethost PRIVATE pnwclient pnwcterm pnwterm)
install (TARGETS pnw-sethost)
