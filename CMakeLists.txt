cmake_minimum_required (VERSION 3.20)
project (PathNoWorks VERSION 0.1.0 LANGUAGES CXX)

# PathNoWorks: Pathworks-style DECnet desktop tools on top of cppdecnet.
# The tools talk to a running decnetd through its API socket; libdecnet is
# linked only for its protocol codecs (JSON, NICE, later DAP).

set (CMAKE_CXX_STANDARD 20)
set (CMAKE_CXX_STANDARD_REQUIRED ON)
set (CMAKE_CXX_EXTENSIONS OFF)
set (CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (NOT CMAKE_BUILD_TYPE)
    set (CMAKE_BUILD_TYPE RelWithDebInfo)
endif ()

add_compile_options (-Wall -Wextra -Wpedantic -Wshadow -Wno-unused-parameter)

# ----------------------------------------------------------------- cppdecnet

set (CPPDECNET_DIR "${CMAKE_SOURCE_DIR}/../Decnet/cppdecnet"
     CACHE PATH "cppdecnet source tree")
set (CPPDECNET_FLAVOUR "release"
     CACHE STRING "cppdecnet build to link: release, or debug (sanitizers)")
set_property (CACHE CPPDECNET_FLAVOUR PROPERTY STRINGS release debug)

get_filename_component (CPPDECNET_DIR "${CPPDECNET_DIR}" ABSOLUTE)
if (NOT EXISTS "${CPPDECNET_DIR}/include/decnet/node.h")
    message (FATAL_ERROR "cppdecnet not found at ${CPPDECNET_DIR}; "
                         "set -DCPPDECNET_DIR=...")
endif ()

set (CPPDECNET_BUILD_DIR "${CPPDECNET_DIR}/build/${CPPDECNET_FLAVOUR}")
set (CPPDECNET_LIB "${CPPDECNET_BUILD_DIR}/lib/libdecnet.a")
set (CPPDECNET_BIN "${CPPDECNET_BUILD_DIR}/bin")

# cppdecnet builds with its own Makefile.  Run it every build; it does
# nothing when the library is up to date.
find_program (MAKE_PROGRAM NAMES gmake make REQUIRED)
add_custom_target (cppdecnet
    COMMAND ${MAKE_PROGRAM} -C "${CPPDECNET_DIR}" BUILD=${CPPDECNET_FLAVOUR}
            lib daemon tools
    BYPRODUCTS "${CPPDECNET_LIB}"
    COMMENT "Building cppdecnet (${CPPDECNET_FLAVOUR})"
    USES_TERMINAL)

find_package (Threads REQUIRED)

add_library (decnet STATIC IMPORTED GLOBAL)
set_target_properties (decnet PROPERTIES
    IMPORTED_LOCATION "${CPPDECNET_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${CPPDECNET_DIR}/include"
    INTERFACE_LINK_LIBRARIES Threads::Threads)
add_dependencies (decnet cppdecnet)

# The debug flavour of libdecnet is built with the sanitizers, so anything
# that links it must be too.
if (CPPDECNET_FLAVOUR STREQUAL "debug")
    add_compile_options (-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options (-fsanitize=address,undefined)
endif ()

# -------------------------------------------------------------------- targets

add_subdirectory (lib/pnwclient)
add_subdirectory (lib/pnwdap)
add_subdirectory (tools/pnw-ncp)
add_subdirectory (tools/pnw-nft)

enable_testing ()
add_subdirectory (tests)
