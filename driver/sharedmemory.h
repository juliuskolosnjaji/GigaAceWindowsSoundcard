#ifndef GIGAACE_VSC_SHAREDMEMORY_H
#define GIGAACE_VSC_SHAREDMEMORY_H

#include "driver.h"

EXTERN_C_START

#define GIGAACE_SHM_RING_MAGIC 0x47414345u
#define GIGAACE_SHM_NAME L"Global\\GigaACEVirtualDevice"
#define GIGAACE_SHM_NAME_A "Global\\GigaACEVirtualDevice"

#define GIGAACE_SHM_STATE_STOPPED 0
#define GIGAACE_SHM_STATE_RUNNING 1

#define GIGAACE_MAX_CHANNELS 128

typedef struct _GigaACESharedMemoryLayout {
    ULONG Magic;
    ULONG Version;
    ULONG ChannelCount;
    ULONG CapacityFrames;
    ULONG BytesPerSample;
    ULONG Reserved0;
    double SampleRate;
    volatile ULONGLONG WriteIndex;
    volatile ULONGLONG OverrunCount;
    volatile ULONG StreamState;
    volatile ULONG Generation;
    ULONG DataOffset;
    ULONG Reserved1;
} GigaCESharedMemoryLayout;

typedef struct _GigaACESharedMemory {
    HANDLE SectionHandle;
    PVOID Mapping;
    SIZE_T MapSize;
    GigaCESharedMemoryLayout* Layout;
    float* Samples;
    BOOLEAN IsInitialized;
    ULONG ChannelCount;
    ULONG CapacityFrames;
    double SampleRate;
    volatile ULONGLONG ReadIndex;
} GigaACESharedMemory;

NTSTATUS
GigaACEInitializeSharedMemory(
    _Out_ GigaACESharedMemory* Shm
);

NTSTATUS
GigaACEOpenSharedMemory(
    _Out_ GigaACESharedMemory* Shm
);

void
GigaACECloseSharedMemory(
    _Inout_ GigaACESharedMemory* Shm
);

NTSTATUS
GigaACEReadSamples(
    _In_ GigaACESharedMemory* Shm,
    _Out_ float* Output,
    _In_ ULONG FrameCount,
    _In_ ULONG ChannelCount
);

BOOLEAN
GigaACEIsStreamRunning(
    _In_ GigaACESharedMemory* Shm
);

ULONG
GigaACEGetChannelCount(
    _In_ GigaACESharedMemory* Shm
);

double
GigaACEGetSampleRate(
    _In_ GigaACESharedMemory* Shm
);

EXTERN_C_END

#endif
