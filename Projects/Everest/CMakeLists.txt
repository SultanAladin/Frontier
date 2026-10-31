# Project Everest build configuration
cmake_minimum_required(VERSION 3.24)

add_library(EverestProject STATIC
    Source/EverestApplication.cpp
    Source/EverestSimulationRecords.cpp
    Source/EverestWorkspace.cpp
)
target_include_directories(EverestProject PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
)
target_link_libraries(EverestProject PUBLIC FrontierEngine)
