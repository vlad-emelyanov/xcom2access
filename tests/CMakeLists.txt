# Graph kernel conformance suite. Compiles src/Graph/** STANDALONE — no game SDK, Prism, or
# Windows include paths or links — which is what enforces the kernel/host boundary (spec §0).
add_executable(GraphTests
    graph/TestHarness.hpp
    graph/TestMain.cpp
    graph/GraphBuilderTests.cpp
    graph/KeyGraphTests.cpp
    graph/GraphAnnouncerTests.cpp
    graph/FlyoutTests.cpp
    ${CMAKE_SOURCE_DIR}/src/Graph/GraphBuilder.cpp
    ${CMAKE_SOURCE_DIR}/src/Graph/KeyGraph.cpp
    ${CMAKE_SOURCE_DIR}/src/Graph/GraphAnnouncer.cpp
)

target_include_directories(GraphTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/graph
)

add_test(NAME GraphTests COMMAND GraphTests)
