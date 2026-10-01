add_test (NAME ncp_smoke
          COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/ncp_smoke.sh
                  $<TARGET_FILE:pnw-ncp> ${CPPDECNET_BIN}/decnetd)
set_tests_properties (ncp_smoke PROPERTIES TIMEOUT 120)

# The file access tests use PyDECnet's FAL as the far end.
set (PYDECNET_DIR "${CMAKE_SOURCE_DIR}/../Decnet/pydecnet/pydecnet"
     CACHE PATH "PyDECnet source tree, for its FAL")
get_filename_component (PYDECNET_DIR "${PYDECNET_DIR}" ABSOLUTE)
if (EXISTS "${PYDECNET_DIR}/decnet/applications/fal.py")
    add_test (NAME nft_pydecnet_fal
              COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/nft_smoke.sh
                      $<TARGET_FILE_DIR:pnw-dir> ${CPPDECNET_BIN}/decnetd
                      pydecnet ${PYDECNET_DIR})
    set_tests_properties (nft_pydecnet_fal PROPERTIES TIMEOUT 180)
else ()
    message (STATUS "PyDECnet not found at ${PYDECNET_DIR}; "
                    "skipping the file access tests")
endif ()

# And against cppdecnet's own FAL, which also takes writes.
add_test (NAME nft_dnfal
          COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/nft_smoke.sh
                  $<TARGET_FILE_DIR:pnw-dir> ${CPPDECNET_BIN}/decnetd
                  dnfal ${CPPDECNET_BIN}/dnfal)
set_tests_properties (nft_dnfal PROPERTIES TIMEOUT 180)

# dnfal's user file: who may connect, where, and whether they may write.
add_test (NAME fal_auth
          COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/fal_auth.sh
                  $<TARGET_FILE_DIR:pnw-dir> ${CPPDECNET_BIN}/decnetd
                  ${CPPDECNET_BIN}/dnfal)
set_tests_properties (fal_auth PROPERTIES TIMEOUT 180)

add_executable (test_names test_names.cc)
target_link_libraries (test_names PRIVATE pnwdap)
add_test (NAME names COMMAND test_names)

# The FUSE mount, if it was built.
if (TARGET pnw-fs)
    add_test (NAME fs_smoke
              COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/fs_smoke.sh
                      $<TARGET_FILE:pnw-fs> ${CPPDECNET_BIN}/decnetd
                      ${CPPDECNET_BIN}/dnfal)
    set_tests_properties (fs_smoke PROPERTIES TIMEOUT 180)
endif ()

add_executable (test_cterm test_cterm.cc)
target_link_libraries (test_cterm PRIVATE pnwcterm)
add_test (NAME cterm COMMAND test_cterm)

add_executable (test_lat test_lat.cc)
target_link_libraries (test_lat PRIVATE pnwlat)
add_test (NAME lat COMMAND test_lat)

# Mail-11 from one node to another, both ends pnw-mail.
add_test (NAME mail_smoke
          COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/mail_smoke.sh
                  $<TARGET_FILE:pnw-mail> ${CPPDECNET_BIN}/decnetd)
set_tests_properties (mail_smoke PROPERTIES TIMEOUT 180)

# MOP through decnetd's API, between two stations on a UDP-carried LAN.
add_test (NAME mop_smoke
          COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/mop_smoke.sh
                  $<TARGET_FILE:pnw-mop> ${CPPDECNET_BIN}/decnetd)
set_tests_properties (mop_smoke PROPERTIES TIMEOUT 120)

# The desktop, if it was built.
if (TARGET pnwgui)
    find_package (Qt6 REQUIRED COMPONENTS Test)
    add_executable (test_gui test_gui.cc)
    set_target_properties (test_gui PROPERTIES AUTOMOC ON)
    target_link_libraries (test_gui PRIVATE pnwgui Qt6::Test)
    add_test (NAME gui_smoke
              COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/gui_smoke.sh
                      $<TARGET_FILE:test_gui> ${CPPDECNET_BIN}/decnetd
                      ${CPPDECNET_BIN}/dnfal $<TARGET_FILE_DIR:pnw-dir>)
    set_tests_properties (gui_smoke PROPERTIES TIMEOUT 180)
endif ()

# X11 over DECnet, both directions, with a stand-in X server.
find_program (PYTHON3 python3)
if (PYTHON3)
    add_test (NAME x11_smoke
              COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/x11_smoke.sh
                      $<TARGET_FILE:pnw-x11> ${CPPDECNET_BIN}/decnetd)
    set_tests_properties (x11_smoke PROPERTIES TIMEOUT 120)
endif ()
