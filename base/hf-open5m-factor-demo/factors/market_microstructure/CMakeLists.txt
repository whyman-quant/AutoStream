set(MODULE_USE_PROJECT_CXX_STANDARD OFF)
set(MODULE_CXX_STANDARD 17)
include(${CMAKE_CURRENT_SOURCE_DIR}/../FactorModuleTemplate.cmake)

if(BUILD_TESTING)
    add_executable(market_microstructure_factor_entry_test
        ${CMAKE_SOURCE_DIR}/tests/market_microstructure/factor_entry_test.cpp)
    target_link_libraries(market_microstructure_factor_entry_test PRIVATE factors_market_microstructure)
    add_test(NAME market_microstructure_factor_entry COMMAND market_microstructure_factor_entry_test)
endif()
