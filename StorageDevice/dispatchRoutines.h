#pragma once
#include <wdm.h>

NTSTATUS handleCleanup(PDEVICE_OBJECT deviceObject, PIRP irp);
NTSTATUS handleClose(PDEVICE_OBJECT deviceObject, PIRP irp);
NTSTATUS handleCreate(PDEVICE_OBJECT deviceObject, PIRP irp);
NTSTATUS handleRead(PDEVICE_OBJECT deviceObject, PIRP irp);
NTSTATUS handleWrite(PDEVICE_OBJECT deviceObject, PIRP irp);
NTSTATUS handleUnsupporeted(PDEVICE_OBJECT deviceObject, PIRP irp);
NTSTATUS handleQueryInfo(PDEVICE_OBJECT deviceObject, PIRP irp);
NTSTATUS handleQueryVolInfo(PDEVICE_OBJECT deviceObject, PIRP irp);
NTSTATUS handlePower(PDEVICE_OBJECT deviceObject, PIRP irp);
