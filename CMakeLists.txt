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

# Prism builds import libraries for the readers it delay-loads (ZDSR, PC-Talker, BoyPC...) by
# shelling out to the MSVC librarian, which it locates with a bare `find_program(NAMES lib
# llvm-lib)` — i.e. a PATH search. lib.exe is only on PATH inside a Developer Command Prompt,
# so a plain `cmake -B build -A x64` from an ordinary shell fails the CONFIGURE with "Neither
# 'lib' nor 'llvm-lib' found", which reads as a missing toolchain rather than a missing shell
# environment. CMake already knows exactly where the compiler is, and lib.exe is its neighbour,
# so hint the search there and populate Prism's own cache variable before its find_program runs.
if(MSVC AND NOT PRISM_LIB_TOOL)
    get_filename_component(_msvc_bin_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    find_program(PRISM_LIB_TOOL NAMES lib llvm-lib HINTS "${_msvc_bin_dir}")
    if(PRISM_LIB_TOOL)
        message(STATUS "X2Access: MSVC librarian for Prism: ${PRISM_LIB_TOOL}")
    endif()
endif()

add_subdirectory(deps/prism EXCLUDE_FROM_ALL)

# Generated SDK global definitions (GObjects/GNames externs)
set(SDK_DIR "${CMAKE_SOURCE_DIR}/generated_sdk/X2SDK")

# The generated SDK is a BUILD INPUT that is not in git (113 MB, .gitignore'd), so a fresh
# clone — or a move to a new machine — starts without it and the only way to get it back is a
# generation run against the live game (`just sdk-build`, `just run`, `just sdk-gen`,
# `just sdk-adopt`). Left unchecked, that state fails at GENERATE time with "Cannot find source
# file: .../GameDefines.cpp", which reads like a corrupt checkout, and it takes the whole
# project down with it — including the Graph kernel tests and the UnrealScript companion, both
# of which need no SDK at all. So the ASI target is declared only when its input is actually
# there, and everything that does not depend on it keeps building meanwhile.
if(EXISTS "${SDK_DIR}/GameDefines.cpp")
    set(X2ACCESS_SDK_PRESENT TRUE)
else()
    set(X2ACCESS_SDK_PRESENT FALSE)
    message(WARNING
        "generated_sdk/X2SDK is missing, so the X2Access ASI target is NOT declared.\n"
        "  Everything that needs no SDK still builds: GraphTests, build_uscript.\n"
        "  To recover it: just sdk-build && just run && just sdk-gen && just sdk-adopt\n"
        "  (the generator dumps from the LIVE game; run it with the same mod set players use).\n"
        "  This tree must then be RE-CONFIGURED: the target's existence is decided here, once,\n"
        "  so a cache written now stays targetless however many SDKs appear later. sdk-adopt\n"
        "  re-runs configure itself; a hand-built SDK needs `just configure` before `just build`.")
endif()

# Loopback HTTP dev harness (src/dev_server.cpp). OFF here means the TU is not compiled at all,
# so a release .asi contains no socket code — the runtime marker file is the second gate, not
# the only one. `just` turns it ON for the local dev tree; see the justfile's devserver var.
option(X2ACCESS_DEVSERVER "Build the loopback HTTP dev harness (dev builds only)" OFF)

# Graph kernel conformance suite (runs on the build machine, no game and no generated SDK
# needed — that independence is the point, see src/Graph/ControlId.hpp).
enable_testing()
add_subdirectory(tests)

if(X2ACCESS_SDK_PRESENT)

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
    # The companion wire grammar, in its own TU because it is STL-only and the recipes that
    # decode it are not — menu_navigator.cpp includes windows.h, so tests/ cannot compile it.
    src/wire_record.cpp
    src/mouse_gate.cpp
    src/tile_reader.cpp
    src/free_aim.cpp
    src/object_scanner.cpp
    src/geo_scanner.cpp
    src/tactical_hud.cpp
    src/unit_info.cpp
    src/avenger.cpp
    src/dropship.cpp
    src/text_input.cpp
    src/hacking_screen.cpp
    src/transient_reader.cpp
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
    # The one TU that really does include the full generated SDK (SdkHeaders.hpp): huge headers
    # need /bigobj, and the generated code is not warning-clean.
    set_source_files_properties(
        src/sdk_layout_checks.cpp
        PROPERTIES COMPILE_FLAGS "/w /bigobj"
    )
    # game_ui.cpp is NOT that TU and never was — it takes GameDefines + Core_structs + Core_classes
    # and stops there, on purpose (see its own header comment). It shared the /w line above purely
    # by proximity, which silenced every warning in the file where EVERY engine read in the mod
    # lives: explicit offsets, hand-rolled length arithmetic, the guarded FString copies. C4018
    # (signed/unsigned compare), C4267 and C4244 (narrowing) in a bounds check are precisely the
    # bug that truncates a payload and reads to a blind player as "this screen has nothing more"
    # — the failure mode the size-the-buffer-from-the-data rule in CLAUDE.md was written for. So
    # it keeps default warnings, for the same reason stated for gui_dump.cpp above. If a generated
    # header genuinely warns here, silence it BY NUMBER (/wd####) as line ~173 does; never with a
    # blanket /w. /bigobj stays: it costs nothing and this TU is large enough not to gamble on the
    # section limit.
    set_source_files_properties(src/game_ui.cpp PROPERTIES COMPILE_FLAGS "/bigobj")
endif()

# Output as .asi for Ultimate ASI Loader compatibility
set_target_properties(X2Access PROPERTIES
    OUTPUT_NAME "X2Access"
    SUFFIX ".asi"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

endif()  # X2ACCESS_SDK_PRESENT

# ---------------------------------------------------------------------------
# XCOM 2 paths (override with -DXCOM2_SDK_PATH=... at configure time)
# ---------------------------------------------------------------------------
set(XCOM2_SDK_PATH "C:/Program Files (x86)/Steam/steamapps/common/XCOM 2 War of the Chosen SDK"
    CACHE PATH "Path to XCOM 2 WOTC SDK")
set(XCOM2_GAME_PATH "C:/Program Files (x86)/Steam/steamapps/common/XCOM 2/XCom2-WarOfTheChosen"
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

if(X2ACCESS_SDK_PRESENT)
    add_custom_target(deploy
        COMMAND ${CMAKE_COMMAND} -E make_directory "${DEPLOY_SCRIPTS_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:X2Access>" "${DEPLOY_SCRIPTS_DIR}/X2Access.asi"
        DEPENDS X2Access
        COMMENT "Deploying X2Access.asi and X2AccessCompanion to game"
        VERBATIM
    )
else()
    # Without the generated SDK there is no .asi to copy, but the companion mod still compiles
    # and deploys itself — so `deploy` stays a valid target and does the half it can, rather
    # than disappearing from the build and making `just deploy` look like a broken recipe.
    add_custom_target(deploy
        COMMAND ${CMAKE_COMMAND} -E echo
            "deploy: companion mod only - generated_sdk/X2SDK is missing, so no X2Access.asi was built"
        COMMENT "Deploying X2AccessCompanion to game (no ASI: generated SDK missing)"
        VERBATIM
    )
endif()
add_dependencies(deploy build_uscript)
