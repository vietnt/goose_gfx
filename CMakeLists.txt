# The gfx package's native part: SDL3, and the C layers that gfx.goose (over
# SDL_GPU), audio.goose (SDL's audio) and ui.goose (Nuklear, drawn through
# gfx) declare their extern fns against, under src/. One package, since the
# three share SDL and ui draws through gfx. Built by the Goose compiler
# through its wrapper project when a program needs the package
# (docs/design/packages.md §5.2), or into the compiler itself as a builtin
# package (§7.1). What a user of the package links is goose_gfx_package,
# which carries all three layers and SDL.

cmake_minimum_required(VERSION 3.20)
project(goose_gfx_package C)

# SDL3 and Nuklear: the submodules beside this file, or the Goose tree's own
# while the package lives in that tree. Without either the package builds
# nothing, and says so.
foreach(dep IN ITEMS SDL nuklear)
    set(GOOSE_GFX_${dep} "" CACHE PATH "The ${dep} source tree")
    if(NOT GOOSE_GFX_${dep})
        foreach(cand "${CMAKE_CURRENT_SOURCE_DIR}/third_party/${dep}"
                     "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/${dep}")
            if(EXISTS "${cand}/CMakeLists.txt" OR EXISTS "${cand}/nuklear.h")
                set(GOOSE_GFX_${dep} "${cand}")
                break()
            endif()
        endforeach()
    endif()
    if(NOT GOOSE_GFX_${dep})
        message(STATUS "gfx: ${dep} not found (the third_party/${dep} submodule); building nothing")
        return()
    endif()
endforeach()

# --- a video backend on Linux ------------------------------------------------
# SDL's configure does not fail without the X11 and Wayland development
# headers: it quietly builds an SDL that cannot open a window. Build nothing
# instead, saying what to install.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(X11 QUIET)
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(GFX_WAYLAND QUIET wayland-client)
    endif()
    if(NOT X11_FOUND AND NOT GFX_WAYLAND_FOUND)
        message(STATUS "gfx: neither the X11 nor the Wayland development files were found, "
                       "so SDL could not open a window; building nothing. "
                       "Install the packages in SDL's docs/README-linux.md "
                       "(at least libx11-dev libxext-dev, or libwayland-dev libxkbcommon-dev "
                       "wayland-protocols) and reconfigure.")
        return()
    endif()
endif()

# --- SDL ---------------------------------------------------------------------
# Static only. SDL_DEPS_SHARED stays on: on Linux SDL then links only the C
# library and loads X11, Wayland, audio and udev at runtime, so a program
# gains no hard GUI dependencies. SDL_GPU draws through neither the 2D
# renderer nor OpenGL, and nothing uses the camera, so those are left out.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
set(SDL_RENDER OFF CACHE BOOL "" FORCE)
set(SDL_OPENGL OFF CACHE BOOL "" FORCE)
set(SDL_OPENGLES OFF CACHE BOOL "" FORCE)
add_subdirectory("${GOOSE_GFX_SDL}" SDL EXCLUDE_FROM_ALL)

# --- SDL fixes carried until the pinned SDL has them --------------------------
# A fixed backend compiles from a copy in the build tree, and the submodule
# stays as fetched. gfx_sdl_read reads a backend SDL builds into gfx_text, or
# leaves gfx_text empty when this platform doesn't build it; gfx_sdl_fix
# replaces a piece of it that occurs exactly once, failing otherwise, which
# means SDL has changed there and the fix needs looking at again;
# gfx_sdl_compile swaps the original for the fixed copy.
get_target_property(gfx_sdl_sources SDL3-static SOURCES)
function(gfx_sdl_read path)
    set(text "")
    if("${GOOSE_GFX_SDL}/${path}" IN_LIST gfx_sdl_sources)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${GOOSE_GFX_SDL}/${path}")
        file(READ "${GOOSE_GFX_SDL}/${path}" text)
        string(REPLACE "\r\n" "\n" text "${text}")
    endif()
    set(gfx_text "${text}" PARENT_SCOPE)
endfunction()
function(gfx_sdl_fix old new)
    string(FIND "${gfx_text}" "${old}" first)
    string(FIND "${gfx_text}" "${old}" last REVERSE)
    if(first EQUAL -1 OR NOT first EQUAL last)
        message(FATAL_ERROR "gfx: SDL no longer has this code exactly once; look at the fix for it "
                            "in this CMakeLists again:\n${old}")
    endif()
    string(REPLACE "${old}" "${new}" text "${gfx_text}")
    set(gfx_text "${text}" PARENT_SCOPE)
endfunction()
function(gfx_sdl_compile path)
    get_filename_component(name "${path}" NAME)
    get_filename_component(dir "${GOOSE_GFX_SDL}/${path}" DIRECTORY)
    set(fixed "${CMAKE_CURRENT_BINARY_DIR}/sdl_fixed/${name}")
    file(WRITE "${fixed}" "${gfx_text}")
    get_target_property(sources SDL3-static SOURCES)
    list(REMOVE_ITEM sources "${GOOSE_GFX_SDL}/${path}")
    set_property(TARGET SDL3-static PROPERTY SOURCES ${sources} "${fixed}")
    # Its relative includes ("../SDL_sysgpu.h") resolve from the original's directory.
    set_source_files_properties("${fixed}" TARGET_DIRECTORY SDL3-static
                                PROPERTIES INCLUDE_DIRECTORIES "${dir}")
endfunction()

# SDL 3.4.16's Direct3D 12 backend registers its debug-layer message callback
# with a NULL cookie, which ID3D12InfoQueue1::RegisterMessageCallback rejects
# with E_INVALIDARG, so no debug-layer message ever reaches SDL's log. SDL
# fixed it in 67bf94eed5 ("gpu: Pass a valid pCallbackCookie pointer to
# RegisterMessageCallback"), in release-3.4.18 as 39c59cd2e1. Only Windows
# builds the backend.
gfx_sdl_read(src/gpu/d3d12/SDL_gpu_d3d12.c)
if(gfx_text)
    set(gfx_call "(ID3D12InfoQueue1_RegisterMessageCallback\\([^;]*D3D12_MESSAGE_CALLBACK_FLAG_NONE,[ \t\r\n]*NULL,[ \t\r\n]*)NULL\\);")
    if(NOT gfx_text MATCHES "${gfx_call}")
        message(FATAL_ERROR "gfx: SDL_gpu_d3d12.c no longer registers its debug callback with a NULL "
                            "cookie; remove the fix for it from this CMakeLists")
    endif()
    string(REGEX REPLACE "${gfx_call}" "DWORD callbackCookie = 0;\n    \\1&callbackCookie);"
           gfx_text "${gfx_text}")
    gfx_sdl_compile(src/gpu/d3d12/SDL_gpu_d3d12.c)
endif()

# SDL's Vulkan backend compacts fragmented memory during a submit: it moves
# each resource of a block into new memory, copying its contents, and points
# the resource's handle at the new one. A command buffer still being recorded
# then holds the old one, and its later commands, compute writes among them,
# reach the old memory after the copy was made, so everything later sees the
# contents from before them. SDL guards only against pending transfers
# (still so in SDL's main branch as of 2026-10). Here it doesn't compact
# while another command buffer is being recorded, as when gfx submits a new
# resource's first contents in a frame; it does so at a later submit.
gfx_sdl_read(src/gpu/vulkan/SDL_gpu_vulkan.c)
if(gfx_text)
    gfx_sdl_fix("    Uint8 defragInProgress;\n"
                "    Uint8 defragInProgress;\n    SDL_AtomicInt recordingCommandBufferCount;\n")
    gfx_sdl_fix("    if (!VULKAN_INTERNAL_BeginCommandBuffer(renderer, commandBuffer)) {\n        return NULL;\n    }\n"
                "    if (!VULKAN_INTERNAL_BeginCommandBuffer(renderer, commandBuffer)) {\n        return NULL;\n    }\n    SDL_AtomicIncRef(&renderer->recordingCommandBufferCount);\n")
    gfx_sdl_fix("        renderer->claimedWindowCount == 0;\n\n    SDL_LockMutex(renderer->submitLock);\n"
                "        renderer->claimedWindowCount == 0;\n\n    (void)SDL_AtomicDecRef(&renderer->recordingCommandBufferCount);\n    SDL_LockMutex(renderer->submitLock);\n")
    gfx_sdl_fix("        renderer->allocationsToDefragCount > 0 &&\n        !renderer->defragInProgress) {\n        if (!VULKAN_INTERNAL_DefragmentMemory("
                "        renderer->allocationsToDefragCount > 0 &&\n        !renderer->defragInProgress &&\n        SDL_GetAtomicInt(&renderer->recordingCommandBufferCount) == 0) {\n        if (!VULKAN_INTERNAL_DefragmentMemory(")
    gfx_sdl_fix("    renderer = vulkanCommandBuffer->renderer;\n\n    result = renderer->vkResetCommandBuffer("
                "    renderer = vulkanCommandBuffer->renderer;\n    (void)SDL_AtomicDecRef(&renderer->recordingCommandBufferCount);\n\n    result = renderer->vkResetCommandBuffer(")
    gfx_sdl_compile(src/gpu/vulkan/SDL_gpu_vulkan.c)
endif()

# --- the gfx layer -----------------------------------------------------------
add_library(goose_gfx STATIC
    src/gfx/gfx_api.h
    src/gfx/gfx_blob.h
    src/gfx/gfx_internal.h
    src/gfx/gfx_device.c
    src/gfx/gfx_resources.c
    src/gfx/gfx_pipeline.c
    src/gfx/gfx_draw.c
)
target_link_libraries(goose_gfx PUBLIC SDL3::SDL3-static)
# The Vulkan headers SDL carries, for the feature structs SDL_GPUVulkanOptions takes.
target_include_directories(goose_gfx PRIVATE "${GOOSE_GFX_SDL}/src/video/khronos")

# --- the audio layer: the PCM mixer, no window ---------------------------------
add_library(goose_audio STATIC src/audio/audio.c src/audio/audio_api.h)
target_link_libraries(goose_audio PUBLIC SDL3::SDL3-static)

# --- the ui layer: Nuklear, one header, compiled once in ui_nuklear.c ----------
set(ui_layer_sources
    src/ui/ui_core.c
    src/ui/ui_font.c
    src/ui/ui_window.c
    src/ui/ui_widget.c
    src/ui/ui_style.c
    src/ui/ui_draw.c
)
add_library(goose_ui STATIC src/ui/ui_api.h src/ui/ui_internal.h src/ui/ui_nuklear.c ${ui_layer_sources})
target_include_directories(goose_ui PRIVATE "${GOOSE_GFX_nuklear}")
if(NOT WIN32)
    target_link_libraries(goose_ui PUBLIC m)
endif()

foreach(t IN ITEMS goose_gfx goose_audio goose_ui)
    set_target_properties(${t} PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON
                          INTERPROCEDURAL_OPTIMIZATION OFF)
endforeach()
# Warnings for the layers, none for Nuklear's implementation: third-party
# code is not ours to act on.
if(MSVC)
    foreach(t IN ITEMS goose_gfx goose_audio)
        target_compile_options(${t} PRIVATE /W4 /utf-8)
    endforeach()
    set_source_files_properties(${ui_layer_sources} PROPERTIES COMPILE_OPTIONS "/W4;/utf-8")
    set_source_files_properties(src/ui/ui_nuklear.c PROPERTIES COMPILE_OPTIONS "/W0;/utf-8")
    foreach(t IN ITEMS goose_gfx goose_audio goose_ui)
        target_compile_definitions(${t} PRIVATE _CRT_SECURE_NO_WARNINGS)
    endforeach()
else()
    foreach(t IN ITEMS goose_gfx goose_audio)
        target_compile_options(${t} PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
    endforeach()
    set_source_files_properties(${ui_layer_sources} PROPERTIES COMPILE_OPTIONS
                                "-Wall;-Wextra;-Wno-missing-field-initializers")
    set_source_files_properties(src/ui/ui_nuklear.c PROPERTIES COMPILE_OPTIONS "-w")
endif()

# --- the package -------------------------------------------------------------
add_library(goose_gfx_package INTERFACE)
target_link_libraries(goose_gfx_package INTERFACE goose_gfx goose_audio goose_ui)

# The audio layer's own test, never part of a normal build: SDL's dummy
# driver, no device.
add_executable(goose_audio_test EXCLUDE_FROM_ALL test/audio_device.c)
target_link_libraries(goose_audio_test PRIVATE goose_audio)
set_target_properties(goose_audio_test PROPERTIES C_STANDARD 11)

file(STRINGS "${GOOSE_GFX_SDL}/include/SDL3/SDL_version.h" gfx_sdl_version
     REGEX "^#define SDL_(MAJOR|MINOR|MICRO)_VERSION +[0-9]+")
string(REGEX REPLACE "[^;]* ([0-9]+);[^;]* ([0-9]+);[^;]* ([0-9]+)" "\\1.\\2.\\3"
       gfx_sdl_version "${gfx_sdl_version}")
file(READ "${GOOSE_GFX_nuklear}/clib.json" ui_clib)
string(REGEX MATCH "\"version\": *\"([0-9.]+)\"" ui_version_match "${ui_clib}")
message(STATUS "gfx: SDL ${gfx_sdl_version} and Nuklear ${CMAKE_MATCH_1}, static")
