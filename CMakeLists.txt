cmake_minimum_required(VERSION 3.12)
project(doombf)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED   YES)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

get_filename_component(DOOMBF_ROOT_DIR ${CMAKE_SOURCE_DIR} ABSOLUTE CACHE)
 
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# make use of runtime directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}/latest")

if(MSVC)
set(BF_COMPILE_OPTIONS "")
else()
set(BF_COMPILE_OPTIONS "-O3")
endif()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/make)

if(MSVC)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    add_definitions(-D_SCL_SECURE_NO_WARNINGS)
    add_definitions(-D_USE_MATH_DEFINES)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
else()
  if (${CMAKE_HOST_SYSTEM_NAME} MATCHES "Darwin")
    find_library(MISC_FRAMEWORKS UniformTypeIdentifiers)
    set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64")
  elseif(${CMAKE_HOST_SYSTEM_NAME} MATCHES "Linux")
    find_package(X11 REQUIRED)
    set(MISC_FRAMEWORKS "${MISC_STATIC_LIBRARIES}")
  endif()
    #set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
    add_definitions(-DGL_SILENCE_DEPRECATION=1)
endif()

if (WIN32)
    set(_TCC_BIN "tcc.exe")
else()
    set(_TCC_BIN "tcc")
endif()

add_subdirectory(tcc)
set(TCC_EXE "${CMAKE_SOURCE_DIR}/bin/${_TCC_BIN}")  # Path to your tcc.exe, adjust if needed

add_subdirectory(frontend)

add_subdirectory(bf/hackablebf)
add_subdirectory(bf/industrial-bf)
