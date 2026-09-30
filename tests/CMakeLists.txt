add_test (NAME ncp_smoke
          COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/ncp_smoke.sh
                  $<TARGET_FILE:pnw-ncp> ${CPPDECNET_BIN}/decnetd)
set_tests_properties (ncp_smoke PROPERTIES TIMEOUT 120)

# The file access tests use PyDECnet's FAL as the far end.
set (PYDECNET_DIR "${CMAKE_SOURCE_DIR}/../Decnet/pydecnet/pydecnet"
     CACHE PATH "PyDECnet source tree, for its FAL")
get_filename_component (PYDECNET_DIR "${PYDECNET_DIR}" ABSOLUTE)
if (EXISTS "${PYDECNET_DIR}/decnet/applications/fal.py")
    add_test (NAME nft_smoke
              COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/nft_smoke.sh
                      $<TARGET_FILE_DIR:pnw-dir> ${CPPDECNET_BIN}/decnetd
                      ${PYDECNET_DIR})
    set_tests_properties (nft_smoke PROPERTIES TIMEOUT 180)
else ()
    message (STATUS "PyDECnet not found at ${PYDECNET_DIR}; "
                    "skipping the file access tests")
endif ()
