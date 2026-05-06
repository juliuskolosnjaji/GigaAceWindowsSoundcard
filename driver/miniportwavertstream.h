#ifndef GIGAACE_VSC_MINIPORTWAVERTSTREAM_H
#define GIGAACE_VSC_MINIPORTWAVERTSTREAM_H

#include "driver.h"

EXTERN_C_START

class MiniportWaveRT;

class MiniportWaveRTStream : public IMiniportWaveRTStream
{
private:
    LONG m_RefCount;
    MiniportWaveRT* m_Miniport;
    PPORTWAVERTSTREAM m_PortStream;
    ULONG m_Pin;
    BOOLEAN m_Capture;
    ULONG m_ChannelCount;
    ULONG m_SampleRate;
    ULONG m_BitsPerSample;
    ULONG m_BufferFrames;
    ULONG m_BufferSize;
    PVOID m_Buffer;
    ULONGLONG m_Position;
    BOOLEAN m_IsRunning;
    LARGE_INTEGER m_StartTime;
    LARGE_INTEGER m_PerformanceFrequency;
    KSDATAFORMAT_WAVEFORMATEXTENSIBLE m_Format;

public:
    MiniportWaveRTStream();
    ~MiniportWaveRTStream();

    IMP_IUnknown;
    IMP_IMiniportWaveRTStream;

    static NTSTATUS Create(
        _Out_ MiniportWaveRTStream** Stream,
        _In_ MiniportWaveRT* Miniport,
        _In_ PPORTWAVERTSTREAM PortStream,
        _In_ ULONG Pin,
        _In_ BOOLEAN Capture,
        _In_ PKSDATAFORMAT DataFormat
    );
};

EXTERN_C_END

#endif
