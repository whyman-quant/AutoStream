include(${CMAKE_CURRENT_SOURCE_DIR}/../FactorModuleTemplate.cmake)
if(BUILD_TESTING)
  add_executable(liquidity_resilience_v2_factor_entry_test
      ${CMAKE_SOURCE_DIR}/tests/liquidity_resilience_v2/factor_entry_test.cpp)
  target_link_libraries(liquidity_resilience_v2_factor_entry_test PRIVATE factors_liquidity_resilience_v2)
  add_test(NAME liquidity_resilience_v2_factor_entry COMMAND liquidity_resilience_v2_factor_entry_test)
endif()
