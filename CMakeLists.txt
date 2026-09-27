# SPDX-License-Identifier: MIT
# Copyright (c) 2015-2026 Tomislav Radanovic

cmake_minimum_required(VERSION 3.22)

# Lets the MSVC debug-information format be chosen through
# CMAKE_MSVC_DEBUG_INFORMATION_FORMAT (CI sets it to Embedded / /Z7,
# since a compiler cache cannot key /Zi's shared PDB per object file).
# CMAKE_POLICY_DEFAULT_CMP0141 carries the same policy into every
# FetchContent'd dependency's own project() call, whose
# cmake_minimum_required() would otherwise leave it unset for that
# dependency's targets. Guarded for a CMake below 3.25, which does not
# know this policy.
if(POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
    set(CMAKE_POLICY_DEFAULT_CMP0141 NEW)
endif()

project(AlphaEngine C CXX)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# Add debug definitions and flags. Use generator expressions so this works
# correctly with multi-config generators (e.g. Visual Studio) where
# CMAKE_BUILD_TYPE is empty at configure time. With CMP0141 NEW and
# CMAKE_MSVC_DEBUG_INFORMATION_FORMAT unset, MSVC Debug already gets
# ProgramDatabase (/Zi) as CMake's own default, so only /Od is added here.
add_compile_definitions($<$<CONFIG:Debug>:_DEBUG>)
if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    add_compile_options($<$<CONFIG:Debug>:/Od>)
else()
    add_compile_options($<$<CONFIG:Debug>:-g> $<$<CONFIG:Debug>:-O0>)
endif()

# Sanitizers. ALPHAENGINE_SANITIZE is the value handed to -fsanitize=
# ("address,undefined" or "thread"; ASan and TSan cannot be combined). It is
# applied directory-wide from this point on, so every target configured
# below — the engine and the fetched dependencies (SDL3, ...) — is
# instrumented with the same runtime, which is what keeps a report inside
# SDL's code readable and avoids mixing instrumented and uninstrumented
# objects in one link. GCC / Clang only: MSVC spells its (ASan-only) support
# differently and the CI smoke job's sanitizer entry runs on Linux.
set(ALPHAENGINE_SANITIZE "" CACHE STRING
    "Sanitizers to build with, as the -fsanitize= value (e.g. address,undefined or thread); empty for none")
if(ALPHAENGINE_SANITIZE)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        message(FATAL_ERROR "ALPHAENGINE_SANITIZE is only supported with GCC or Clang")
    endif()
    add_compile_options(-fsanitize=${ALPHAENGINE_SANITIZE} -fno-omit-frame-pointer)
    add_link_options(-fsanitize=${ALPHAENGINE_SANITIZE})
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

add_definitions(-DVERSION_MAJOR=0)
add_definitions(-DVERSION_MINOR=0)
add_definitions(-DVERSION_PATCH=1)

if(APPLE)
    execute_process(COMMAND 
        xcrun --show-sdk-path OUTPUT_VARIABLE MACOS_SDK_PATH 
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    set(CMAKE_OSX_SYSROOT ${MACOS_SDK_PATH})
endif(APPLE)

set(LIBRARY_OUTPUT_PATH ${CMAKE_SOURCE_DIR}/Binaries/)
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_SOURCE_DIR}/Binaries/)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_VERBOSE_MAKEFILE false)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(VENDOR_ROOT ${CMAKE_SOURCE_DIR}/vendor/)

add_custom_target(build-time-make-directory ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory Binaries)

include_directories(${CMAKE_SOURCE_DIR})

# Main executable. Sources are contributed by per-directory CMakeLists.txt
# files via target_sources(). The initial source is supplied here so the
# target is well-formed before subdirectories extend it.
add_executable(AlphaEngine runtime/main_loop.cpp)

add_subdirectory(runtime)
add_subdirectory(core)
add_subdirectory(platform)
add_subdirectory(assets)
add_subdirectory(rendering_engine)
add_subdirectory(external)

# Shaders — the GLSL sources under shaders/ are embedded into the
# shader_registry library at build time (see shaders/CMakeLists.txt) and
# looked up through rendering_engine/gpu/shader_library. Debug builds
# also know where the source tree's shaders/ directory is so a shader
# can be edited on disk and picked up at the next launch without a C++
# rebuild (ALPHAENGINE_SHADER_DIR overrides the location).
add_subdirectory(shaders)
target_link_libraries(AlphaEngine shader_registry)
target_compile_definitions(AlphaEngine PRIVATE
    $<$<CONFIG:Debug>:ALPHAENGINE_SHADER_SOURCE_DIR="${CMAKE_SOURCE_DIR}/shaders">)

# Threads — the core worker pool (core/job_pool) uses std::thread. MSVC links its
# threading support implicitly; on Linux this pulls in pthread.
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(AlphaEngine Threads::Threads)

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    target_compile_options(AlphaEngine PRIVATE -stdlib=libc++)
    target_link_options(AlphaEngine PRIVATE -stdlib=libc++)
endif()

# Format-string checking. core::logging::message carries a printf-format
# attribute on GCC / Clang (ALPHAENGINE_PRINTF_FORMAT in core/log.hpp), so
# -Wformat validates every LOG_* call site's conversions against its
# arguments. MSVC has no equivalent outside /analyze.
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(AlphaEngine PRIVATE -Wformat)
endif()

# SDL3 — fetched and built statically from source
include(FetchContent)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.2.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(SDL3)
target_link_libraries(AlphaEngine SDL3::SDL3-static)

# GLM — header-only, fetched from upstream and pinned to 1.0.1
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glm)
target_link_libraries(AlphaEngine glm::glm)

# nlohmann/json — header-only, fetched from upstream and pinned to v3.11.3.
# Backs the settings.json loader in core/settings_parse. The FetchContent
# name is `json` so an offline build can point FETCHCONTENT_SOURCE_DIR_JSON
# at a local checkout.
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(json)
target_link_libraries(AlphaEngine nlohmann_json::nlohmann_json)

# glslang — fetched and built statically. Compiles GLSL to SPIR-V at
# runtime for the Vulkan backend's shader modules (cached on disk by
# rendering_engine/gpu/shader_compiler).
# ENABLE_OPT=OFF drops the SPIRV-Tools optimizer dependency: glslang's
# CMake hard-errors with ENABLE_OPT=ON unless SPIRV-Tools and
# SPIRV-Headers sit in its External/ directory, which a shallow tag
# fetch does not provide. Turning the optimizer on means fetching those
# two repositories as well; the engine ships unoptimised SPIR-V for now.
# ENABLE_RTTI stays ON: the shader compiler derives its #include
# resolver from glslang's polymorphic TShader::Includer and drives
# TShader / TProgram from RTTI-enabled engine code, and UBSan's vptr
# check (part of -fsanitize=undefined) references those classes'
# typeinfo, which a -fno-rtti glslang would never emit — the sanitizer
# build then fails to link.
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(ENABLE_RTTI ON CACHE BOOL "" FORCE)
set(ENABLE_CTEST OFF CACHE BOOL "" FORCE)
set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    glslang
    GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
    GIT_TAG 15.0.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glslang)
target_link_libraries(AlphaEngine
    glslang::glslang
    glslang::SPIRV
    glslang::glslang-default-resource-limits)

target_include_directories(AlphaEngine PRIVATE ${VENDOR_ROOT}stb/ ${VENDOR_ROOT}cgltf/)

# Vulkan — the GPU backend at rendering_engine/gpu/backend/vulkan/
# links against the loader from the system Vulkan SDK.
find_package(Vulkan REQUIRED)
target_link_libraries(AlphaEngine Vulkan::Vulkan)

# Vulkan Memory Allocator — header-only, fetched from upstream and
# pinned by release tag like the other dependencies. Its CMakeLists
# exports the GPUOpen::VulkanMemoryAllocator interface target (an
# include directory; install is off for a non-top-level project). The
# Vulkan backend sub-allocates every buffer and image from VMA's memory
# blocks instead of one vkAllocateMemory per resource; the single
# implementation TU is rendering_engine/gpu/backend/vulkan/
# vk_allocator.cpp, and the header lives outside the format / tidy
# scope with the rest of the fetched code.
FetchContent_Declare(
    vma
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG v3.4.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(vma)
target_link_libraries(AlphaEngine GPUOpen::VulkanMemoryAllocator)
# Clang enables -Wnullability-completeness by default and VMA annotates
# only its public API, so the implementation TU would emit some nine
# hundred warnings per build; that one warning is silenced for that
# one file (a vendored-code exemption, set here because source
# properties apply to targets defined in the same directory).
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set_source_files_properties(
        ${CMAKE_SOURCE_DIR}/rendering_engine/gpu/backend/vulkan/vk_allocator.cpp
        PROPERTIES COMPILE_OPTIONS "-Wno-nullability-completeness")
endif()

# Dear ImGui — fetched from source and built as a small static library
# from the same explicit-file pattern the rest of the tree uses. ImGui
# ships no CMakeLists, so FetchContent only populates the sources and we
# compile the core plus the SDL3 platform backend and the Vulkan
# renderer backend ourselves. The docking branch is pinned by tag so the
# debug tooling can dock and tear off panels.
#
# ImGui is debug-only: it is linked into AlphaEngine and the
# ALPHAENGINE_HAS_IMGUI macro is defined for Debug configurations only,
# via generator expressions so this also holds for multi-config
# generators (Visual Studio) where CMAKE_BUILD_TYPE is empty at configure
# time. Release builds never link ImGui and the debug-UI layer compiles
# down to empty no-ops.
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.8-docking
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(imgui)

# ImGuizmo — the 3D translate/rotate/scale gizmo ImGui itself does not
# provide, fetched the same way and folded into the same `imgui` static
# library below so it shares its Debug-only fate: Release never compiles
# or links it either. Only its ImGuizmo.{h,cpp} are used; the repository
# also ships GraphEditor / ImCurveEdit / ImGradient / ImSequencer /
# ImVectorEditor, unrelated widget libraries this engine has no editor
# for, so SOURCE_SUBDIR points at the one directory with no CMakeLists.txt
# of its own — FetchContent_MakeAvailable then only populates the source
# (like ImGui above) instead of add_subdirectory-ing ImGuizmo's own
# ImGuizmo::imguizmo target (and its own copy of every one of those
# extra widgets) into every configuration. Pinned to the commit that
# updates ImGuizmo's AddPolyline() / AddRect() calls for the ImGui
# 1.92.8 argument order above; ImGuizmo's own tags predate that ImGui
# release.
FetchContent_Declare(
    imguizmo
    GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
    GIT_TAG 18cef5e031d8c6973d80284c67f60549fafd78c1
    SOURCE_SUBDIR src
)
FetchContent_MakeAvailable(imguizmo)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
    ${imguizmo_SOURCE_DIR}/src/ImGuizmo.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${imguizmo_SOURCE_DIR}/src)
# The SDL3 backend needs the SDL headers; the Vulkan backend links
# against the loader (default Vulkan prototypes).
target_link_libraries(imgui PUBLIC SDL3::SDL3-static Vulkan::Vulkan)
target_link_libraries(AlphaEngine $<$<CONFIG:Debug>:imgui>)
target_compile_definitions(AlphaEngine PRIVATE $<$<CONFIG:Debug>:ALPHAENGINE_HAS_IMGUI>)

# Jolt Physics — fetched and built as a static library behind the engine's
# physics subsystem (runtime/physics). Jolt keeps its CMake project under
# Build/, and as a non-top-level project it configures only the library;
# the unit tests, samples, viewer and install rules are switched off
# explicitly all the same. The FetchContent name is `JoltPhysics`, so an
# offline build points FETCHCONTENT_SOURCE_DIR_JOLTPHYSICS at a checkout.
#
# The options keep Jolt's build from reaching into ours and ours from
# reaching into Jolt's:
# - no -Werror / /WX (ENABLE_ALL_WARNINGS), no link-time optimisation (a
#   Release libJolt.a of LTO objects would not link into a non-LTO
#   engine), and CMake's own per-config flags instead of Jolt's overrides;
# - the shared (DLL) MSVC runtime like the rest of the build — Jolt
#   defaults to the static one, which fails the link with a runtime
#   mismatch;
# - RTTI on, as for glslang above: engine code derives from Jolt's
#   interfaces (contact listener, layer filters), and UBSan's vptr check
#   needs the typeinfo a -fno-rtti library never emits;
# - the GPU compute interfaces (DX12 / Vulkan / Metal / CPU, which need
#   dxc), the debug renderer, the profiler, object streams and floating-
#   point exceptions off — the engine uses none of them and draws its
#   debug geometry itself;
# - x86 instruction sets capped at SSE4.2 (x86-64-v2). Jolt defaults to
#   AVX2 + FMA + F16C + LZCNT/TZCNT, which would make the executable
#   require a Haswell-class CPU; inline functions shared between the
#   physics translation units and the rest of the engine are merged at
#   link time, so whatever those units are compiled for can surface in
#   engine code too.
set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)
set(ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(JPH_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(OVERRIDE_CXX_FLAGS OFF CACHE BOOL "" FORCE)
set(GENERATE_DEBUG_SYMBOLS OFF CACHE BOOL "" FORCE)
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
set(CPP_RTTI_ENABLED ON CACHE BOOL "" FORCE)
set(JPH_USE_DX12 OFF CACHE BOOL "" FORCE)
set(JPH_USE_VK OFF CACHE BOOL "" FORCE)
set(JPH_USE_MTL OFF CACHE BOOL "" FORCE)
set(JPH_USE_CPU_COMPUTE OFF CACHE BOOL "" FORCE)
set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(PROFILER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(ENABLE_OBJECT_STREAM OFF CACHE BOOL "" FORCE)
set(FLOATING_POINT_EXCEPTIONS_ENABLED OFF CACHE BOOL "" FORCE)
set(USE_SSE4_1 ON CACHE BOOL "" FORCE)
set(USE_SSE4_2 ON CACHE BOOL "" FORCE)
set(USE_AVX OFF CACHE BOOL "" FORCE)
set(USE_AVX2 OFF CACHE BOOL "" FORCE)
set(USE_AVX512 OFF CACHE BOOL "" FORCE)
set(USE_LZCNT OFF CACHE BOOL "" FORCE)
set(USE_TZCNT OFF CACHE BOOL "" FORCE)
set(USE_F16C OFF CACHE BOOL "" FORCE)
set(USE_FMADD OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    JoltPhysics
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG v5.6.0
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR Build
)
FetchContent_MakeAvailable(JoltPhysics)
# Jolt publishes its configuration as usage requirements: JPH_* defines
# that must be identical in every translation unit that includes a Jolt
# header (Jolt checks them against the library in RegisterTypes) plus the
# matching instruction-set flags. Linking the target normally would push
# all of that onto every engine source, so the engine links the library
# alone — through an interface target whose $<LINK_ONLY:...> link carries
# no usage requirements — and hands them to the physics implementation
# unit, the only one that includes Jolt, as source properties (set here
# because source properties apply to targets defined in the same
# directory). Both sides evaluate the same properties per configuration,
# so the Debug / Release defines always agree with the library's.
add_library(jolt_link_only INTERFACE)
target_link_libraries(jolt_link_only INTERFACE $<LINK_ONLY:Jolt>)
target_link_libraries(AlphaEngine jolt_link_only)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/runtime/physics/physics_world.cpp
    PROPERTIES
        COMPILE_DEFINITIONS "$<TARGET_PROPERTY:Jolt,INTERFACE_COMPILE_DEFINITIONS>"
        COMPILE_OPTIONS "$<TARGET_PROPERTY:Jolt,INTERFACE_COMPILE_OPTIONS>"
        INCLUDE_DIRECTORIES "$<TARGET_PROPERTY:Jolt,INTERFACE_INCLUDE_DIRECTORIES>")

# KTX-Software — libktx's read-only library (ktx_read), fetched and built
# statically behind the KTX2 decoder of the CPU asset module
# (assets/texture_decode): the KTX2 container, its zstd /
# zlib supercompression, the Basis Universal transcoder (ETC1S / UASTC to
# BC1-7, ASTC 4x4 or RGBA8) and the ASTC decoder it links. The tools, tests,
# docs, bindings, ETC unpacking and the GL / Vulkan upload helpers are
# off (the engine uploads through its own gpu device), and the directory is
# excluded from `all`, so the full libktx with the Basis and ASTC encoders is
# configured but never built. The FetchContent name is `ktx`, so an offline
# build points FETCHCONTENT_SOURCE_DIR_KTX at a checkout; the CTS test
# submodule is not cloned. KTX's configure needs CMake 3.22 and bash (Git
# for Windows' on Windows) and reads its version from the checkout's tag.
# astc-encoder is capped at SSE4.1 for the same reason as Jolt above (KTX
# defaults it to AVX2), and BUILD_SHARED_LIBS is held off only around the
# fetch because KTX defaults to a shared libktx.
set(KTX_FEATURE_TESTS OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_TOOLS OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_DOC OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_JNI OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_PY OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_ETC_UNPACK OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_GL_UPLOAD OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_VK_UPLOAD OFF CACHE BOOL "" FORCE)
set(BASISU_SUPPORT_OPENCL OFF CACHE BOOL "" FORCE)
set(ASTCENC_ISA_SSE41 ON CACHE BOOL "" FORCE)
set(ALPHAENGINE_BUILD_SHARED_LIBS_SAVED ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF)
FetchContent_Declare(
    ktx
    GIT_REPOSITORY https://github.com/KhronosGroup/KTX-Software.git
    GIT_TAG v4.4.2
    GIT_SHALLOW TRUE
    GIT_SUBMODULES ""
)
FetchContent_MakeAvailable(ktx)
set(BUILD_SHARED_LIBS ${ALPHAENGINE_BUILD_SHARED_LIBS_SAVED})
set_property(DIRECTORY ${ktx_SOURCE_DIR} PROPERTY EXCLUDE_FROM_ALL TRUE)
target_link_libraries(AlphaEngine ktx_read)

# Lua 5.4 and sol2 — the embedded scripting behind runtime/scripting (the
# script host and lua_behavior).
#
# Lua comes from the Lua team's own repository, pinned by release tag. It
# ships no CMakeLists, so FetchContent only populates it and the engine
# builds the core and the standard libraries as a small static C library
# from an explicit file list: the stand-alone interpreter (lua.c), the
# test harness (ltests.c) and the one-file amalgamation (onelua.c) are left
# out. It is compiled as C, so a Lua error unwinds with longjmp and sol2
# turns C++ exceptions into Lua errors at every boundary. On POSIX systems
# LUA_USE_POSIX picks the POSIX variants of the few OS calls the standard
# libraries make (mkstemp rather than tmpnam, say). The FetchContent name
# is `lua`, so an offline build points FETCHCONTENT_SOURCE_DIR_LUA at a
# checkout.
#
# sol2 is header-only and pinned by release tag. Its own CMakeLists needs
# CMake 3.26 and only wraps the include/ directory in an interface target,
# so SOURCE_SUBDIR names that directory — which has no CMakeLists — and
# FetchContent only populates the sources (FETCHCONTENT_SOURCE_DIR_SOL2
# for an offline build).
#
# Neither is a usage requirement of the engine target: the scripting
# implementation units are the only ones that include Lua or sol2, so the
# include directories and sol2's configuration go to those files alone, as
# source properties (set here because source properties apply to targets
# defined in the same directory), and the rest of the engine never parses
# them. Every one of those units gets the same sol2 configuration: all the
# safety checks on (argument and return types are checked, so a script
# passing the wrong type raises a Lua error rather than corrupting memory)
# and sol2's own error printing off (the engine logs script errors). MSVC
# needs /bigobj for the binding units' section count. The engine builds
# without warnings-as-errors, and neither library adds any.
FetchContent_Declare(
    lua
    GIT_REPOSITORY https://github.com/lua/lua.git
    GIT_TAG v5.4.8
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(lua)
add_library(lua STATIC
    ${lua_SOURCE_DIR}/lapi.c
    ${lua_SOURCE_DIR}/lauxlib.c
    ${lua_SOURCE_DIR}/lbaselib.c
    ${lua_SOURCE_DIR}/lcode.c
    ${lua_SOURCE_DIR}/lcorolib.c
    ${lua_SOURCE_DIR}/lctype.c
    ${lua_SOURCE_DIR}/ldblib.c
    ${lua_SOURCE_DIR}/ldebug.c
    ${lua_SOURCE_DIR}/ldo.c
    ${lua_SOURCE_DIR}/ldump.c
    ${lua_SOURCE_DIR}/lfunc.c
    ${lua_SOURCE_DIR}/lgc.c
    ${lua_SOURCE_DIR}/linit.c
    ${lua_SOURCE_DIR}/liolib.c
    ${lua_SOURCE_DIR}/llex.c
    ${lua_SOURCE_DIR}/lmathlib.c
    ${lua_SOURCE_DIR}/lmem.c
    ${lua_SOURCE_DIR}/loadlib.c
    ${lua_SOURCE_DIR}/lobject.c
    ${lua_SOURCE_DIR}/lopcodes.c
    ${lua_SOURCE_DIR}/loslib.c
    ${lua_SOURCE_DIR}/lparser.c
    ${lua_SOURCE_DIR}/lstate.c
    ${lua_SOURCE_DIR}/lstring.c
    ${lua_SOURCE_DIR}/lstrlib.c
    ${lua_SOURCE_DIR}/ltable.c
    ${lua_SOURCE_DIR}/ltablib.c
    ${lua_SOURCE_DIR}/ltm.c
    ${lua_SOURCE_DIR}/lundump.c
    ${lua_SOURCE_DIR}/lutf8lib.c
    ${lua_SOURCE_DIR}/lvm.c
    ${lua_SOURCE_DIR}/lzio.c)
target_compile_definitions(lua PRIVATE
    $<$<NOT:$<PLATFORM_ID:Windows>>:LUA_USE_POSIX>
    $<$<C_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>)
target_link_libraries(AlphaEngine lua)

FetchContent_Declare(
    sol2
    GIT_REPOSITORY https://github.com/ThePhD/sol2.git
    GIT_TAG v3.5.0
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR include
)
FetchContent_MakeAvailable(sol2)

set(ALPHAENGINE_SCRIPTING_SOURCES
    ${CMAKE_SOURCE_DIR}/runtime/scripting/lua_behavior.cpp
    ${CMAKE_SOURCE_DIR}/runtime/scripting/math_bindings.cpp
    ${CMAKE_SOURCE_DIR}/runtime/scripting/script_host.cpp
    ${CMAKE_SOURCE_DIR}/runtime/scripting/world_bindings.cpp)
set(ALPHAENGINE_SCRIPTING_OPTIONS "")
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    list(APPEND ALPHAENGINE_SCRIPTING_OPTIONS /bigobj)
endif()
set_source_files_properties(
    ${ALPHAENGINE_SCRIPTING_SOURCES}
    PROPERTIES
        INCLUDE_DIRECTORIES "${sol2_SOURCE_DIR}/include;${lua_SOURCE_DIR}"
        COMPILE_DEFINITIONS "SOL_ALL_SAFETIES_ON=1;SOL_PRINT_ERRORS=0"
        COMPILE_OPTIONS "${ALPHAENGINE_SCRIPTING_OPTIONS}")

if(APPLE)
    target_link_libraries(AlphaEngine "-framework CoreFoundation -framework IOKit -framework CoreVideo")
endif(APPLE)
