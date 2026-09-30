add_executable (pnw-mail main.cc)
target_link_libraries (pnw-mail PRIVATE pnwmail)
install (TARGETS pnw-mail)
