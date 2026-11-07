# Native code of the OUYA app (port/docs/ouya-machbarkeit.md; a separate app, decision of the
# project owner, 22.10.2026). Stage O2: the GX renderer for GLES 2 (gx_gl2.cpp) and its checks on
# the PC; stage O3: the app around it (ouya_app.cpp, built by Gradle: port/ouya/app/build.gradle,
# -DRE4_OUYA_APP=ON).
#
# On the PC (Linux, WSL; Mesa's EGL and GLES):
#   cmake -S port/ouya/app/src/main/cpp -B BUILD && cmake --build BUILD && ctest --test-dir BUILD
# or port/ouya/tools/check.sh.
cmake_minimum_required(VERSION 3.22.1)
project(re4_ouya LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# shared with the main app and left as they are there: the GX texture decoder, the chunk format;
# for the checks the main app's GLES 3 renderer as the reference
set(RE4_MAIN_APP_CPP "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../android-decomp/app/src/main/cpp")
set(RE4_CORE_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../whole/runtime")
set(RE4_OUYA_TOOLS "${CMAKE_CURRENT_SOURCE_DIR}/../../../../tools")

if(ANDROID AND NOT CMAKE_BUILD_TYPE MATCHES "Rel")
    # Gradle's debug build compiles with -O0: the renderer then ran unoptimised in the app (the
    # texture decoder took 23 ms a texture on the OUYA, its films 1 picture a second, 07.11.2026;
    # the measurements of ouya-gx-render.ps1 were optimised). -O2 comes after the build type's -O0
    add_compile_options(-O2)
endif()
add_library(re4_gx2 STATIC gx_gl2.cpp gx_gl2_shader.cpp "${RE4_MAIN_APP_CPP}/gx_texture.cpp")
target_include_directories(re4_gx2 PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${RE4_MAIN_APP_CPP}" "${RE4_CORE_INCLUDE}")
target_compile_options(re4_gx2 PRIVATE -Wall -Wextra -Werror)
set_target_properties(re4_gx2 PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(ANDROID)
    target_link_libraries(re4_gx2 PUBLIC GLESv2 log)
    option(RE4_OUYA_APP "the OUYA app's library (Gradle)" OFF)
    if(RE4_OUYA_APP)
        # the app: the core process's client (runtime/core_ipc*.cpp), the renderer, OpenSL ES
        add_library(re4ouya SHARED ouya_app.cpp "${RE4_CORE_INCLUDE}/core_ipc.cpp"
            "${RE4_CORE_INCLUDE}/core_ipc_client.cpp")
        target_include_directories(re4ouya PRIVATE "${RE4_CORE_INCLUDE}")
        target_link_libraries(re4ouya PRIVATE re4_gx2 GLESv2 EGL OpenSLES log dl)
        target_compile_options(re4ouya PRIVATE -Wall -Wextra)
    endif()
else()
    enable_testing()
    find_path(RE4_GLES_INCLUDE_DIR GLES2/gl2.h REQUIRED)
    find_library(RE4_EGL_LIBRARY NAMES EGL libEGL.so.1 REQUIRED)
    find_library(RE4_GLES_LIBRARY NAMES GLESv2 libGLESv2.so.2 REQUIRED)
    # the main app's GLES 3 renderer, contexts, pictures (tools/gx_test_common.h)
    add_library(re4_gx_reference STATIC "${RE4_OUYA_TOOLS}/gx_test_common.cpp" "${RE4_MAIN_APP_CPP}/gx_gl.cpp"
        "${RE4_MAIN_APP_CPP}/gx_gl_shader.cpp")
    target_include_directories(re4_gx_reference PUBLIC "${RE4_OUYA_TOOLS}" "${RE4_MAIN_APP_CPP}" "${RE4_CORE_INCLUDE}"
        ${RE4_GLES_INCLUDE_DIR})
    target_link_libraries(re4_gx_reference PUBLIC re4_gx2 ${RE4_EGL_LIBRARY} ${RE4_GLES_LIBRARY} dl)
    target_compile_options(re4_gx_reference PRIVATE -Wall -Wextra -Werror)
    # synthetic scenes: GLES 2 (compact vertices) against GLES 3 (float vertices)
    add_executable(gx_gl2_check "${RE4_OUYA_TOOLS}/gx_gl2_check.cpp")
    target_link_libraries(gx_gl2_check PRIVATE re4_gx2 re4_gx_reference)
    target_compile_options(gx_gl2_check PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    add_test(NAME gx_gl2_check COMMAND gx_gl2_check)
    # frames of the game core (RE4_GX_DUMP) drawn by both renderers, compared (local, disc data)
    add_executable(gx_gl2_compare "${RE4_OUYA_TOOLS}/gx_gl2_compare.cpp")
    target_link_libraries(gx_gl2_compare PRIVATE re4_gx2 re4_gx_reference)
    target_compile_options(gx_gl2_compare PRIVATE -Wall -Wextra -Werror)
    # frames drawn by the GLES 2 renderer alone, with times (also built for the OUYA by
    # port/scripts/ouya-gx-render.ps1)
    add_executable(gx_gl2_render "${RE4_OUYA_TOOLS}/gx_gl2_render.cpp")
    target_link_libraries(gx_gl2_render PRIVATE re4_gx2 ${RE4_EGL_LIBRARY} ${RE4_GLES_LIBRARY})
    target_compile_options(gx_gl2_render PRIVATE -Wall -Wextra -Werror)
    # what a GLES 2 device offers (also built for the OUYA by port/scripts/ouya-gpu-info.ps1)
    add_executable(gles2_info "${RE4_OUYA_TOOLS}/gles2_info.cpp")
    target_link_libraries(gles2_info PRIVATE ${RE4_EGL_LIBRARY} ${RE4_GLES_LIBRARY})
    target_compile_options(gles2_info PRIVATE -Wall -Wextra -Werror)
endif()
