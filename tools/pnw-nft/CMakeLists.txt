# One source, three commands.
foreach (cmd dir type copy)
    add_executable (pnw-${cmd} main.cc)
    target_compile_definitions (pnw-${cmd} PRIVATE PNW_COMMAND="${cmd}")
    target_link_libraries (pnw-${cmd} PRIVATE pnwdap)
    install (TARGETS pnw-${cmd})
endforeach ()
