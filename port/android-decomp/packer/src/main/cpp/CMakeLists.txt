# The Texture Packer's native library: the PC tool's core (port/tools/texpack) and its JNI side
cmake_minimum_required(VERSION 3.22.1)
project(texpacker CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(TEXPACK ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../tools/texpack)
add_library(texpacker SHARED packer_jni.cpp ${TEXPACK}/packer.cpp ${TEXPACK}/image.cpp ${TEXPACK}/etc2.cpp
    ${TEXPACK}/bc7.cpp ${TEXPACK}/jpeg.cpp ${TEXPACK}/zip.cpp)
target_include_directories(texpacker PRIVATE ${TEXPACK})
target_compile_options(texpacker PRIVATE -O2 -Wall -Wextra)
target_link_libraries(texpacker PRIVATE log)
target_link_options(texpacker PRIVATE "-Wl,-z,max-page-size=16384")
