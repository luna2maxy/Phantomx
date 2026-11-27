add_executable(phantomx_unit_tests
  unit/test_main.cpp
  unit/test_types.cpp
  unit/test_config.cpp
  unit/test_serialization.cpp
  unit/test_cli.cpp
  unit/test_protocol_vectors.cpp
  unit/test_crypto.cpp
  unit/test_utxo.cpp
  unit/test_database.cpp
  unit/test_poi.cpp
  unit/test_consensus.cpp
  unit/test_rewards.cpp
  unit/test_pool.cpp
  unit/test_rpc.cpp
)
target_link_libraries(phantomx_unit_tests PRIVATE PhantomX::common)
target_include_directories(phantomx_unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(phantomx_unit_tests PRIVATE cxx_std_20)
phantomx_apply_warnings(phantomx_unit_tests)
add_test(NAME phantomx.unit COMMAND phantomx_unit_tests)

if(PHANTOMX_BUILD_INTEGRATION_TESTS)
  add_executable(phantomx_integration_tests
    integration/test_integration.cpp
  )
  target_link_libraries(phantomx_integration_tests PRIVATE PhantomX::common)
  target_include_directories(phantomx_integration_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_features(phantomx_integration_tests PRIVATE cxx_std_20)
  phantomx_apply_warnings(phantomx_integration_tests)
  add_test(NAME phantomx.integration COMMAND phantomx_integration_tests)

  add_executable(phantomx_wallet_integration_tests
    integration/test_wallet_integration.cpp
  )
  target_link_libraries(phantomx_wallet_integration_tests PRIVATE PhantomX::common)
  target_include_directories(phantomx_wallet_integration_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_features(phantomx_wallet_integration_tests PRIVATE cxx_std_20)
  phantomx_apply_warnings(phantomx_wallet_integration_tests)
  add_test(NAME phantomx.integration.wallet COMMAND phantomx_wallet_integration_tests)

  add_executable(phantomx_network_tests
    network/test_p2p.cpp
  )
  target_link_libraries(phantomx_network_tests PRIVATE PhantomX::common)
  target_include_directories(phantomx_network_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_features(phantomx_network_tests PRIVATE cxx_std_20)
  phantomx_apply_warnings(phantomx_network_tests)
  add_test(NAME phantomx.network COMMAND phantomx_network_tests)

  add_executable(phantomx_anti_sybil_tests
    poi/anti_sybil/test_anti_sybil.cpp
  )
  target_link_libraries(phantomx_anti_sybil_tests PRIVATE PhantomX::common)
  target_include_directories(phantomx_anti_sybil_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_features(phantomx_anti_sybil_tests PRIVATE cxx_std_20)
  phantomx_apply_warnings(phantomx_anti_sybil_tests)
  add_test(NAME phantomx.poi.anti_sybil COMMAND phantomx_anti_sybil_tests)

  add_executable(phantomx_verifier_tests
    verifier/test_verifier.cpp
  )
  target_link_libraries(phantomx_verifier_tests PRIVATE PhantomX::common)
  target_include_directories(phantomx_verifier_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_features(phantomx_verifier_tests PRIVATE cxx_std_20)
  phantomx_apply_warnings(phantomx_verifier_tests)
  add_test(NAME phantomx.verifier COMMAND phantomx_verifier_tests)

  # Smoke tests for executables
  add_test(NAME phantomx.smoke.phantomxd COMMAND phantomxd --version)
  add_test(NAME phantomx.smoke.cli COMMAND phantomx-cli --version)
  add_test(NAME phantomx.smoke.wallet COMMAND phantomx-wallet --version)
  add_test(NAME phantomx.smoke.poi COMMAND phantomx-poi --version)
  add_test(NAME phantomx.smoke.pool COMMAND phantomx-pool --version)
endif()
