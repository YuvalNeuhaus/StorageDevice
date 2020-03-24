#include "common.h"

void completeRequest(PDEVICE_OBJECT deviceObject, PIRP irp, PNTSTATUS status) {
	// Complete the request only if deviceObject is the PDO
	if (deviceObject->DeviceObjectExtension->DeviceNode != NULL || deviceObject->AttachedDevice == NULL) {
		IoCompleteRequest(irp, IO_NO_INCREMENT);
	} else {
		IoSkipCurrentIrpStackLocation(irp);
		*status = IoCallDriver(deviceObject->AttachedDevice, irp);
	}
}

NTSTATUS validateOutputBufferLength(PIRP irp, UINT64 outputBufferLen, UINT64 sizeNeeded) {
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	if (outputBufferLen < sizeNeeded) {
		TRACE("StorageDevice::OutputBufferLength is too small. Got %llu, expected %llu\n", outputBufferLen, sizeNeeded);
		COMPLETE_IRP_WITH_STATUS(STATUS_BUFFER_TOO_SMALL)
	}

	RETURN_STATUS(STATUS_SUCCESS);

cleanup:
	return status;
}
