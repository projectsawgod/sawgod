// Package smoke check: load the shipped dynamic synth and soundfont, then render a non-silent note.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#define OPEN(path) LoadLibraryA(path)
#define SYMBOL(lib, name) GetProcAddress(lib, name)
#else
#include <dlfcn.h>
#define OPEN(path) dlopen(path, RTLD_NOW | RTLD_LOCAL)
#define SYMBOL(lib, name) dlsym(lib, name)
#endif

int main(int argc, char **argv) {
    if (argc != 3)
        return 2;
#ifdef _WIN32
    HMODULE lib = OPEN(argv[1]);
#else
    void *lib = OPEN(argv[1]);
#endif
    if (!lib) {
        fprintf(stderr, "Cannot load bundled FluidSynth\n");
        return 1;
    }
#define API(ret, name, args)                                                                                 \
    ret(*name) args = (ret(*) args)SYMBOL(lib, #name);                                                       \
    if (!name)                                                                                               \
    return 1
    API(void *, new_fluid_settings, (void));
    API(int, fluid_settings_setnum, (void *, const char *, double));
    API(int, fluid_settings_setint, (void *, const char *, int));
    API(void *, new_fluid_synth, (void *));
    API(int, fluid_synth_sfload, (void *, const char *, int));
    API(int, fluid_synth_noteon, (void *, int, int, int));
    API(int, fluid_synth_write_float, (void *, int, void *, int, int, void *, int, int));
    API(void, delete_fluid_synth, (void *));
    API(void, delete_fluid_settings, (void *));
    void *settings = new_fluid_settings();
    if (!settings)
        return 1;
    fluid_settings_setnum(settings, "synth.sample-rate", 24000);
    fluid_settings_setint(settings, "synth.cpu-cores", 1);
    void *synth = new_fluid_synth(settings);
    if (!synth || fluid_synth_sfload(synth, argv[2], 1) < 0 || fluid_synth_noteon(synth, 0, 60, 100) < 0)
        return 1;
    float left[2400], right[2400], peak = 0;
    if (fluid_synth_write_float(synth, 2400, left, 0, 1, right, 0, 1) < 0)
        return 1;
    for (int i = 0; i < 2400; ++i) {
        if (!isfinite(left[i]) || !isfinite(right[i]))
            return 1;
        if (fabsf(left[i]) > peak)
            peak = fabsf(left[i]);
    }
    delete_fluid_synth(synth);
    delete_fluid_settings(settings);
    printf("Bundled FluidSynth + GeneralUser GS: peak %.6f\n", peak);
    return peak > 0.00001f ? 0 : 1;
}
