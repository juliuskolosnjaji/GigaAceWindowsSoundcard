#ifndef GIGAACE_VSC_TRACE_H
#define GIGAACE_VSC_TRACE_H

#pragma once

#include <TraceLoggingProvider.h>

TRACELOGGING_DECLARE_PROVIDER(g_GigaAceVSCProvider);

#define GIGAACE_VSC_PROVIDER_NAME "GigaAce.VSC.Driver"

inline
NTSTATUS
GigaAceVSCInitializeTracing(
    _In_ PDRIVER_OBJECT DriverObject
)
{
    UNREFERENCED_PARAMETER(DriverObject);
    NTSTATUS status = TraceLoggingRegister(g_GigaAceVSCProvider);
    return status;
}

inline
void
GigaAceVSCCleanupTracing()
{
    TraceLoggingUnregister(g_GigaAceVSCProvider);
}

#endif
