#pragma once
#include <wdm.h>
#include "macros.h"

#define POOL_TAG (0)

NTSTATUS validateOutputBufferLength(PIRP irp, UINT64 outputBufferLen, UINT64 sizeNeeded);
