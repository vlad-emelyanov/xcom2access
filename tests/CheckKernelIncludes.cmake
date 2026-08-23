# The kernel/host boundary, as a test rather than a comment.
#
# src/Graph/ is STL-only by contract (CLAUDE.md; src/Graph/ControlId.hpp) — no UE3/SDK, no engine,
# no Windows. Until this script existed the contract's only teeth were the GraphTests target
# compiling the kernel standalone, and that catches just ONE half: an SDK header fails because
# generated_sdk/X2SDK is not on that target's include path, but the Windows SDK is on MSVC's
# DEFAULT include path and kernel32 is on its default link line — so an `#include <windows.h>`
# plus a GetTickCount64() call inside KeyGraph.cpp would have compiled, linked and gone green.
# The cost of that lands on whoever next lifts the kernel into a project that is not Windows-only
# (this core has already crossed four), i.e. on the port rather than on the commit.
#
# ⚠ This is a TRIPWIRE, not a proof. It reads includes and a few platform tokens; it does not
# parse C++ and cannot see a host concept smuggled in as a plain integer. Treat a green result as
# "nobody reached for a host header", not as "the kernel is engine-neutral".
#
# The include rule is an ALLOWLIST, deliberately: a denylist naming windows.h still lets <intrin.h>,
# <winsock2.h> and <ppl.h> through, and the whole point is to catch the header nobody thought of.
# A genuinely new STL header is a one-line addition below and a reviewer's nod — which is the
# conversation this check exists to force.
#
# Run: cmake -DKERNEL_DIR=<dir> -P CheckKernelIncludes.cmake  (registered as the GraphKernelBoundary
# test, so it runs inside `just test` with no game, no SDK and no compiler.)

cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED KERNEL_DIR)
    message(FATAL_ERROR "CheckKernelIncludes: pass -DKERNEL_DIR=<directory to scan>")
endif()

# The C++ standard library, by header name. Freestanding C wrappers included; the bare C headers
# (<string.h>, <math.h>) are deliberately absent — the kernel uses the <c...> spellings.
set(STL_HEADERS
    algorithm any array atomic barrier bit bitset
    cassert cctype cerrno cfenv cfloat charconv chrono cinttypes climits clocale cmath compare
    complex concepts condition_variable coroutine csetjmp csignal cstdarg cstddef cstdint cstdio
    cstdlib cstring ctime cuchar cwchar cwctype
    deque exception execution expected filesystem format forward_list fstream functional future
    generator initializer_list iomanip ios iosfwd iostream istream iterator
    latch limits list locale map mdspan memory memory_resource mutex
    new numbers numeric optional ostream print queue random ranges ratio regex
    scoped_allocator semaphore set shared_mutex source_location span spanstream sstream stack
    stacktrace stdexcept stdfloat stop_token streambuf string string_view syncstream system_error
    thread tuple type_traits typeindex typeinfo
    unordered_map unordered_set utility valarray variant vector version
)

# Tokens that mean "this file knows what platform it is on" — the way a host concern gets in
# WITHOUT an include. `windows.h` itself is not listed: the allowlist above already refuses it,
# under any spelling.
set(PLATFORM_TOKENS "_WIN32" "_WIN64" "__declspec" "WINAPI")

file(GLOB KERNEL_FILES "${KERNEL_DIR}/*.hpp" "${KERNEL_DIR}/*.h" "${KERNEL_DIR}/*.cpp")
if(KERNEL_FILES STREQUAL "")
    # A silent pass over an empty file list would be the worst outcome: a moved directory would
    # read as a clean kernel forever.
    message(FATAL_ERROR "CheckKernelIncludes: no sources found under ${KERNEL_DIR}")
endif()

set(VIOLATIONS "")

foreach(path IN LISTS KERNEL_FILES)
    get_filename_component(name "${path}" NAME)
    file(READ "${path}" content)

    # System includes: the header name must be in the allowlist.
    string(REGEX MATCHALL "#[ \t]*include[ \t]*<[^>\n]+>" angled "${content}")
    foreach(inc IN LISTS angled)
        string(REGEX REPLACE "^.*<(.+)>$" "\\1" header "${inc}")
        list(FIND STL_HEADERS "${header}" found)
        if(found EQUAL -1)
            list(APPEND VIOLATIONS
                "${name}: #include <${header}> is not a C++ standard library header")
        endif()
    endforeach()

    # Quoted includes: siblings only. "GraphTypes.hpp" is the kernel's own style and is fine;
    # anything carrying a path separator is reaching OUT of the directory, which is the smell.
    string(REGEX MATCHALL "#[ \t]*include[ \t]*\"[^\"\n]+\"" quoted "${content}")
    foreach(inc IN LISTS quoted)
        string(REGEX REPLACE "^.*\"(.+)\"$" "\\1" header "${inc}")
        if(header MATCHES "[/\\\\]")
            list(APPEND VIOLATIONS
                "${name}: #include \"${header}\" leaves the kernel directory")
        endif()
    endforeach()

    foreach(token IN LISTS PLATFORM_TOKENS)
        if(content MATCHES "${token}")
            list(APPEND VIOLATIONS "${name}: mentions ${token} — the kernel is platform-neutral")
        endif()
    endforeach()
endforeach()

list(LENGTH KERNEL_FILES file_count)

if(VIOLATIONS)
    list(JOIN VIOLATIONS "\n  " report)
    message(FATAL_ERROR
        "Kernel boundary violated in ${KERNEL_DIR}:\n  ${report}\n"
        "src/Graph/ is STL-only by contract. Host concerns belong in an adapter OUTSIDE it "
        "(see CLAUDE.md and src/Graph/ControlId.hpp). If the header really is standard and "
        "really is missing from this script's allowlist, adding it there is the fix.")
endif()

message(STATUS "Kernel boundary OK: ${file_count} files under ${KERNEL_DIR} include only the STL and their siblings")
