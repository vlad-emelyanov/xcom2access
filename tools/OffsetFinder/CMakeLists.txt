cmake_minimum_required(VERSION 3.20)
project(OffsetFinder LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Must be built as x64")
endif()

add_library(OffsetFinder SHARED src/main.cpp)
target_link_libraries(OffsetFinder PRIVATE psapi)

set_target_properties(OffsetFinder PROPERTIES
    OUTPUT_NAME "OffsetFinder"
    SUFFIX ".asi"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
