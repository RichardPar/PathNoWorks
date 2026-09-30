add_library (pnwlat STATIC src/lat.cc)
target_include_directories (pnwlat PUBLIC include)
target_link_libraries (pnwlat PUBLIC decnet)
