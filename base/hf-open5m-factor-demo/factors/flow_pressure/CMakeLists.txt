set(MODULE_USE_PROJECT_CXX_STANDARD OFF)
set(MODULE_CXX_STANDARD 17)
include(${CMAKE_CURRENT_SOURCE_DIR}/../FactorModuleTemplate.cmake)

if(BUILD_TESTING)
    add_executable(flow_pressure_factor_entry_test
        ${CMAKE_SOURCE_DIR}/tests/flow_pressure/factor_entry_test.cpp)
    target_link_libraries(flow_pressure_factor_entry_test PRIVATE factors_flow_pressure)
    add_test(NAME flow_pressure_factor_entry COMMAND flow_pressure_factor_entry_test)
endif()
