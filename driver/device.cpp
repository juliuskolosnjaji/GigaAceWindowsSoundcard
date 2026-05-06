#include "device.h"
#include "miniportwavert.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, GigaAceVSCInitializeDevice)
#pragma alloc_text(PAGE, GigaAceVSCStartDevice)
#pragma alloc_text(PAGE, GigaAceVSCStopDevice)
#endif

NTSTATUS
GigaAceVSCInitializeDevice(
    _In_ WDFDEVICE Device
)
{
    PAGED_CODE();

    NTSTATUS status = STATUS_SUCCESS;
    PDEVICE_CONTEXT pDeviceContext;
    UNICODE_STRING deviceName;
    UNICODE_STRING symbolicLink;

    pDeviceContext = DeviceGetContext(Device);

    pDeviceContext->ChannelCount = DEFAULT_CHANNELS;
    pDeviceContext->SampleRate = DEFAULT_SAMPLE_RATE;
    pDeviceContext->BitsPerSample = DEFAULT_BITS_PER_SAMPLE;
    pDeviceContext->AdapterObject = nullptr;
    pDeviceContext->Miniport = nullptr;
    pDeviceContext->IsStarted = FALSE;

    KeInitializeEvent(&pDeviceContext->StopEvent, NotificationEvent, FALSE);

    RtlInitUnicodeString(&deviceName, DEVICE_NAME);
    RtlInitUnicodeString(&symbolicLink, SYMBOLIC_LINK_NAME);

    status = IoCreateSymbolicLink(&symbolicLink, &deviceName);
    if (!NT_SUCCESS(status))
    {
        TraceLoggingWrite(g_GigaAceVSCProvider, "CreateSymbolicLink_Failed",
            TraceLoggingNTStatus(status, "Status"));
    }

    return status;
}

NTSTATUS
GigaAceVSCStartDevice(
    _In_ WDFDEVICE Device
)
{
    PAGED_CODE();

    NTSTATUS status = STATUS_SUCCESS;
    PDEVICE_CONTEXT pDeviceContext;
    PDEVICE_OBJECT pdo;
    PUNKNOWN unknownMiniport = nullptr;
    PPORT port = nullptr;
    PUNKNOWN unknownPort = nullptr;
    PMINIPORTWAVERT miniport = nullptr;

    pDeviceContext = DeviceGetContext(Device);
    pdo = WdfDeviceWdmGetDeviceObject(Device);

    status = NewMiniportWaveRT(
        &miniport,
        nullptr,
        nullptr
    );
    if (!NT_SUCCESS(status))
        goto Exit;

    status = miniport->QueryInterface(IID_IUnknown, (PVOID*)&unknownMiniport);
    if (!NT_SUCCESS(status))
        goto Exit;

    status = PcNewPort(&port, CLSID_PortWaveRT);
    if (!NT_SUCCESS(status))
        goto Exit;

    status = port->Init(
        pdo,
        nullptr,
        unknownMiniport,
        nullptr,
        nullptr
    );
    if (!NT_SUCCESS(status))
        goto Exit;

    status = port->QueryInterface(IID_IPortWaveRT, (PVOID*)&unknownPort);
    if (!NT_SUCCESS(status))
        goto Exit;

    status = PcRegisterSubdevice(
        pdo,
        L"Wave",
        unknownPort
    );
    if (!NT_SUCCESS(status))
        goto Exit;

    pDeviceContext->Miniport = unknownMiniport;
    unknownMiniport = nullptr;
    pDeviceContext->AdapterObject = unknownPort;
    unknownPort = nullptr;
    pDeviceContext->IsStarted = TRUE;

    TraceLoggingWrite(g_GigaAceVSCProvider, "DeviceStarted",
        TraceLoggingString("GigaACE Virtual Sound Card", "Description"));

Exit:
    if (miniport)
        miniport->Release();
    if (unknownMiniport)
        unknownMiniport->Release();
    if (port)
        port->Release();
    if (unknownPort)
        unknownPort->Release();

    return status;
}

NTSTATUS
GigaAceVSCStopDevice(
    _In_ WDFDEVICE Device
)
{
    PAGED_CODE();

    PDEVICE_CONTEXT pDeviceContext;
    UNICODE_STRING symbolicLink;

    pDeviceContext = DeviceGetContext(Device);

    if (pDeviceContext->IsStarted)
    {
        if (pDeviceContext->Miniport)
        {
            pDeviceContext->Miniport->Release();
            pDeviceContext->Miniport = nullptr;
        }
        if (pDeviceContext->AdapterObject)
        {
            pDeviceContext->AdapterObject->Release();
            pDeviceContext->AdapterObject = nullptr;
        }
        pDeviceContext->IsStarted = FALSE;
    }

    RtlInitUnicodeString(&symbolicLink, SYMBOLIC_LINK_NAME);
    IoDeleteSymbolicLink(&symbolicLink);

    return STATUS_SUCCESS;
}
