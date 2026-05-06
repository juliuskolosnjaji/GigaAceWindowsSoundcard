#ifndef GIGAACE_VSC_DEVICE_H
#define GIGAACE_VSC_DEVICE_H

#include "driver.h"

EXTERN_C_START

#define MAX_CHANNELS 128
#define DEFAULT_CHANNELS 64
#define DEFAULT_SAMPLE_RATE 48000
#define DEFAULT_BITS_PER_SAMPLE 24
#define DEVICE_NAME L"\\Device\\GigaAceVSC"
#define SYMBOLIC_LINK_NAME L"\\DosDevices\\GigaAceVSC"

typedef struct _DEVICE_CONTEXT
{
    WDFDEVICE Device;
    PUNKNOWN AdapterObject;
    PUNKNOWN Miniport;
    ULONG ChannelCount;
    ULONG SampleRate;
    ULONG BitsPerSample;
    KEVENT StopEvent;
    BOOLEAN IsStarted;
} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, DeviceGetContext)

NTSTATUS
GigaAceVSCInitializeDevice(
    _In_ WDFDEVICE Device
);

NTSTATUS
GigaAceVSCInstallSubdevice(
    _In_ PDEVICE_OBJECT PhysicalDeviceObject,
    _In_ WDFDEVICE WdfDevice,
    _In_ PCWSTR Name,
    _In_ PUNKNOWN Miniport,
    _In_ PUNKNOWN Adapter,
    _In_ ULONG Channels,
    _In_ ULONG SampleRate,
    _In_ ULONG BitsPerSample
);

NTSTATUS
GigaAceVSCStartDevice(
    _In_ WDFDEVICE Device
);

NTSTATUS
GigaAceVSCStopDevice(
    _In_ WDFDEVICE Device
);

EXTERN_C_END

#endif
