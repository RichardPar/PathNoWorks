add_library (pnwmail STATIC src/mail11.cc)
target_include_directories (pnwmail PUBLIC include)
target_link_libraries (pnwmail PUBLIC pnwclient)
