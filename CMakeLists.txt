cmake_minimum_required(VERSION 3.24)
# Apple Silicon's first OS release; set before project() initializes the toolchain.
if(CMAKE_HOST_APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()
# A tagged/dispatch release supplies the display version; ordinary builds keep the dev label.
set(SAWGOD_RELEASE_VERSION "$ENV{SAWGOD_RELEASE_VERSION}" CACHE STRING "Release version, optionally prefixed by v")
set(SAWGOD_PROJECT_VERSION 0.0.1)
set(SAWGOD_VERSION_LABEL "0.0.1-dev")
if(NOT SAWGOD_RELEASE_VERSION STREQUAL "")
    if(NOT SAWGOD_RELEASE_VERSION MATCHES "^v?([0-9]+\\.[0-9]+\\.[0-9]+)(-[0-9A-Za-z.-]+)?$")
        message(FATAL_ERROR "Invalid SAWGOD_RELEASE_VERSION")
    endif()
    set(SAWGOD_PROJECT_VERSION "${CMAKE_MATCH_1}")
    string(REGEX REPLACE "^v" "" SAWGOD_VERSION_LABEL "${SAWGOD_RELEASE_VERSION}")
endif()
project(sawgod VERSION ${SAWGOD_PROJECT_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Keep build-machine paths out of released binaries, including dependency assertions.
if(MSVC)
    add_compile_options("/pathmap:${CMAKE_SOURCE_DIR}=.")
else()
    add_compile_options("-ffile-prefix-map=${CMAKE_SOURCE_DIR}=.")
endif()

if(ANDROID)
    # SDK CMake 4 removed compatibility with miniz's 3.0 minimum. Keep its sources unmodified.
    set(CMAKE_POLICY_VERSION_MINIMUM 3.10)
    # Also bounds AGP's direct Ninja invocation, regardless of its host CPU count.
    set_property(GLOBAL PROPERTY JOB_POOLS sawgod_android_cpu=4)
    set(CMAKE_JOB_POOL_COMPILE sawgod_android_cpu)
    set(CMAKE_JOB_POOL_LINK sawgod_android_cpu)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    set(SAWGOD_BUILD_TESTS OFF CACHE BOOL "Host tests are run in the Linux build" FORCE)
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(SAWGOD_SYSTEM_SDL "Use find_package(SDL3) instead of fetching our own" OFF)
option(SAWGOD_BUILD_TESTS "Build the sawgod_tests target" ON)
# The options menu's Debug category (Show FPS...): on for development builds, off for release builds.
if(CMAKE_BUILD_TYPE MATCHES "^(Release|MinSizeRel)$")
    set(SAWGOD_DEBUG_MENU_DEFAULT OFF)
else()
    set(SAWGOD_DEBUG_MENU_DEFAULT ON)
endif()
option(SAWGOD_DEBUG_MENU "Compile in the options menu's Debug category" ${SAWGOD_DEBUG_MENU_DEFAULT})
# GB graphics (game/gfx_swap.h) ship by default; OFF omits the graphics fidelity controls.
option(SAWGOD_GFX_SWAP "Compile in GFX Mode and the GFX Swap button (on: a release feature; off takes it out)" ON)
# M8: every frame point is native. ON makes V30MZ::step() a hard error before fetching any instruction;
# this covers both Wsc::tick dispatch and direct calls -- for the app's release builds;
# default OFF keeps the interpreter path available for the difftest suite (it runs the *original* code on
# purpose to compare against). See src/emu/v30mz.cpp.
option(SAWGOD_NO_INTERPRETER "Hard error instead of interpreting a ROM instruction with no native" OFF)

add_library(sawgod_warnings INTERFACE)
if(MSVC)
    # /utf-8: our sources are UTF-8 (Japanese text, full-width characters); NOMINMAX: windows.h's min/max
    # macros would break std::min/std::max
    target_compile_options(sawgod_warnings INTERFACE /W4 /utf-8)
    target_compile_definitions(sawgod_warnings INTERFACE NOMINMAX)
else()
    target_compile_options(sawgod_warnings INTERFACE -Wall -Wextra)
endif()

# --- SDL3 ---------------------------------------------------------------
if(SAWGOD_SYSTEM_SDL)
    find_package(SDL3 3.4 REQUIRED CONFIG)
else()
    include(FetchContent)
    if(ANDROID)
        set(SDL_SHARED ON CACHE BOOL "" FORCE)
        set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    else()
        set(SDL_SHARED OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC ON CACHE BOOL "" FORCE)
    endif()
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    if(ANDROID)
        set(SDL_ANDROID_JAR OFF CACHE BOOL "Java sources are compiled by Gradle" FORCE)
        set(SDL_SENSOR OFF CACHE BOOL "Gamepad-only build; no motion controls" FORCE)
    endif()
    FetchContent_Declare(
        SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.4.16
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(SDL3)
endif()

# Private timestamp instrumentation currently targets the static desktop SDL library.
if(NOT ANDROID)
    include(cmake/GpuTiming.cmake)
endif()

# --- miniz (MIT; reading mod zips, engine/mod_pack) ------------------------------
include(FetchContent)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_FUZZERS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(INSTALL_PROJECT OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    miniz
    GIT_REPOSITORY https://github.com/richgel999/miniz.git
    GIT_TAG 3.0.2
    GIT_SHALLOW TRUE
)
if(ANDROID)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "miniz stays static; FluidSynth opts into shared separately" FORCE)
endif()
# miniz 3.0.2 declares CMake 3.0, rejected by CMake 4. Scope the compatibility
# floor to this dependency instead of changing policies for the whole project.
function(sawgod_fetch_miniz)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    FetchContent_MakeAvailable(miniz)
endfunction()
sawgod_fetch_miniz()
if(ANDROID)
    # All archive IO is through memory buffers, with our own atomic file persistence.
    target_compile_definitions(miniz PUBLIC MINIZ_NO_STDIO)
endif()

# --- 7-Zip's C decoder (public domain; .7z ROMs on the ROM screen, engine/archive_7z) ---
# No CMake project upstream: fetch the sources and build the decoder-only subset here.
FetchContent_Declare(
    lzma7z
    GIT_REPOSITORY https://github.com/ip7z/7zip.git
    GIT_TAG 26.01
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(lzma7z)
enable_language(C)
set(LZMA7Z_C ${lzma7z_SOURCE_DIR}/C)
add_library(sawgod_7z STATIC
    ${LZMA7Z_C}/7zAlloc.c ${LZMA7Z_C}/7zArcIn.c ${LZMA7Z_C}/7zBuf.c ${LZMA7Z_C}/7zBuf2.c ${LZMA7Z_C}/7zCrc.c
    ${LZMA7Z_C}/7zCrcOpt.c ${LZMA7Z_C}/7zDec.c ${LZMA7Z_C}/7zStream.c ${LZMA7Z_C}/Bcj2.c ${LZMA7Z_C}/Bra.c
    ${LZMA7Z_C}/Bra86.c ${LZMA7Z_C}/BraIA64.c ${LZMA7Z_C}/CpuArch.c ${LZMA7Z_C}/Delta.c ${LZMA7Z_C}/Lzma2Dec.c
    ${LZMA7Z_C}/LzmaDec.c ${LZMA7Z_C}/Ppmd7.c ${LZMA7Z_C}/Ppmd7Dec.c
)
target_include_directories(sawgod_7z SYSTEM PUBLIC ${LZMA7Z_C})
target_compile_definitions(sawgod_7z PRIVATE Z7_PPMD_SUPPORT Z7_EXTRACT_ONLY)
set_target_properties(sawgod_7z PROPERTIES C_STANDARD 99 POSITION_INDEPENDENT_CODE ON)

# --- Dear ImGui (MIT; the options overlay) --------------------------------------
# No CMake project upstream: fetch the sources and build the core plus the SDL_Renderer backend here.
include(FetchContent)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.9b
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(imgui)
add_library(sawgod_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp
)
target_include_directories(sawgod_imgui SYSTEM PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_compile_definitions(sawgod_imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
target_link_libraries(sawgod_imgui PUBLIC SDL3::SDL3)

# --- engine --------------------------------------------------------------
set_property(SOURCE src/engine/version.cpp PROPERTY COMPILE_DEFINITIONS
    "SAWGOD_VERSION_STRING=\"SawGod ${SAWGOD_VERSION_LABEL}\"")
add_library(sawgod_engine STATIC
    src/engine/version.cpp
    src/engine/version.h
    src/engine/sha1.cpp
    src/engine/rom_identity.cpp
    src/engine/assets.cpp
    src/engine/assets.h
    src/engine/config.cpp
    src/engine/config.h
    src/engine/settings.cpp
    src/engine/settings.h
    src/engine/mod_pack.cpp
    src/engine/indexed_png.cpp
    src/engine/button_icons.cpp
    src/engine/button_icons.h
    src/engine/mod_pack.h
    src/engine/screen.cpp
    src/engine/screen.h
    src/engine/persist.cpp
    src/engine/persist.h
    src/engine/sgz.cpp
    src/engine/sgz.h
    src/engine/rom_check.cpp
    src/engine/rom_check.h
    src/engine/archive_7z.cpp
    src/engine/debug_log.cpp
    src/engine/debug_log.h
)
target_include_directories(sawgod_engine PUBLIC ${CMAKE_SOURCE_DIR}/src)
if(SAWGOD_GFX_SWAP)
    target_compile_definitions(sawgod_engine PUBLIC SAWGOD_GFX_SWAP)
endif()
if(SAWGOD_DEBUG_MENU)
    target_compile_definitions(sawgod_engine PUBLIC SAWGOD_DEBUG_MENU)
endif()
target_link_libraries(sawgod_engine PRIVATE sawgod_warnings miniz sawgod_7z)
# The button icons (assets/ui/button_icons.png + .csv, ours), compiled in: bytes and a string.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/assets/ui/button_icons.png
    ${CMAKE_SOURCE_DIR}/assets/ui/button_icons.csv)
file(READ ${CMAKE_SOURCE_DIR}/assets/ui/button_icons.png icons_hex HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," icons_bytes "${icons_hex}")
file(CONFIGURE OUTPUT ${CMAKE_BINARY_DIR}/generated/button_icons_png.inc CONTENT "${icons_bytes}\n" @ONLY)
file(READ ${CMAKE_SOURCE_DIR}/assets/ui/button_icons.csv icons_csv)
file(CONFIGURE OUTPUT ${CMAKE_BINARY_DIR}/generated/button_icons_csv.inc
    CONTENT "R\"SAWGODCSV(${icons_csv})SAWGODCSV\"\n" @ONLY)
target_include_directories(sawgod_engine PRIVATE ${CMAKE_BINARY_DIR}/generated)
# The build identity (version label + source revision), refreshed on every build: stored archives built by
# another build are rebuilt (engine/sgz.h).
add_custom_target(sawgod_build_id
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DLABEL=${SAWGOD_VERSION_LABEL}
        -DOUT=${CMAKE_BINARY_DIR}/generated/build_id.inc -P ${CMAKE_SOURCE_DIR}/cmake/BuildId.cmake
    BYPRODUCTS ${CMAKE_BINARY_DIR}/generated/build_id.inc
    VERBATIM)
add_dependencies(sawgod_engine sawgod_build_id)

# --- emulation core (bootstrap runtime: runs the original ROM) ----------------
add_library(sawgod_emu STATIC
    src/emu/v30mz.cpp
    src/emu/v30mz.h
    src/emu/wsc.cpp
    src/emu/wsc.h
    src/emu/png.cpp
    src/emu/png.h
    src/emu/apu.cpp
    src/emu/apu.h
    src/emu/v30mz_timing.inc
    src/emu/gb_cpu.cpp
    src/emu/gb_cpu.h
    src/emu/gb_apu.cpp
    src/emu/gb_apu.h
    src/emu/gb_song.cpp
    src/emu/gb_song.h
)
target_include_directories(sawgod_emu PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(sawgod_emu PRIVATE sawgod_warnings)
if(SAWGOD_NO_INTERPRETER)
    target_compile_definitions(sawgod_emu PRIVATE SAWGOD_NO_INTERPRETER)
endif()

# --- decompiled game code ----------------------------------------------------
add_library(sawgod_game STATIC
    src/game/setup.cpp
    src/game/machine.h
    src/game/fiber.cpp
    src/game/hooks.cpp
    src/game/save_file.cpp
    src/game/save_file.h
    src/game/gb_extract.cpp
    src/game/gb_intro.cpp
    src/game/world_card_lettering.cpp
    src/game/title_menu_lettering.cpp
    src/game/title_logo.cpp
    src/game/ending_notice_lettering.cpp
    src/game/intro_lettering.cpp
    src/game/gb_battle_effects.cpp
    src/game/gb_extract.h
    src/game/save_slots.cpp
    src/game/save_slots.h
    src/game/session.cpp
    src/game/session.h
    src/game/tables.cpp
    src/game/resources.cpp
    src/game/map_lz.cpp
    src/game/maps.cpp
    src/game/light_tags.cpp
    src/game/water_tags.cpp
    src/game/presentation_scene.cpp
    src/game/terrain_veil.cpp
    src/game/landmark_tags.cpp
    src/game/mode7.cpp
    src/game/gfx_swap.cpp
    src/game/info.cpp
    src/game/battle_ui.cpp
    src/game/inventory_ui.cpp
    src/game/item_dialog_ui.cpp
    src/game/ui_chrome.cpp
    src/game/ui_detail.cpp
    src/game/ui_items.cpp
    src/game/save_ui.cpp
    src/game/service_ui.cpp
    src/game/dialogue_ui.cpp
    src/game/host_picture.cpp
    src/game/party_ui.cpp
    src/game/creation_ui.cpp
    src/game/ui_sprites.cpp
    src/game/bestiary_ui.cpp
    src/game/text_wrap.cpp
    src/game/text_fit.cpp
    src/game/player_names.cpp
    src/game/tables.h
    src/game/text.cpp
    src/game/text.h
    src/game/text_keys.cpp
    src/game/text_render.cpp
    src/game/text_render.h
    src/game/text_tokens.cpp
    src/game/text_tokens.h
    src/game/text_keys.h
    src/game/hooks.h
    src/game/fiber.h
    src/game/routines.cpp
    src/game/bank34_misc.cpp
    src/game/routines_bank34.cpp
    src/game/routines_bank35.cpp
    src/game/bank37.cpp
    src/game/routines_bank37.cpp
    src/game/routines_bank36.cpp
    src/game/routines_bank3c.cpp
    src/game/bank36_misc.cpp
    src/game/routines_bank36x.cpp
    src/game/bank36_battle.cpp
    src/game/battle_rewards.cpp
    src/game/bank34.cpp
    src/game/bank34.h
    src/game/bank34_engine.cpp
    src/game/bank34_yields.cpp
    src/game/bank34_menus.cpp
    src/game/bank3d_sound.cpp
    src/game/bank3d_sound.h
    src/game/bank3f_orijinaru.cpp
    src/game/bank3f_orijinaru.h
    src/game/bank35_field.cpp
    src/game/bank35_world.cpp
    src/game/bank3c_title.cpp
    src/game/bank35_field.h
    src/game/system.cpp
    src/game/system.h
    src/game/routines_system.cpp
)
target_include_directories(sawgod_game PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(sawgod_game PUBLIC sawgod_emu sawgod_engine PRIVATE sawgod_warnings)

if(NOT ANDROID)
add_executable(sawgod-headless src/tools/field_preview.cpp src/tools/headless.cpp src/app/sound_mix.cpp src/app/gb_music.cpp src/app/gb_cues.cpp)
target_link_libraries(sawgod-headless PRIVATE sawgod_game sawgod_emu sawgod_engine sawgod_warnings)

# M15 audio-swap spike: GB songs through our own GB core, WSC songs through the native driver (loops, logs)
add_executable(sawgod-audio-spike src/tools/audio_spike.cpp src/app/sound_mix.cpp src/app/gb_music.cpp src/app/gb_cues.cpp)
target_link_libraries(sawgod-audio-spike PRIVATE sawgod_game sawgod_emu sawgod_engine sawgod_warnings)

endif()

# Shared live Enhanced mixer; the map is the same source as the offline renderer.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(Threads REQUIRED)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/tools/enhanced_music.json ${CMAKE_SOURCE_DIR}/tools/generate_enhanced_music.py
    ${CMAKE_SOURCE_DIR}/tools/audio_seats.py
    ${CMAKE_SOURCE_DIR}/tools/audio_drums.py ${CMAKE_SOURCE_DIR}/tools/audio_enhanced_percussion.py
    ${CMAKE_SOURCE_DIR}/tools/audio_casting.py
    ${CMAKE_SOURCE_DIR}/tools/audio_enhanced_drums/142.tab
    ${CMAKE_SOURCE_DIR}/tools/audio_enhanced_drums/143.tab
    ${CMAKE_SOURCE_DIR}/tools/audio_enhanced_drums/145.tab)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/generated)
execute_process(COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/generate_enhanced_music.py
    ${CMAKE_SOURCE_DIR}/tools/enhanced_music.json ${CMAKE_BINARY_DIR}/generated/enhanced_music_maps.inc
    COMMAND_ERROR_IS_FATAL ANY)
add_library(sawgod_enhanced_music STATIC src/app/enhanced_music.cpp)
target_include_directories(sawgod_enhanced_music PRIVATE ${CMAKE_BINARY_DIR}/generated)
set(SAWGOD_DEV_SOUNDFONT "${CMAKE_SOURCE_DIR}/ref/soundfonts/GeneralUser-GS.sf2" CACHE FILEPATH
    "Development soundfont fallback (empty for release packages)")
if(ANDROID)  # never a development path inside an APK, whatever an older cache holds
    set(SAWGOD_DEV_SOUNDFONT "" CACHE FILEPATH "Development soundfont fallback (empty for release packages)" FORCE)
endif()
target_compile_definitions(sawgod_enhanced_music PRIVATE SAWGOD_DEV_SOUNDFONT="${SAWGOD_DEV_SOUNDFONT}")
target_link_libraries(sawgod_enhanced_music PRIVATE SDL3::SDL3)
target_link_libraries(sawgod_enhanced_music PUBLIC sawgod_game PRIVATE sawgod_warnings Threads::Threads ${CMAKE_DL_LIBS})

# --- app -------------------------------------------------------------------
# The front end's SDL/ImGui layer: the options overlay skinned with the ROM's own font and window tiles,
# input mapping, the settings registry.
# Linux/Windows can bootstrap the existing binary shader outputs before Metal is
# generated. macOS and CI's all-shader check require MSL as well.
set(SAWGOD_SHADER_CHECK_ARGS --all --check)
if(NOT APPLE)
    list(APPEND SAWGOD_SHADER_CHECK_ARGS --allow-missing-msl)
endif()
execute_process(COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/compile_shaders.py
    ${SAWGOD_SHADER_CHECK_ARGS} COMMAND_ERROR_IS_FATAL ANY)
add_library(sawgod_ui STATIC
    src/app/mode7_renderer.cpp
    src/engine/fast_forward_vfx.cpp
    src/engine/focus_pass.cpp
    src/engine/enhanced_graphics.cpp
    src/engine/menu_fill.cpp
    src/engine/hd_menus.cpp
    src/engine/falloff_field.cpp
    src/engine/influence_stamp.cpp
    src/engine/influence_buffer.cpp
    src/engine/presentation_renderer.cpp
    src/engine/presentation_renderer.h
    src/engine/ui_skin.cpp
    src/engine/ui_skin.h
    src/engine/overlay.cpp
    src/engine/overlay.h
    src/engine/input.cpp
    src/engine/touch_controls.cpp
    src/engine/input_defaults.cpp
    src/engine/input.h
    src/engine/data_dir.cpp
    src/engine/data_dir.h
)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/assets/ui/touch/atlas.png)
file(READ ${CMAKE_SOURCE_DIR}/assets/ui/touch/atlas.png touch_hex HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," touch_bytes "${touch_hex}")
file(CONFIGURE OUTPUT ${CMAKE_BINARY_DIR}/generated/touch_art.inc CONTENT "${touch_bytes}\n" @ONLY)
target_include_directories(sawgod_ui PRIVATE ${CMAKE_BINARY_DIR}/generated)
target_include_directories(sawgod_ui PUBLIC ${CMAKE_SOURCE_DIR}/src)
# Offline shader outputs are embedded: no compiler/runtime DLL or deployment path required.
# A toolchain bootstrap can build ordinary GPU presentation before generating the identity shader.
set(SAWGOD_IDENTITY_HEADER ${CMAKE_SOURCE_DIR}/src/engine/shaders/identity.generated.h)
set(SAWGOD_IDENTITY_MANIFEST ${CMAKE_SOURCE_DIR}/assets/shaders/identity.manifest.json)
if(EXISTS ${SAWGOD_IDENTITY_HEADER} AND EXISTS ${SAWGOD_IDENTITY_MANIFEST})
    file(READ ${SAWGOD_IDENTITY_MANIFEST} SAWGOD_IDENTITY_JSON)
    string(JSON SAWGOD_IDENTITY_SOURCE_SHA GET "${SAWGOD_IDENTITY_JSON}" source_sha256)
    string(JSON SAWGOD_IDENTITY_HEADER_SHA GET "${SAWGOD_IDENTITY_JSON}" header_sha256)
    file(SHA256 ${CMAKE_SOURCE_DIR}/assets/shaders/identity.frag.hlsl SAWGOD_IDENTITY_ACTUAL_SOURCE_SHA)
    file(SHA256 ${SAWGOD_IDENTITY_HEADER} SAWGOD_IDENTITY_ACTUAL_HEADER_SHA)
    if(NOT SAWGOD_IDENTITY_SOURCE_SHA STREQUAL SAWGOD_IDENTITY_ACTUAL_SOURCE_SHA OR
       NOT SAWGOD_IDENTITY_HEADER_SHA STREQUAL SAWGOD_IDENTITY_ACTUAL_HEADER_SHA)
        message(FATAL_ERROR "Identity shader outputs are stale: run tools/compile_shaders.py")
    endif()
    target_compile_definitions(sawgod_ui PRIVATE SAWGOD_IDENTITY_COMPILED)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        ${CMAKE_SOURCE_DIR}/assets/shaders/identity.frag.hlsl ${SAWGOD_IDENTITY_MANIFEST} ${SAWGOD_IDENTITY_HEADER})
else()
    message(STATUS "Identity shaders unavailable: run tools/compile_shaders.py with an offline compiler")
endif()
# Enhanced, Mode 7 and focus shaders have the same offline source/output freshness contract.
foreach(SAWGOD_SHADER enhanced mode7 focus influence)
    file(READ ${CMAKE_SOURCE_DIR}/assets/shaders/${SAWGOD_SHADER}.manifest.json SAWGOD_SHADER_JSON)
    string(JSON SAWGOD_SHADER_SOURCE_SHA GET "${SAWGOD_SHADER_JSON}" source_sha256)
    string(JSON SAWGOD_SHADER_HEADER_SHA GET "${SAWGOD_SHADER_JSON}" header_sha256)
    file(SHA256 ${CMAKE_SOURCE_DIR}/assets/shaders/${SAWGOD_SHADER}.frag.hlsl SAWGOD_SHADER_ACTUAL_SOURCE_SHA)
    file(SHA256 ${CMAKE_SOURCE_DIR}/src/engine/shaders/${SAWGOD_SHADER}.generated.h SAWGOD_SHADER_ACTUAL_HEADER_SHA)
    if(NOT SAWGOD_SHADER_SOURCE_SHA STREQUAL SAWGOD_SHADER_ACTUAL_SOURCE_SHA OR
       NOT SAWGOD_SHADER_HEADER_SHA STREQUAL SAWGOD_SHADER_ACTUAL_HEADER_SHA)
        message(FATAL_ERROR "${SAWGOD_SHADER} shaders are stale: run tools/compile_shaders.py --name ${SAWGOD_SHADER}")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        ${CMAKE_SOURCE_DIR}/assets/shaders/${SAWGOD_SHADER}.frag.hlsl
        ${CMAKE_SOURCE_DIR}/assets/shaders/${SAWGOD_SHADER}.manifest.json
        ${CMAKE_SOURCE_DIR}/src/engine/shaders/${SAWGOD_SHADER}.generated.h)
endforeach()
# Our own lower-case glyphs (assets/ui/font_lowercase.txt, not ROM-derived), compiled in as a string.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/assets/ui/font_lowercase.txt)
file(READ ${CMAKE_SOURCE_DIR}/assets/ui/font_lowercase.txt SAWGOD_FONT_LOWERCASE)
file(WRITE ${CMAKE_BINARY_DIR}/generated/ui_font_lowercase.inc
     "R\"SAWGODFONT(${SAWGOD_FONT_LOWERCASE})SAWGODFONT\"\n")
target_include_directories(sawgod_ui PRIVATE ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(sawgod_ui PUBLIC sawgod_engine sawgod_imgui PRIVATE sawgod_warnings)

# First run: the ROM screen, the stored ROM copy and the .sgz archives (app/first_run.h).
add_library(sawgod_first_run STATIC src/app/first_run.cpp src/app/first_run.h src/app/rom_screen.cpp
    src/app/rom_screen.h src/app/rom_screen_text.h src/app/rom_screen_window.cpp src/app/splash.cpp src/app/splash.h)
target_include_directories(sawgod_first_run PUBLIC ${CMAKE_SOURCE_DIR}/src)
# The logos (assets/ui/, ours) for the ROM screen and the splash, and the window icon, compiled in as bytes: they show before any data
# exists.
foreach(logo "sawgod_logo.png:rom_screen_logo.inc" "sawgod_logo_on_white.png:splash_logo.inc"
        "app_icon_256.png:app_icon.inc")
    string(REPLACE ":" ";" logo "${logo}")
    list(GET logo 0 logo_png)
    list(GET logo 1 logo_inc)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/assets/ui/${logo_png})
    file(READ ${CMAKE_SOURCE_DIR}/assets/ui/${logo_png} logo_hex HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," logo_bytes "${logo_hex}")
    file(CONFIGURE OUTPUT ${CMAKE_BINARY_DIR}/generated/${logo_inc} CONTENT "${logo_bytes}\n" @ONLY)
endforeach()
target_include_directories(sawgod_first_run PRIVATE ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(sawgod_first_run PUBLIC sawgod_game sawgod_engine PRIVATE sawgod_warnings)
target_link_libraries(sawgod_first_run PRIVATE SDL3::SDL3)
if(ANDROID)
    include(cmake/AndroidFluidSynth.cmake)
    enable_language(ASM)
    target_sources(sawgod_game PRIVATE src/game/fiber_android.S)
    add_library(sawgod SHARED)
    add_dependencies(sawgod libfluidsynth)
else()
    add_executable(sawgod)
endif()
target_sources(sawgod PRIVATE src/app/audio_queue.cpp src/app/audio_output.cpp src/app/world_card_overlay.cpp src/app/title_logo_overlay.cpp src/tools/field_preview.cpp src/app/main.cpp src/app/launch_args.cpp src/app/launch_args.h src/app/game_resources.cpp src/app/game_resources.h src/app/input_movie.cpp src/app/input_movie.h src/app/save_setup.cpp src/app/save_setup.h src/app/shot_report.cpp src/app/shot_report.h src/app/startup.cpp src/app/startup.h src/app/picture_draw.cpp src/app/picture_draw.h src/app/field_motion.cpp src/app/field_motion.h src/app/shot_fixture.cpp src/app/shot_fixture.h src/app/button_prompts.cpp src/app/button_prompts.h src/app/ui_screens.h src/app/audio_review.cpp src/app/sound_mix.cpp src/app/sound_mix.h src/app/gb_music.cpp src/app/gb_music.h src/app/gb_cues.cpp src/app/gb_cues.h)
target_link_libraries(sawgod PRIVATE sawgod_first_run sawgod_enhanced_music sawgod_ui sawgod_engine sawgod_game sawgod_emu sawgod_warnings)
if(APPLE)
    set(SAWGOD_RESOURCE_DIR "$<TARGET_BUNDLE_CONTENT_DIR:sawgod>/Resources")
else()
    set(SAWGOD_RESOURCE_DIR "$<TARGET_FILE_DIR:sawgod>")
endif()
# The GB field maps' authored data (assets/gb_maps/: grids, pairings and strip names; no ROM content), next to
# the app as gb_maps/: the first run decodes the pictures from the player's ROM into gb.sgz (game/gb_extract.h).
add_custom_command(TARGET sawgod POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different ${CMAKE_SOURCE_DIR}/assets/gb_maps
        ${SAWGOD_RESOURCE_DIR}/gb_maps
    VERBATIM)
target_link_libraries(sawgod PRIVATE SDL3::SDL3)
if(APPLE)
    include(cmake/MacBundle.cmake)
endif()

if(MINGW)
    target_link_options(sawgod PRIVATE -static -static-libgcc -static-libstdc++)
endif()
# main() keeps ~2 MB of frame state on its stack. Linux and macOS give the main thread 8 MB; Windows takes the
# .exe's reserve (MinGW's default is 2 MB), which overflowed on the first-run ROM import. Wine doesn't enforce
# it, so check_first_run.py reads the header. Android sizes its SDL thread separately (SDLActivity.java).
if(MINGW)
    target_link_options(sawgod PRIVATE -Wl,--stack,8388608)
elseif(MSVC)
    target_link_options(sawgod PRIVATE /STACK:8388608)
endif()
# A windowed app: no console window beside the game (SDL_main.h supplies WinMain). Output still reaches a pipe or
# file the caller redirects to, which is how the tools and release checks read it.
set_target_properties(sawgod PROPERTIES WIN32_EXECUTABLE TRUE)
if(WIN32)
    # The .exe's icon in Explorer: the SG mark (assets/ui/sawgod.ico, tools/release/app_icons.py).
    file(CONFIGURE OUTPUT ${CMAKE_BINARY_DIR}/generated/sawgod.rc
        CONTENT "1 ICON \"${CMAKE_SOURCE_DIR}/assets/ui/sawgod.ico\"\n")
    target_sources(sawgod PRIVATE ${CMAKE_BINARY_DIR}/generated/sawgod.rc)
endif()

# The original cartridge front end has no dependency on host UI or Enhanced presentation.
if(NOT ANDROID)
    add_executable(sawgod-vanilla src/vanilla/main.cpp src/vanilla/input.cpp
        src/engine/input_defaults.cpp src/app/input_movie.cpp
        src/app/audio_output.cpp src/app/audio_queue.cpp)
    target_link_libraries(sawgod-vanilla PRIVATE sawgod_game SDL3::SDL3 sawgod_warnings)
    set_target_properties(sawgod-vanilla PROPERTIES WIN32_EXECUTABLE TRUE)
    if(MINGW)
        target_link_options(sawgod-vanilla PRIVATE -static -static-libgcc -static-libstdc++ -Wl,--stack,8388608)
    elseif(MSVC)
        target_link_options(sawgod-vanilla PRIVATE /STACK:8388608)
    endif()
    if(APPLE)
        set_target_properties(sawgod-vanilla PROPERTIES MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/cmake/VanillaInfo.plist.in")
    endif()
    include(cmake/VanillaLinkCheck.cmake)
    sawgod_check_vanilla_link(sawgod-vanilla)
endif()

# --- tests ------------------------------------------------------------------
if(SAWGOD_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# English: assets/text/en.csv, copied next to the app as text/en.csv (the app's default English).
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND AND NOT ANDROID)
  if(SAWGOD_BUILD_TESTS)
    add_test(NAME influence_fragment_budget COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_influence_budget.py)
  endif()
  set(SAWGOD_EN ${CMAKE_SOURCE_DIR}/assets/text/en.csv)
  # A development checkout may regenerate it first (cmake/developer/, absent from source releases).
  include(${CMAKE_SOURCE_DIR}/cmake/developer/english_text.cmake OPTIONAL)
  if(NOT SAWGOD_EN_FROM_SHEETS)
    if(NOT EXISTS "${SAWGOD_EN}")
      message(FATAL_ERROR "Source release needs authored assets/text/en.csv")
    endif()
    add_custom_command(TARGET sawgod POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory ${SAWGOD_RESOURCE_DIR}/text
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SAWGOD_EN} ${SAWGOD_RESOURCE_DIR}/text/en.csv
      COMMAND ${CMAKE_COMMAND} -E rm -f ${SAWGOD_RESOURCE_DIR}/text/en_fill.csv VERBATIM)
  endif()
else()
  message(STATUS "Using packaged assets/text/en.csv")
endif()

# Bake during English extraction, then embed the authored PNGs like the splash logos.
# Embedding needs only CMake, never Python, the font or Pillow.
file(GLOB SAWGOD_CARD_PNGS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/assets/ui/world_cards/*.png ${CMAKE_SOURCE_DIR}/assets/ui/world_cards/*.json)
set(SAWGOD_CARD_INC ${CMAKE_BINARY_DIR}/generated/world_card_lettering.inc)
if(TARGET english_text)
    set(SAWGOD_CARD_TEXT_DEP english_text ${SAWGOD_EN})
endif()
add_custom_command(OUTPUT ${SAWGOD_CARD_INC}
    COMMAND ${CMAKE_COMMAND} -DSOURCE_ROOT=${CMAKE_SOURCE_DIR} -DOUTPUT=${SAWGOD_CARD_INC}
        -P ${CMAKE_SOURCE_DIR}/tools/embed_world_cards.cmake
    DEPENDS ${SAWGOD_CARD_PNGS} ${CMAKE_SOURCE_DIR}/tools/embed_world_cards.cmake ${SAWGOD_CARD_TEXT_DEP}
    VERBATIM)
target_sources(sawgod_game PRIVATE ${SAWGOD_CARD_INC})
# The Enhanced title logo's kanji line (assets/ui/title_kanji.png, tools/title_logo_kanji.py), compiled in.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/assets/ui/title_kanji.png)
file(READ ${CMAKE_SOURCE_DIR}/assets/ui/title_kanji.png title_kanji_hex HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," title_kanji_bytes "${title_kanji_hex}")
file(CONFIGURE OUTPUT ${CMAKE_BINARY_DIR}/generated/title_kanji_png.inc CONTENT "${title_kanji_bytes}\n" @ONLY)
target_include_directories(sawgod_game PRIVATE ${CMAKE_BINARY_DIR}/generated)
