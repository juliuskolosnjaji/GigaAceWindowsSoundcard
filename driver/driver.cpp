#include "driver.h"
#include "device.h"
#include "miniport.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, GigaAceVSCDeviceAdd)
#pragma alloc_text(PAGE, GigaAceVSCDriverContextCleanup)
#endif

NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath
)
{
    NTSTATUS status;

    WDF_DRIVER_CONFIG config;
    WDF_OBJECT_ATTRIBUTES attributes;

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.EvtCleanupCallback = GigaAceVSCDriverContextCleanup;

    WDF_DRIVER_CONFIG_INIT(&config, GigaAceVSCDeviceAdd);

    status = WdfDriverCreate(
        DriverObject,
        RegistryPath,
        &attributes,
        &config,
        WDF_NO_HANDLE
    );

    if (!NT_SUCCESS(status))
    {
        TraceLoggingWrite(g_GigaAceVSCProvider, "DriverEntry_Failed",
            TraceLoggingNTStatus(status, "Status"));
        return status;
    }

    return status;
}

NTSTATUS
GigaAceVSCDeviceAdd(
    _In_ WDFDRIVER       Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit
)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(Driver);

    NTSTATUS status = STATUS_SUCCESS;
    WDFDEVICE device;
    WDF_OBJECT_ATTRIBUTES deviceAttributes;
    PDEVICE_CONTEXT pDeviceContext;

    WdfFdoInitSetFilter(DeviceInit);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, DEVICE_CONTEXT);

    status = WdfDeviceCreate(&DeviceInit, &deviceAttributes, &device);
    if (!NT_SUCCESS(status))
    {
        TraceLoggingWrite(g_GigaAceVSCProvider, "DeviceCreate_Failed",
            TraceLoggingNTStatus(status, "Status"));
        return status;
    }

    pDeviceContext = DeviceGetContext(device);
    pDeviceContext->Device = device;
    pDeviceContext->AdapterObject = nullptr;
    pDeviceContext->Miniport = nullptr;

    status = GigaAceVSCInitializeDevice(device);
    if (!NT_SUCCESS(status))
    {
        TraceLoggingWrite(g_GigaAceVSCProvider, "InitializeDevice_Failed",
            TraceLoggingNTStatus(status, "Status"));
        return status;
    }

    return status;
}

void
GigaAceVSCDriverContextCleanup(
    _In_ WDFOBJECT DriverObject
)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(DriverObject);

    GigaAceVSCCleanupTracing();
}
