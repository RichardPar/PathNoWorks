add_library (pnwdap STATIC src/dap.cc)
target_include_directories (pnwdap PUBLIC include)
target_link_libraries (pnwdap PUBLIC pnwclient)
