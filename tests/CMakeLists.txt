include(FetchContent)
FetchContent_Declare(
    doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.5.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(doctest)

add_executable(sawgod_tests test_decomp.cpp test_sound_driver.cpp test_fast_forward_vfx.cpp test_fidelity.cpp test_field_preview.cpp ${CMAKE_SOURCE_DIR}/src/tools/field_preview.cpp test_version.cpp test_battle_ui.cpp test_inventory_ui.cpp test_cheats.cpp test_v30mz.cpp test_assets.cpp test_config.cpp test_hooks.cpp test_map_lz.cpp test_maps.cpp test_mod_pack.cpp test_native_frames.cpp test_persist.cpp test_resources.cpp test_save_file.cpp test_save_slots.cpp test_tables.cpp test_text.cpp)
target_link_libraries(sawgod_tests PRIVATE sawgod_first_run sawgod_enhanced_music sawgod_engine sawgod_ui sawgod_game sawgod_emu doctest::doctest_with_main sawgod_warnings miniz)
target_sources(sawgod_tests PRIVATE test_audio_queue.cpp ${CMAKE_SOURCE_DIR}/src/app/audio_queue.cpp ${CMAKE_SOURCE_DIR}/src/app/audio_output.cpp)
target_sources(sawgod_tests PRIVATE test_audio_review.cpp ${CMAKE_SOURCE_DIR}/src/app/audio_review.cpp)
target_link_libraries(sawgod_tests PRIVATE SDL3::SDL3)
target_sources(sawgod_tests PRIVATE test_falloff_field.cpp test_graphics_transitions.cpp test_debug_log.cpp)
target_sources(sawgod_tests PRIVATE test_terrain_veil.cpp test_host_picture.cpp test_audio_switch.cpp test_enhanced_music.cpp test_mode7.cpp test_presentation.cpp test_party_ui.cpp test_item_dialog_ui.cpp test_creation_ui.cpp test_bestiary_ui.cpp test_save_ui.cpp test_service_ui.cpp test_dialogue_ui.cpp test_ui_detail.cpp test_gb_core.cpp test_gb_music.cpp test_gb_sfx.cpp ${CMAKE_SOURCE_DIR}/src/app/gb_music.cpp ${CMAKE_SOURCE_DIR}/src/app/gb_cues.cpp ${CMAKE_SOURCE_DIR}/src/app/sound_mix.cpp test_player_names.cpp test_ui_chrome.cpp test_text_fit.cpp test_text_wrap.cpp test_first_run.cpp)
target_compile_definitions(sawgod_tests PRIVATE SAWGOD_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
target_sources(sawgod_tests PRIVATE test_field_npcs.cpp test_battle_rewards.cpp)
target_sources(sawgod_tests PRIVATE test_pixel_upload.cpp)
target_sources(sawgod_tests PRIVATE test_battle_lighting.cpp test_music_fade.cpp test_menu_fill.cpp test_hd_menus.cpp)


# Optional developer targets must also build when cross-compiling without a test emulator.
file(GLOB developer_test_targets CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/developer/*_targets.cmake")
foreach(targets IN LISTS developer_test_targets)
    include("${targets}")
endforeach()


# Cross-compiling for Windows (e.g. the windows-mingw preset): run test binaries under Wine
# so test discovery and CTest execution both work on the Linux build host.
if(CMAKE_CROSSCOMPILING AND WIN32)
    find_program(SAWGOD_WINE_EXECUTABLE wine)
    if(SAWGOD_WINE_EXECUTABLE)
        set_target_properties(sawgod_tests PROPERTIES CROSSCOMPILING_EMULATOR "${SAWGOD_WINE_EXECUTABLE}")
    endif()
endif()


# Without an emulator a cross-compiled test binary can't run, so there is nothing to register
# (the CI mingw job only checks that everything builds).
if(CMAKE_CROSSCOMPILING AND NOT CMAKE_CROSSCOMPILING_EMULATOR AND NOT SAWGOD_WINE_EXECUTABLE)
    return()
endif()

include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(sawgod_tests)
# Optional SAWGOD_WSC_ROM fixture; bounded single-process replay of the chapter checkpoints.
add_test(NAME hoststate_save_sweep COMMAND ${SAWGOD_WINE_EXECUTABLE} $<TARGET_FILE:sawgod_tests>
    --no-skip=true "--test-case=saves made across the story*")
set_tests_properties(hoststate_save_sweep PROPERTIES TIMEOUT 300 LABELS "hoststate;rom"
    SKIP_REGULAR_EXPRESSION "Set SAWGOD_WSC_ROM for the save/load sweep")

# Headless smoke test: the app should run --frames N and exit 0 with no display.
add_test(NAME sawgod_smoke COMMAND ${SAWGOD_WINE_EXECUTABLE} $<TARGET_FILE:sawgod> --scale 1 --frames 3)
set_tests_properties(sawgod_smoke PROPERTIES
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy"
)



target_sources(sawgod_tests PRIVATE test_enhanced_followups.cpp)

# Native sequence comparisons are small, bounded core tests; the catalog drives the same authored code.
target_sources(sawgod_tests PRIVATE test_battle_animations.cpp)

# Native hit-boundary comparisons remain in the unit suite.

target_sources(sawgod_tests PRIVATE test_display_motion.cpp test_picture_motion.cpp test_minimap.cpp
    ${CMAKE_SOURCE_DIR}/src/app/picture_draw.cpp)


target_sources(sawgod_tests PRIVATE test_gb_effects.cpp)

target_sources(sawgod_tests PRIVATE test_gb_intro.cpp ${CMAKE_SOURCE_DIR}/src/app/world_card_overlay.cpp)

# Bionic has no ucontext. Test its cooperative assembly backend on the host's matching 64-bit ABI.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|aarch64)$")
    enable_language(ASM)
    add_executable(sawgod-android-fiber-test test_android_fiber.cpp
        ${CMAKE_SOURCE_DIR}/src/game/fiber.cpp ${CMAKE_SOURCE_DIR}/src/game/fiber_android.S)
    target_include_directories(sawgod-android-fiber-test PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_compile_definitions(sawgod-android-fiber-test PRIVATE SAWGOD_TEST_ANDROID_FIBER)
    target_link_libraries(sawgod-android-fiber-test PRIVATE doctest::doctest_with_main sawgod_warnings)
    add_test(NAME android_fiber COMMAND sawgod-android-fiber-test)
endif()

add_test(NAME name_scan COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_name_scan.py)

# Developer suites can register additional targets/tests without exporting their inputs.
# A present include must register its complete suite; missing inputs are test failures.
file(GLOB developer_test_suites CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/developer/*_tests.cmake")
foreach(suite IN LISTS developer_test_suites)
    include("${suite}")
endforeach()

# Report-only timeline diagnostics and the ROM fact checks.
target_sources(sawgod_tests PRIVATE test_frame_audit.cpp)
target_sources(sawgod_tests PRIVATE test_enhanced_facts.cpp)

# Original-cartridge setup, exercised through the shipped binary's replay path.
add_test(NAME decomp_recording_replay COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_SOURCE_DIR}/tests/decomp_replay.py $<TARGET_FILE:sawgod-decomp>
    $<TARGET_FILE:sawgod-headless>)
set_tests_properties(decomp_recording_replay PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 300 LABELS "rom;decomp")
add_test(NAME decomp_link COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/decomp_link.py
    ${CMAKE_BINARY_DIR} $<TARGET_FILE:sawgod-decomp>)
