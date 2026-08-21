cmake_minimum_required(VERSION 3.25)
project(X2Access VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Force x64 — XCOM 2 WOTC is 64-bit only
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "X2Access must be built as 64-bit (x64)")
endif()

# Static MSVC runtime (/MT), so the .asi drops its dependency on the VC++ redistributable —
# an ASI is loaded before anything else in the process and must not need a side-by-side DLL.
# ⛔ It must be cache-FORCEd, not just set: deps/prism sets CMAKE_MSVC_RUNTIME_LIBRARY itself
# off CMAKE_BUILD_TYPE, which is EMPTY under the Visual Studio (multi-config) generator this
# project configures with (`cmake -B build -A x64`) — so an unpinned cache silently builds a
# /MT Prism against a /MTd X2Access in Debug.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "" FORCE)

# Prism — the speech/screen-reader backend (https://github.com/ethindp/prism), replacing SRAL.
# Same library and same pin (v0.17.3) as CyberAccess, so a speech bug reproduces across both.
# Built STATIC into X2Access.asi: the static path is also what makes Prism define PRISM_STATIC
# for us, so <prism.h> resolves to plain externs instead of __declspec(dllimport). Prism's own
# default is BUILD_SHARED_LIBS ON, so the pin below has to be in the cache before its options
# run.
#
# Shims OFF: the only shim Prism ships is a drop-in Tolk replacement DLL, which is for OTHER
# programs to load — nothing here consumes it, and it is not ours to install into a shared
# game directory. (It also wants /experimental:c11atomics, which the Visual Studio generator
# will not carry into its configure-time probes, so enabling it fails the configure outright.)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(PRISM_ENABLE_SHIMS OFF CACHE BOOL "" FORCE)
set(PRISM_ENABLE_TOLK_SHIM OFF CACHE BOOL "" FORCE)
# Legacy backends ON: System Access and Window-Eyes. Discontinued readers, but Prism's own
# release packages ship them, they cost a few KB, and a player still on one is a player we
# would otherwise serve nothing at all.
set(PRISM_ENABLE_LEGACY_BACKENDS ON CACHE BOOL "" FORCE)
set(PRISM_ENABLE_SYSTEM_ACCESS_BACKEND ON CACHE BOOL "" FORCE)
set(PRISM_ENABLE_WINDOW_EYES_BACKEND ON CACHE BOOL "" FORCE)
set(PRISM_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(PRISM_ENABLE_DEMOS OFF CACHE BOOL "" FORCE)
set(PRISM_ENABLE_GDEXTENSION OFF CACHE BOOL "" FORCE)
add_subdirectory(deps/prism EXCLUDE_FROM_ALL)

# Generated SDK global definitions (GObjects/GNames externs)
set(SDK_DIR "${CMAKE_SOURCE_DIR}/generated_sdk/X2SDK")

# Loopback HTTP dev harness (src/dev_server.cpp). OFF here means the TU is not compiled at all,
# so a release .asi contains no socket code — the runtime marker file is the second gate, not
# the only one. `just` turns it ON for the local dev tree; see the justfile's devserver var.
option(X2ACCESS_DEVSERVER "Build the loopback HTTP dev harness (dev builds only)" OFF)

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
    src/mouse_gate.cpp
    src/tile_reader.cpp
    src/free_aim.cpp
    src/object_scanner.cpp
    src/tactical_hud.cpp
    src/unit_info.cpp
    src/avenger.cpp
    src/text_input.cpp
    src/hacking_screen.cpp
    src/tutorial_reader.cpp
    src/narrator.cpp
    # Compile-time verification of hand-transcribed offsets against the generated SDK
    src/sdk_layout_checks.cpp
    # Graph A11y Kernel — engine-neutral, STL-only (see src/Graph/ControlId.hpp)
    src/Graph/GraphBuilder.cpp
    src/Graph/GraphSheet.cpp
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
    ${SDK_DIR}
    ${SDK_DIR}/SDK_HEADERS
)

# Graph kernel conformance suite (runs on the build machine, no game needed)
enable_testing()
add_subdirectory(tests)

# prism::prism carries PRISM_STATIC and the include dir itself (see the Prism block above).
target_link_libraries(X2Access PRIVATE prism::prism)

# Dev harness: two TUs (the server, and the Desktop Duplication screenshot which owns the
# D3D/GDI+ includes). The define is target-wide because log.cpp's /log ring, tts.cpp's /speech
# tap and the arbiter's synthetic injection are gated on it too — all free in a release build.
if(X2ACCESS_DEVSERVER)
    target_sources(X2Access PRIVATE src/dev_server.cpp src/screen_capture.cpp src/gui_dump.cpp)
    target_compile_definitions(X2Access PRIVATE X2ACCESS_DEVSERVER)
    target_link_libraries(X2Access PRIVATE ws2_32 shell32 d3d11 dxgi gdiplus)
    message(STATUS "X2Access: dev harness ENABLED (loopback HTTP, marker-gated at runtime)")
endif()

# Suppress warnings on generated SDK code
if(MSVC)
    set_source_files_properties(
        "${SDK_DIR}/GameDefines.cpp"
        src/sdk_impl.cpp
        src/ue3_reflection.cpp
        PROPERTIES COMPILE_FLAGS "/w"
    )
    # The dumper keeps default warnings on purpose — it walks raw engine memory, so a real
    # warning there is worth seeing. Only the generated FString::ToString wchar->char narrowing
    # is silenced; nothing in our own code warns at /W3.
    set_source_files_properties(src/gui_dump.cpp PROPERTIES COMPILE_FLAGS "/wd4244")
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
