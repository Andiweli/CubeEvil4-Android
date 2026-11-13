# CubeEvil4 Texture Packer (port/docs/textur-lader-plan.md): build with build.ps1 (Windows, Visual Studio) or
#   cmake -S port/tools/texpack -B build/texpack && cmake --build build/texpack   (Linux, for the tests)
cmake_minimum_required(VERSION 3.16)
project(texpack CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
  # one .exe without the Visual C++ runtime DLLs
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  add_compile_options(/utf-8 /W3 /O2 /D_CRT_SECURE_NO_WARNINGS /DUNICODE /D_UNICODE)
else()
  add_compile_options(-O2 -Wall -Wextra)
endif()

find_package(Threads REQUIRED)

add_library(texpack_core STATIC etc2.cpp image.cpp bc7.cpp jpeg.cpp zip.cpp packer.cpp)
target_include_directories(texpack_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(texpack texpack.cpp)
target_link_libraries(texpack PRIVATE texpack_core Threads::Threads)
if(WIN32)
  target_link_libraries(texpack PRIVATE shell32 ole32)
  set_target_properties(texpack PROPERTIES OUTPUT_NAME CubeEvil4-TexturePacker)
endif()

add_executable(texpack_tests tests/texpack_tests.cpp)
target_link_libraries(texpack_tests PRIVATE texpack_core Threads::Threads)

# the decoder against Mesa's (WSL/Linux with EGL and GLES 3): -DTEXPACK_GL_CHECK=ON
option(TEXPACK_GL_CHECK "build etc2_gl_check (needs EGL and GLESv2)" OFF)
if(TEXPACK_GL_CHECK)
  add_executable(etc2_gl_check tests/etc2_gl_check.cpp)
  target_link_libraries(etc2_gl_check PRIVATE texpack_core EGL GLESv2)
endif()
