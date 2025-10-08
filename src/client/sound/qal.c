/*
Copyright (C) 2022 Andrey Nazarov

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "shared/shared.h"
#include "system/system.h"
#include "common/cvar.h"
#include "common/common.h"
#include "common/files.h"

#include <AL/alc.h>

#define QALAPI
#include "qal.h"

#ifndef ALC_SOFT_output_mode
#define ALC_OUTPUT_MODE_SOFT    0x19AC
#define ALC_STEREO_BASIC_SOFT   0x19AE
#endif

static LPALCCLOSEDEVICE qalcCloseDevice;
static LPALCCREATECONTEXT qalcCreateContext;
static LPALCGETINTEGERV qalcGetIntegerv;
static LPALCGETSTRING qalcGetString;
static LPALCDESTROYCONTEXT qalcDestroyContext;
static LPALCISEXTENSIONPRESENT qalcIsExtensionPresent;
static LPALCMAKECONTEXTCURRENT qalcMakeContextCurrent;
static LPALCOPENDEVICE qalcOpenDevice;

typedef struct {
    const char *name;
    void *dest;
    void *builtin;
} alfunction_t;

typedef struct {
    const char *extension;
    const alfunction_t *functions;
} alsection_t;

#define QALC_FN(x)  { "alc"#x, &qalc##x, alc##x }
#define QAL_FN(x)   { "al"#x, &qal##x, al##x }

static const alsection_t sections[] = {
    {
        .functions = (const alfunction_t []) {
            QALC_FN(CloseDevice),
            QALC_FN(CreateContext),
            QALC_FN(DestroyContext),
            QALC_FN(GetIntegerv),
            QALC_FN(GetString),
            QALC_FN(IsExtensionPresent),
            QALC_FN(MakeContextCurrent),
            QALC_FN(OpenDevice),
            QAL_FN(BufferData),
            QAL_FN(Bufferiv),
            QAL_FN(DeleteBuffers),
            QAL_FN(DeleteSources),
            QAL_FN(Disable),
            QAL_FN(DistanceModel),
            QAL_FN(Enable),
            QAL_FN(GenBuffers),
            QAL_FN(GenSources),
            QAL_FN(GetEnumValue),
            QAL_FN(GetError),
            QAL_FN(GetProcAddress),
            QAL_FN(GetSourcef),
            QAL_FN(GetSourcei),
            QAL_FN(GetString),
            QAL_FN(IsExtensionPresent),
            QAL_FN(Listener3f),
            QAL_FN(Listenerf),
            QAL_FN(Listenerfv),
            QAL_FN(Source3f),
            QAL_FN(SourcePause),
            QAL_FN(SourcePlay),
            QAL_FN(SourceQueueBuffers),
            QAL_FN(SourceStop),
            QAL_FN(SourceUnqueueBuffers),
            QAL_FN(Sourcef),
            QAL_FN(Sourcei),
            QAL_FN(Source3i),
            { NULL }
        }
    },
    {
        .extension = "ALC_EXT_EFX",
        .functions = (const alfunction_t []) {
            QAL_FN(DeleteFilters),
            QAL_FN(Filterf),
            QAL_FN(Filteri),
            QAL_FN(GenFilters),
            QAL_FN(GenEffects),
            QAL_FN(DeleteEffects),
            QAL_FN(Effecti),
            QAL_FN(Effectiv),
            QAL_FN(Effectf),
            QAL_FN(Effectfv),
            QAL_FN(GenAuxiliaryEffectSlots),
            QAL_FN(DeleteAuxiliaryEffectSlots),
            QAL_FN(AuxiliaryEffectSloti),
            { NULL }
        }
    },
};

static cvar_t   *al_device;
static cvar_t   *al_hrtf;

static void *handle;
static ALCdevice *device;
static ALCcontext *context;

void QAL_Shutdown(void)
{
    if (context) {
        qalcMakeContextCurrent(NULL);
        qalcDestroyContext(context);
        context = NULL;
    }
    if (device) {
        qalcCloseDevice(device);
        device = NULL;
    }

    for (int i = 0; i < q_countof(sections); i++) {
        const alsection_t *sec = &sections[i];
        const alfunction_t *func;

        for (func = sec->functions; func->name; func++)
            *(void **)func->dest = NULL;
    }

    if (handle) {
        Sys_FreeLibrary(handle);
        handle = NULL;
    }

    if (al_device)
        al_device->flags &= ~CVAR_SOUND;
    if (al_hrtf)
        al_hrtf->flags &= ~CVAR_SOUND;
}

static const char *const al_drivers[] = {
#ifdef _WIN32
    "soft_oal", "openal32"
#elif (defined __APPLE__)
    "libopenal.1.dylib", "libopenal.dylib"
#else
    "libopenal.so.1", "libopenal.so"
#endif
};

static const char *get_device_list(void)
{
    if (qalcIsExtensionPresent(NULL, "ALC_ENUMERATE_ALL_EXT"))
        return qalcGetString(NULL, ALC_ALL_DEVICES_SPECIFIER);

    return qalcGetString(NULL, ALC_DEVICE_SPECIFIER);
}

static void print_device_list(void)
{
    const char *list = get_device_list();
    if (!list || !*list) {
        Com_Printf("No devices available\n");
        return;
    }

    Com_Printf("Available devices:\n");
    do {
        Com_Printf("%s\n", list);
        list += strlen(list) + 1;
    } while (*list);
}

int QAL_Init(void)
{
    const alsection_t *sec;
    const alfunction_t *func;
    ALCint major, minor;
    int i;

    al_device = Cvar_Get("al_device", "", 0);
    al_hrtf = Cvar_Get("al_hrtf", "0", 0);

    if (!*al_device->string) {
        for (i = 0, sec = sections; i < q_countof(sections); i++, sec++)
            for (func = sec->functions; func->name; func++)
                *(void **)func->dest = func->builtin;
    } else {
        for (i = 0; i < q_countof(al_drivers); i++) {
            Com_DPrintf("Trying %s\n", al_drivers[i]);
            Sys_LoadLibrary(al_drivers[i], NULL, &handle);
            if (handle)
                break;
        }
        if (!handle)
            return -1;

        for (i = 0, sec = sections; i < q_countof(sections); i++, sec++) {
            if (sec->extension)
                continue;

            for (func = sec->functions; func->name; func++) {
                void *addr = Sys_GetProcAddress(handle, func->name);
                if (!addr)
                    goto fail;
                *(void **)func->dest = addr;
            }
        }
    }

    major = minor = 0;
    qalcGetIntegerv(NULL, ALC_MAJOR_VERSION, 1, &major);
    qalcGetIntegerv(NULL, ALC_MINOR_VERSION, 1, &minor);
    if (major < 1 || minor < 0 || (major == 1 && minor == 0)) {
        Com_SetLastError("At least OpenAL 1.1 required");
        goto fail;
    }

    if (!strcmp(al_device->string, "?")) {
        print_device_list();
        Cvar_Reset(al_device);
    }

    device = qalcOpenDevice(al_device->string[0] ? al_device->string : NULL);
    if (!device) {
        Com_SetLastError(va("alcOpenDevice(%s) failed", al_device->string));
        goto fail;
    }

    Com_DDPrintf("ALC_EXTENSIONS: %s\n", qalcGetString(device, ALC_EXTENSIONS));

    if (al_hrtf->integer != 1 && qalcIsExtensionPresent(device, "ALC_SOFT_HRTF")) {
        ALCint attrs[] = {
            ALC_HRTF_SOFT, al_hrtf->integer > 1,
            0
        };
        context = qalcCreateContext(device, attrs);
    } else {
        context = qalcCreateContext(device, NULL);
    }
    if (!context) {
        Com_SetLastError("alcCreateContext failed");
        goto fail;
    }

    if (!qalcMakeContextCurrent(context)) {
        Com_SetLastError("alcMakeContextCurrent failed");
        goto fail;
    }
    
    if (*al_device->string) {
        for (i = 0, sec = sections; i < q_countof(sections); i++, sec++) {
            if (!sec->extension)
                continue;
            if (!qalcIsExtensionPresent(device, sec->extension))
                continue;

            for (func = sec->functions; func->name; func++) {
                void *addr = qalGetProcAddress(func->name);
                if (!addr)
                    break;
                *(void **)func->dest = addr;
            }

            if (func->name) {
                for (func = sec->functions; func->name; func++)
                    *(void **)func->dest = NULL;

                Com_EPrintf("Couldn't load extension %s\n", sec->extension);
                continue;
            }

            Com_DPrintf("Loaded extension %s\n", sec->extension);
        }
    }

    al_device->flags |= CVAR_SOUND;
    if (qalcIsExtensionPresent(device, "ALC_SOFT_HRTF"))
        al_hrtf->flags |= CVAR_SOUND;

    if (qalcIsExtensionPresent(device, "ALC_SOFT_output_mode")) {
        ALCint mode = 0;
        qalcGetIntegerv(device, ALC_OUTPUT_MODE_SOFT, 1, &mode);
        Com_DDPrintf("ALC_OUTPUT_MODE_SOFT: %#x\n", mode);
        if (mode != ALC_STEREO_BASIC_SOFT)
            return 1;
    }

    return 0;

fail:
    QAL_Shutdown();
    return -1;
}

int QAL_GetSampleRate(void)
{
    ALCint freq = 0;
    qalcGetIntegerv(device, ALC_FREQUENCY, 1, &freq);

    // sanity check
    if (freq < 11025 || freq > 48000)
        return 0;

    return freq;
}
