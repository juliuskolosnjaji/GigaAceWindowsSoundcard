#ifndef GIGAACE_VSC_DRIVER_H
#define GIGAACE_VSC_DRIVER_H

#include <ntddk.h>
#include <wdf.h>
#include <portcls.h>
#include <stdunk.h>
#include <ks.h>
#include <ksmedia.h>
#include "trace.h"

EXTERN_C_START

DRIVER_INITIALIZE DriverEntry;

EVT_WDF_DRIVER_DEVICE_ADD   GigaAceVSCDeviceAdd;
EVT_WDF_OBJECT_CONTEXT_CLEANUP GigaAceVSCDriverContextCleanup;

EXTERN_C_END

#endif
