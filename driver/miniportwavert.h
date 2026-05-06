#ifndef GIGAACE_VSC_MINIPORTWAVERT_H
#define GIGAACE_VSC_MINIPORTWAVERT_H

#include "driver.h"
#include "sharedmemory.h"

EXTERN_C_START

NTSTATUS
NewMiniportWaveRT(
    _Out_ PMINIPORTWAVERT* Miniport,
    _In_ PUNKNOWN AdapterUnknown,
    _In_ PRESOURCELIST ResourceList
);

class MiniportWaveRT : public IMiniportWaveRT
{
private:
    LONG m_RefCount;
    PUNKNOWN m_AdapterUnknown;
    PPORTWAVERT m_Port;
    WDFDEVICE m_WdfDevice;
    GigaACESharedMemory m_SharedMemory;
    ULONG m_MaxChannels;
    ULONG m_SampleRate;

public:
    MiniportWaveRT();
    ~MiniportWaveRT();

    STDMETHODIMP_(ULONG) NonDelegatingAddRef();
    STDMETHODIMP_(ULONG) NonDelegatingRelease();
    STDMETHODIMP NonDelegatingQueryInterface(_In_ REFIID, _Out_ PVOID*);

    STDMETHODIMP_(NTSTATUS) GetDescription(_Out_ PPCFILTER_DESCRIPTOR* Description);
    STDMETHODIMP_(NTSTATUS) DataRangeIntersection(
        _In_ ULONG PinId,
        _In_ PKSDATARANGE DataRange,
        _In_ PKSDATARANGE MatchingDataRange,
        _In_ ULONG OutputBufferLength,
        _Out_writes_bytes_to_opt_(OutputBufferLength, *ResultantFormatLength)
            PVOID ResultantFormat,
        _Out_ PULONG ResultantFormatLength);

    STDMETHODIMP_(NTSTATUS) Init(
        _In_ PUNKNOWN UnknownAdapter,
        _In_ PRESOURCELIST ResourceList,
        _In_ PPORTWAVERT Port);
    STDMETHODIMP_(NTSTATUS) NewStream(
        _Out_ PMINIPORTWAVERTSTREAM* Stream,
        _In_ PPORTWAVERTSTREAM PortStream,
        _In_ ULONG Pin,
        _In_ BOOLEAN Capture,
        _In_ PKSDATAFORMAT DataFormat);
    STDMETHODIMP_(NTSTATUS) GetDeviceDescription(
        _Out_ PDEVICE_DESCRIPTION DeviceDescription);

    static NTSTATUS Create(
        _Out_ MiniportWaveRT** Miniport,
        _In_ PUNKNOWN AdapterUnknown,
        _In_ PRESOURCELIST ResourceList
    );

private:
    static const KSPIN_INTERFACE m_PinInterface;
    static const KSPIN_MEDIUM m_Medium;
    static KSPIN_DESCRIPTOR_EX m_PinDescriptor;
    static PCPIN_DESCRIPTOR m_PinDescriptors[];
    static PCNODE_DESCRIPTOR m_NodeDescriptors[];
    static PCCONNECTION_DESCRIPTOR m_Connections[];
    static PCFILTER_DESCRIPTOR m_FilterDescriptor;
};

EXTERN_C_END

#endif
