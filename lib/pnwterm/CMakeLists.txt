add_library (pnwterm STATIC src/term.cc)
target_include_directories (pnwterm PUBLIC include)
target_link_libraries (pnwterm PUBLIC decnet)
