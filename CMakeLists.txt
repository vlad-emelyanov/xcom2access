cmake_minimum_required(VERSION 3.25)
project(X2Access VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Force x64 — XCOM 2 WOTC is 64-bit only
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "X2Access must be built as 64-bit (x64)")
endif()

# SRAL: static build, no UIA, no tests
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_SRAL_TEST OFF CACHE BOOL "" FORCE)
set(SRAL_DISABLE_UIA ON CACHE BOOL "" FORCE)
add_subdirectory(deps/SRAL)

# Generated SDK global definitions (GObjects/GNames externs)
set(SDK_DIR "${CMAKE_SOURCE_DIR}/generated_sdk/X2SDK")

add_library(X2Access SHARED
    src/dllmain.cpp
    src/tts.cpp
    src/input.cpp
    src/input_arbiter.cpp
    src/log.cpp
    src/hooks.cpp
    src/sdk_globals.cpp
    src/sdk_impl.cpp
    src/event_dispatch.cpp
    src/process_event_hook.cpp
    src/ue3_reflection.cpp
    src/game_ui.cpp
    src/menu_navigator.cpp
    src/tile_reader.cpp
    # Compile-time verification of hand-transcribed offsets against the generated SDK
    src/sdk_layout_checks.cpp
    # Graph A11y Kernel — engine-neutral, STL-only (see src/Graph/ControlId.hpp)
    src/Graph/GraphBuilder.cpp
    src/Graph/KeyGraph.cpp
    src/Graph/GraphAnnouncer.cpp
    # Generated SDK — the globals definition file. Since the 2026-08-19 regeneration the
    # *_classes.cpp wrappers are CORRECT (name-based UFunction::FindFunction, no FUNC_Native
    # games) and may be added here when a package's typed calls are worth it. Two caveats
    # before you do: XComGame_classes.cpp alone is ~28 MB of one translation unit, and the
    # wrappers call ProcessEvent WITHOUT an SEH guard, so a stale `this` faults instead of
    # returning false the way GameUI::CallNamedFunction does. Field READS stay on explicit
    # offsets through game_ui.cpp's guarded primitives regardless.
    "${SDK_DIR}/GameDefines.cpp"
)

target_include_directories(X2Access PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/deps/SRAL/Include
    ${SDK_DIR}
    ${SDK_DIR}/SDK_HEADERS
)

# Graph kernel conformance suite (runs on the build machine, no game needed)
enable_testing()
add_subdirectory(tests)

# SRAL_STATIC so SRAL.h doesn't use __declspec(dllimport)
target_compile_definitions(X2Access PRIVATE SRAL_STATIC)

target_link_libraries(X2Access PRIVATE SRAL_static)

# Suppress warnings on generated SDK code
if(MSVC)
    set_source_files_properties(
        "${SDK_DIR}/GameDefines.cpp"
        src/sdk_impl.cpp
        src/ue3_reflection.cpp
        PROPERTIES COMPILE_FLAGS "/w"
    )
    # TUs including the full generated SDK — huge headers need /bigobj, and the generated
    # code is not warning-clean
    set_source_files_properties(
        src/game_ui.cpp
        src/sdk_layout_checks.cpp
        PROPERTIES COMPILE_FLAGS "/w /bigobj"
    )
endif()

# Output as .asi for Ultimate ASI Loader compatibility
set_target_properties(X2Access PROPERTIES
    OUTPUT_NAME "X2Access"
    SUFFIX ".asi"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# ---------------------------------------------------------------------------
# XCOM 2 paths (override with -DXCOM2_SDK_PATH=... at configure time)
# ---------------------------------------------------------------------------
set(XCOM2_SDK_PATH "D:/SteamLibrary/steamapps/common/XCOM 2 War of the Chosen SDK"
    CACHE PATH "Path to XCOM 2 WOTC SDK")
set(XCOM2_GAME_PATH "D:/SteamLibrary/steamapps/common/XCOM 2/XCom2-WarOfTheChosen"
    CACHE PATH "Path to XCOM 2 WOTC game directory")

# ---------------------------------------------------------------------------
# UnrealScript compilation via X2ModBuildCommon
# ---------------------------------------------------------------------------
set(USCRIPT_BUILD_PS1 "${CMAKE_SOURCE_DIR}/mods/X2AccessCompanion/.scripts/build.ps1")
set(USCRIPT_MODS_DIR  "${CMAKE_SOURCE_DIR}/mods")

find_program(PWSH_EXECUTABLE pwsh)
if(NOT PWSH_EXECUTABLE)
    # Fall back to Windows PowerShell if pwsh 7 is not installed
    set(PWSH_EXECUTABLE powershell.exe)
endif()

add_custom_target(build_uscript
    COMMAND "${PWSH_EXECUTABLE}" -NonInteractive -ExecutionPolicy Bypass
        -File "${USCRIPT_BUILD_PS1}"
        -srcDirectory "${USCRIPT_MODS_DIR}"
        -sdkPath "${XCOM2_SDK_PATH}"
        -gamePath "${XCOM2_GAME_PATH}"
        -config "default"
    WORKING_DIRECTORY "${USCRIPT_MODS_DIR}"
    COMMENT "Compiling X2AccessCompanion UnrealScript"
    VERBATIM
)

# ---------------------------------------------------------------------------
# Deploy: copy ASI to game + trigger UScript build (which auto-deploys mod)
# ---------------------------------------------------------------------------
set(DEPLOY_SCRIPTS_DIR "${XCOM2_GAME_PATH}/Binaries/Win64/scripts")

add_custom_target(deploy
    COMMAND ${CMAKE_COMMAND} -E make_directory "${DEPLOY_SCRIPTS_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:X2Access>" "${DEPLOY_SCRIPTS_DIR}/X2Access.asi"
    DEPENDS X2Access
    COMMENT "Deploying X2Access.asi and X2AccessCompanion to game"
    VERBATIM
)
add_dependencies(deploy build_uscript)
