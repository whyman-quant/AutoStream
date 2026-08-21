# demofw00 uses the copied fw analyzers. Some of them require C++17
# (for example std::optional in ChipStructureAnalyzer).
set(MODULE_USE_PROJECT_CXX_STANDARD OFF)
set(MODULE_CXX_STANDARD 17)

include(${CMAKE_CURRENT_SOURCE_DIR}/../FactorModuleTemplate.cmake)
