cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(industrial_bf LANGUAGES C)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

option(IBF_ENABLE_JIT "Build the GNU Lightning JIT backend" OFF)
option(IBF_ENABLE_ASSERTS "Enable @/! assertions in optimized BF programs" ON)
option(IBF_ENABLE_DEBUGGER "Enable the interactive debugger" OFF)
option(IBF_ENABLE_FAST_ROL "Enable the AVX2 rolling optimizer" OFF)
option(IBF_ENABLE_DUMP_TAPE "Allow dumping tape.bin after an assertion" OFF)
option(IBF_PORTABLE_DISPATCH "Use the portable switch-based evaluator" ON)
set(IBF_PAGE_SIZE_POWER "29" CACHE STRING "Industrial-BF page size as a power of two")

if(CMAKE_SIZEOF_VOID_P LESS 8 AND IBF_PAGE_SIZE_POWER GREATER_EQUAL 28)
    message(FATAL_ERROR
        "The default Industrial-BF tape requires a 64-bit host build. "
        "Use an x64 compiler/toolchain, or explicitly choose a smaller "
        "IBF_PAGE_SIZE_POWER for evaluator-only experiments.")
endif()

if(MSVC)
    set(IBF_PORTABLE_DISPATCH ON CACHE BOOL "Use the portable switch-based evaluator" FORCE)
endif()

if(WIN32 AND IBF_ENABLE_JIT)
    message(FATAL_ERROR "IBF_ENABLE_JIT is not supported by this native Windows build. Use the portable evaluator.")
endif()

set(ASSERTS ${IBF_ENABLE_ASSERTS})
set(DEBUGGER ${IBF_ENABLE_DEBUGGER})
set(FAST_ROL ${IBF_ENABLE_FAST_ROL})
set(DUMP_TAPE ${IBF_ENABLE_DUMP_TAPE})
set(JIT ${IBF_ENABLE_JIT})
set(DISABLE_ROLLING OFF)

set(IBF_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${IBF_GENERATED_DIR}")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/evaluator/config.cmake.h.in"
    "${IBF_GENERATED_DIR}/ibf_config_generated.h"
    @ONLY
)

add_executable(ibf "${CMAKE_CURRENT_SOURCE_DIR}/evaluator/main.c")
target_include_directories(ibf PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/evaluator"
    "${IBF_GENERATED_DIR}"
)
target_compile_definitions(ibf PRIVATE IBF_CONFIG_HEADER="ibf_config_generated.h")

if(IBF_PORTABLE_DISPATCH)
    target_compile_definitions(ibf PRIVATE IBF_PORTABLE_DISPATCH=1)
endif()

if(IBF_ENABLE_JIT)
    find_path(LIGHTNING_INCLUDE_DIR lightning.h REQUIRED)
    find_library(LIGHTNING_LIBRARY NAMES lightning REQUIRED)
    target_include_directories(ibf PRIVATE "${LIGHTNING_INCLUDE_DIR}")
    target_link_libraries(ibf PRIVATE "${LIGHTNING_LIBRARY}")
endif()

if(IBF_ENABLE_FAST_ROL)
    if(MSVC)
        target_compile_options(ibf PRIVATE /arch:AVX2)
    else()
        target_compile_options(ibf PRIVATE -mavx2)
    endif()
endif()

if(MSVC)
    target_compile_options(ibf PRIVATE /O2 /W4 /wd4244 /wd4267)
    target_compile_definitions(ibf PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(ibf PRIVATE -O3 -Wall -Wextra -Wno-unused-parameter -Wno-implicit-fallthrough)
endif()

add_executable(bld "${CMAKE_CURRENT_SOURCE_DIR}/loader/loader.c")
add_executable(bpk "${CMAKE_CURRENT_SOURCE_DIR}/packer/packer.c")

foreach(target IN ITEMS ibf bld bpk)
    if(COMMAND doombf_set_runtime_output)
        doombf_set_runtime_output(${target})
    elseif(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
        set_target_properties(${target} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        )
        foreach(configuration IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
            string(TOUPPER "${configuration}" configuration_upper)
            set_target_properties(${target} PROPERTIES
                "RUNTIME_OUTPUT_DIRECTORY_${configuration_upper}" "${CMAKE_BINARY_DIR}/bin"
            )
        endforeach()
    endif()
endforeach()
