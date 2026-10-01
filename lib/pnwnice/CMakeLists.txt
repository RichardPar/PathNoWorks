add_library (pnwnice STATIC src/nodes.cc)
target_include_directories (pnwnice PUBLIC include)
target_link_libraries (pnwnice PUBLIC pnwclient)
