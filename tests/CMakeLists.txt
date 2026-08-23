# Graph kernel conformance suite. Compiles src/Graph/** STANDALONE — no game SDK, Prism, or
# Windows include paths or links — which is what enforces the kernel/host boundary (spec §0).
add_executable(GraphTests
    graph/TestHarness.hpp
    graph/TestMain.cpp
    graph/GraphBuilderTests.cpp
    graph/KeyGraphTests.cpp
    graph/GraphAnnouncerTests.cpp
    graph/FlyoutTests.cpp
    graph/GraphSheetTests.cpp
    ${CMAKE_SOURCE_DIR}/src/Graph/GraphBuilder.cpp
    ${CMAKE_SOURCE_DIR}/src/Graph/GraphSheet.cpp
    ${CMAKE_SOURCE_DIR}/src/Graph/KeyGraph.cpp
    ${CMAKE_SOURCE_DIR}/src/Graph/GraphAnnouncer.cpp
)

target_include_directories(GraphTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/graph
)

add_test(NAME GraphTests COMMAND GraphTests)

# The other half of the kernel/host boundary. The target above proves the kernel needs no SDK,
# but the Windows SDK sits on MSVC's DEFAULT include path, so it would happily compile an
# `#include <windows.h>` — the contract's Windows clause had no teeth at all. This script reads
# the includes instead of compiling them; it needs no game, no SDK and no compiler.
add_test(NAME GraphKernelBoundary
    COMMAND ${CMAKE_COMMAND}
        -DKERNEL_DIR=${CMAKE_SOURCE_DIR}/src/Graph
        -P ${CMAKE_CURRENT_SOURCE_DIR}/CheckKernelIncludes.cmake)
