cmake_minimum_required(VERSION 3.20)

project(PhantomX
  VERSION 0.1.0
  DESCRIPTION "PhantomX Layer 1 foundation"
  HOMEPAGE_URL "https://github.com/phantomx-labs/phantomx"
  LANGUAGES CXX
)

include(CTest)
include(GNUInstallDirs)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(PHANTOMX_BUILD_TESTS "Build PhantomX tests" ON)
option(PHANTOMX_BUILD_INTEGRATION_TESTS "Build integration tests" ON)
option(PHANTOMX_BUILD_QT "Build Qt desktop wallet (requires Qt6)" OFF)
option(PHANTOMX_ENABLE_WARNINGS "Enable compiler warnings" ON)
option(PHANTOMX_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
option(PHANTOMX_ENABLE_INSTALL "Enable install rules" ON)

# ---------------------------------------------------------------------------
# C++ standard
# ---------------------------------------------------------------------------
# C++20 chosen for broad modern compiler support (MSVC 19.29+, GCC 10+, Clang 12+)
# and useful features (concepts, designated initializers, <span>, <chrono> improvements).
# No external dependencies currently require a newer standard.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(PhantomXWarnings)

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
find_package(PkgConfig QUIET)
find_package(jansson CONFIG QUIET)
if(NOT jansson_FOUND AND PkgConfig_FOUND)
  pkg_check_modules(JANSSON REQUIRED jansson)
endif()
if(NOT jansson_FOUND AND NOT JANSSON_FOUND)
  # Fallback manual lookup
  find_path(JANSSON_INCLUDE_DIR jansson.h)
  find_library(JANSSON_LIBRARY NAMES jansson libjansson)
  if(JANSSON_INCLUDE_DIR AND JANSSON_LIBRARY)
    set(JANSSON_FOUND TRUE)
    set(JANSSON_INCLUDE_DIRS "${JANSSON_INCLUDE_DIR}")
    set(JANSSON_LIBRARIES "${JANSSON_LIBRARY}")
  endif()
endif()
if(NOT jansson_FOUND AND NOT JANSSON_FOUND)
  message(FATAL_ERROR "jansson not found. Install libjansson-dev/jansson or provide vcpkg toolchain.")
endif()
if(TARGET jansson::jansson)
  set(PHANTOMX_JANSSON_TARGET jansson::jansson)
elseif(TARGET jansson)
  set(PHANTOMX_JANSSON_TARGET jansson)
endif()
if(PkgConfig_FOUND)
  pkg_check_modules(CURL QUIET libcurl)
endif()
if(NOT CURL_FOUND)
  find_package(CURL REQUIRED)
endif()
find_package(unofficial-sodium CONFIG QUIET)
if(unofficial-sodium_FOUND)
  set(PHANTOMX_SODIUM_TARGET unofficial-sodium::sodium)
elseif(PkgConfig_FOUND)
  pkg_check_modules(SODIUM QUIET libsodium)
endif()

if(NOT unofficial-sodium_FOUND AND NOT SODIUM_FOUND)
  find_path(SODIUM_INCLUDE_DIR sodium.h)
  find_library(SODIUM_LIBRARY NAMES sodium libsodium)
  if(SODIUM_INCLUDE_DIR AND SODIUM_LIBRARY)
    set(SODIUM_FOUND TRUE)
    set(SODIUM_INCLUDE_DIRS "${SODIUM_INCLUDE_DIR}")
    set(SODIUM_LIBRARIES "${SODIUM_LIBRARY}")
  endif()
endif()

if(NOT unofficial-sodium_FOUND AND NOT SODIUM_FOUND)
  message(FATAL_ERROR "libsodium not found. Install libsodium-dev/libsodium or provide vcpkg toolchain.")
endif()

find_package(unofficial-sqlite3 CONFIG QUIET)
find_package(SQLite3 QUIET)
if(NOT SQLite3_FOUND AND NOT unofficial-sqlite3_FOUND)
  if(PkgConfig_FOUND)
    pkg_check_modules(SQLITE3 QUIET sqlite3)
  endif()
endif()
if(NOT SQLite3_FOUND AND NOT SQLITE3_FOUND AND NOT unofficial-sqlite3_FOUND)
  find_path(SQLITE3_INCLUDE_DIR sqlite3.h)
  find_library(SQLITE3_LIBRARY NAMES sqlite3 libsqlite3)
  if(SQLITE3_INCLUDE_DIR AND SQLITE3_LIBRARY)
    set(SQLite3_FOUND TRUE)
    set(SQLite3_INCLUDE_DIRS "${SQLITE3_INCLUDE_DIR}")
    set(SQLITE3_LIBRARIES "${SQLITE3_LIBRARY}")
  endif()
endif()
if(NOT SQLite3_FOUND AND NOT SQLITE3_FOUND AND NOT unofficial-sqlite3_FOUND)
  message(FATAL_ERROR "SQLite3 not found. Install libsqlite3-dev/sqlite3 or provide vcpkg toolchain.")
endif()

# ---------------------------------------------------------------------------
# Library targets
# ---------------------------------------------------------------------------
add_library(phantomx_common
  src/common/types.cpp
  src/common/error.cpp
  src/common/logging.cpp
  src/common/serialization.cpp
  src/common/config.cpp
  src/common/paths.cpp
  src/common/cli.cpp
  src/common/crypto.cpp
  src/wallet/keystore.cpp
  src/wallet/node_client.cpp
  src/wallet/wallet.cpp
  src/primitives/hash.cpp
  src/primitives/merkle.cpp
  src/primitives/bech32m.cpp
  src/primitives/transaction.cpp
  src/primitives/block.cpp
  src/blockchain/genesis.cpp
  src/blockchain/chain.cpp
  src/database/sqlite_db.cpp
  src/network/protocol.cpp
  src/network/peer.cpp
  src/network/node.cpp
  src/network/socket_io.cpp
  src/poi/measurement/measurement.cpp
  src/poi/challenge/challenge.cpp
  src/poi/evidence/evidence.cpp
  src/poi/score.cpp
  src/poi/uptime_proof.cpp
  src/poi/simulator.cpp
  src/poi/identity/identity.cpp
  src/poi/anti_sybil/anti_sybil.cpp
  src/verifier/registry.cpp
  src/verifier/assignment.cpp
  src/verifier/service.cpp
  src/consensus/consensus.cpp
  src/consensus/bft.cpp
  src/consensus/bft_config.cpp
  src/consensus/pow.cpp
  src/aggregator/aggregator.cpp
  src/rewards/rewards.cpp
  src/pool/pool.cpp
  src/rpc/rpc.cpp
  src/rpc/http_server.cpp
  src/rpc/node_handlers.cpp
  src/utxo/utxo.cpp
  src/utxo/validation.cpp
  src/utxo/mempool.cpp
  src/utxo/tx_builder.cpp
)

add_library(PhantomX::common ALIAS phantomx_common)

target_include_directories(phantomx_common
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(phantomx_common PUBLIC cxx_std_20)
phantomx_apply_warnings(phantomx_common)

if(PHANTOMX_JANSSON_TARGET)
  set(_PHANTOMX_JANSSON_LINK ${PHANTOMX_JANSSON_TARGET})
else()
  set(_PHANTOMX_JANSSON_LINK ${JANSSON_LIBRARIES})
endif()
if(unofficial-sodium_FOUND)
  target_link_libraries(phantomx_common PUBLIC ${PHANTOMX_SODIUM_TARGET} ${_PHANTOMX_JANSSON_LINK})
else()
  target_include_directories(phantomx_common PUBLIC ${SODIUM_INCLUDE_DIRS})
  target_link_libraries(phantomx_common PUBLIC ${SODIUM_LIBRARIES} ${_PHANTOMX_JANSSON_LINK})
  if(SODIUM_LIBRARY_DIRS)
    target_link_directories(phantomx_common PUBLIC ${SODIUM_LIBRARY_DIRS})
  endif()
endif()
if(NOT PHANTOMX_JANSSON_TARGET AND JANSSON_INCLUDE_DIRS)
  target_include_directories(phantomx_common PUBLIC ${JANSSON_INCLUDE_DIRS})
endif()

if(TARGET CURL::libcurl)
  target_link_libraries(phantomx_common PUBLIC CURL::libcurl)
elseif(CURL_LIBRARIES)
  target_include_directories(phantomx_common PUBLIC ${CURL_INCLUDE_DIRS})
  target_link_libraries(phantomx_common PUBLIC ${CURL_LIBRARIES})
endif()

if(TARGET unofficial::sqlite3::sqlite3)
  target_link_libraries(phantomx_common PUBLIC unofficial::sqlite3::sqlite3)
elseif(TARGET SQLite::SQLite3)
  target_link_libraries(phantomx_common PUBLIC SQLite::SQLite3)
else()
  target_include_directories(phantomx_common PUBLIC ${SQLITE3_INCLUDE_DIRS})
  target_link_libraries(phantomx_common PUBLIC ${SQLITE3_LIBRARIES})
  if(SQLITE3_LIBRARY_DIRS)
    target_link_directories(phantomx_common PUBLIC ${SQLITE3_LIBRARY_DIRS})
  endif()
endif()

if(WIN32)
  target_link_libraries(phantomx_common PUBLIC ws2_32)
endif()

if(WIN32)
  target_compile_definitions(phantomx_common PUBLIC PHANTOMX_PLATFORM_WINDOWS=1 NOMINMAX WIN32_LEAN_AND_MEAN)
elseif(APPLE)
  target_compile_definitions(phantomx_common PUBLIC PHANTOMX_PLATFORM_APPLE=1)
elseif(UNIX)
  target_compile_definitions(phantomx_common PUBLIC PHANTOMX_PLATFORM_LINUX=1)
endif()

# ---------------------------------------------------------------------------
# Executables
# ---------------------------------------------------------------------------
function(phantomx_add_executable target source)
  add_executable(${target} ${source})
  target_link_libraries(${target} PRIVATE PhantomX::common)
  target_compile_features(${target} PRIVATE cxx_std_20)
  phantomx_apply_warnings(${target})
  set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endfunction()

phantomx_add_executable(phantomxd src/daemon/main.cpp)
phantomx_add_executable(phantomx-cli apps/phantomx-cli/main.cpp)
phantomx_add_executable(phantomx-wallet apps/phantomx-wallet/main.cpp)
phantomx_add_executable(phantomx-poi src/poi/main.cpp)
phantomx_add_executable(phantomx-pool apps/phantomx-pool/main.cpp)

if(PHANTOMX_BUILD_QT)
  add_subdirectory(apps/phantomx-qt)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(PHANTOMX_BUILD_TESTS)
  add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
if(PHANTOMX_ENABLE_INSTALL)
  install(TARGETS phantomx_common phantomxd phantomx-cli phantomx-wallet phantomx-poi phantomx-pool
    EXPORT PhantomXTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )
  install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
message(STATUS "PhantomX ${PROJECT_VERSION}")
message(STATUS "  C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Tests: ${PHANTOMX_BUILD_TESTS}")
message(STATUS "  Integration tests: ${PHANTOMX_BUILD_INTEGRATION_TESTS}")
message(STATUS "  Warnings as errors: ${PHANTOMX_WARNINGS_AS_ERRORS}")
