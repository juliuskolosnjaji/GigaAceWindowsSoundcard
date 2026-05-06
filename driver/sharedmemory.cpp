#include "sharedmemory.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, GigaACEInitializeSharedMemory)
#pragma alloc_text(PAGE, GigaACEOpenSharedMemory)
#pragma alloc_text(PAGE, GigaACECloseSharedMemory)
#endif

NTSTATUS
GigaACEInitializeSharedMemory(
    _Out_ GigaACESharedMemory* Shm
)
{
    PAGED_CODE();

    if (!Shm)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(Shm, sizeof(GigaACESharedMemory));

    NTSTATUS status = GigaACEOpenSharedMemory(Shm);
    return status;
}

NTSTATUS
GigaACEOpenSharedMemory(
    _Out_ GigaACESharedMemory* Shm
)
{
    PAGED_CODE();

    if (!Shm)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(Shm, sizeof(GigaCESharedMemory));

    UNICODE_STRING sectionName;
    RtlInitUnicodeString(&sectionName, GIGAACE_SHM_NAME);

    OBJECT_ATTRIBUTES objAttrs;
    InitializeObjectAttributes(&objAttrs,
        &sectionName,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
        NULL,
        NULL);

    NTSTATUS status = ZwOpenSection(
        &Shm->SectionHandle,
        SECTION_MAP_READ | SECTION_MAP_WRITE,
        &objAttrs
    );

    if (!NT_SUCCESS(status))
    {
        TraceLoggingWrite(g_GigaAceVSCProvider, "OpenSharedMemory_Failed",
            TraceLoggingNTStatus(status, "Status"),
            TraceLoggingWideString(GIGAACE_SHM_NAME, "SectionName"));
        return status;
    }

    SECTION_BASIC_INFORMATION sectionInfo;
    status = ZwQuerySection(
        Shm->SectionHandle,
        SectionBasicInformation,
        &sectionInfo,
        sizeof(sectionInfo),
        NULL
    );

    if (!NT_SUCCESS(status))
    {
        ZwClose(Shm->SectionHandle);
        Shm->SectionHandle = NULL;
        return status;
    }

    Shm->MapSize = (SIZE_T)sectionInfo.AllocationSize.QuadPart;

    SIZE_T viewSize = 0;
    status = ZwMapViewOfSection(
        Shm->SectionHandle,
        ZwCurrentProcess(),
        &Shm->Mapping,
        0,
        0,
        NULL,
        &viewSize,
        ViewShare,
        0,
        PAGE_READWRITE
    );

    if (!NT_SUCCESS(status))
    {
        ZwClose(Shm->SectionHandle);
        Shm->SectionHandle = NULL;
        return status;
    }

    GigaCESharedMemoryLayout* layout = (GigaCESharedMemoryLayout*)Shm->Mapping;

    if (layout->Magic != GIGAACE_SHM_RING_MAGIC)
    {
        ZwUnmapViewOfSection(ZwCurrentProcess(), Shm->Mapping);
        ZwClose(Shm->SectionHandle);
        Shm->Mapping = NULL;
        Shm->SectionHandle = NULL;
        TraceLoggingWrite(g_GigaAceVSCProvider, "InvalidSharedMemoryMagic",
            TraceLoggingUInt32(layout->Magic, "Magic"));
        return STATUS_INVALID_PARAMETER;
    }

    Shm->Layout = layout;
    Shm->Samples = (float*)((PUCHAR)Shm->Mapping + layout->DataOffset);
    Shm->ChannelCount = layout->ChannelCount;
    Shm->CapacityFrames = layout->CapacityFrames;
    Shm->SampleRate = layout->SampleRate;
    Shm->ReadIndex = 0;
    Shm->IsInitialized = TRUE;

    TraceLoggingWrite(g_GigaAceVSCProvider, "SharedMemory_Opened",
        TraceLoggingUInt32(layout->ChannelCount, "Channels"),
        TraceLoggingDouble(layout->SampleRate, "SampleRate"),
        TraceLoggingUInt32(layout->CapacityFrames, "CapacityFrames"));

    return STATUS_SUCCESS;
}

void
GigaACECloseSharedMemory(
    _Inout_ GigaACESharedMemory* Shm
)
{
    PAGED_CODE();

    if (!Shm)
        return;

    if (Shm->Mapping)
    {
        ZwUnmapViewOfSection(ZwCurrentProcess(), Shm->Mapping);
        Shm->Mapping = NULL;
    }

    if (Shm->SectionHandle)
    {
        ZwClose(Shm->SectionHandle);
        Shm->SectionHandle = NULL;
    }

    RtlZeroMemory(Shm, sizeof(GigaACESharedMemory));
}

NTSTATUS
GigaACEReadSamples(
    _In_ GigaACESharedMemory* Shm,
    _Out_ float* Output,
    _In_ ULONG FrameCount,
    _In_ ULONG ChannelCount
)
{
    if (!Shm || !Shm->IsInitialized || !Shm->Layout || !Output)
        return STATUS_INVALID_PARAMETER;

    ULONG capacity = Shm->CapacityFrames;
    ULONG channels = Shm->Layout->ChannelCount;
    ULONGLONG writeIndex = Shm->Layout->WriteIndex;
    ULONGLONG readIndex = Shm->ReadIndex;

    if (writeIndex > readIndex + capacity)
    {
        readIndex = writeIndex - capacity;
        Shm->Layout->OverrunCount++;
    }

    ULONGLONG available = (writeIndex > readIndex) ?
        (writeIndex - readIndex) : 0;

    ULONG framesToCopy = (ULONG)(available < FrameCount ? available : FrameCount);

    for (ULONG frame = 0; frame < framesToCopy; ++frame)
    {
        ULONG slot = (ULONG)((readIndex + frame) % capacity);
        RtlCopyMemory(
            &Output[frame * ChannelCount],
            &Shm->Samples[slot * channels],
            ChannelCount * sizeof(float)
        );
    }

    if (framesToCopy < FrameCount)
    {
        SIZE_T remaining = (SIZE_T)(FrameCount - framesToCopy) * ChannelCount;
        RtlZeroMemory(
            &Output[framesToCopy * ChannelCount],
            remaining * sizeof(float)
        );
    }

    Shm->ReadIndex = readIndex + framesToCopy;

    return STATUS_SUCCESS;
}

BOOLEAN
GigaACEIsStreamRunning(
    _In_ GigaACESharedMemory* Shm
)
{
    if (!Shm || !Shm->Layout)
        return FALSE;

    return (Shm->Layout->StreamState == GIGAACE_SHM_STATE_RUNNING);
}

ULONG
GigaACEGetChannelCount(
    _In_ GigaACESharedMemory* Shm
)
{
    if (!Shm || !Shm->Layout)
        return 0;

    return Shm->Layout->ChannelCount;
}

double
GigaACEGetSampleRate(
    _In_ GigaACESharedMemory* Shm
)
{
    if (!Shm || !Shm->Layout)
        return 48000.0;

    return Shm->Layout->SampleRate;
}
