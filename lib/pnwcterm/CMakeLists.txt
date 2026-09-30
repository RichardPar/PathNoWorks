add_library (pnwcterm STATIC src/cterm.cc)
target_include_directories (pnwcterm PUBLIC include)
target_link_libraries (pnwcterm PUBLIC decnet)
