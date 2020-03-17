#include <wdm.h>

#define CHECK_STATUS(retval) status=retval; \
							 if (!NT_SUCCESS(status)) goto cleanup;
#define TRACE(...) DbgPrintEx(DPFLTR_IHVDRIVER_ID, 0xFFFFFFFF, __VA_ARGS__)


PDEVICE_OBJECT g_deviceObject = NULL;

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

EXTERN_C
NTSTATUS DriverEntry(PDRIVER_OBJECT driverObject, PUNICODE_STRING) {
	UNREFERENCED_PARAMETER(driverObject);
	TRACE("StorageDevice::DriverEntry\n");

	NTSTATUS status = STATUS_UNSUCCESSFUL;
	driverObject->DriverUnload = driverUnload;
	UNICODE_STRING deviceName = RTL_CONSTANT_STRING(L"\\Device\\MyStorageDevice");
	UNICODE_STRING deviceSymLink = RTL_CONSTANT_STRING(L"\\DosDevices\\MyStorageDeviceSymLink");
	CHECK_STATUS(IoCreateDevice(driverObject, 0, &deviceName, FILE_DEVICE_DISK, 0, TRUE, &g_deviceObject));
	CHECK_STATUS(IoCreateSymbolicLink(&deviceSymLink, &deviceName));
	status = STATUS_SUCCESS;
	goto cleanup;

cleanup:
	return status;
}