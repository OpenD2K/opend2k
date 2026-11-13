# Compiler settings for OpenD2K's own targets (never applied to SDL or third_party code).
# Warnings are never errors: a new compiler's new warning must not break a player's build.

# Options every target of ours gets (source encoding, Windows header hygiene, big objects).
function(d2k_project_options t)
    if(MSVC)  # cl.exe and clang-cl
        target_compile_options(${t} PRIVATE
            /utf-8               # sources are UTF-8 without BOM (translations in render/ui_text_*.inc)
            /permissive-         # standard conformance (two-phase lookup etc.), as clang / gcc
            /Zc:__cplusplus      # report the real C++ version
            /bigobj              # large generated / table-heavy translation units
            $<$<CXX_COMPILER_ID:MSVC>:/MP>)  # parallel compilation inside the VS generator
        target_compile_definitions(${t} PRIVATE
            NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS _USE_MATH_DEFINES)
    elseif(WIN32)  # MinGW
        target_compile_definitions(${t} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _USE_MATH_DEFINES)
    endif()
endfunction()

function(d2k_warnings t)
    if(MSVC)
        # /W4 minus the noisiest conversions-in-original-arithmetic families (the simulation copies
        # the original's integer maths on purpose).
        target_compile_options(${t} PRIVATE /W4 /wd4244 /wd4267 /wd4389 /wd4245 /wd4100)
    else()
        target_compile_options(${t} PRIVATE -Wall -Wextra)
    endif()
endfunction()

# The simulation must give the same results on every compiler / CPU (saves, replays, later
# multiplayer). Floating point is used in a few places that copy x87 code (__ftol of sqrt, the
# sin/cos table, mission condition floats). Rules:
#   - no FMA contraction (a*b+c fused changes rounding): GCC defaults to -ffp-contract=fast and
#     clang to "on" - both off; clang-cl maps /fp:precise to contraction "on", so pass it through
#     /clang:; MSVC's cl does not contract under /fp:precise since VS 2022 (only with /fp:contract);
#   - no x87 excess precision: 64-bit targets use SSE2 already; 32-bit x86 with GCC/clang needs
#     -msse2 -mfpmath=sse (MSVC defaults to /arch:SSE2);
#   - no -ffast-math / /fp:fast anywhere in the project.
function(d2k_deterministic_fp t)
    if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        target_compile_options(${t} PRIVATE /fp:precise)
        if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
            target_compile_options(${t} PRIVATE /clang:-ffp-contract=off)
        endif()
    else()
        target_compile_options(${t} PRIVATE -ffp-contract=off)
        if(CMAKE_SIZEOF_VOID_P EQUAL 4 AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(i.86|x86|X86|AMD64|x86_64)$")
            target_compile_options(${t} PRIVATE -msse2 -mfpmath=sse)
        endif()
    endif()
endfunction()
