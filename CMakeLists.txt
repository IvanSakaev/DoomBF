cmake_minimum_required(VERSION 3.20)
project(DoomBF VERSION 1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(DOOMBF_BUILD_NATIVE "Build Doom with the native Windows/X11 frontend" ON)
option(DOOMBF_BUILD_BF "Build bfk_doom.elf and translate it to doom.bpk" OFF)
option(DOOMBF_BUILD_FRONTEND "Build the stream frontend (frnt)" ON)
option(DOOMBF_BUILD_IBF "Build the industrial Brainfuck interpreter" ON)

set(DOOMBF_OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin" CACHE PATH "Directory for runnable DoomBF artifacts")
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

if(DOOMBF_BUILD_FRONTEND)
    add_subdirectory(frontend)
endif()

if(DOOMBF_BUILD_IBF)
    add_subdirectory(industrial-bf)
endif()

add_subdirectory(doom)

set(DOOMBF_ALL_DEPENDENCIES)
if(TARGET doom_native)
    list(APPEND DOOMBF_ALL_DEPENDENCIES doom_native)
endif()
if(TARGET frnt)
    list(APPEND DOOMBF_ALL_DEPENDENCIES frnt)
endif()
if(TARGET ibf)
    list(APPEND DOOMBF_ALL_DEPENDENCIES ibf)
endif()
if(TARGET doom_bpk)
    list(APPEND DOOMBF_ALL_DEPENDENCIES doom_bpk)
endif()

add_custom_target(doombf_all DEPENDS ${DOOMBF_ALL_DEPENDENCIES})

if(TARGET ibf AND TARGET frnt AND TARGET doom_bpk)
    find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)
    add_custom_target(run_bf
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/run_pipeline.py"
            --ibf "$<TARGET_FILE:ibf>"
            --frontend "$<TARGET_FILE:frnt>"
            --program "${DOOMBF_OUTPUT_DIR}/doom.bpk"
        DEPENDS ibf frnt doom_bpk
        WORKING_DIRECTORY "${DOOMBF_OUTPUT_DIR}"
        USES_TERMINAL
        COMMENT "Running the bidirectional DoomBF pipeline"
    )
endif()
