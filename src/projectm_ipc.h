#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define ABM_PROJECTM_PCM_COUNT 512

/* Shared, versioned IPC contract between the playback process and renderer. */
struct ABMProjectMSharedState {
    DWORD magic;
    DWORD version;
    volatile LONG ready;
    volatile LONG sequence;
    volatile LONG presetDelta;
    volatile LONG autoSwitch;
    volatile LONG_PTR childWindow;
    float pcm[ABM_PROJECTM_PCM_COUNT];
};
#define ABM_PROJECTM_IPC_MAGIC 0x41424D50u
#define ABM_PROJECTM_IPC_VERSION 1u
