#include "common.h"

NTSTATUS validateOutputBufferLength(PIRP irp, UINT64 outputBufferLen, UINT64 sizeNeeded) {
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	if (outputBufferLen < sizeNeeded) {
		TRACE("StorageDevice::OutputBufferLength is too small. Got %llu, expected %llu\n", outputBufferLen, sizeNeeded);
		COMPLETE_IRP_WITH_STATUS(STATUS_BUFFER_OVERFLOW);
	}

	RETURN_STATUS(STATUS_SUCCESS);

cleanup:
	return status;
}
