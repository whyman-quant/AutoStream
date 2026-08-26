# Minimal production module for the sfm_stream_001 book_imbalance seed set.
set(MODULE_USE_PROJECT_CXX_STANDARD OFF)
set(MODULE_CXX_STANDARD 17)
include(${CMAKE_CURRENT_SOURCE_DIR}/../FactorModuleTemplate.cmake)

if(BUILD_TESTING)
    add_executable(book_imbalance_factor_entry_test
        ${CMAKE_SOURCE_DIR}/tests/book_imbalance/factor_entry_test.cpp)
    target_link_libraries(book_imbalance_factor_entry_test PRIVATE factors_book_imbalance)
    add_test(NAME book_imbalance_factor_entry COMMAND book_imbalance_factor_entry_test)
endif()
