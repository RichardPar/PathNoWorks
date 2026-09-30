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
