#include <wdm.h>

#define CHECK_STATUS(retval) status=retval; \
							 if (!NT_SUCCESS(status)) goto cleanup;
#define TRACE(...) DbgPrintEx(DPFLTR_IHVDRIVER_ID, 0xFFFFFFFF, __VA_ARGS__)
#define RETURN_STATUS(ret) status=ret;goto cleanup;

#define STORAGE_SIZE (1024*1024*50)
#define POOL_TAG (0)

PDEVICE_OBJECT g_deviceObject = NULL;
PCHAR g_storage = NULL;


void driverUnload(PDRIVER_OBJECT driverObject) {
	UNREFERENCED_PARAMETER(driverObject);
	TRACE("StorageDevice::driverUnload\n");
	if (g_deviceObject != NULL) {
		IoDeleteDevice(g_deviceObject);
	}
	UNICODE_STRING deviceSymLink = RTL_CONSTANT_STRING(L"\\DosDevices\\MyStorageDeviceSymLink");
	NTSTATUS status;
	status = IoDeleteSymbolicLink(&deviceSymLink);
	if (!NT_SUCCESS(status)) {
		TRACE("StorageDevice::IoDeleteSymbolicLink failed with status %lu\n", status);
	}
}

NTSTATUS readWrite(UINT64 offset, UINT32 length, BOOLEAN isWrite, PIRP irp) {
	NTSTATUS status = STATUS_UNSUCCESSFUL;

	if (offset > MAXUINT32) {
		TRACE("StorageDevice::%s got offset that is bigget than MAXUINT32\n", isWrite ? "Write" : "Read");
		irp->IoStatus.Information = 0;
		irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
		RETURN_STATUS(STATUS_INVALID_PARAMETER);
	}

	if (offset >= STORAGE_SIZE) {
		TRACE("StorageDevice::%s requests for an offset beyond the limits\n", isWrite ? "Write" : "Read");
		irp->IoStatus.Information = 0;
		irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
		RETURN_STATUS(STATUS_INVALID_PARAMETER);
	}

	
	UINT32 bytesToWrite = 0;
	if (isWrite) {
		if (offset + length >= STORAGE_SIZE) {
			TRACE("StorageDevice::Write requests for an offset beyond the limits\n");
			irp->IoStatus.Information = 0;
			irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
			RETURN_STATUS(STATUS_INVALID_PARAMETER);
		}
		bytesToWrite = length;
	} else {
		bytesToWrite = min(length, STORAGE_SIZE - static_cast<UINT32>(offset));
	}

	PVOID dst = isWrite ? &g_storage[offset] : irp->AssociatedIrp.SystemBuffer;
	PVOID src = isWrite ? irp->AssociatedIrp.SystemBuffer : &g_storage[offset];
	RtlCopyMemory(dst, src, bytesToWrite);
	irp->IoStatus.Information = bytesToWrite;
	irp->IoStatus.Status = STATUS_SUCCESS;
	RETURN_STATUS(STATUS_SUCCESS);

cleanup:
	return status;
}

NTSTATUS handleRead(PDEVICE_OBJECT deviceObject, PIRP irp) {
	UNREFERENCED_PARAMETER(deviceObject);
	TRACE("StorageDevice::handleRead\n");
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
	UINT64 offset = stack->Parameters.Read.ByteOffset.QuadPart;
	RETURN_STATUS(readWrite(offset, stack->Parameters.Read.Length, FALSE, irp));

cleanup:
	IoCompleteRequest(irp, IO_NO_INCREMENT);
	return status;
}

NTSTATUS handleWrite(PDEVICE_OBJECT deviceObject, PIRP irp) {
	UNREFERENCED_PARAMETER(deviceObject);
	TRACE("StorageDevice::handleWrite\n");
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
	UINT64 offset = stack->Parameters.Write.ByteOffset.QuadPart;
	RETURN_STATUS(readWrite(offset, stack->Parameters.Write.Length, TRUE, irp));

cleanup:
	IoCompleteRequest(irp, IO_NO_INCREMENT);
	return status;
}

NTSTATUS handleOperationSuccess(PDEVICE_OBJECT deviceObject, PIRP irp) {
	UNREFERENCED_PARAMETER(deviceObject);
	irp->IoStatus.Information = 0;
	irp->IoStatus.Status = STATUS_SUCCESS;
	IoCompleteRequest(irp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}

EXTERN_C
NTSTATUS DriverEntry(PDRIVER_OBJECT driverObject, PUNICODE_STRING) {
	UNREFERENCED_PARAMETER(driverObject);
	TRACE("StorageDevice::DriverEntry\n");

	NTSTATUS status = STATUS_UNSUCCESSFUL;
	driverObject->DriverUnload = driverUnload;
	driverObject->MajorFunction[IRP_MJ_READ] = handleRead;
	driverObject->MajorFunction[IRP_MJ_WRITE] = handleWrite;
	driverObject->MajorFunction[IRP_MJ_CREATE] = handleOperationSuccess;
	driverObject->MajorFunction[IRP_MJ_CLOSE] = handleOperationSuccess;


	UNICODE_STRING deviceName = RTL_CONSTANT_STRING(L"\\Device\\MyStorageDevice");
	UNICODE_STRING deviceSymLink = RTL_CONSTANT_STRING(L"\\DosDevices\\MyStorageDeviceSymLink");

	g_storage = reinterpret_cast<PCHAR>(ExAllocatePoolWithTag(NonPagedPool, STORAGE_SIZE, POOL_TAG));
	if (g_storage == NULL) {
		TRACE("StorageDevice::ExAllocatePoolWithTag failed\n");
		RETURN_STATUS(STATUS_INSUFFICIENT_RESOURCES);
	}
	RtlZeroMemory(g_storage, STORAGE_SIZE);

	CHECK_STATUS(IoCreateDevice(driverObject, 0, &deviceName, FILE_DEVICE_UNKNOWN, 0, TRUE, &g_deviceObject));
	CHECK_STATUS(IoCreateSymbolicLink(&deviceSymLink, &deviceName));

	g_deviceObject->Flags |= DO_BUFFERED_IO;
	RETURN_STATUS(STATUS_SUCCESS);

cleanup:
	g_deviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
	return status;
}