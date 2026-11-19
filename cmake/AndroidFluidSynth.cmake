# Build the same unmodified FluidSynth/GCEM as the desktop package, with C++11 OS services.
# Only sample rendering is used; SDL owns the audio device. Sources are staged outside ref/.
set(SAWGOD_FLUIDSYNTH_SOURCE "${CMAKE_SOURCE_DIR}/build/deps/fluidsynth-2.6.1" CACHE PATH "FluidSynth 2.6.1 sources")
set(GCEM_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/build/deps/gcem-012ae73c6d0a2cb09ffe86475f5c6fba3926e200/include" CACHE PATH "GCEM headers")
if(NOT EXISTS "${SAWGOD_FLUIDSYNTH_SOURCE}/CMakeLists.txt" OR NOT EXISTS "${GCEM_INCLUDE_DIR}/gcem.hpp")
    message(FATAL_ERROR "Run tools/android_prepare.py first (see docs/ANDROID.md)")
endif()
set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
set(osal cpp11 CACHE STRING "" FORCE)
foreach(feature alsa aufile dbus jack ladspa libsndfile midishare network oss dsound wasapi waveout
        winmidi sdl3 pulseaudio pipewire readline openmp systemd native-dls signalsmith portaudio opensles oboe)
    set(enable-${feature} OFF CACHE BOOL "" FORCE)
endforeach()
add_subdirectory("${SAWGOD_FLUIDSYNTH_SOURCE}" "${CMAKE_BINARY_DIR}/fluidsynth" EXCLUDE_FROM_ALL)
# The upstream int32 audio clipper rounds its bound to float intentionally; Clang diagnoses that
# constant conversion (GCC's desktop build does not). Keep the unmodified dependency sources.
target_compile_options(libfluidsynth-OBJ PRIVATE -Wno-implicit-const-int-float-conversion)
# Android's loader and APK native library packaging use an unversioned .so name.
set_target_properties(libfluidsynth PROPERTIES VERSION "" SOVERSION "")
