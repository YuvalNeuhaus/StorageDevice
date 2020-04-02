#include <ntifs.h>
#include <ntddk.h>
# include <wdmsec.h>
#include "StorageDevice.h"
#include "macros.h"
#include "deviceControl.h"
#include "common.h"
#include "globals.h"
#include "dispatchRoutines.h"

DEFINE_GUID(GUID_DEVINTERFACE_DISK, 0x53f56307L, 0xb6bf, 0x11d0, 0x94, 0xf2, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b);
DEFINE_GUID(GUID_DEVINTERFACE_VOLUME, 0x53f5630dL, 0xb6bf, 0x11d0, 0x94, 0xf2, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b);

void driverUnload(PDRIVER_OBJECT driverObject) {
	UNREFERENCED_PARAMETER(driverObject);
	TRACE_FUNC;
	
	IoDetachDevice(g_pdo);

	if (g_fdo != NULL) {
		IoDeleteDevice(g_fdo);
	}

	
	// TODO: I'm not sure that this is my responsibility to delete the PDO because the PNP manager created it in IoReportDetectedDevice
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

EXTERN_C
NTSTATUS DriverEntry(PDRIVER_OBJECT driverObject, PUNICODE_STRING) {
	TRACE_FUNC;
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	UNICODE_STRING deviceName = RTL_CONSTANT_STRING(DEVICE_NAME);
	UNICODE_STRING deviceSymLink = RTL_CONSTANT_STRING(SYMBOLIC_LINK);
	PUNICODE_STRING interfaceSymLink = NULL;
	PDEVICE_OBJECT attachedDevice = NULL;
	
	for (UINT32 i = 0; i < IRP_MJ_MAXIMUM_FUNCTION; i++) {
		driverObject->MajorFunction[i] = handleUnsupporeted;
	}
	driverObject->DriverUnload = driverUnload;
	driverObject->MajorFunction[IRP_MJ_READ] = handleRead;
	driverObject->MajorFunction[IRP_MJ_WRITE] = handleWrite;
	driverObject->MajorFunction[IRP_MJ_CREATE] = handleCreate;
	driverObject->MajorFunction[IRP_MJ_CLOSE] = handleClose;
	driverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = handleIoCtl;
	driverObject->MajorFunction[IRP_MJ_CLEANUP] = handleCleanup;
	driverObject->MajorFunction[IRP_MJ_QUERY_INFORMATION] = handleQueryInfo;
	driverObject->MajorFunction[IRP_MJ_QUERY_VOLUME_INFORMATION] = handleQueryVolInfo;
	driverObject->MajorFunction[IRP_MJ_POWER] = handlePower;

	// Create the storage memory
	g_storage = reinterpret_cast<PCHAR>(ExAllocatePoolWithTag(NonPagedPool, STORAGE_SIZE, POOL_TAG));
	if (g_storage == NULL) {
		TRACE("StorageDevice::ExAllocatePoolWithTag for g_storage failed\n");
		RETURN_STATUS(STATUS_INSUFFICIENT_RESOURCES);
	}
	RtlZeroMemory(g_storage, STORAGE_SIZE);

	// Create to FDO
	CHECK_STATUS(IoCreateDevice(driverObject,
								sizeof(COMMON_DEVICE_EXTENSION),
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
										INTERFACE_TYPE::InterfaceTypeUndefined,
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

	interfaceSymLink = reinterpret_cast<PUNICODE_STRING>(ExAllocatePoolWithTag(NonPagedPool, sizeof(UNICODE_STRING), POOL_TAG));
	if (interfaceSymLink == NULL) {
		TRACE("StorageDevice::Failed to allocate memory for interfaceSymLink\n");
		RETURN_STATUS(STATUS_INSUFFICIENT_RESOURCES);
	}

	// Register to the MOUNTDEV_MOUNTED_DEVICE_GUID interface
	CHECK_STATUS(IoRegisterDeviceInterface(g_pdo, &MOUNTDEV_MOUNTED_DEVICE_GUID, NULL, interfaceSymLink));
	CHECK_STATUS(IoSetDeviceInterfaceState(interfaceSymLink, TRUE));
	RETURN_STATUS(STATUS_SUCCESS);

cleanup:
	if (interfaceSymLink != NULL && interfaceSymLink->Buffer != NULL) {
		RtlFreeUnicodeString(interfaceSymLink);
	}

	if (!NT_SUCCESS(status)) {
		TRACE("StorageDevice::DriverEntry failed with status %lu\n", status);
	}
	return status;
}
