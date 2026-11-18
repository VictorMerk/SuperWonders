cmake_minimum_required(VERSION 3.25)
project(SwitchGamesRecomp C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The game this build is for: games/<NX_GAME>/ holds what only that game needs (game.cpp: nx/game.h), the shared
# runtime is the rest. One build directory per game.
set(NX_GAME "hades" CACHE STRING "Game to build (a folder under games/)")
set(NX_GAME_DIR "${CMAKE_SOURCE_DIR}/games/${NX_GAME}")
if(WIN32 AND NX_GAME STREQUAL "wonder")
    enable_language(RC)
endif()
if(NOT EXISTS "${NX_GAME_DIR}/game.cpp")
    message(FATAL_ERROR "games/${NX_GAME}/game.cpp not found")
endif()

# Generated code of the game (python -m recomp <nso> <gen dir>/<module> --name <module> --base ...). Never committed.
# Hades keeps the layout from before there were other games (private/gen); the others use private/<game>/gen.
if(NX_GAME STREQUAL "hades")
    set(NX_GEN_DEFAULT "${CMAKE_SOURCE_DIR}/private/gen")
else()
    set(NX_GEN_DEFAULT "${CMAKE_SOURCE_DIR}/private/${NX_GAME}/gen")
endif()
set(NX_GEN_DIR "${NX_GEN_DEFAULT}" CACHE PATH "Recompiled module sources")

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # Localized source strings are UTF-8 and narrow literals must remain UTF-8 in the executable too.
    # Setting only the input charset still lets Clang convert unrepresentable scripts to '?' on Windows.
    add_compile_options(-finput-charset=UTF-8 -fexec-charset=UTF-8)
    add_compile_options(-Wno-unused-label -Wno-unused-value -Wno-parentheses-equality -Wno-unused-variable
                        -Wno-unused-but-set-variable -Wno-constant-logical-operand -Wno-unused-function)
endif()
if(MSVC)
    add_compile_options(/arch:AVX2)
elseif(MINGW)
    add_compile_options(-mavx2)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    # x86-64-v3 (Haswell and later): AVX2, as on Windows, and CMPXCHG16B, without which the recompiled code's
    # 128-bit __atomic_compare_exchange_n becomes a libatomic call (__atomic_compare_exchange_16, unresolved).
    add_compile_options(-march=x86-64-v3)
endif()
if(WIN32)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# Vulkan headers (the same registry version tools/gen_vk.py generated from). The loader comes with the GPU driver.
if(DEFINED ENV{VULKAN_SDK})
    set(NX_VULKAN_SDK "$ENV{VULKAN_SDK}" CACHE PATH "Vulkan SDK")
else()
    set(NX_VULKAN_SDK "C:/VulkanSDK/1.4.357.0" CACHE PATH "Vulkan SDK")
endif()

# The runtime's own compute shaders (runtime/shaders), compiled to SPIR-V here and #included as word lists: nothing is
# compiled when the game runs. glslc comes with the Vulkan SDK (the NDK's shader-tools on Android, shaderc on macOS).
set(NX_SHADER_OUT "${CMAKE_BINARY_DIR}/shaders")
file(GLOB NX_NDK_SHADER_TOOLS "${ANDROID_NDK}/shader-tools/*")
# glslc, else glslangValidator (Ubuntu 22.04, the AppImage's build container, has no glslc): both write the SPIR-V as a
# list of words to #include (glslc's is optimized, glslangValidator's isn't; either is valid).
# (each in a cache variable of its own: a reconfigure must not hand glslc's arguments to glslangValidator)
find_program(NX_GLSLC glslc HINTS "${NX_VULKAN_SDK}/Bin" "${NX_VULKAN_SDK}/bin" ${NX_NDK_SHADER_TOOLS})
if(NX_GLSLC)
    set(NX_GLSL_TOOL "${NX_GLSLC}")
    set(NX_GLSL_ARGS -O --target-env=vulkan1.0 -mfmt=num -o)
else()
    find_program(NX_GLSLANG glslangValidator HINTS "${NX_VULKAN_SDK}/Bin" "${NX_VULKAN_SDK}/bin" REQUIRED)
    set(NX_GLSL_TOOL "${NX_GLSLANG}")
    set(NX_GLSL_ARGS -V --target-env vulkan1.0 -x -o)
endif()
foreach(shader astc_decode.comp)
    set(out "${NX_SHADER_OUT}/${shader}.inc")
    add_custom_command(OUTPUT "${out}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${NX_SHADER_OUT}"
        COMMAND "${NX_GLSL_TOOL}" ${NX_GLSL_ARGS} "${out}" "${CMAKE_SOURCE_DIR}/runtime/shaders/${shader}"
        DEPENDS "${CMAKE_SOURCE_DIR}/runtime/shaders/${shader}"
        COMMENT "glslc ${shader}")
    list(APPEND NX_SHADER_SOURCES "${out}")
endforeach()

# Small OFL controller-prompt fonts: ordinary C++ arrays also work on compilers without #embed.
set(NX_PC_FONT_DATA "")
foreach(font nintendo xbox playstation)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/third_party/fonts/PCButtons/${font}.ttf")
    file(READ "${CMAKE_SOURCE_DIR}/third_party/fonts/PCButtons/${font}.ttf" font_hex HEX)
    string(REGEX REPLACE "(..)" "0x\\1," font_bytes "${font_hex}")
    string(APPEND NX_PC_FONT_DATA "const unsigned char ${font}[] = {${font_bytes}};\n")
endforeach()
file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/generated/pc_button_fonts.inc" CONTENT "${NX_PC_FONT_DATA}" @ONLY)
set_source_files_properties(runtime/src/button_font.cpp PROPERTIES INCLUDE_DIRECTORIES "${CMAKE_BINARY_DIR}/generated")

# Unit tests of runtime pieces that build without the game (docs/nvn/WINDOWS-PLAN.md, "Test plan"): ctest.
option(NX_TESTS "Build the standalone unit tests" OFF)
if(NX_TESTS AND NOT ANDROID)
    enable_testing()
    add_executable(friends_service_test tests/friends_service_test.cpp runtime/src/ipc.cpp runtime/src/svc_friends.cpp)
    target_include_directories(friends_service_test PRIVATE runtime/include runtime/src)
    add_test(NAME friends_service COMMAND friends_service_test)
    add_executable(keyboard_actions_test tests/keyboard_actions_test.cpp runtime/src/overlay/settings.cpp)
    target_include_directories(keyboard_actions_test PRIVATE runtime/src)
    add_test(NAME keyboard_actions COMMAND keyboard_actions_test)
    add_executable(keyboard_defaults_test tests/keyboard_defaults_test.cpp runtime/src/overlay/settings.cpp)
    target_include_directories(keyboard_defaults_test PRIVATE runtime/src)
    add_test(NAME keyboard_defaults COMMAND keyboard_defaults_test)
    add_executable(settings_test tests/settings_test.cpp runtime/src/overlay/settings.cpp)
    target_include_directories(settings_test PRIVATE runtime/src/overlay runtime/src)
    add_test(NAME settings COMMAND settings_test)
    add_executable(spirv_nan_test tests/spirv_nan_test.cpp)
    target_include_directories(spirv_nan_test PRIVATE runtime/src/nvn)
    add_test(NAME spirv_nan COMMAND spirv_nan_test)
    add_executable(mod_catalog_test tests/mod_catalog_test.cpp runtime/src/launcher/mod_catalog.cpp)
    target_include_directories(mod_catalog_test PRIVATE runtime/src)
    add_test(NAME mod_catalog COMMAND mod_catalog_test)
    add_executable(game_source_test tests/game_source_test.cpp runtime/src/launcher/game_source.cpp)
    target_include_directories(game_source_test PRIVATE runtime/src)
    add_test(NAME game_source COMMAND game_source_test)
    add_executable(frametrace_test tests/frametrace_test.cpp runtime/src/frametrace.cpp)
    target_include_directories(frametrace_test PRIVATE runtime/src)
    if(WIN32)
        target_link_libraries(frametrace_test PRIVATE psapi)
    endif()
    add_test(NAME frametrace COMMAND frametrace_test)
    find_package(Python3 QUIET COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME frametrace_report COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/frametrace_report_test.py")
        add_test(NAME setup_paths COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/setup_paths_test.py")
        add_test(NAME wonder_realtime COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/wonder_realtime_test.py")
        add_test(NAME nup_package COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/nup_package_test.py")
        add_test(NAME wonder_library COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/wonder_library_test.py")
        add_test(NAME gamebuild_coverage COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/gamebuild_coverage_test.py")
        add_test(NAME lan_trace_compare COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/lan_trace_compare_test.py")
    endif()
    add_executable(file_io_test tests/file_io_test.cpp)
    target_include_directories(file_io_test PRIVATE runtime/src)
    add_test(NAME file_io COMMAND file_io_test)
    add_executable(profile_account_test tests/profile_account_test.cpp runtime/src/profiles.cpp runtime/src/svc_account.cpp)
    target_include_directories(profile_account_test PRIVATE runtime/include runtime/src)
    add_test(NAME profile_account COMMAND profile_account_test)
    add_executable(profiles_test tests/profiles_test.cpp runtime/src/profiles.cpp runtime/src/launcher/save_backup.cpp)
    target_include_directories(profiles_test PRIVATE runtime/src)
    add_test(NAME profiles COMMAND profiles_test)
    add_executable(save_backup_test tests/save_backup_test.cpp runtime/src/launcher/save_backup.cpp)
    target_include_directories(save_backup_test PRIVATE runtime/src)
    add_test(NAME save_backup COMMAND save_backup_test)
    add_executable(shader_bundle_test tests/shader_bundle_test.cpp runtime/src/shader_bundle.cpp)
    target_include_directories(shader_bundle_test PRIVATE runtime/src)
    add_test(NAME shader_bundle COMMAND shader_bundle_test)
    add_executable(cache_file_test tests/cache_file_test.cpp)
    target_include_directories(cache_file_test PRIVATE runtime/src/nvn)
    add_test(NAME cache_file COMMAND cache_file_test)
    add_executable(pipeline_cache_data_test tests/pipeline_cache_data_test.cpp)
    target_include_directories(pipeline_cache_data_test PRIVATE runtime/src/nvn "${NX_VULKAN_SDK}/Include")
    add_test(NAME pipeline_cache_data COMMAND pipeline_cache_data_test)
    add_executable(pipeline_blend_key_test tests/pipeline_blend_key_test.cpp)
    target_include_directories(pipeline_blend_key_test PRIVATE runtime/src runtime/include "${NX_VULKAN_SDK}/Include")
    add_test(NAME pipeline_blend_key COMMAND pipeline_blend_key_test)
    add_executable(present_geometry_test tests/present_geometry_test.cpp)
    target_include_directories(present_geometry_test PRIVATE runtime/src/nvn)
    add_test(NAME present_geometry COMMAND present_geometry_test)
    add_executable(texture_cache_test tests/texture_cache_test.cpp runtime/src/nvn/texture_cache.cpp)
    target_include_directories(texture_cache_test PRIVATE runtime/src/nvn)
    add_test(NAME texture_cache COMMAND texture_cache_test)
    find_package(Threads REQUIRED)
    add_executable(stream_cache_test tests/stream_cache_test.cpp runtime/src/stream_cache.cpp)
    target_include_directories(stream_cache_test PRIVATE runtime/src)
    target_link_libraries(stream_cache_test PRIVATE Threads::Threads)
    add_test(NAME stream_cache COMMAND stream_cache_test)
    if(WIN32 OR (CMAKE_SYSTEM_NAME STREQUAL "Linux"))
        add_executable(netplay_timing_test tests/netplay_timing_test.cpp)
        target_include_directories(netplay_timing_test PRIVATE runtime/src)
        add_test(NAME netplay_timing COMMAND netplay_timing_test)
        add_executable(netplay_trace_test tests/netplay_trace_test.cpp)
        target_include_directories(netplay_trace_test PRIVATE runtime/src)
        add_test(NAME netplay_trace COMMAND netplay_trace_test)
        add_executable(netplay_pipeline_test tests/netplay_pipeline_test.cpp)
        target_include_directories(netplay_pipeline_test PRIVATE runtime/src)
        add_test(NAME netplay_pipeline COMMAND netplay_pipeline_test)
        add_executable(wonder_lan_seed_test tests/wonder_lan_seed_test.cpp)
        target_include_directories(wonder_lan_seed_test PRIVATE "${CMAKE_SOURCE_DIR}/games/wonder")
        add_test(NAME wonder_lan_seed COMMAND wonder_lan_seed_test)
        add_executable(netplay_wire_test tests/netplay_wire_test.cpp runtime/src/netplay_wire.cpp runtime/src/launcher/update_crypto.c)
        target_include_directories(netplay_wire_test PRIVATE runtime/src)
        add_test(NAME netplay_wire COMMAND netplay_wire_test)
        add_executable(netplay_save_test tests/netplay_save_test.cpp runtime/src/netplay_save.cpp runtime/src/launcher/update_crypto.c)
        target_include_directories(netplay_save_test PRIVATE runtime/src)
        add_test(NAME netplay_save COMMAND netplay_save_test)
        add_executable(netplay_session_test tests/netplay_session_test.cpp runtime/src/netplay_wire.cpp runtime/src/netplay_save.cpp runtime/src/netplay_session.cpp runtime/src/launcher/update_crypto.c)
        target_include_directories(netplay_session_test PRIVATE runtime/src)
        if(WIN32)
            target_link_libraries(netplay_session_test PRIVATE ws2_32)
        endif()
        add_test(NAME netplay_session COMMAND netplay_session_test)
        add_executable(netplay_latency_bench tools/perf/lan_transport_bench.cpp runtime/src/netplay_wire.cpp runtime/src/netplay_save.cpp runtime/src/netplay_session.cpp runtime/src/launcher/update_crypto.c)
        target_include_directories(netplay_latency_bench PRIVATE runtime/src)
        if(WIN32)
            target_link_libraries(netplay_latency_bench PRIVATE ws2_32)
        endif()
        add_executable(netplay_lobby_test tests/netplay_lobby_test.cpp runtime/src/profiles.cpp runtime/src/netplay_game.cpp runtime/src/netplay_wire.cpp runtime/src/netplay_save.cpp runtime/src/netplay_session.cpp runtime/src/launcher/update_crypto.c)
        target_include_directories(netplay_lobby_test PRIVATE runtime/include runtime/src)
        target_compile_definitions(netplay_lobby_test PRIVATE NX_VERSION_STRING="test")
        if(WIN32)
            target_link_libraries(netplay_lobby_test PRIVATE ws2_32)
        endif()
        add_test(NAME netplay_lobby COMMAND netplay_lobby_test)
    endif()
    add_executable(button_font_test tests/button_font_test.cpp runtime/src/button_font.cpp runtime/src/overlay/settings.cpp)
    target_include_directories(button_font_test PRIVATE runtime/include runtime/src)
    add_test(NAME button_font COMMAND button_font_test)
    add_executable(input_script_test tests/input_script_test.cpp runtime/src/input_script.cpp)
    target_include_directories(input_script_test PRIVATE runtime/include runtime/src)
    add_test(NAME input_script COMMAND input_script_test)
    set(NX_WARP_SHADER "${CMAKE_BINARY_DIR}/shaders/warp32.spv")
    if(NX_GLSLC)
        set(NX_WARP_GLSL_ARGS --target-env=vulkan1.1)
    else()
        set(NX_WARP_GLSL_ARGS -V --target-env vulkan1.1)
    endif()
    add_custom_command(OUTPUT "${NX_WARP_SHADER}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/shaders"
        COMMAND "${NX_GLSL_TOOL}" ${NX_WARP_GLSL_ARGS} -o "${NX_WARP_SHADER}" "${CMAKE_SOURCE_DIR}/tests/shaders/warp32.comp"
        DEPENDS tests/shaders/warp32.comp)
    add_executable(warp32_test tests/warp32_test.cpp runtime/src/nvn/nvn_spirv_warp.cpp "${NX_WARP_SHADER}")
    target_include_directories(warp32_test PRIVATE runtime/src/nvn)
    add_test(NAME warp32 COMMAND warp32_test "${NX_WARP_SHADER}")
    if(WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_executable(warp32_gpu_test tests/warp32_gpu_test.cpp runtime/src/nvn/nvn_spirv_warp.cpp "${NX_WARP_SHADER}")
        target_include_directories(warp32_gpu_test PRIVATE runtime/src/nvn "${NX_VULKAN_SDK}/Include")
        if(WIN32)
            target_link_libraries(warp32_gpu_test PRIVATE "${NX_VULKAN_SDK}/Lib/vulkan-1.lib")
        else()
            target_link_libraries(warp32_gpu_test PRIVATE vulkan)
        endif()
        add_test(NAME warp32_gpu COMMAND warp32_gpu_test "${NX_WARP_SHADER}")
        set_tests_properties(warp32_gpu PROPERTIES SKIP_RETURN_CODE 77)
        set(NX_NAN_SHADER "${CMAKE_BINARY_DIR}/shaders/nan_minmax.spv")
        add_custom_command(OUTPUT "${NX_NAN_SHADER}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/shaders"
            COMMAND "${NX_GLSL_TOOL}" ${NX_WARP_GLSL_ARGS} -o "${NX_NAN_SHADER}" "${CMAKE_SOURCE_DIR}/tests/shaders/nan_minmax.comp"
            DEPENDS tests/shaders/nan_minmax.comp)
        add_executable(spirv_nan_gpu_test tests/spirv_nan_gpu_test.cpp "${NX_NAN_SHADER}")
        target_include_directories(spirv_nan_gpu_test PRIVATE runtime/src/nvn "${NX_VULKAN_SDK}/Include")
        if(WIN32)
            target_link_libraries(spirv_nan_gpu_test PRIVATE "${NX_VULKAN_SDK}/Lib/vulkan-1.lib")
        else()
            target_link_libraries(spirv_nan_gpu_test PRIVATE vulkan)
        endif()
        add_test(NAME spirv_nan_gpu COMMAND spirv_nan_gpu_test "${NX_NAN_SHADER}")
        set_tests_properties(spirv_nan_gpu PROPERTIES SKIP_RETURN_CODE 77)
    endif()
    add_executable(settings_service_test tests/settings_service_test.cpp runtime/src/svc_settings.cpp)
    target_include_directories(settings_service_test PRIVATE runtime/include runtime/src)
    add_test(NAME settings_service COMMAND settings_service_test)
    find_package(Python3 QUIET COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        execute_process(COMMAND "${Python3_EXECUTABLE}" -c "import fontTools" RESULT_VARIABLE NX_FONTTOOLS_MISSING
                        OUTPUT_QUIET ERROR_QUIET)
        if(NOT NX_FONTTOOLS_MISSING)
            add_test(NAME overlay_font_coverage COMMAND "${Python3_EXECUTABLE}"
                     "${CMAKE_SOURCE_DIR}/tests/overlay_font_coverage_test.py")
        endif()
    endif()
    add_executable(gpu_pick_test tests/gpu_pick_test.cpp runtime/src/nvn/nvn_gpu_pick.cpp)
    target_compile_definitions(gpu_pick_test PRIVATE NX_GPU_PICK_STANDALONE)
    target_include_directories(gpu_pick_test PRIVATE runtime/src/nvn)
    add_test(NAME gpu_pick COMMAND gpu_pick_test)
    add_executable(tiling_test tests/tiling_test.cpp runtime/src/nvn/nvn_tiling.cpp)
    target_include_directories(tiling_test PRIVATE runtime/src/nvn)
    add_test(NAME tiling COMMAND tiling_test --selftest)
    add_executable(astc_test tests/astc_test.cpp runtime/src/nvn/nvn_astc.cpp)
    target_include_directories(astc_test PRIVATE runtime/src/nvn)
    add_test(NAME astc COMMAND astc_test --selftest)
    if(WIN32)  # the GPU decoder against the CPU one (needs a Vulkan device)
        add_executable(astc_gpu_test tests/astc_gpu_test.cpp runtime/src/nvn/nvn_astc.cpp
                       "${NX_SHADER_OUT}/astc_decode.comp.inc")
        target_include_directories(astc_gpu_test PRIVATE runtime/src/nvn "${NX_SHADER_OUT}" "${NX_VULKAN_SDK}/Include")
        target_link_libraries(astc_gpu_test PRIVATE "${NX_VULKAN_SDK}/Lib/vulkan-1.lib")
        add_test(NAME astc_gpu COMMAND astc_gpu_test)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")  # the same, with the system's Vulkan loader and headers
        add_executable(astc_gpu_test tests/astc_gpu_test.cpp runtime/src/nvn/nvn_astc.cpp
                       "${NX_SHADER_OUT}/astc_decode.comp.inc")
        target_include_directories(astc_gpu_test PRIVATE runtime/src/nvn "${NX_SHADER_OUT}")
        target_link_libraries(astc_gpu_test PRIVATE vulkan)
        add_test(NAME astc_gpu COMMAND astc_gpu_test)
    endif()
    add_executable(shader_cache_test tests/shader_cache_test.cpp runtime/src/nvn/nvn_shaders.cpp
                   runtime/src/nvn/nvn_spirv_reflect.cpp)
    target_compile_definitions(shader_cache_test PRIVATE NX_SHADERS_STANDALONE)
    target_include_directories(shader_cache_test PRIVATE runtime/src/nvn)
    if(EXISTS "${CMAKE_SOURCE_DIR}/private/wonder/spirv")  # the game's own shaders: never in the repository
        add_test(NAME shader_cache COMMAND shader_cache_test "${CMAKE_SOURCE_DIR}/private/wonder")
    endif()
    if(WIN32)
        add_executable(memguard_test tests/memguard_test.cpp)
        target_link_libraries(memguard_test PRIVATE psapi)
        add_test(NAME memguard COMMAND memguard_test)
    endif()
endif()

# The game code library (nx/gamelib.h): the program holds the runtime only and loads the recompiled modules from
# <game>_game.dll / lib<game>_game.so, built from the player's own files. So the program builds, and can ship, without
# any generated code. OFF: one program with the game code linked in, as before (Android and macOS stay that way).
if(WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(NX_GAME_LIBRARY_DEFAULT ON)
else()
    set(NX_GAME_LIBRARY_DEFAULT OFF)
endif()
option(NX_GAME_LIBRARY "Load the game code from a library of its own" ${NX_GAME_LIBRARY_DEFAULT})
if(NX_GAME_LIBRARY AND (ANDROID OR APPLE))
    message(FATAL_ERROR "NX_GAME_LIBRARY is for Windows and Linux")
endif()
if(WIN32)
    set(NX_GAMELIB_FILE "${NX_GAME}_game.dll")
else()
    set(NX_GAMELIB_FILE "lib${NX_GAME}_game.so")
endif()

if(NX_TESTS)
    # SuperWonders Update's plain logic (launcher/update_core.c, update_crypto.c): versions, releases, file lists,
    # paths, launcher installs
    add_executable(update_core_test tests/update_core_test.c runtime/src/launcher/update_core.c
                   runtime/src/launcher/update_crypto.c)
    add_test(NAME update_core COMMAND update_core_test)
endif()

# Without the generated code (a checkout with no game data) only the tests above are built, and the program when it
# loads the game code from a library.
file(GLOB NX_GEN_PROBE "${NX_GEN_DIR}/main/*.cpp")
if(NOT NX_GEN_PROBE AND NOT NX_GAME_LIBRARY)
    message(WARNING "No generated code in ${NX_GEN_DIR}/main: building the unit tests only")
    return()
elseif(NOT NX_GEN_PROBE)
    message(STATUS "No generated code in ${NX_GEN_DIR}/main: building the program only, not ${NX_GAMELIB_FILE}")
endif()

if(NX_GEN_PROBE)
foreach(mod main sdk)
    file(GLOB src CONFIGURE_DEPENDS "${NX_GEN_DIR}/${mod}/*.cpp")
    add_library(game_${mod} STATIC ${src})
    target_include_directories(game_${mod} PRIVATE runtime/include "${NX_GEN_DIR}/${mod}")
    if(NX_GAME_LIBRARY AND NOT WIN32)
        # Linked into a shared library. Hidden visibility keeps the calls between recompiled functions direct.
        set_target_properties(game_${mod} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    endif()
    if(ANDROID)
        # Hidden visibility: calls between recompiled functions are direct, not through the PLT.
        # armv8.2-a+crypto: cpu.h's AES/SHA paths need __ARM_FEATURE_AES/SHA2 (the NDK default armv8-a has neither).
        target_compile_options(game_${mod} PRIVATE -O2 -fPIC -fvisibility=hidden -fno-semantic-interposition
                               -march=armv8.2-a+crypto)
    elseif(APPLE OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_compile_options(game_${mod} PRIVATE -O2 -fvisibility=hidden)
        if(NX_GAME_LIBRARY)
            target_compile_options(game_${mod} PRIVATE -fno-semantic-interposition)
        endif()
    endif()
endforeach()

if(NX_GAME_LIBRARY)
    # wonder_game.dll / libwonder_game.so: the two modules and the jumps to the runtime (runtime/src/gamelib_entry.cpp).
    # Nothing else of the runtime is in it, and it exports only nx_gamelib_bind.
    add_library(${NX_GAME}_game SHARED runtime/src/gamelib_entry.cpp)
    target_include_directories(${NX_GAME}_game PRIVATE runtime/include)
    target_compile_definitions(${NX_GAME}_game PRIVATE NX_GAME_NAME="${NX_GAME}")
    target_link_libraries(${NX_GAME}_game PRIVATE game_main game_sdk)
    if(WIN32)
        set_target_properties(${NX_GAME}_game PROPERTIES PREFIX "" IMPORT_PREFIX "")
    endif()
    if(MINGW)
        # The C++ runtime as DLLs (libc++.dll, libunwind.dll, next to the program), shared with it: the runtime throws
        # LongJmp, the recompiled code catches it. A map for tools/symbolize.py, like the program's.
        target_link_options(${NX_GAME}_game PRIVATE "-Wl,-Map,$<TARGET_FILE_DIR:${NX_GAME}_game>/${NX_GAME}_game.map")
    elseif(NOT WIN32)
        target_compile_options(${NX_GAME}_game PRIVATE -fvisibility=hidden)
        target_link_options(${NX_GAME}_game PRIVATE -Wl,-Bsymbolic -Wl,--no-undefined)
    endif()
endif()
endif()  # NX_GEN_PROBE

set(NX_RUNTIME_SOURCES
    runtime/src/main.cpp
    runtime/src/loader.cpp
    runtime/src/memory.cpp
    runtime/src/kernel.cpp
    runtime/src/kernel_watch.cpp
    runtime/src/ipc.cpp
    runtime/src/svc_am.cpp
    runtime/src/svc_settings.cpp
    runtime/src/svc_nfp.cpp
    runtime/src/svc_misc.cpp
    runtime/src/svc_time.cpp
    runtime/src/svc_process.cpp
    runtime/src/hle_fs.cpp
    runtime/src/stream_cache.cpp
    runtime/src/svc_account.cpp
    runtime/src/profiles.cpp
    runtime/src/svc_friends.cpp
    runtime/src/hle_ro.cpp
    runtime/src/hle_vi.cpp
    runtime/src/hle_hid.cpp
    runtime/src/hle_net.cpp
    runtime/src/hle_audio.cpp
    runtime/src/hle_pl.cpp
    runtime/src/input_script.cpp
    runtime/src/controllers.cpp
    runtime/src/button_font.cpp
    runtime/src/shader_bundle.cpp
    runtime/src/play.cpp
    runtime/src/touch_layout.cpp
    runtime/src/perf_hint.cpp
    runtime/src/frame_stats.cpp
    runtime/src/frametrace.cpp
    runtime/src/write_watch.cpp
    runtime/src/nvn/nvn_caps.cpp
    runtime/src/nvn/nvn_renderpass.cpp
    runtime/src/nvn/nvn_gpu_busy.cpp
    runtime/src/nvn/nvn_janitor.cpp
    runtime/src/nvn/nvn_trace.cpp
    runtime/src/nvn/nvn_device.cpp
    runtime/src/nvn/nvn_objects.cpp
    runtime/src/nvn/nvn_incarnation.cpp
    runtime/src/nvn/nvn_texture_layout.cpp
    runtime/src/nvn/nvn_program.cpp
    runtime/src/nvn/nvn_queue.cpp
    runtime/src/nvn/nvn_cmd.cpp
    runtime/src/nvn/nvn_state.cpp
    runtime/src/nvn/nvn_format.cpp
    runtime/src/nvn/nvn_enums.cpp
    runtime/src/nvn/nvn_sampler.cpp
    runtime/src/nvn/nvn_sampler_objects.cpp
    runtime/src/nvn/nvn_draw_state.cpp
    runtime/src/nvn/nvn_raster.cpp
    runtime/src/nvn/nvn_worker.cpp
    runtime/src/nvn/nvn_shaders.cpp
    runtime/src/nvn/nvn_shader_prepare.cpp
    runtime/src/nvn/nvn_spirv_reflect.cpp
    runtime/src/nvn/nvn_tiling.cpp
    runtime/src/nvn/nvn_vk.cpp
    runtime/src/nvn/nvn_gpu_pick.cpp
    runtime/src/nvn/nvn_image.cpp
    runtime/src/nvn/nvn_frames.cpp
    runtime/src/nvn/nvn_dedup.cpp
    runtime/src/nvn/nvn_written.cpp
    runtime/src/nvn/nvn_replay.cpp
    runtime/src/nvn/nvn_debug.cpp
    runtime/src/nvn/nvn_binds.cpp
    runtime/src/nvn/nvn_copy.cpp
    runtime/src/nvn/nvn_pipeline.cpp
    runtime/src/nvn/nvn_dummy.cpp
    runtime/src/nvn/nvn_fsr.cpp
    runtime/src/nvn/nvn_framegen.cpp
    runtime/src/nvn/nvn_fxaa.cpp
    runtime/src/nvn/nvn_compare_twin.cpp
    runtime/src/nvn/nvn_pipeline_cache.cpp
    runtime/src/nvn/nvn_upload.cpp
    runtime/src/nvn/texture_cache.cpp
    runtime/src/nvn/nvn_astc.cpp
    runtime/src/nvn/nvn_spirv_patch.cpp
    runtime/src/nvn/nvn_spirv_warp.cpp
    runtime/src/nvn/nvn_spirv_int_textures.cpp
    runtime/src/nvn/nvn_spirv_locals.cpp
    runtime/src/nvn/nvn_spirv_unused.cpp
    runtime/src/nvn/nvn_pipeline_async.cpp
    runtime/src/nvn/nvn_spirv_clip.cpp
    runtime/src/nvn/nvn_ultrawide.cpp
    runtime/src/nvn/nvn_present.cpp
    runtime/src/nvn/nvn_pipeline_cache.cpp
    runtime/src/nvn/nvn_bc.cpp
    runtime/src/nvn/nvn_draw.cpp
    runtime/src/nvn/nvn_sets.cpp
    runtime/src/nvn/nvn_clears.cpp
    runtime/src/nvn/nvn_mask_clear.cpp
    runtime/src/nvn/nvn_draw_trace.cpp
    runtime/src/vk_bridge.cpp
    runtime/src/vk_swapchain.cpp
    runtime/src/vk_gen.cpp
    runtime/src/vk_present.cpp
    runtime/src/overlay/settings.cpp
    runtime/src/overlay/overlay.cpp
    runtime/src/overlay/overlay_menu.cpp
    runtime/src/overlay/overlay_progress.cpp
    runtime/src/overlay/overlay_ui.cpp
    runtime/src/overlay/option_rows.cpp
    runtime/src/overlay/overlay_vk.cpp
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_vulkan.cpp
)
# Dear ImGui (third_party/imgui, MIT): the overlay menu. Its Vulkan backend loads functions through the runtime's
# loader (ImGui_ImplVulkan_LoadFunctions), like the rest of the runtime.
set_source_files_properties(third_party/imgui/backends/imgui_impl_vulkan.cpp PROPERTIES
    COMPILE_DEFINITIONS "VK_NO_PROTOTYPES;IMGUI_IMPL_VULKAN_NO_PROTOTYPES")
# AMD FidelityFX FSR 1 (third_party/fsr1, MIT): its CPU-side constant setup (FsrEasuCon, FsrRcasCon).
set_source_files_properties(runtime/src/nvn/nvn_fsr.cpp PROPERTIES
    INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/third_party/fsr1")
set_source_files_properties(runtime/src/overlay/overlay_vk.cpp PROPERTIES
    COMPILE_DEFINITIONS "IMGUI_IMPL_VULKAN_NO_PROTOTYPES")
file(GLOB NX_GAME_SOURCES CONFIGURE_DEPENDS "${NX_GAME_DIR}/*.cpp")
if(WIN32 OR (CMAKE_SYSTEM_NAME STREQUAL "Linux"))
    list(APPEND NX_RUNTIME_SOURCES runtime/src/netplay_wire.cpp runtime/src/netplay_save.cpp runtime/src/netplay_session.cpp runtime/src/netplay_game.cpp)
    if(NOT NX_GAME_LIBRARY)
        list(APPEND NX_RUNTIME_SOURCES runtime/src/launcher/update_crypto.c)
    endif()
endif()
list(APPEND NX_RUNTIME_SOURCES ${NX_GAME_SOURCES} ${NX_SHADER_SOURCES} runtime/src/gamelib.cpp)
if(NX_GAME_LIBRARY)
    # SuperWonders Launcher: the start screen before the game (setup, settings, mods, updates; launcher/launcher.h)
    list(APPEND NX_RUNTIME_SOURCES runtime/src/launcher/profile_ui.cpp runtime/src/launcher/save_backup.cpp runtime/src/launcher/mod_catalog.cpp runtime/src/launcher/game_source.cpp runtime/src/launcher/launcher.cpp runtime/src/launcher/updates.cpp runtime/src/launcher/test_pad.cpp
         runtime/src/launcher/update_core.c runtime/src/launcher/update_crypto.c)
    if(NOT WIN32)
        list(APPEND NX_RUNTIME_SOURCES runtime/src/launcher/update_appimage.c)  # (an AppImage updates itself)
    endif()
endif()
# The release's version (VERSION): the launcher shows it, the updater compares it with the newest release's.
file(STRINGS "${CMAKE_SOURCE_DIR}/VERSION" NX_VERSION LIMIT_COUNT 1)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/VERSION")
# The recompiled modules: linked into the program, or loaded from the game code library (above).
if(NX_GAME_LIBRARY)
    set(NX_GAME_MODULES "")
else()
    set(NX_GAME_MODULES game_main game_sdk)
endif()

if(ANDROID)
    # NativeActivity app: lib<game>.so, entry android_main (android_native_app_glue).
    set(GLUE "${ANDROID_NDK}/sources/android/native_app_glue")
    add_library(${NX_GAME} SHARED ${NX_RUNTIME_SOURCES} runtime/src/platform_android.cpp
        runtime/src/platform_android_input.cpp runtime/src/platform_android_audio.cpp runtime/src/memguard_android.cpp
        "${GLUE}/android_native_app_glue.c")
    target_include_directories(${NX_GAME} PRIVATE runtime/include runtime/src third_party/imgui "${NX_GAME_DIR}" "${NX_VULKAN_SDK}/Include" "${GLUE}")
    # Our ANativeActivity_onCreate (platform_android.cpp) runs first, on the UI thread, then calls the glue's.
    set_source_files_properties("${GLUE}/android_native_app_glue.c" PROPERTIES
        COMPILE_DEFINITIONS "ANativeActivity_onCreate=glue_ANativeActivity_onCreate")
    target_link_libraries(${NX_GAME} PRIVATE ${NX_GAME_MODULES} android log aaudio)
    target_compile_options(${NX_GAME} PRIVATE -fvisibility=hidden -fno-semantic-interposition -march=armv8.2-a+crypto)
    # The glue's ANativeActivity_onCreate must be exported. 16 KB page alignment: Android 15+ devices with 16 KB pages.
    target_link_options(${NX_GAME} PRIVATE -u ANativeActivity_onCreate -Wl,-z,max-page-size=16384)
elseif(APPLE)
    # macOS: SDL3 (window, input, audio) and the Vulkan loader + MoltenVK from Homebrew
    # (brew install sdl3 vulkan-headers vulkan-loader molten-vk shaderc).
    find_package(SDL3 REQUIRED CONFIG)
    find_path(NX_VULKAN_INCLUDE vulkan/vulkan.h HINTS /opt/homebrew/include /usr/local/include)
    add_executable(${NX_GAME} ${NX_RUNTIME_SOURCES} runtime/src/platform_mac.cpp runtime/src/memguard_mac.cpp)
    target_include_directories(${NX_GAME} PRIVATE runtime/include runtime/src third_party/imgui "${NX_GAME_DIR}" "${NX_VULKAN_INCLUDE}")
    target_link_libraries(${NX_GAME} PRIVATE ${NX_GAME_MODULES} SDL3::SDL3)
    target_compile_options(${NX_GAME} PRIVATE -fvisibility=hidden)
    # The executable loads near 0x1_0000_0000, below the guest ranges (0x40_0000_0000 and up). The game runs on its
    # own thread with a 64 MiB stack (platform_mac.cpp), so the main thread's stack size does not matter.
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Linux: SDL3 (window, input, audio, the Vulkan surface) and the Vulkan headers from the distribution
    # (Ubuntu/Debian: apt install libsdl3-dev libvulkan-dev). The loader comes with the GPU driver (libvulkan1).
    # The F1 settings menu (pc_menu_win.cpp) is Win32 drawing code and is not built here.
    find_package(Threads REQUIRED)
    # NX_SDL_BUNDLED: SDL3 built from its release here and linked in (the AppImage, built on Ubuntu 22.04, which has no
    # SDL3; tools/linux/package.sh). Off: the distribution's (apt install libsdl3-dev).
    option(NX_SDL_BUNDLED "Build SDL3 from source and link it statically" OFF)
    if(NX_SDL_BUNDLED)
        include(FetchContent)
        set(SDL_SHARED OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC ON CACHE BOOL "" FORCE)
        set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
        set(SDL_TESTS OFF CACHE BOOL "" FORCE)
        set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(SDL3
            URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz
            URL_HASH SHA256=9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        FetchContent_MakeAvailable(SDL3)
        if(NOT TARGET SDL3::SDL3)  # (SDL 3.4 as a subproject defines it itself)
            add_library(SDL3::SDL3 ALIAS SDL3-static)
        endif()
    else()
        find_package(SDL3 REQUIRED CONFIG)
    endif()
    # vk_gen.{h,cpp} need the registry version tools/gen_vk.py generated from (1.4.357); distributions ship older
    # headers (Ubuntu 26.04: 1.4.341). Use the LunarG SDK when VULKAN_SDK is set, else Khronos' headers at that tag.
    if(DEFINED ENV{VULKAN_SDK})
        set(NX_VULKAN_INCLUDE "$ENV{VULKAN_SDK}/include")
    else()
        include(FetchContent)
        FetchContent_Declare(VulkanHeaders
            GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
            GIT_TAG v1.4.357
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(VulkanHeaders)
        set(NX_VULKAN_INCLUDE "${vulkanheaders_SOURCE_DIR}/include")
    endif()
    add_executable(${NX_GAME} ${NX_RUNTIME_SOURCES} runtime/src/platform_linux.cpp runtime/src/platform_linux_input.cpp
                   runtime/src/memguard_linux.cpp)
    target_include_directories(${NX_GAME} PRIVATE runtime/include runtime/src third_party/imgui "${NX_GAME_DIR}" "${NX_VULKAN_INCLUDE}")
    target_link_libraries(${NX_GAME} PRIVATE ${NX_GAME_MODULES} SDL3::SDL3 Threads::Threads ${CMAKE_DL_LIBS})
    target_compile_options(${NX_GAME} PRIVATE -fvisibility=hidden)
    # The executable is position dependent and loads at 0x40_0000, far below the guest ranges (0x40_0000_0000 and
    # up); shared libraries and the host heap are mapped top-down from near 0x7fff_ffff_ffff. The game runs on its
    # own thread with a 64 MiB stack (platform_linux.cpp).
    target_link_options(${NX_GAME} PRIVATE -no-pie)
else()
    if(WIN32)
        # SDL's HIDAPI gamepad backend provides native DualSense support without a driver wrapper.
        include(FetchContent)
        set(SDL_SHARED OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC ON CACHE BOOL "" FORCE)
        set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
        set(SDL_TESTS OFF CACHE BOOL "" FORCE)
        set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
        set(SDL3_BUNDLED_SOURCE "${CMAKE_SOURCE_DIR}/tools/portable/source/SDL3-3.4.18")
        if(EXISTS "${SDL3_BUNDLED_SOURCE}/CMakeLists.txt")
            # FetchContent's explicit source override avoids interpreting a Windows path as a file URL.
            set(FETCHCONTENT_SOURCE_DIR_SDL3 "${SDL3_BUNDLED_SOURCE}" CACHE PATH "Bundled SDL source" FORCE)
        endif()
        FetchContent_Declare(SDL3
            URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz
            URL_HASH SHA256=9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        FetchContent_MakeAvailable(SDL3)
    endif()
    add_executable(${NX_GAME} ${NX_RUNTIME_SOURCES} runtime/src/platform_win.cpp runtime/src/pc_menu_win.cpp
                   runtime/src/memguard_win.cpp)
    target_include_directories(${NX_GAME} PRIVATE runtime/include runtime/src third_party/imgui "${NX_GAME_DIR}" "${NX_VULKAN_SDK}/Include")
    if(MINGW)
        # LLVM-MinGW ships the XAudio2 2.9 import library under its SDK name. Its plain "xinput" is XInput 1.3 (the
        # DirectX 2010 redistributable, missing on a clean Windows); 1.4 ships with Windows 8 and later.
        target_link_libraries(${NX_GAME} PRIVATE ${NX_GAME_MODULES} xinput1_4 xaudio2_9 ole32 psapi shell32 uuid winhttp ws2_32)
        if(NX_GAME_LIBRARY)
            # The C++ runtime shared with the game code library (one copy: LongJmp is thrown here and caught there),
            # so libc++.dll and libunwind.dll go next to the program. The UCRT stays a system DLL.
            get_filename_component(NX_MINGW_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
            add_custom_command(TARGET ${NX_GAME} POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${NX_MINGW_BIN}/libc++.dll" "${NX_MINGW_BIN}/libunwind.dll"
                        "$<TARGET_FILE_DIR:${NX_GAME}>")
        else()
            # libc++ and libunwind linked in: without it wonder.exe needs libc++.dll and libunwind.dll next to it, which
            # nothing copies there (the portable pack copies the MSVC runtime). The UCRT stays a system DLL.
            target_link_options(${NX_GAME} PRIVATE -static)
        endif()
    else()
        target_link_libraries(${NX_GAME} PRIVATE ${NX_GAME_MODULES} xinput xaudio2 ole32 psapi winhttp ws2_32)
    endif()
    if(WIN32)
        target_link_libraries(${NX_GAME} PRIVATE SDL3::SDL3-static)
        if(NX_GAME_LIBRARY AND NX_GAME STREQUAL "wonder")
            add_custom_command(TARGET ${NX_GAME} POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "$<TARGET_FILE:${NX_GAME}>" "$<TARGET_FILE_DIR:${NX_GAME}>/wonder-launcher.exe"
                VERBATIM)
        endif()
        if(NX_GAME STREQUAL "wonder")
            file(TO_CMAKE_PATH "${CMAKE_SOURCE_DIR}/assets/wonder_app.ico" WONDER_APP_ICON_PATH)
            configure_file(assets/wonder_app.rc.in "${CMAKE_CURRENT_BINARY_DIR}/wonder_app.rc" @ONLY)
            target_sources(${NX_GAME} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/wonder_app.rc")
        endif()
    endif()
    # The runtime uses the portable C library (getenv, fopen, ...) on every host: no "_s" variants.
    target_compile_definitions(${NX_GAME} PRIVATE _CRT_SECURE_NO_WARNINGS _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING)
    if(MSVC)
        # Keep host allocations low so the guest ranges (0x40_0000_0000..0x70_0000_0000) are always free; a big
        # main-thread stack (recompiled calls nest deeply); a link map for tools/symbolize.py.
        target_link_options(${NX_GAME} PRIVATE /STACK:67108864 /HIGHENTROPYVA:NO /MAP)
    elseif(MINGW)
        # llvm-mingw portable-toolchain equivalents of the MSVC linker options above.
        target_link_options(${NX_GAME} PRIVATE -Wl,--stack,67108864 -Wl,--disable-high-entropy-va
                            "-Wl,-Map,$<TARGET_FILE_DIR:${NX_GAME}>/${NX_GAME}.map")
    endif()
endif()

target_include_directories(${NX_GAME} PRIVATE "${NX_SHADER_OUT}")
if(NX_TESTS AND (WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux"))
    add_executable(controllers_test tests/controllers_test.cpp runtime/src/controllers.cpp)
    target_include_directories(controllers_test PRIVATE runtime/include runtime/src)
    if(WIN32)
        target_link_libraries(controllers_test PRIVATE SDL3::SDL3-static)
    else()
        target_link_libraries(controllers_test PRIVATE SDL3::SDL3)
    endif()
    add_test(NAME controllers COMMAND controllers_test)
    set_tests_properties(controllers PROPERTIES SKIP_RETURN_CODE 77)
endif()
target_compile_definitions(${NX_GAME} PRIVATE NX_GAME_NAME="${NX_GAME}" NX_GAMELIB_FILE="${NX_GAMELIB_FILE}"
                           NX_GAME_LIBRARY=$<BOOL:${NX_GAME_LIBRARY}> NX_VERSION_STRING="${NX_VERSION}")
# SuperWonders Update (launcher/updates.cpp): with a release key's public half (tools/sign_update.py --public), only
# signed releases are installed; without one, GitHub's SHA-256 of the archive is still required.
set(SUPERWONDERS_UPDATE_PUBKEY "" CACHE STRING "Release signing key, public half (128 hex digits); empty: not required")
if(SUPERWONDERS_UPDATE_PUBKEY)
    target_compile_definitions(${NX_GAME} PRIVATE SUPERWONDERS_UPDATE_PUBKEY="${SUPERWONDERS_UPDATE_PUBKEY}")
endif()
