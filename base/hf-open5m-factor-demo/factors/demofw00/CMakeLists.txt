# demofw00 uses the copied fw analyzers. Some of them require C++17
# (for example std::optional in ChipStructureAnalyzer).
set(MODULE_USE_PROJECT_CXX_STANDARD OFF)
set(MODULE_CXX_STANDARD 17)

include(${CMAKE_CURRENT_SOURCE_DIR}/../FactorModuleTemplate.cmake)

if(BUILD_TESTING)
    add_executable(order_synthesizer_test
        ${CMAKE_SOURCE_DIR}/tests/demofw00/order_synthesizer_test.cpp)
    target_include_directories(order_synthesizer_test PRIVATE
        ${CMAKE_SOURCE_DIR})
    target_link_libraries(order_synthesizer_test PRIVATE factors_demofw00)
    add_test(NAME order_synthesizer COMMAND order_synthesizer_test)
endif()
