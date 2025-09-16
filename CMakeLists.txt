cmake_minimum_required(VERSION 3.20)
project(gitdump LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
  *.cc *.cpp *.cxx
)

add_executable(gitdump ${SOURCES})

if(MSVC)
  target_compile_options(gitdump PRIVATE /W4 /permissive- /utf-8)
  target_compile_definitions(gitdump PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(gitdump PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(APPLE)
  if(NOT DEFINED CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "x86_64" CACHE STRING "" FORCE)
  endif()
endif()

include(CheckCXXCompilerFlag)
set(NEED_STDCXXFS OFF)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  execute_process(COMMAND ${CMAKE_CXX_COMPILER} -dumpfullversion -dumpversion OUTPUT_VARIABLE GCC_VERSION)
  if(GCC_VERSION VERSION_LESS 9)
    set(NEED_STDCXXFS ON)
  endif()
endif()

if(NEED_STDCXXFS)
  target_link_libraries(gitdump PRIVATE stdc++fs)
endif()

set_target_properties(gitdump PROPERTIES
  OUTPUT_NAME gitdump
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

install(TARGETS gitdump RUNTIME DESTINATION bin)
