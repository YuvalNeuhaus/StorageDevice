#pragma once
#include <wdm.h>
#include "macros.h"

void completeRequest(PDEVICE_OBJECT deviceObject, PIRP irp, PNTSTATUS status);

NTSTATUS validateOutputBufferLength(PIRP irp, UINT64 outputBufferLen, UINT64 sizeNeeded);
