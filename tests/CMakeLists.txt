add_test (NAME ncp_smoke
          COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/ncp_smoke.sh
                  $<TARGET_FILE:pnw-ncp> ${CPPDECNET_BIN}/decnetd)
set_tests_properties (ncp_smoke PROPERTIES TIMEOUT 120)
