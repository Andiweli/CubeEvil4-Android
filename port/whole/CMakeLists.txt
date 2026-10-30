# Whole-program port build. See port/docs/whole-program-status.md.
#
#   libre4_main.so            main.dol game units + platform runtime + generated stubs
#   modules/libre4_<mod>.so   one library per original REL module (114)
#   re4_host                  Linux launcher (not built for Android)
cmake_minimum_required(VERSION 3.20)
project(re4_whole C CXX ASM)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
get_filename_component(RE4_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(i.86|x86)$")
    set(RE4_ARCH i686)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(armv7.*|arm)$")
    set(RE4_ARCH armv7)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
    set(RE4_ARCH x86_64)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    set(RE4_ARCH arm64)
else()
    message(FATAL_ERROR "unsupported processor ${CMAKE_SYSTEM_PROCESSOR}")
endif()
# Milestone 12, way A (port/docs/arm64-bestandsaufnahme.md): native 64 bit with
# everything the game points to below 4 GB, so the game's 32-bit address
# arithmetic stays right.
if(RE4_ARCH STREQUAL "x86_64" OR RE4_ARCH STREQUAL "arm64")
    set(RE4_LP64 ON)
else()
    set(RE4_LP64 OFF)
endif()
# __FILE__ without the checkout and build directory: DvdReadN copies it into
# the 48-byte DVD_REQ_WORK::reqfile (include/dvd.h, src/game/dvd.cpp), so a
# long build path overflowed it. Game units become "src/game/x.cpp" (the more
# specific prefix of the prepared tree wins), port code "port/whole/...".
add_compile_options("-fmacro-prefix-map=${RE4_ROOT}/=")
if(NOT ANDROID)
    # 64-bit file interfaces for the 32-bit Linux test builds: readdir and
    # stat fail (EOVERFLOW) on file systems with 64-bit inode numbers (WSL's
    # Windows drives, qemu-arm on a 64-bit host) without them.
    add_compile_definitions(_FILE_OFFSET_BITS=64)
endif()
# Optimisation (the Android build uses 2, port/tools/whole_build_android.sh;
# whole_build.sh RE4_OPT=2 checks the same on Linux). Empty: none, as before.
set(RE4_OPT_LEVEL "" CACHE STRING "optimisation level of the whole build (empty = -O0)")
if(RE4_OPT_LEVEL)
    add_compile_options(-O${RE4_OPT_LEVEL})
endif()
if(RE4_ARCH STREQUAL "i686")
    # Single-precision float arithmetic in SSE registers, as on ARM (VFP) and
    # the GameCube: x87 keeps excess precision between operations, so float
    # results (and the bit-exact math tests) would differ from the device.
    add_compile_options(-msse2 -mfpmath=sse)
endif()
# No fused multiply-add anywhere (the game's own flags below have it too): aarch64 has FMA
# instructions, and clang fuses a*b+c into one rounding where the flag is missing. On the
# arm64-v8a core that were the parts without the game's flags: the SDK's matrix and vector math
# (C_MTXConcat 24, C_MTXInverse 20, C_MTX44Concat 48 fused instructions ...), the sound
# effects, a few runtime functions; armhf (VFPv3) and x86 have none, so the 32/64-bit compare
# on the PC could not see it (19.10.2026). whole_build.sh counts them in the aarch64 build.
add_compile_options(-ffp-contract=off)
# Check build with sanitizers (whole_build.sh RE4_SANITIZE=address,undefined: build folder
# whole-<target>-O2-san; the room tour of the PC check, whole_r100_check.sh --sanitize).
# AddressSanitizer stops at the first access outside a global, a local or a block of the C
# library's heap, with where it happened (the game heaps in MEM1 have no guard zones);
# UndefinedBehaviorSanitizer names each undefined operation once and goes on (a shift by 32 or
# more, an array index past its end, a float that does not fit its integer, a bool that is
# neither 0 nor 1 ...). Left out: vptr and function (the game converts between its object types
# and calls task functions through TaskFunc on purpose), return (CodeWarrior code ends non-void
# functions without a value, -fno-strict-return), null, nonnull-attribute and pointer-overflow
# (offsetof-like address arithmetic, memcpy of 0 bytes from 0; a real access through 0 crashes
# anyway; object-size takes PG_OFS, &((GLOBAL_WK*) 0)->f, for an access through 0:
# AddressSanitizer checks the sizes), alignment (the SDK's sound library writes words to 2-byte
# aligned DSP blocks, as the GameCube allows; over 100 places) and float-cast-overflow (the game's
# code is built with
# -fno-strict-float-cast-overflow: it clamps like the GameCube). The runtime is a shared
# library (-shared-libsan): the modules are libraries of their own, loaded with dlopen and linked
# with --no-undefined.
set(RE4_SANITIZE "" CACHE STRING "sanitizers of a check build (-fsanitize=LIST; empty = none)")
if(RE4_SANITIZE)
    set(san_off vptr,function,return,null,nonnull-attribute,returns-nonnull-attribute,pointer-overflow,object-size,float-cast-overflow)
    # alignment only when asked for by name (RE4_SANITIZE=alignment): the search for misaligned
    # floats and pointers, which armv7 reads with vldr / ldrd / ldm and which then fault (patch 0102)
    if(NOT RE4_SANITIZE MATCHES "alignment")
        string(APPEND san_off ",alignment")
    endif()
    set(RE4_SANITIZE_FLAGS -fsanitize=${RE4_SANITIZE} -fno-omit-frame-pointer -fno-sanitize=${san_off})
    # (line tables: file and line in the reports)
    add_compile_options(${RE4_SANITIZE_FLAGS} -gline-tables-only)
    # clang's runtime for the target, its folder in each program and library (no
    # LD_LIBRARY_PATH needed). Ubuntu ships it for x86 only: for aarch64 the cross GCC's
    # UndefinedBehaviorSanitizer runtime (libubsan1-arm64-cross, the same compiler-rt code; qemu
    # finds it through QEMU_LD_PREFIX). AddressSanitizer does not run there: with GCC's libasan
    # even a three-line program crashes under qemu-aarch64, and in a guest space of 4 GB
    # (whole_r100_check.sh) its shadow at 64 GB has no room (22.10.2026). On x86_64 its shadow
    # (0x7FFF8000-0x8FFF6FFF) lies where MEM1 has to be.
    if(RE4_SANITIZE MATCHES "address")
        set(san_lib asan)
    else()
        set(san_lib ubsan_standalone)
    endif()
    set(san_arch ${CMAKE_SYSTEM_PROCESSOR})
    if(RE4_ARCH STREQUAL "i686")
        set(san_arch i386)
    elseif(RE4_ARCH STREQUAL "armv7")
        set(san_arch armhf)
    endif()
    execute_process(COMMAND ${CMAKE_C_COMPILER} -print-file-name=libclang_rt.${san_lib}-${san_arch}.so
        OUTPUT_VARIABLE san_path OUTPUT_STRIP_TRAILING_WHITESPACE)
    set(san_gcc "${CMAKE_FIND_ROOT_PATH}/lib")
    if(IS_ABSOLUTE "${san_path}")
        get_filename_component(san_dir "${san_path}" DIRECTORY)
        set(RE4_SANITIZE_LINK ${RE4_SANITIZE_FLAGS} -shared-libsan "-Wl,-rpath,${san_dir}")
    elseif(NOT RE4_SANITIZE MATCHES "address" AND CMAKE_FIND_ROOT_PATH AND EXISTS "${san_gcc}/libubsan.so.1")
        set(RE4_SANITIZE_LINK ${RE4_SANITIZE_FLAGS} -fno-sanitize-link-runtime "${san_gcc}/libubsan.so.1")
    else()
        message(FATAL_ERROR "RE4_SANITIZE=${RE4_SANITIZE}: libclang_rt.${san_lib}-${san_arch}.so not found "
            "(clang's sanitizer runtime; on aarch64 only RE4_SANITIZE=undefined)")
    endif()
    add_link_options(${RE4_SANITIZE_LINK})
endif()

# --- prepared original sources -------------------------------------------------
set(RE4_PREPARED "${CMAKE_CURRENT_BINARY_DIR}/prepared")
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${RE4_ROOT}/port/tools/whole_prepare.py" --out "${RE4_PREPARED}"
    RESULT_VARIABLE prepare_result)
if(NOT prepare_result EQUAL 0)
    message(FATAL_ERROR "whole_prepare.py failed")
endif()
file(GLOB_RECURSE RE4_PREPARE_INPUTS CONFIGURE_DEPENDS
    "${RE4_ROOT}/src/*" "${RE4_ROOT}/include/*"
    "${CMAKE_CURRENT_SOURCE_DIR}/patches/*.patch"
    "${RE4_ROOT}/config/G4BE08/*.py" "${RE4_ROOT}/config/G4BE08/config.yml")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${RE4_ROOT}/port/tools/whole_prepare.py" "${RE4_ROOT}/port/tools/whole_asm_check.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/byte_views.json" ${RE4_PREPARE_INPUTS})

file(READ "${RE4_PREPARED}/modules.json" RE4_PLAN)
string(JSON RE4_MAIN_COUNT LENGTH "${RE4_PLAN}" main)
string(JSON RE4_MODULE_COUNT LENGTH "${RE4_PLAN}" modules)
set(RE4_TREE "${RE4_PREPARED}/tree")

# --- compile settings of original game code ------------------------------------
add_library(re4_game_flags INTERFACE)
target_include_directories(re4_game_flags BEFORE INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${RE4_TREE}/include"
    "${RE4_TREE}/src")  # overlay units include shared units as "wep/..." or "tools/..."
target_compile_options(re4_game_flags INTERFACE
    "-fmacro-prefix-map=${RE4_TREE}/="
    -include "${CMAKE_CURRENT_SOURCE_DIR}/include/re4_whole_prelude.h"
    -std=gnu++11
    -Wno-c++11-narrowing
    -fdelayed-template-parsing
    -fno-strict-aliasing
    -ffp-contract=off
    -fwrapv
    # ProDG's -mfast-cast converts float to u8/s8/u16/s16 with psq_st through
    # GQR2-5, which clamps to the type's range; clang wraps (or on ARM keeps the
    # unclamped value in the register). This flag makes clang clamp as well.
    -fno-strict-float-cast-overflow
    -fno-strict-return
    -Wno-address-of-temporary
    -ferror-limit=0)
if(RE4_LP64)
    # The game keeps addresses in 32-bit integers ((u32) p, about 3000 places):
    # C++ rejects such casts on LP64, the Microsoft extensions accept them (a
    # warning). Right because every address the game sees lies below 4 GB.
    # The 4-byte pointer fields of fixed-form structures (RE4_P32, TaskFunc) must
    # not reach a variadic function as they are (printf("%s", info->name) would
    # pass the 4-byte object, not a pointer): such calls are errors, and format
    # warnings name them for printf-like functions (whole_build.sh counts them).
    target_compile_options(re4_game_flags INTERFACE -fms-extensions -Wno-everything -Werror=class-varargs -Wformat)
else()
    target_compile_options(re4_game_flags INTERFACE -w)
endif()

set(RE4_LINK_OBJECTS "")
function(re4_unit_list json_path out_var)
    string(JSON n LENGTH "${RE4_PLAN}" ${json_path})
    math(EXPR last "${n} - 1")
    set(units "")
    foreach(i RANGE ${last})
        string(JSON unit GET "${RE4_PLAN}" ${json_path} ${i})
        set(path "${RE4_TREE}/${unit}")
        get_filename_component(dir "${path}" DIRECTORY)
        # each unit includes headers from its own directory with ""
        set_property(SOURCE "${path}" APPEND PROPERTY INCLUDE_DIRECTORIES "${dir}")
        list(APPEND units "${path}")
    endforeach()
    set(${out_var} "${units}" PARENT_SCOPE)
endfunction()

re4_unit_list("main" RE4_MAIN_UNITS)
add_library(re4_main_obj OBJECT ${RE4_MAIN_UNITS})
target_link_libraries(re4_main_obj PRIVATE re4_game_flags)
string(APPEND RE4_LINK_OBJECTS "main=$<TARGET_OBJECTS:re4_main_obj>\n")

# Platform layer (libre4_platform): Dolphin OS/DVD/ARAM/VI replacements, the
# module loader and the reused native code of the earlier port. A library of
# its own, so its C++ allocations (and those of the C++ runtime) never reach the
# game's operator new, which serves the game heaps.
add_library(re4_platform_obj OBJECT
    runtime/aram.cpp
    runtime/byte_order.cpp
    runtime/card_files.cpp
    runtime/dolphin_os.cpp
    runtime/dvd.cpp
    runtime/game_window.cpp
    runtime/gci.cpp
    runtime/gx_fifo.cpp
    runtime/gx_decode.cpp
    runtime/host_file.cpp
    runtime/log.cpp
    runtime/mem1.cpp
    runtime/module_loader.cpp
    runtime/newlib_ctype.c
    runtime/os_link.cpp
    runtime/os_thread.cpp
    runtime/pad.cpp
    runtime/vi.cpp
    runtime/watch.cpp
    "${RE4_ROOT}/port/src/stack_context.cpp")
target_include_directories(re4_platform_obj PUBLIC runtime PRIVATE "${RE4_ROOT}/port/src")
target_compile_features(re4_platform_obj PRIVATE cxx_std_17)
if(RE4_ARCH STREQUAL "arm64")
    # Stack switching: the Boost.Context assembly of the app's arm64 build
    set(RE4_FCONTEXT
        "${RE4_ROOT}/port/third_party/boost_context/make_arm64_aapcs_elf_gas.S"
        "${RE4_ROOT}/port/third_party/boost_context/jump_arm64_aapcs_elf_gas.S")
    target_sources(re4_platform_obj PRIVATE ${RE4_FCONTEXT})
endif()
if(RE4_ARCH STREQUAL "armv7")
    # Stack switching: the Boost.Context assembly of the Android build
    # (port/third_party/boost_context, see port/cmake/ContextSources.cmake).
    set(RE4_FCONTEXT
        "${RE4_ROOT}/port/third_party/boost_context/make_arm_aapcs_elf_gas.S"
        "${RE4_ROOT}/port/third_party/boost_context/jump_arm_aapcs_elf_gas.S")
    set_source_files_properties(${RE4_FCONTEXT} PROPERTIES COMPILE_OPTIONS "-marm;-U__SOFTFP__")
    target_sources(re4_platform_obj PRIVATE ${RE4_FCONTEXT})
endif()
string(APPEND RE4_LINK_OBJECTS "runtime=$<TARGET_OBJECTS:re4_platform_obj>\n")

# Formatumwandler (runtime/formats): disc data to host byte order. Part of the
# platform library, but compiled with the game's flags and headers so the
# converters use the original structure definitions. Small SDK replacements
# that need the game's structures sit here too.
add_library(re4_formats_obj OBJECT
    runtime/ai.cpp
    runtime/alarm.cpp
    runtime/dsp.cpp
    runtime/exi.cpp
    runtime/memcard.cpp
    runtime/tex_palette.cpp
    runtime/formats/formats.cpp
    runtime/formats/formats_arc.cpp
    runtime/formats/formats_dbg.cpp
    runtime/formats/formats_eff.cpp
    runtime/formats/formats_event.cpp
    runtime/formats/formats_id.cpp
    runtime/formats/formats_lit.cpp
    runtime/formats/formats_mes.cpp
    runtime/formats/formats_model.cpp
    runtime/formats/formats_room.cpp
    runtime/formats/formats_scene.cpp
    runtime/formats/formats_snd.cpp
    runtime/formats/formats_tex.cpp)
target_link_libraries(re4_formats_obj PRIVATE re4_game_flags)
target_include_directories(re4_formats_obj PRIVATE runtime runtime/formats)
string(APPEND RE4_LINK_OBJECTS "runtime=$<TARGET_OBJECTS:re4_formats_obj>\n")

# Port code that reads the game's own state, compiled as game code into
# libre4_main: the per-frame state trace (RE4_TRACE), the snapshot of the
# core bridge for the app's renderer (runtime/re4_core.h) and the depth of
# field notes and gameplay autofocus (runtime/dof.cpp, patch 0065) and the
# gyro aiming (runtime/gyro.cpp), the taps in the sub screens (runtime/menu_touch.cpp), the
# picture format without the letterbox (runtime/picture.cpp), the knife by a swipe
# (runtime/knife_swipe.cpp) and the quick-time events by a tap (runtime/qte_touch.cpp), and the instant save payload (runtime/instant_game.cpp),
# the game language (runtime/game_language.cpp) and the cheat menu (runtime/cheats.cpp).
add_library(re4_gamehooks_obj OBJECT runtime/state_trace.cpp runtime/core_bridge.cpp runtime/movie_status.cpp
    runtime/dof.cpp runtime/gyro.cpp runtime/menu_touch.cpp runtime/picture.cpp runtime/knife_swipe.cpp
    runtime/qte_touch.cpp runtime/instant_game.cpp runtime/game_language.cpp runtime/cheats.cpp
    runtime/gx_owner.cpp runtime/potato.cpp runtime/aim_options.cpp
    runtime/sound_volume.cpp)
target_link_libraries(re4_gamehooks_obj PRIVATE re4_game_flags)
target_include_directories(re4_gamehooks_obj PRIVATE runtime)
string(APPEND RE4_LINK_OBJECTS "main=$<TARGET_OBJECTS:re4_gamehooks_obj>\n")

# Toolchain-runtime replacements that belong to the game code (libre4_main):
# the SN constructor lists, GCC 2.95 allocation names, the game heap for modules.
# Also the embedding interface the Android app starts the game with
# (runtime/core_api.cpp, runtime/re4_core.h).
add_library(re4_glue_obj OBJECT
    runtime/core_api.cpp
    runtime/instant_store.cpp
    runtime/game_heap.cpp
    runtime/sn_runtime.cpp)
target_include_directories(re4_glue_obj PRIVATE runtime)
string(APPEND RE4_LINK_OBJECTS "runtime=$<TARGET_OBJECTS:re4_glue_obj>\n")

# Portable C parts of the Dolphin SDK (src/lib), compiled unchanged: the heap
# allocator, whose descriptors the game reads directly (main_mem.cpp), the
# controller dead zones (Padclamp: the game's PadRead calls PADClamp on the raw
# values of runtime/pad.cpp), GX, the sound libraries, and the C_* reference
# versions of the matrix/vector math (whole_prepare.py extracts them into
# prepared/sdk_c; runtime/mtx_ps.c maps the PS* names to them).
set(RE4_SDK_UNITS OSAlloc OSArena Padclamp
    GXAttr GXBump GXDisplayList GXDraw GXFifo GXFrameBuf GXGeometry GXInit GXLight GXMisc
    GXPerf GXPixel GXTev GXTexture GXTransform
    # sound (M8): AX on the DSP of runtime/dsp.cpp and the AI of runtime/ai.cpp,
    # the mixer, synthesizer, sequencer, articulation and the AXFX effects
    AX AXAlloc AXAux AXCL AXComp AXOut AXProf AXSPB AXVPB DSPCode
    mix syn synctrl synenv synlfo synmix synpitch synsample synvoice synwt seq
    axart axart3d axartcents axartenv axartlfo axartlpf axartsound
    axfx delay chorus reverb_hi reverb_hi_4ch reverb_std)
set(RE4_SDK_SOURCES runtime/mtx_ps.c)
foreach(unit IN LISTS RE4_SDK_UNITS)
    list(APPEND RE4_SDK_SOURCES "${RE4_TREE}/src/lib/${unit}.c")
endforeach()
foreach(unit mtx mtxvec mtx44 mtx44vec vec quat)
    list(APPEND RE4_SDK_SOURCES "${RE4_PREPARED}/sdk_c/${unit}.c")
endforeach()
add_library(re4_sdk_obj OBJECT ${RE4_SDK_SOURCES})
target_include_directories(re4_sdk_obj BEFORE PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include" "${RE4_TREE}/include" "${RE4_TREE}/src/lib")
target_compile_definitions(re4_sdk_obj PRIVATE SDK_REVISION=1 NDEBUG=1)
target_compile_options(re4_sdk_obj PRIVATE -std=gnu99 -fno-strict-aliasing -fwrapv -w
    -include "${CMAKE_CURRENT_SOURCE_DIR}/include/re4_sdk_intrinsics.h")
# GXDraw.c has a file-scope array named `index`, which glibc's <strings.h>
# (pulled in by <string.h> in GNU mode) declares as a function
set_source_files_properties("${RE4_TREE}/src/lib/GXDraw.c" PROPERTIES COMPILE_OPTIONS "-std=c99")
string(APPEND RE4_LINK_OBJECTS "runtime=$<TARGET_OBJECTS:re4_sdk_obj>\n")

# Memory card (M10): the CARD library on the EXI functions and the card of
# runtime/exi.cpp / memcard.cpp; re4_card_port.h stands in for the OS low
# memory and VI register the library reads (patch 0057: command byte order).
# Linked into libre4_main like the CRI middleware: CARDNet.c refers to
# functions the original linker had dropped, which resolve to stubs there.
set(RE4_CARD_UNITS CARDBios CARDBlock CARDCheck CARDCreate CARDDelete CARDDir CARDFormat CARDMount
    CARDNet CARDOpen CARDRdwr CARDRead CARDStat CARDUnlock CARDWrite)
set(RE4_CARD_SOURCES "")
foreach(unit IN LISTS RE4_CARD_UNITS)
    list(APPEND RE4_CARD_SOURCES "${RE4_TREE}/src/lib/${unit}.c")
endforeach()
add_library(re4_card_obj OBJECT ${RE4_CARD_SOURCES})
target_include_directories(re4_card_obj BEFORE PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include" "${RE4_TREE}/include" "${RE4_TREE}/src/lib")
target_compile_definitions(re4_card_obj PRIVATE SDK_REVISION=1 NDEBUG=1)
target_compile_options(re4_card_obj PRIVATE -std=gnu99 -fno-strict-aliasing -fwrapv -w
    "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/include/re4_sdk_intrinsics.h"
    "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/include/re4_card_port.h")
string(APPEND RE4_LINK_OBJECTS "runtime=$<TARGET_OBJECTS:re4_card_obj>\n")

# CRI middleware (M9): ADX, Sofdec MPEG video and the CVFS file interfaces in
# configure.py's CRI_LIBS order. Built by CRI with `-char signed`; the codeless
# register pins are dropped by whole_prepare.py, the remaining asm and the
# byte-order dependent reads are reviewed patches (0048-0051). Calls without a
# prototype compile as in CodeWarrior (int result, see the status file).
set(RE4_CRI_UNITS
    adx_tlk adx_tsvr adx_rnaa lsc lsc_ini lsc_svr sj_rbf sj_utl sj_err svm ax_rna rna_res
    adx_inis adx_sjd adx_bahx adx_bsc adx_bsps adx_amp adx_crs adx_dcd adx_errs adx_fsvr
    adx_stmc adx_xpnd adx_mgc adx_dcd5 adx_fini adx_fs cri_cvfs lsc_err lsc_crs sj_crs sj_mem
    sj_uni rna_err rna_crs adx_bwav adx_baif adx_bau adx_fcch adx_insh adx_sfa adx_sje adx_sugc
    adx_gc adx_dcd3 mwsfdfrm mwsfdsvm mwsfdsfx mwsfdcre mwsfdlib mwsfdply mwsfdset mwsfdsvr
    mwsfdsst mwsfx_ARGB8888PLN mwsfx_Y84C44 sud_lib sfx_set sfx_cnv sfx_lib sfx_alp sfx_zmv
    sfx_sud sfx_cnv_to_ARGB8888PLN sfx_cnv_to_Y84C44 sfx_YCC420PLN_to_ARGB8888PLN
    sfx_YCC420PLN_to_Y84C44 sfd_adxt sfd_aoap sfd_buf sfd_con sfd_cre sfd_hds sfd_lib sfd_mem
    sfd_mps sfd_mpv sfd_mpvf sfd_pl2 sfd_ply sfd_pts sfd_see sfd_set sfd_tim sfd_trn sfd_uo
    sfd_vom sfd_tmr sfd_tst sfh_main cmptime memsetd muldiv muldivr uty_tmr mwsfdrna mwsfdrsc
    mwsfdsl mwstm mwsfdsee cftyp422_ppc cftfx cftcoladj sfx_inf mcp_not mps_dec mps_del mps_get
    mps_lib mpv_deli mpv_emp mpv_err mpv_frm mpv_get mpv_hdec mpv_lib mpv_vlc mpv_umc mpv_mcy
    mpv_m2v mpvabdec memcpyd uty_ppc dct_fsri mpv_bdec mpv_cdec mpv_cmc mpv_dec mpv_mc dct_ac
    dct_ver pl2link pl2enc cft_common mfci gcci gcci_sub)
set(RE4_CRI_SOURCES "")
foreach(unit IN LISTS RE4_CRI_UNITS)
    list(APPEND RE4_CRI_SOURCES "${RE4_TREE}/src/lib/${unit}.c")
endforeach()
add_library(re4_cri_obj OBJECT ${RE4_CRI_SOURCES})
target_include_directories(re4_cri_obj BEFORE PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include" "${RE4_TREE}/include" "${RE4_TREE}/src/lib"
    "${RE4_TREE}/src/lib/cri")
target_compile_definitions(re4_cri_obj PRIVATE SDK_REVISION=1 NDEBUG=1)
target_compile_options(re4_cri_obj PRIVATE -std=gnu99 -fsigned-char -fno-strict-aliasing -fwrapv
    -ffp-contract=off -w -Wno-error=implicit-function-declaration
    -include "${CMAKE_CURRENT_SOURCE_DIR}/include/re4_cri_port.h")
string(APPEND RE4_LINK_OBJECTS "runtime=$<TARGET_OBJECTS:re4_cri_obj>\n")

# Reviewed native code of the earlier port, reused unchanged: the yz2 decode
# loop (port/src/yz2_kernel.cpp) behind yz2Decode_Decode. Its yz2_structs.inc
# holds the original structs of src/game/yz2code.cpp plus the kernel's bounds
# and statistics members, extracted as port/cmake/Yz2Sources.cmake does.
file(READ "${RE4_ROOT}/src/game/yz2code.cpp" yz2_source)
string(REPLACE "\r\n" "\n" yz2_source "${yz2_source}")
string(FIND "${yz2_source}" "struct Yz2InEv {" yz2_start)
string(FIND "${yz2_source}" "// Parses the yz2 header" yz2_end)
math(EXPR yz2_count "${yz2_end} - ${yz2_start}")
string(SUBSTRING "${yz2_source}" ${yz2_start} ${yz2_count} yz2_structs)
string(REPLACE "static Yz2InEv in_ev;" "" yz2_structs "${yz2_structs}")
string(REPLACE "u32 size1;    // 0x10  second hex field of the header string"
    "u32 size1;    // Original scalar; following members are native adapter state.\n    const u8* srcEnd;\n    u8* workEnd;\n    Yz2Checkpoint checkpoint;\n    Yz2Statistics* statistics;"
    yz2_structs "${yz2_structs}")
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/reuse/yz2_structs.inc" CONTENT
    "// Original field definitions (src/game/yz2code.cpp) plus the kernel's state.\n${yz2_structs}" @ONLY)
add_library(re4_reuse_obj OBJECT
    "${RE4_ROOT}/port/src/yz2_kernel.cpp"
    runtime/yz2_decode.cpp)
target_include_directories(re4_reuse_obj PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/reuse" "${RE4_ROOT}/port/src" "${RE4_ROOT}/port/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/include" runtime)
target_compile_features(re4_reuse_obj PRIVATE cxx_std_17)
string(APPEND RE4_LINK_OBJECTS "runtime=$<TARGET_OBJECTS:re4_reuse_obj>\n")

set(RE4_MODULES "")
math(EXPR last_module "${RE4_MODULE_COUNT} - 1")
foreach(i RANGE ${last_module})
    string(JSON mod MEMBER "${RE4_PLAN}" modules ${i})
    list(APPEND RE4_MODULES ${mod})
    re4_unit_list("modules;${mod};sources" units)
    string(JSON ndef LENGTH "${RE4_PLAN}" modules ${mod} defines)
    math(EXPR last_def "${ndef} - 1")
    set(defines "")
    foreach(d RANGE ${last_def})
        string(JSON def GET "${RE4_PLAN}" modules ${mod} defines ${d})
        list(APPEND defines "${def}")
    endforeach()
    add_library(re4_mod_${mod}_obj OBJECT ${units})
    target_link_libraries(re4_mod_${mod}_obj PRIVATE re4_game_flags)
    target_compile_definitions(re4_mod_${mod}_obj PRIVATE ${defines})
    string(APPEND RE4_LINK_OBJECTS "${mod}=$<TARGET_OBJECTS:re4_mod_${mod}_obj>\n")
endforeach()

# --- link plan: stubs, module table, cross-module trampolines ------------------
set(RE4_GEN "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/link_objects.txt" CONTENT "${RE4_LINK_OBJECTS}")
set(RE4_GEN_FILES "${RE4_GEN}/stubs.cpp" "${RE4_GEN}/modules.cpp" "${RE4_GEN}/link_report.json")
set(RE4_OBJ_DEPS re4_main_obj re4_gamehooks_obj re4_platform_obj re4_formats_obj re4_glue_obj re4_sdk_obj re4_card_obj re4_cri_obj
    re4_reuse_obj
    "$<TARGET_OBJECTS:re4_main_obj>" "$<TARGET_OBJECTS:re4_gamehooks_obj>" "$<TARGET_OBJECTS:re4_platform_obj>" "$<TARGET_OBJECTS:re4_formats_obj>"
    "$<TARGET_OBJECTS:re4_glue_obj>" "$<TARGET_OBJECTS:re4_sdk_obj>" "$<TARGET_OBJECTS:re4_card_obj>" "$<TARGET_OBJECTS:re4_cri_obj>"
    "$<TARGET_OBJECTS:re4_reuse_obj>")
foreach(mod IN LISTS RE4_MODULES)
    list(APPEND RE4_GEN_FILES "${RE4_GEN}/imports/${mod}.cpp" "${RE4_GEN}/imports/${mod}.S")
    list(APPEND RE4_OBJ_DEPS re4_mod_${mod}_obj "$<TARGET_OBJECTS:re4_mod_${mod}_obj>")
endforeach()
if(CMAKE_CXX_COMPILER_TARGET)
    set(RE4_TARGET_FLAG "--target=${CMAKE_CXX_COMPILER_TARGET}")
endif()
if(ANDROID)
    # the trial link must see the Android logging and loader libraries, or
    # their functions would be planned as stubs
    list(APPEND RE4_TARGET_FLAG -llog -ldl)
endif()
if(RE4_SANITIZE)
    # likewise the sanitizer runtime (__asan_*, __ubsan_*)
    list(APPEND RE4_TARGET_FLAG ${RE4_SANITIZE_LINK})
endif()
add_custom_command(
    OUTPUT "${RE4_GEN}/link_plan.stamp"
    BYPRODUCTS ${RE4_GEN_FILES}
    COMMAND "${Python3_EXECUTABLE}" "${RE4_ROOT}/port/tools/whole_link_plan.py"
        --plan "${RE4_PREPARED}/modules.json"
        --objects "${CMAKE_CURRENT_BINARY_DIR}/link_objects.txt"
        --arch ${RE4_ARCH} --out "${RE4_GEN}"
        -- "${CMAKE_CXX_COMPILER}" ${RE4_TARGET_FLAG}
    COMMAND "${CMAKE_COMMAND}" -E touch "${RE4_GEN}/link_plan.stamp"
    DEPENDS ${RE4_OBJ_DEPS} "${RE4_ROOT}/port/tools/whole_link_plan.py"
    COMMENT "Planning the link (stubs, module imports)"
    VERBATIM)
add_custom_target(re4_link_plan DEPENDS "${RE4_GEN}/link_plan.stamp")

set(RE4_LINK_FLAGS -fuse-ld=lld -Wl,--no-undefined)
if(ANDROID)
    # 16 KB pages (newer devices), as the app's own library
    list(APPEND RE4_LINK_FLAGS "-Wl,-z,max-page-size=16384")
endif()
if(RE4_ARCH STREQUAL "i686" OR RE4_ARCH STREQUAL "armv7")  # size_t = unsigned int ('j')
    set(RE4_GAME_HEAP_WRAP -Wl,--wrap=_Znwj -Wl,--wrap=_Znaj -Wl,--wrap=_ZdlPv -Wl,--wrap=_ZdaPv)
    set(RE4_MAIN_VER "${CMAKE_CURRENT_SOURCE_DIR}/runtime/libre4_main.ver")
else()  # LP64: size_t = unsigned long ('m')
    set(RE4_GAME_HEAP_WRAP -Wl,--wrap=_Znwm -Wl,--wrap=_Znam -Wl,--wrap=_ZdlPv -Wl,--wrap=_ZdaPv)
    set(RE4_MAIN_VER "${CMAKE_CURRENT_SOURCE_DIR}/runtime/libre4_main_lp64.ver")
endif()

# --- libre4_platform ----------------------------------------------------------------
add_library(re4_platform SHARED
    $<TARGET_OBJECTS:re4_platform_obj>
    $<TARGET_OBJECTS:re4_formats_obj>
    $<TARGET_OBJECTS:re4_sdk_obj>
    $<TARGET_OBJECTS:re4_reuse_obj>
    "${RE4_GEN}/modules.cpp")
add_dependencies(re4_platform re4_link_plan)
target_include_directories(re4_platform PRIVATE runtime)
target_link_options(re4_platform PRIVATE ${RE4_LINK_FLAGS})
target_link_libraries(re4_platform PRIVATE ${CMAKE_DL_LIBS} m)
if(ANDROID)
    target_link_libraries(re4_platform PRIVATE log)
endif()

# --- libre4_main ------------------------------------------------------------------
add_library(re4_main SHARED
    $<TARGET_OBJECTS:re4_main_obj>
    $<TARGET_OBJECTS:re4_gamehooks_obj>
    $<TARGET_OBJECTS:re4_glue_obj>
    # the CRI middleware was part of main.dol: its references into code the
    # original linker dropped resolve to the generated stubs like the game's
    $<TARGET_OBJECTS:re4_cri_obj>
    # likewise the memory card library (M10)
    $<TARGET_OBJECTS:re4_card_obj>
    "${RE4_GEN}/stubs.cpp")
add_dependencies(re4_main re4_link_plan)
target_include_directories(re4_main PRIVATE runtime)
target_link_options(re4_main PRIVATE ${RE4_LINK_FLAGS}
    "-Wl,--version-script=${RE4_MAIN_VER}")
set_property(TARGET re4_main APPEND PROPERTY LINK_DEPENDS "${RE4_MAIN_VER}")
target_link_libraries(re4_main PUBLIC re4_platform PRIVATE m)

# --- one library per REL module ---------------------------------------------------
foreach(mod IN LISTS RE4_MODULES)
    add_library(re4_${mod} SHARED
        $<TARGET_OBJECTS:re4_mod_${mod}_obj>
        runtime/module_crt.cpp
        "${RE4_GEN}/imports/${mod}.cpp"
        "${RE4_GEN}/imports/${mod}.S")
    add_dependencies(re4_${mod} re4_link_plan)
    target_include_directories(re4_${mod} PRIVATE runtime)
    # -Bsymbolic: a module's references bind to its own copies of duplicated units.
    # --wrap: `new`/`delete` of game code use the game heap (runtime/game_heap.cpp).
    target_link_options(re4_${mod} PRIVATE ${RE4_LINK_FLAGS} -Wl,-Bsymbolic ${RE4_GAME_HEAP_WRAP})
    target_link_libraries(re4_${mod} PRIVATE re4_main)
    set_target_properties(re4_${mod} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/modules")
endforeach()

# --- trampoline self-test (re4_host --self-test) -------------------------------------
add_library(re4test_target SHARED tests/trampoline_target.cpp)
add_custom_command(
    OUTPUT "${RE4_GEN}/test/re4test_caller.cpp" "${RE4_GEN}/test/re4test_caller.S"
    COMMAND "${Python3_EXECUTABLE}" "${RE4_ROOT}/port/tools/whole_link_plan.py"
        --emit-imports re4test_caller ${RE4_ARCH} "${RE4_GEN}/test" re4test_mix re4test_wide
    DEPENDS "${RE4_ROOT}/port/tools/whole_link_plan.py"
    VERBATIM)
add_library(re4test_caller SHARED tests/trampoline_caller.cpp
    "${RE4_GEN}/test/re4test_caller.cpp" "${RE4_GEN}/test/re4test_caller.S")
target_include_directories(re4test_caller PRIVATE runtime)
foreach(t re4test_target re4test_caller)
    target_link_options(${t} PRIVATE ${RE4_LINK_FLAGS} -Wl,-Bsymbolic)
    target_link_libraries(${t} PRIVATE re4_main)
    set_target_properties(${t} PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/modules"
        OUTPUT_NAME re4_${t})
endforeach()

# --- runtime tests without disc data (re4_host --runtime-test) -------------------------
add_library(re4test_rel SHARED tests/rel_module.cpp runtime/module_crt.cpp)
target_link_options(re4test_rel PRIVATE ${RE4_LINK_FLAGS} -Wl,-Bsymbolic)
target_link_libraries(re4test_rel PRIVATE re4_main)
set_target_properties(re4test_rel PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/modules"
    OUTPUT_NAME re4_re4test_rel)

# --- the core process on Android (armeabi-v7a) -------------------------------------
# A program in the app's library folder (named lib*.so so that the APK carries it):
# the core runs in it when MEM1 is taken in the app's process (port/docs/32bit-kernprozess.md).
if(ANDROID AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    add_executable(re4_coreproc host/coreproc.cpp runtime/core_ipc.cpp)
    target_include_directories(re4_coreproc PRIVATE runtime)
    target_compile_features(re4_coreproc PRIVATE cxx_std_17)
    target_link_libraries(re4_coreproc PRIVATE ${CMAKE_DL_LIBS} log)
    target_link_options(re4_coreproc PRIVATE -Wl,-z,max-page-size=16384)
    set_target_properties(re4_coreproc PROPERTIES OUTPUT_NAME libre4_coreproc.so SUFFIX "")
endif()

# --- Linux launcher -----------------------------------------------------------------
# re4_host (host/loader.c) keeps the address range below 0x83000000 free, then
# loads libre4_host.so (the launcher and the runtime tests) and with it the
# game libraries, which therefore lie above 0x80000000 as on Android.
if(NOT ANDROID)
    add_executable(re4_instant_store_test tests/instant_store_test.cpp runtime/instant_store.cpp runtime/gci.cpp)
    target_include_directories(re4_instant_store_test PRIVATE runtime)
    target_compile_features(re4_instant_store_test PRIVATE cxx_std_17)
    # tests/game_probe.cpp: checks that need the game's flags and headers
    add_library(re4_probe_obj OBJECT tests/game_probe.cpp tests/core_probe.cpp tests/stream_probe.cpp
        tests/gx_probe.cpp tests/movie_probe.cpp tests/card_probe.cpp tests/instant_game_probe.cpp)
    target_link_libraries(re4_probe_obj PRIVATE re4_game_flags)
    add_library(re4_hostlib SHARED host/main.cpp tests/runtime_tests.cpp tests/core_tests.cpp
        tests/core_remote_probe.cpp
        "${RE4_ROOT}/port/src/sha256.cpp"
        $<TARGET_OBJECTS:re4_probe_obj>)
    target_include_directories(re4_hostlib PRIVATE runtime "${RE4_ROOT}/port/include" "${RE4_ROOT}/port/tests"
        "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_compile_features(re4_hostlib PRIVATE cxx_std_17)
    add_dependencies(re4_hostlib re4test_rel re4test_caller re4test_target)
    target_link_options(re4_hostlib PRIVATE -fuse-ld=lld)
    target_link_libraries(re4_hostlib PRIVATE re4_main ${CMAKE_DL_LIBS})
    set_target_properties(re4_hostlib PROPERTIES OUTPUT_NAME re4_host)
    add_executable(re4_host host/loader.c)
    add_dependencies(re4_host re4_hostlib)
    # the float functions of libm through their double versions (host/det_math.c): LD_PRELOAD
    # of the 32/64-bit comparison of the PC check only (whole_r100_check.sh --compare)
    add_library(re4_detmath SHARED host/det_math.c)
    target_compile_options(re4_detmath PRIVATE -fno-builtin -O2)
    target_link_libraries(re4_detmath PRIVATE m)
    add_dependencies(re4_host re4_detmath)
    target_link_options(re4_host PRIVATE -fuse-ld=lld)
    target_link_libraries(re4_host PRIVATE ${CMAKE_DL_LIBS} pthread)
    # the core in a process of its own (port/docs/32bit-kernprozess.md), 32 bit as on
    # Android: re4_coreproc (host/coreproc.cpp, on Android libre4_coreproc.so) and the
    # app's side against it (re4_core_remote_test, the client of core_mode.cpp)
    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
        add_executable(re4_coreproc host/coreproc.cpp runtime/core_ipc.cpp)
        add_dependencies(re4_coreproc re4_main re4_hostlib)
        add_executable(re4_core_remote_test tests/core_remote_test.cpp runtime/core_ipc.cpp
            runtime/core_ipc_client.cpp)
        foreach(t re4_coreproc re4_core_remote_test)
            target_include_directories(${t} PRIVATE runtime)
            target_compile_features(${t} PRIVATE cxx_std_17)
            target_link_options(${t} PRIVATE -fuse-ld=lld)
            target_link_libraries(${t} PRIVATE ${CMAKE_DL_LIBS} pthread)
        endforeach()
    endif()

    # --- format comparison (re4_format_compare) -----------------------------------------
    # The Formatumwandler against the checked big-endian readers of the app
    # (port/android-decomp): the readers and what they link, compiled as the
    # app compiles them (C++17, original floating-point rules).
    set(RE4_APP_CPP "${RE4_ROOT}/port/android-decomp/app/src/main/cpp")
    set(RE4_APP_READERS archive smd gx_stream smx sat enemy_spawn event_data model_skin room_lighting
        room_flow scene collision_query materials village_flow animation texture room_state
        ports/yz2 ports/motion64/motion ports/motion64/math ports/motion64/ik
        ports/fdlibm/sf_sin ports/fdlibm/sf_cos ports/fdlibm/kf_sin ports/fdlibm/kf_cos
        ports/fdlibm/ef_rem_pio2 ports/fdlibm/kf_rem_pio2 ports/fdlibm/sf_scalbn ports/fdlibm/sf_floor
        ports/fdlibm/sf_fabs ports/fdlibm/ef_acos ports/fdlibm/ef_sqrt)
    list(TRANSFORM RE4_APP_READERS PREPEND "${RE4_APP_CPP}/")
    list(TRANSFORM RE4_APP_READERS APPEND ".cpp")
    add_library(re4_app_readers STATIC ${RE4_APP_READERS})
    target_include_directories(re4_app_readers PUBLIC "${RE4_APP_CPP}")
    target_compile_features(re4_app_readers PUBLIC cxx_std_17)
    target_compile_options(re4_app_readers PRIVATE -fno-fast-math -ffp-contract=off -fno-strict-aliasing -w)
    # the game side: converters and original structures (game flags)
    add_library(re4_compare_game_obj OBJECT tests/format_compare/game_side.cpp)
    target_link_libraries(re4_compare_game_obj PRIVATE re4_game_flags)
    target_include_directories(re4_compare_game_obj PRIVATE runtime runtime/formats tests/format_compare)
    add_executable(re4_format_compare tests/format_compare/main.cpp tests/format_compare/app_side.cpp
        $<TARGET_OBJECTS:re4_compare_game_obj>)
    target_include_directories(re4_format_compare PRIVATE tests/format_compare)
    target_link_options(re4_format_compare PRIVATE -fuse-ld=lld)
    target_link_libraries(re4_format_compare PRIVATE re4_app_readers re4_platform)
endif()
