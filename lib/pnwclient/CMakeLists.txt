add_library (pnwclient STATIC
    src/api.cc
    src/reasons.cc)
target_include_directories (pnwclient PUBLIC include)
target_link_libraries (pnwclient PUBLIC decnet)
