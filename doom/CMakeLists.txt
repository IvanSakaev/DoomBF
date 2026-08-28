cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(doombf_doom LANGUAGES C)
    option(DOOMBF_BUILD_NATIVE "Build Doom with the native Windows/X11 frontend" ON)
    option(DOOMBF_BUILD_BF "Build bfk_doom.elf and translate it to doom.bpk" OFF)
    set(DOOMBF_OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin" CACHE PATH "Artifact output directory")
    file(MAKE_DIRECTORY "${DOOMBF_OUTPUT_DIR}")
    function(doombf_set_runtime_output target)
        set_target_properties(${target} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${DOOMBF_OUTPUT_DIR}"
        )
        foreach(configuration IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
            string(TOUPPER "${configuration}" configuration_upper)
            set_target_properties(${target} PROPERTIES
                "RUNTIME_OUTPUT_DIRECTORY_${configuration_upper}" "${DOOMBF_OUTPUT_DIR}"
            )
        endforeach()
    endfunction()
endif()

get_filename_component(DOOMBF_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(DOOM_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/DoomSources.cmake")

set(DOOM_WAD "${CMAKE_CURRENT_SOURCE_DIR}/data/doom.wad")

if(DOOMBF_BUILD_NATIVE)
    if(WIN32)
        set(DOOM_NATIVE_FRONTEND "${CMAKE_CURRENT_SOURCE_DIR}/win_doom.c")
        set(DOOM_NATIVE_OUTPUT_NAME "win_doom")
    else()
        set(DOOM_NATIVE_FRONTEND "${CMAKE_CURRENT_SOURCE_DIR}/lnx_doom.c")
        set(DOOM_NATIVE_OUTPUT_NAME "lnx_doom")
        find_package(X11 REQUIRED)
    endif()

    add_executable(doom_native
        "${DOOM_NATIVE_FRONTEND}"
        ${DOOM_BACKEND_SOURCES}
        ${DOOM_CRT_SOURCES}
    )
    target_include_directories(doom_native PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/backend"
        "${CMAKE_CURRENT_SOURCE_DIR}/crt"
    )
    set_target_properties(doom_native PROPERTIES
        OUTPUT_NAME "${DOOM_NATIVE_OUTPUT_NAME}"
        C_STANDARD 11
        C_STANDARD_REQUIRED YES
    )
    doombf_set_runtime_output(doom_native)

    if(WIN32)
        target_link_libraries(doom_native PRIVATE user32 gdi32)
        target_compile_definitions(doom_native PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_include_directories(doom_native PRIVATE ${X11_INCLUDE_DIR})
        target_link_libraries(doom_native PRIVATE ${X11_LIBRARIES})
    endif()

    if(MSVC)
        target_compile_options(doom_native PRIVATE /O2)
    else()
        target_compile_options(doom_native PRIVATE -O2 -fsigned-char)
    endif()

    if(EXISTS "${DOOM_WAD}")
        add_custom_command(TARGET doom_native POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${DOOM_WAD}" "$<TARGET_FILE_DIR:doom_native>/doom.wad"
            COMMENT "Copying doom.wad next to the native executable"
        )
    endif()
endif()

if(DOOMBF_BUILD_BF)
    if(NOT EXISTS "${DOOM_WAD}")
        message(FATAL_ERROR
            "doom/data/doom.wad is required for DOOMBF_BUILD_BF=ON. "
            "See doom/data/README.md.")
    endif()

    find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" -c "import capstone, elftools; assert hasattr(capstone, 'CS_ARCH_RISCV')"
        RESULT_VARIABLE DOOMBF_PYTHON_DEPS_RESULT
        ERROR_VARIABLE DOOMBF_PYTHON_DEPS_ERROR
    )
    if(NOT DOOMBF_PYTHON_DEPS_RESULT EQUAL 0)
        message(FATAL_ERROR
            "Python packages capstone and pyelftools are required. "
            "Run: ${Python3_EXECUTABLE} -m pip install -r ${DOOMBF_REPO_ROOT}/requirements.txt\n"
            "${DOOMBF_PYTHON_DEPS_ERROR}")
    endif()

    set(RISCV_GCC_EXECUTABLE "" CACHE FILEPATH "Path to riscv64-unknown-elf-gcc or compatible GCC")
    if(NOT RISCV_GCC_EXECUTABLE)
        find_program(DOOMBF_FOUND_RISCV_GCC
            NAMES
                riscv64-unknown-elf-gcc
                riscv64-unknown-elf-gcc.exe
                riscv64-elf-gcc
                riscv64-elf-gcc.exe
                riscv32-unknown-elf-gcc
                riscv32-unknown-elf-gcc.exe
                riscv32-none-elf-gcc
                riscv32-none-elf-gcc.exe
            HINTS
                "${DOOMBF_REPO_ROOT}/toolchains/riscv/bin"
                "${DOOMBF_REPO_ROOT}/tools/riscv/bin"
        )
        if(DOOMBF_FOUND_RISCV_GCC)
            set(RISCV_GCC_EXECUTABLE "${DOOMBF_FOUND_RISCV_GCC}" CACHE FILEPATH
                "Path to riscv64-unknown-elf-gcc or compatible GCC" FORCE)
        endif()
    endif()
    if(NOT RISCV_GCC_EXECUTABLE OR NOT EXISTS "${RISCV_GCC_EXECUTABLE}")
        message(FATAL_ERROR
            "A RISC-V GCC was not found. Put it at "
            "toolchains/riscv/bin/riscv64-unknown-elf-gcc.exe, set "
            "RISCV_GCC_EXECUTABLE, or add it to PATH.")
    endif()

    execute_process(
        COMMAND "${RISCV_GCC_EXECUTABLE}" --version
        RESULT_VARIABLE RISCV_GCC_CHECK_RESULT
        OUTPUT_QUIET
        ERROR_VARIABLE RISCV_GCC_CHECK_ERROR
    )
    if(NOT RISCV_GCC_CHECK_RESULT EQUAL 0)
        message(FATAL_ERROR "RISC-V GCC cannot be executed: ${RISCV_GCC_CHECK_ERROR}")
    endif()

    set(DOOMBF_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
    set(DOOMBF_BF_DIR "${CMAKE_CURRENT_BINARY_DIR}/bf")
    file(MAKE_DIRECTORY "${DOOMBF_GENERATED_DIR}" "${DOOMBF_BF_DIR}")

    set(DOOM_WAD_HEADER "${DOOMBF_GENERATED_DIR}/doom_wad.h")
    add_custom_command(
        OUTPUT "${DOOM_WAD_HEADER}"
        COMMAND "${Python3_EXECUTABLE}" "${DOOMBF_REPO_ROOT}/tools/bin2c.py"
            "${DOOM_WAD}" "${DOOM_WAD_HEADER}" --symbol data_doom_wad
        DEPENDS "${DOOM_WAD}" "${DOOMBF_REPO_ROOT}/tools/bin2c.py"
        VERBATIM
        COMMENT "Embedding doom.wad into doom_wad.h"
    )

    set(DOOM_RISCV_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/bfk_doom.c"
        ${DOOM_BACKEND_SOURCES}
        ${DOOM_CRT_SOURCES}
    )
    set(DOOM_RISCV_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/riscv_sources.txt")
    file(WRITE "${DOOM_RISCV_MANIFEST}" "")
    foreach(source IN LISTS DOOM_RISCV_SOURCES)
        file(APPEND "${DOOM_RISCV_MANIFEST}" "${source}\n")
    endforeach()

    set(DOOM_ELF "${DOOMBF_BF_DIR}/bfk_doom.elf")
    set(DOOM_ELF_OUTPUT "${DOOMBF_OUTPUT_DIR}/bfk_doom.elf")
    add_custom_command(
        OUTPUT "${DOOM_ELF}" "${DOOM_ELF_OUTPUT}"
        BYPRODUCTS "${DOOM_ELF}.rsp"
        COMMAND "${Python3_EXECUTABLE}" "${DOOMBF_REPO_ROOT}/tools/build_riscv.py"
            --compiler "${RISCV_GCC_EXECUTABLE}"
            --output "${DOOM_ELF}"
            --linker-script "${CMAKE_CURRENT_SOURCE_DIR}/bfk_link.ld"
            --sources-file "${DOOM_RISCV_MANIFEST}"
            --include-dir "${DOOMBF_GENERATED_DIR}"
            --include-dir "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${DOOM_ELF}" "${DOOM_ELF_OUTPUT}"
        DEPENDS
            "${DOOM_WAD_HEADER}"
            "${DOOM_RISCV_MANIFEST}"
            "${CMAKE_CURRENT_SOURCE_DIR}/bfk_link.ld"
            "${DOOMBF_REPO_ROOT}/tools/build_riscv.py"
            ${DOOM_RISCV_SOURCES}
        VERBATIM
        COMMAND_EXPAND_LISTS
        COMMENT "Compiling Doom as RV32I ELF"
    )
    add_custom_target(bfk_doom_elf DEPENDS "${DOOM_ELF_OUTPUT}")

    file(GLOB_RECURSE RISC_BF_PYTHON_SOURCES CONFIGURE_DEPENDS
        "${DOOMBF_REPO_ROOT}/RISC-BF/*.py"
    )

    set(DOOM_BPK "${DOOMBF_BF_DIR}/doom.bpk")
    set(DOOM_BPK_ADDR "${DOOM_BPK}.addr")
    set(DOOM_BPK_OUTPUT "${DOOMBF_OUTPUT_DIR}/doom.bpk")
    set(DOOM_BPK_ADDR_OUTPUT "${DOOMBF_OUTPUT_DIR}/doom.bpk.addr")
    add_custom_command(
        OUTPUT "${DOOM_BPK}" "${DOOM_BPK_ADDR}" "${DOOM_BPK_OUTPUT}" "${DOOM_BPK_ADDR_OUTPUT}"
        COMMAND "${Python3_EXECUTABLE}" "${DOOMBF_REPO_ROOT}/RISC-BF/risc_bf.py"
            -c "${DOOM_ELF}" "${DOOM_BPK}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${DOOM_BPK}" "${DOOM_BPK_OUTPUT}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${DOOM_BPK_ADDR}" "${DOOM_BPK_ADDR_OUTPUT}"
        DEPENDS "${DOOM_ELF}"
            ${RISC_BF_PYTHON_SOURCES}
        WORKING_DIRECTORY "${DOOMBF_REPO_ROOT}/RISC-BF"
        VERBATIM
        COMMENT "Translating RV32I ELF to compressed Brainfuck"
    )
    add_custom_target(doom_bpk ALL DEPENDS "${DOOM_BPK_OUTPUT}" "${DOOM_BPK_ADDR_OUTPUT}")
endif()
