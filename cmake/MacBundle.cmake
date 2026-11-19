# Resources follow SDL_GetBasePath(): Contents/Resources, never the launch cwd.
set_target_properties(sawgod PROPERTIES
    MACOSX_BUNDLE TRUE
    OUTPUT_NAME SawGod
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/cmake/Info.plist.in"
    BUILD_RPATH "@executable_path/../Frameworks"
    INSTALL_RPATH "@executable_path/../Frameworks")
# Reserve room for package_macos.py to relocate every non-system dylib import.
target_link_options(sawgod PRIVATE "-Wl,-headerpad_max_install_names")
find_program(SAWGOD_SIPS sips REQUIRED)
find_program(SAWGOD_ICONUTIL iconutil REQUIRED)
set(SAWGOD_ICON "${CMAKE_BINARY_DIR}/generated/SawGod.icns")
add_custom_command(OUTPUT "${SAWGOD_ICON}"
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tools/release/macos_icon.py"
        "${CMAKE_SOURCE_DIR}/assets/ui/sawgod_sg_mark.png" "${SAWGOD_ICON}"
        "${SAWGOD_SIPS}" "${SAWGOD_ICONUTIL}"
    DEPENDS "${CMAKE_SOURCE_DIR}/assets/ui/sawgod_sg_mark.png"
        "${CMAKE_SOURCE_DIR}/tools/release/macos_icon.py" VERBATIM)
target_sources(sawgod PRIVATE "${SAWGOD_ICON}")
set_source_files_properties("${SAWGOD_ICON}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
add_custom_command(TARGET sawgod POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different "${CMAKE_SOURCE_DIR}/licenses"
        "${SAWGOD_RESOURCE_DIR}/licenses"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/LICENSE"
        "${CMAKE_SOURCE_DIR}/LICENSES.md" "${SAWGOD_RESOURCE_DIR}"
    VERBATIM)
# The soundfont is optional for developer builds; release packaging requires and verifies it.
if(EXISTS "${SAWGOD_DEV_SOUNDFONT}" AND NOT SAWGOD_DEV_SOUNDFONT STREQUAL "")
    add_custom_command(TARGET sawgod POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SAWGOD_RESOURCE_DIR}/soundfonts"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SAWGOD_DEV_SOUNDFONT}"
            "${SAWGOD_RESOURCE_DIR}/soundfonts/GeneralUser-GS.sf2" VERBATIM)
endif()
