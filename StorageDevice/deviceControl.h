#pragma once
#include <wdm.h>

NTSTATUS handleIoCtl(PDEVICE_OBJECT deviceObject, PIRP irp);
