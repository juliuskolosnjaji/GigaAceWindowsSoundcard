#include "miniportwavertstream.h"
#include "miniportwavert.h"
#include "sharedmemory.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, MiniportWaveRTStream_Create)
#pragma alloc_text(PAGE, MiniportWaveRTStream_SetFormat)
#pragma alloc_text(PAGE, MiniportWaveRTStream_SetState)
#endif

MiniportWaveRTStream::MiniportWaveRTStream()
    : m_RefCount(1),
      m_Miniport(nullptr),
      m_PortStream(nullptr),
      m_Pin(0),
      m_Capture(FALSE),
      m_ChannelCount(2),
      m_SampleRate(48000),
      m_BitsPerSample(32),
      m_BufferFrames(0),
      m_BufferSize(0),
      m_Buffer(nullptr),
      m_Position(0),
      m_IsRunning(FALSE)
{
    RtlZeroMemory(&m_Format, sizeof(m_Format));
    KeQueryPerformanceCounter(&m_PerformanceFrequency);
    m_StartTime.QuadPart = 0;
}

MiniportWaveRTStream::~MiniportWaveRTStream()
{
    if (m_Buffer)
    {
        ExFreePoolWithTag(m_Buffer, 'BufG');
        m_Buffer = nullptr;
    }
}

NTSTATUS
MiniportWaveRTStream::Create(
    _Out_ MiniportWaveRTStream** Stream,
    _In_ MiniportWaveRT* Miniport,
    _In_ PPORTWAVERTSTREAM PortStream,
    _In_ ULONG Pin,
    _In_ BOOLEAN Capture,
    _In_ PKSDATAFORMAT DataFormat
)
{
    PAGED_CODE();

    if (!Stream || !Miniport || !DataFormat)
        return STATUS_INVALID_PARAMETER;

    MiniportWaveRTStream* obj = new (NonPagedPoolNx, 'StiG') MiniportWaveRTStream();
    if (!obj)
        return STATUS_INSUFFICIENT_RESOURCES;

    obj->m_Miniport = Miniport;
    obj->m_PortStream = PortStream;
    obj->m_Pin = Pin;
    obj->m_Capture = Capture;

    if (DataFormat->FormatSize >= sizeof(KSDATAFORMAT_WAVEFORMATEX))
    {
        PKSDATAFORMAT_WAVEFORMATEX waveFormat = (PKSDATAFORMAT_WAVEFORMATEX)DataFormat;
        obj->m_ChannelCount = waveFormat->WaveFormatEx.nChannels;
        obj->m_SampleRate = waveFormat->WaveFormatEx.nSamplesPerSec;
        obj->m_BitsPerSample = waveFormat->WaveFormatEx.wBitsPerSample;

        if (DataFormat->FormatSize >= sizeof(KSDATAFORMAT_WAVEFORMATEXTENSIBLE))
        {
            PKSDATAFORMAT_WAVEFORMATEXTENSIBLE extFormat = (PKSDATAFORMAT_WAVEFORMATEXTENSIBLE)DataFormat;
            RtlCopyMemory(&obj->m_Format, extFormat, sizeof(KSDATAFORMAT_WAVEFORMATEXTENSIBLE));
        }
    }
    else
    {
        obj->m_ChannelCount = 2;
        obj->m_SampleRate = 48000;
        obj->m_BitsPerSample = 32;
    }

    *Stream = obj;
    return STATUS_SUCCESS;
}

STDMETHODIMP_(ULONG)
MiniportWaveRTStream::NonDelegatingAddRef()
{
    return InterlockedIncrement(&m_RefCount);
}

STDMETHODIMP_(ULONG)
MiniportWaveRTStream::NonDelegatingRelease()
{
    ULONG count = InterlockedDecrement(&m_RefCount);
    if (count == 0)
    {
        this->~MiniportWaveRTStream();
        ExFreePoolWithTag(this, 'StiG');
    }
    return count;
}

STDMETHODIMP
MiniportWaveRTStream::NonDelegatingQueryInterface(
    _In_ REFIID Interface,
    _Out_ PVOID* Object
)
{
    PAGED_CODE();

    if (!Object)
        return STATUS_INVALID_PARAMETER;

    if (IsEqualGUIDAligned(Interface, IID_IUnknown))
    {
        *Object = PVOID(PUNKNOWN(PMINIPORTWAVERTSTREAM(this)));
        (*(PUNKNOWN*)Object)->AddRef();
        return STATUS_SUCCESS;
    }
    else if (IsEqualGUIDAligned(Interface, IID_IMiniportWaveRTStream))
    {
        *Object = PVOID(PMINIPORTWAVERTSTREAM(this));
        AddRef();
        return STATUS_SUCCESS;
    }

    *Object = nullptr;
    return STATUS_INVALID_PARAMETER;
}

STDMETHODIMP_(NTSTATUS)
MiniportWaveRTStream::SetFormat(
    _In_ PKSDATAFORMAT DataFormat
)
{
    PAGED_CODE();

    if (DataFormat->FormatSize >= sizeof(KSDATAFORMAT_WAVEFORMATEX))
    {
        PKSDATAFORMAT_WAVEFORMATEX waveFormat = (PKSDATAFORMAT_WAVEFORMATEX)DataFormat;
        m_ChannelCount = waveFormat->WaveFormatEx.nChannels;
        m_SampleRate = waveFormat->WaveFormatEx.nSamplesPerSec;
        m_BitsPerSample = waveFormat->WaveFormatEx.wBitsPerSample;
    }

    return STATUS_SUCCESS;
}

STDMETHODIMP_(NTSTATUS)
MiniportWaveRTStream::SetState(
    _In_ KSSTATE State
)
{
    PAGED_CODE();

    switch (State)
    {
    case KSSTATE_RUN:
        m_IsRunning = TRUE;
        m_StartTime = KeQueryPerformanceCounter(&m_PerformanceFrequency);
        break;

    case KSSTATE_PAUSE:
    case KSSTATE_STOP:
        m_IsRunning = FALSE;
        break;

    case KSSTATE_ACQUIRE:
        break;
    }

    return STATUS_SUCCESS;
}

STDMETHODIMP_(NTSTATUS)
MiniportWaveRTStream::SetNotificationFrames(
    _In_ ULONG Frames
)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(Frames);
    return STATUS_SUCCESS;
}

STDMETHODIMP_(NTSTATUS)
MiniportWaveRTStream::GetBuffer(
    _Out_ PHYSICAL_ADDRESS* PhysicalAddress,
    _Out_ ULONG* BufferSize
)
{
    PAGED_CODE();

    if (!PhysicalAddress || !BufferSize)
        return STATUS_INVALID_PARAMETER;

    GigaACESharedMemory* shm = &m_Miniport->m_SharedMemory;
    if (!shm->IsInitialized || !shm->Layout)
        return STATUS_DEVICE_NOT_READY;

    ULONG bytesPerFrame = m_ChannelCount * (m_BitsPerSample / 8);
    ULONG totalFrames = shm->CapacityFrames;
    ULONG totalBytes = totalFrames * bytesPerFrame;

    m_BufferSize = totalBytes;
    m_Buffer = ExAllocatePool2(
        POOL_FLAG_NON_PAGED,
        totalBytes,
        'BufG'
    );

    if (!m_Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(m_Buffer, totalBytes);
    PhysicalAddress->QuadPart = 0;
    *BufferSize = totalBytes;

    return STATUS_SUCCESS;
}

STDMETHODIMP_(void)
MiniportWaveRTStream::GetHWLatency(
    _Out_ ULONG* Flags,
    _Out_ ULONG* ClockInterval,
    _Out_ ULONG* FIFOSize
)
{
    if (!Flags || !ClockInterval || !FIFOSize)
        return;

    *Flags = KSAUDIO_FIFO_LATENCY;
    *ClockInterval = 0;
    *FIFOSize = 0;
}

STDMETHODIMP_(NTSTATUS)
MiniportWaveRTStream::GetClock(
    _Out_ PKSAUDIO_CLOCK* Clock
)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(Clock);
    return STATUS_NOT_SUPPORTED;
}

STDMETHODIMP_(NTSTATUS)
MiniportWaveRTStream::GetPosition(
    _In_ ULONG FrameSize,
    _Out_ ULONGLONG* Position
)
{
    if (!Position || FrameSize == 0)
        return STATUS_INVALID_PARAMETER;

    GigaACESharedMemory* shm = &m_Miniport->m_SharedMemory;

    if (!shm->IsInitialized || !shm->Layout)
    {
        *Position = m_Position;
        return STATUS_SUCCESS;
    }

    if (!m_IsRunning)
    {
        *Position = m_Position;
        return STATUS_SUCCESS;
    }

    ULONGLONG writeIndex = shm->Layout->WriteIndex;
    ULONGLONG readIndex = m_Position;

    if (writeIndex > readIndex)
    {
        ULONGLONG available = writeIndex - readIndex;
        ULONG framesToRead = (ULONG)(available < 1024 ? available : 1024);

        if (framesToRead > 0)
        {
            ULONG capacity = shm->CapacityFrames;
            ULONG channels = shm->Layout->ChannelCount;
            ULONG bytesPerFrame = m_ChannelCount * (m_BitsPerSample / 8);

            for (ULONG frame = 0; frame < framesToRead; ++frame)
            {
                ULONG slot = (ULONG)((readIndex + frame) % capacity);
                if (m_Buffer)
                {
                    ULONG writeSlot = (ULONG)(m_Position % capacity);
                    RtlCopyMemory(
                        (PUCHAR)m_Buffer + writeSlot * bytesPerFrame,
                        &shm->Samples[slot * channels],
                        m_ChannelCount * sizeof(float)
                    );
                }
                m_Position++;
            }
        }
    }

    *Position = m_Position;
    return STATUS_SUCCESS;
}

STDMETHODIMP_(NTSTATUS)
MiniportWaveRTStream::GetPacketNumber(
    _Out_ ULONG* PacketNumber
)
{
    if (!PacketNumber)
        return STATUS_INVALID_PARAMETER;

    *PacketNumber = (ULONG)(m_Position / 1024);
    return STATUS_SUCCESS;
}

STDMETHODIMP_(NTSTATUS)
MiniportWaveRTStream::SetContentId(
    _In_ ULONG ContentId
)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(ContentId);
    return STATUS_SUCCESS;
}
