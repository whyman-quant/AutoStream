set(MODULE_USE_PROJECT_CXX_STANDARD OFF)
set(MODULE_CXX_STANDARD 17)
include(${CMAKE_CURRENT_SOURCE_DIR}/../FactorModuleTemplate.cmake)

if(BUILD_TESTING)
    add_executable(liquidity_resilience_factor_entry_test
        ${CMAKE_SOURCE_DIR}/tests/liquidity_resilience/factor_entry_test.cpp)
    target_link_libraries(liquidity_resilience_factor_entry_test PRIVATE factors_liquidity_resilience)
    add_test(NAME liquidity_resilience_factor_entry COMMAND liquidity_resilience_factor_entry_test)
endif()
