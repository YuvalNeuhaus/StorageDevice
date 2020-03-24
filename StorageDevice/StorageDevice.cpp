#include "StorageDevice.h"
#include "macros.h"
#include "declarations.h"
#include "deviceControl.h"
#include "common.h"
#include "globals.h"

void driverUnload(PDRIVER_OBJECT driverObject) {
	UNREFERENCED_PARAMETER(driverObject);
	TRACE("StorageDevice::driverUnload\n");
	
	if (g_fdo != NULL) {
		IoDeleteDevice(g_fdo);
	}

	if (g_pdo != NULL) {
		IoDeleteDevice(g_pdo);
	}
	
	UNICODE_STRING deviceSymLink = RTL_CONSTANT_STRING(SYMBOLIC_LINK);
	NTSTATUS status = IoDeleteSymbolicLink(&deviceSymLink);
	if (!NT_SUCCESS(status)) {
		TRACE("StorageDevice::IoDeleteSymbolicLink failed with status %lu\n", status);
	}

	ExFreePoolWithTag(g_storage, POOL_TAG);
}

NTSTATUS completeOrPassToLower(PDEVICE_OBJECT deviceObject, PIRP irp) {
	TRACE("StorageDevice::passToAttached\n");
	NTSTATUS status = STATUS_SUCCESS;
	irp->IoStatus.Information = 0;
	irp->IoStatus.Status = status;
	completeRequest(deviceObject, irp, &status);
	return status;
}

NTSTATUS readWrite(PIRP irp, BOOLEAN isWrite) {
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	UINT64 offset = 0;
	UINT32 length = 0;
	PVOID userBuffer = NULL;
	PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);

	if (isWrite) {
		length = stack->Parameters.Write.Length;
		offset = stack->Parameters.Write.ByteOffset.QuadPart;
	} else {
		length = stack->Parameters.Read.Length;
		offset = stack->Parameters.Read.ByteOffset.QuadPart;
	}
	
	if (irp->AssociatedIrp.SystemBuffer != NULL) {
		userBuffer = irp->AssociatedIrp.SystemBuffer;
	} else if (irp->MdlAddress != NULL) {
		userBuffer = MmGetSystemAddressForMdlSafe(irp->MdlAddress, NormalPagePriority);;
	} else {
		TRACE("StorageDevice::Received an IRP that uses NEITHER_IO which is not supported\n");
		COMPLETE_IRP_WITH_STATUS(STATUS_NOT_SUPPORTED);
	}

	if (offset > MAXUINT32) {
		TRACE("StorageDevice::%s got offset that is bigget than MAXUINT32\n", isWrite ? "Write" : "Read");
		COMPLETE_IRP_WITH_STATUS(STATUS_INVALID_PARAMETER);
	}

	if (offset >= STORAGE_SIZE) {
		TRACE("StorageDevice::%s requests for an offset beyond the limits\n", isWrite ? "Write" : "Read");
		COMPLETE_IRP_WITH_STATUS(STATUS_INVALID_PARAMETER);
	}

	UINT32 bytesToWrite = 0;
	if (isWrite) {
		if (offset + length >= STORAGE_SIZE) {
			TRACE("StorageDevice::Write requests for an offset beyond the limits\n");
			COMPLETE_IRP_WITH_STATUS(STATUS_INVALID_PARAMETER);
		}
		bytesToWrite = length;
	} else {
		bytesToWrite = min(length, STORAGE_SIZE - static_cast<UINT32>(offset));
	}

	PVOID dst = isWrite ? &g_storage[offset] : userBuffer;
	PVOID src = isWrite ? userBuffer : &g_storage[offset];
	RtlCopyMemory(dst, src, bytesToWrite);
	irp->IoStatus.Information = bytesToWrite;
	COMPLETE_IRP_WITH_STATUS(STATUS_SUCCESS);

cleanup:
	return status;
}

NTSTATUS handleRead(PDEVICE_OBJECT deviceObject, PIRP irp) {
	UNREFERENCED_PARAMETER(deviceObject);
	TRACE("StorageDevice::handleRead\n");
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	RETURN_STATUS(readWrite(irp, FALSE));

cleanup:
	completeRequest(deviceObject, irp, &status);
	return status;
}

NTSTATUS handleWrite(PDEVICE_OBJECT deviceObject, PIRP irp) {
	UNREFERENCED_PARAMETER(deviceObject);
	TRACE("StorageDevice::handleWrite\n");
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	RETURN_STATUS(readWrite(irp, TRUE));

cleanup:
	completeRequest(deviceObject, irp, &status);
	return status;
}

NTSTATUS handleUnsupporeted(PDEVICE_OBJECT deviceObject, PIRP irp) {
	UNREFERENCED_PARAMETER(deviceObject);
	PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
	TRACE("StorageDevice::handleUnsupporeted - %lu, %lu\n", stack->MajorFunction, stack->MinorFunction);
	irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
	return STATUS_NOT_SUPPORTED;
}

EXTERN_C
NTSTATUS DriverEntry(PDRIVER_OBJECT driverObject, PUNICODE_STRING) {
	TRACE("StorageDevice::DriverEntry\n");
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	UNICODE_STRING deviceName = RTL_CONSTANT_STRING(DEVICE_NAME);
	UNICODE_STRING deviceSymLink = RTL_CONSTANT_STRING(SYMBOLIC_LINK);
	UNICODE_STRING interfaceSymLink;
	PDEVICE_OBJECT attachedDevice = NULL;

	for (UINT32 i = 0; i < IRP_MJ_MAXIMUM_FUNCTION; i++) {
		driverObject->MajorFunction[i] = handleUnsupporeted;
	}
	driverObject->DriverUnload = driverUnload;
	driverObject->MajorFunction[IRP_MJ_READ] = handleRead;
	driverObject->MajorFunction[IRP_MJ_WRITE] = handleWrite;
	driverObject->MajorFunction[IRP_MJ_CREATE] = completeOrPassToLower;
	driverObject->MajorFunction[IRP_MJ_CLOSE] = completeOrPassToLower;
	driverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = handleIoCtl;

	// Create to storage memory
	g_storage = reinterpret_cast<PCHAR>(ExAllocatePoolWithTag(NonPagedPool, STORAGE_SIZE, POOL_TAG));
	if (g_storage == NULL) {
		TRACE("StorageDevice::ExAllocatePoolWithTag for g_storage failed\n");
		RETURN_STATUS(STATUS_INSUFFICIENT_RESOURCES);
	}
	RtlZeroMemory(g_storage, STORAGE_SIZE);

	// Create to FDO
	CHECK_STATUS(IoCreateDevice(driverObject,
								0,
								&deviceName,
								FILE_DEVICE_DISK,
								FILE_DEVICE_SECURE_OPEN,
								FALSE,
								&g_fdo));
	g_fdo->Flags |= DO_BUFFERED_IO;
	g_fdo->Flags &= ~DO_DEVICE_INITIALIZING;
	CHECK_STATUS(IoCreateSymbolicLink(&deviceSymLink, &deviceName));

	// Report to the PnP Manager and get the PDO
	CHECK_STATUS(IoReportDetectedDevice(driverObject,
						                InterfaceTypeUndefined,
						                (ULONG)-1,
						                (ULONG)-1,
						                NULL,
						                NULL,
						                FALSE,
						                &g_pdo));

	// Attach the FDO to the PDO
	attachedDevice = IoAttachDeviceToDeviceStack(g_fdo, g_pdo);
	if (attachedDevice == NULL) {
		TRACE("StorageDevice::Failed to attach FDO to PDO\n");
		RETURN_STATUS(STATUS_UNSUCCESSFUL);
	}

	// Register to the MOUNTDEV_MOUNTED_DEVICE_GUID interface
	CHECK_STATUS(IoRegisterDeviceInterface(g_pdo, &MOUNTDEV_MOUNTED_DEVICE_GUID, NULL, &interfaceSymLink));
	reinterpret_cast<PCOMMON_DEVICE_EXTENSION>(g_pdo->DeviceExtension)->MountedDeviceInterfaceName = interfaceSymLink;
	CHECK_STATUS(IoSetDeviceInterfaceState(&interfaceSymLink, TRUE));
	RETURN_STATUS(STATUS_SUCCESS);

cleanup:
	if (interfaceSymLink.Buffer != NULL) {
		RtlFreeUnicodeString(&interfaceSymLink);
	}

	if (!NT_SUCCESS(status)) {
		TRACE("StorageDevice::DriverEntry failed with status %lu\n", status);
	}
	return status;
}
