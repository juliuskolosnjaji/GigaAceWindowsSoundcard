#include "miniportwavert.h"
#include "miniportwavertstream.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, MiniportWaveRT_Create)
#pragma alloc_text(PAGE, MiniportWaveRT_GetDescription)
#pragma alloc_text(PAGE, MiniportWaveRT_DataRangeIntersection)
#pragma alloc_text(PAGE, MiniportWaveRT_Init)
#pragma alloc_text(PAGE, MiniportWaveRT_NewStream)
#pragma alloc_text(PAGE, MiniportWaveRT_GetDeviceDescription)
#pragma alloc_text(PAGE, NewMiniportWaveRT)
#endif

MiniportWaveRT::MiniportWaveRT()
    : m_RefCount(1),
      m_AdapterUnknown(nullptr),
      m_Port(nullptr),
      m_WdfDevice(nullptr),
      m_MaxChannels(2),
      m_SampleRate(48000)
{
    RtlZeroMemory(&m_SharedMemory, sizeof(m_SharedMemory));
}

MiniportWaveRT::~MiniportWaveRT()
{
    if (m_SharedMemory.IsInitialized)
    {
        GigaACECloseSharedMemory(&m_SharedMemory);
    }
    if (m_AdapterUnknown)
    {
        m_AdapterUnknown->Release();
    }
}

NTSTATUS
MiniportWaveRT::Create(
    _Out_ MiniportWaveRT** Miniport,
    _In_ PUNKNOWN AdapterUnknown,
    _In_ PRESOURCELIST ResourceList
)
{
    PAGED_CODE();

    if (!Miniport)
        return STATUS_INVALID_PARAMETER;

    MiniportWaveRT* obj = new (NonPagedPoolNx, 'MgiG') MiniportWaveRT();
    if (!obj)
        return STATUS_INSUFFICIENT_RESOURCES;

    NTSTATUS status = GigaACEOpenSharedMemory(&obj->m_SharedMemory);
    if (NT_SUCCESS(status))
    {
        obj->m_AdapterUnknown = AdapterUnknown;
        if (AdapterUnknown)
        {
            AdapterUnknown->AddRef();
        }
        *Miniport = obj;
    }
    else
    {
        delete obj;
    }

    return status;
}

STDMETHODIMP_(ULONG)
MiniportWaveRT::NonDelegatingAddRef()
{
    return InterlockedIncrement(&m_RefCount);
}

STDMETHODIMP_(ULONG)
MiniportWaveRT::NonDelegatingRelease()
{
    ULONG count = InterlockedDecrement(&m_RefCount);
    if (count == 0)
    {
        this->~MiniportWaveRT();
        ExFreePoolWithTag(this, 'MgiG');
    }
    return count;
}

STDMETHODIMP
MiniportWaveRT::NonDelegatingQueryInterface(
    _In_ REFIID Interface,
    _Out_ PVOID* Object
)
{
    PAGED_CODE();

    if (!Object)
        return STATUS_INVALID_PARAMETER;

    if (IsEqualGUIDAligned(Interface, IID_IUnknown))
    {
        *Object = PVOID(PUNKNOWN(PMINIPORTWAVERT(this)));
        (*(PUNKNOWN*)Object)->AddRef();
        return STATUS_SUCCESS;
    }
    else if (IsEqualGUIDAligned(Interface, IID_IMiniport))
    {
        *Object = PVOID(PMINIPORT(this));
        AddRef();
        return STATUS_SUCCESS;
    }
    else if (IsEqualGUIDAligned(Interface, IID_IMiniportWaveRT))
    {
        *Object = PVOID(PMINIPORTWAVERT(this));
        AddRef();
        return STATUS_SUCCESS;
    }

    *Object = nullptr;
    return STATUS_INVALID_PARAMETER;
}

STDMETHODIMP_(NTSTATUS)
MiniportWaveRT::GetDescription(
    _Out_ PPCFILTER_DESCRIPTOR* Description
)
{
    PAGED_CODE();
    *Description = &m_FilterDescriptor;
    return STATUS_SUCCESS;
}

STDMETHODIMP_(NTSTATUS)
MiniportWaveRT::DataRangeIntersection(
    _In_ ULONG PinId,
    _In_ PKSDATARANGE DataRange,
    _In_ PKSDATARANGE MatchingDataRange,
    _In_ ULONG OutputBufferLength,
    _Out_writes_bytes_to_opt_(OutputBufferLength, *ResultantFormatLength)
        PVOID ResultantFormat,
    _Out_ PULONG ResultantFormatLength
)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(PinId);
    UNREFERENCED_PARAMETER(DataRange);
    UNREFERENCED_PARAMETER(MatchingDataRange);
    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(ResultantFormat);
    UNREFERENCED_PARAMETER(ResultantFormatLength);
    return STATUS_NOT_IMPLEMENTED;
}

STDMETHODIMP_(NTSTATUS)
MiniportWaveRT::Init(
    _In_ PUNKNOWN UnknownAdapter,
    _In_ PRESOURCELIST ResourceList,
    _In_ PPORTWAVERT Port
)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(ResourceList);

    m_Port = Port;

    if (UnknownAdapter && !m_AdapterUnknown)
    {
        m_AdapterUnknown = UnknownAdapter;
        UnknownAdapter->AddRef();
    }

    return STATUS_SUCCESS;
}

STDMETHODIMP_(NTSTATUS)
MiniportWaveRT::NewStream(
    _Out_ PMINIPORTWAVERTSTREAM* Stream,
    _In_ PPORTWAVERTSTREAM PortStream,
    _In_ ULONG Pin,
    _In_ BOOLEAN Capture,
    _In_ PKSDATAFORMAT DataFormat
)
{
    PAGED_CODE();

    if (!Stream || !DataFormat)
        return STATUS_INVALID_PARAMETER;

    *Stream = nullptr;

    MiniportWaveRTStream* stream = nullptr;
    NTSTATUS status = MiniportWaveRTStream::Create(
        &stream, this, PortStream, Pin, Capture, DataFormat);

    if (!NT_SUCCESS(status))
        return status;

    status = stream->NonDelegatingQueryInterface(IID_IMiniportWaveRTStream, (PVOID*)Stream);
    if (!NT_SUCCESS(status))
    {
        delete stream;
    }

    return status;
}

STDMETHODIMP_(NTSTATUS)
MiniportWaveRT::GetDeviceDescription(
    _Out_ PDEVICE_DESCRIPTION DeviceDescription
)
{
    PAGED_CODE();
    RtlZeroMemory(DeviceDescription, sizeof(DEVICE_DESCRIPTION));
    DeviceDescription->Version = DEVICE_DESCRIPTION_VERSION3;
    DeviceDescription->DmaWidth = Width32Bits;
    DeviceDescription->DmaPort = 0;
    DeviceDescription->MaximumLength = 0xFFFFFFFF;
    return STATUS_SUCCESS;
}

NTSTATUS
NewMiniportWaveRT(
    _Out_ PMINIPORTWAVERT* Miniport,
    _In_ PUNKNOWN AdapterUnknown,
    _In_ PRESOURCELIST ResourceList
)
{
    PAGED_CODE();

    if (!Miniport)
        return STATUS_INVALID_PARAMETER;

    MiniportWaveRT* obj = nullptr;
    NTSTATUS status = MiniportWaveRT::Create(&obj, AdapterUnknown, ResourceList);
    if (NT_SUCCESS(status))
    {
        *Miniport = obj;
    }

    return status;
}

const KSPIN_INTERFACE MiniportWaveRT::m_PinInterface = { STATIC_KSPINSETID_STANDARD };
const KSPIN_MEDIUM MiniportWaveRT::m_Medium = { STATIC_KSPIN_MEDIUM_DEFAULT };

static KSDATARANGE_AUDIO g_AudioRange =
{
    {
        sizeof(KSDATARANGE_AUDIO),
        0, 0, 0, 0, 0,
        KSDATAFORMAT_TYPE_AUDIO,
        KSDATAFORMAT_SUBTYPE_PCM,
    },
    GIGAACE_MAX_CHANNELS,
    16,
    32,
    0,
    DEFAULT_SAMPLE_RATE * 10000,
    DEFAULT_SAMPLE_RATE * 10000,
};

static PKSDATARANGE g_PinRanges[] = { (PKSDATARANGE)&g_AudioRange };

static KSPIN_DESCRIPTOR_EX MiniportWaveRT::m_PinDescriptor =
{
    &MiniportWaveRT::m_PinInterface,
    &MiniportWaveRT::m_Medium,
    0,
    nullptr,
    1,
    g_PinRanges,
    KSPIN_FLAG_PROCESS_IN_RUN_STATE_ONLY |
    KSPIN_FLAG_GENERATE_MAPPINGS,
    1,
    0,
    0,
    nullptr
};

PCPIN_DESCRIPTOR MiniportWaveRT::m_PinDescriptors[] =
{
    {
        1, 1, 0,
        nullptr,
        {
            0,
            nullptr,
            sizeof(KSPIN_DESCRIPTOR_EX),
            &MiniportWaveRT::m_PinDescriptor,
        }
    }
};

PCNODE_DESCRIPTOR MiniportWaveRT::m_NodeDescriptors[] =
{
    {
        0,
        &KSNODETYPE_MICROPHONE,
        nullptr,
        nullptr,
        nullptr,
    }
};

PCCONNECTION_DESCRIPTOR MiniportWaveRT::m_Connections[] =
{
    { PCFILTER_NODE, 0, PIN_NODE, 0 }
};

PCFILTER_DESCRIPTOR MiniportWaveRT::m_FilterDescriptor =
{
    0,
    nullptr,
    SIZEOF_ARRAY(MiniportWaveRT::m_NodeDescriptors),
    MiniportWaveRT::m_NodeDescriptors,
    nullptr,
    SIZEOF_ARRAY(MiniportWaveRT::m_PinDescriptors),
    MiniportWaveRT::m_PinDescriptors,
    SIZEOF_ARRAY(MiniportWaveRT::m_Connections),
    MiniportWaveRT::m_Connections,
    0,
    nullptr,
};
