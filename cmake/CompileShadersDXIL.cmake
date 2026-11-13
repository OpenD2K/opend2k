# Direct3D 12 shaders: compiles src/render3d/shaders.hlsl to DXIL with dxc at build time and
# embeds the blobs (<build>/generated/shaders_dxil.inc, D2K_HAVE_DXIL=1; render3d/shader_blobs.cpp).
#   D2K_DXIL=AUTO (default): on Windows, when dxc is found; elsewhere off (no D3D12 there)
#   D2K_DXIL=ON: required (configure fails without dxc);  D2K_DXIL=OFF: never
#   D2K_DXC=<path>: the dxc executable (otherwise searched in PATH, the Windows SDK, VULKAN_SDK)
# Without DXIL a Windows build still has 3D: render3d/gpu.cpp then asks SDL for a device that
# takes the build's formats (SPIR-V -> Vulkan). dxc from the Windows SDK signs the DXIL itself
# (dxil.dll next to it); so does dxc >= 1.8.2502 (built-in validator).
set(D2K_DXIL "AUTO" CACHE STRING "Compile the 3D shaders to DXIL with dxc (AUTO / ON / OFF)")
set_property(CACHE D2K_DXIL PROPERTY STRINGS AUTO ON OFF)

function(d2k_dxil_shaders target)
    if(D2K_DXIL STREQUAL "OFF" OR (D2K_DXIL STREQUAL "AUTO" AND NOT WIN32))
        return()
    endif()
    set(_hints "")
    if(WIN32)
        if(DEFINED ENV{WindowsSdkVerBinPath})  # set inside a VS developer prompt
            list(APPEND _hints "$ENV{WindowsSdkVerBinPath}/x64")
        endif()
        set(_pf86 "ProgramFiles(x86)")
        foreach(_root "$ENV{${_pf86}}" "$ENV{ProgramFiles}")
            if(_root)
                file(GLOB _kits LIST_DIRECTORIES true "${_root}/Windows Kits/10/bin/10.*")
                list(SORT _kits COMPARE NATURAL ORDER DESCENDING)  # newest SDK first
                foreach(_k IN LISTS _kits)
                    list(APPEND _hints "${_k}/x64")
                endforeach()
            endif()
        endforeach()
    endif()
    if(DEFINED ENV{VULKAN_SDK})
        list(APPEND _hints "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")
    endif()
    find_program(D2K_DXC NAMES dxc HINTS ${_hints} DOC "dxc (DirectX Shader Compiler) for the DXIL shaders")
    if(NOT D2K_DXC)
        if(D2K_DXIL STREQUAL "ON")
            message(FATAL_ERROR "D2K_DXIL=ON but dxc was not found (install the Windows SDK or set D2K_DXC)")
        endif()
        message(STATUS "3D shaders: dxc not found - no DXIL (Direct3D 12); the 3D mode uses Vulkan (SPIR-V)")
        return()
    endif()
    message(STATUS "3D shaders: DXIL via ${D2K_DXC}")
    set(_src "${CMAKE_SOURCE_DIR}/src/render3d/shaders.hlsl")
    set(_out "${CMAKE_BINARY_DIR}/generated/shaders_dxil.inc")
    add_custom_command(OUTPUT "${_out}"
        COMMAND "${CMAKE_COMMAND}" "-DDXC=${D2K_DXC}" "-DSRC=${_src}" "-DOUT=${_out}"
                -P "${CMAKE_SOURCE_DIR}/cmake/EmbedDXIL.cmake"
        DEPENDS "${_src}" "${CMAKE_SOURCE_DIR}/cmake/EmbedDXIL.cmake"
        COMMENT "Compiling the 3D shaders to DXIL (dxc)"
        VERBATIM)
    target_sources(${target} PRIVATE "${_out}")
    target_include_directories(${target} PRIVATE "${CMAKE_BINARY_DIR}/generated")
    target_compile_definitions(${target} PRIVATE D2K_HAVE_DXIL=1)
endfunction()
