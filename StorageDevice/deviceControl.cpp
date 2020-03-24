#include "deviceControl.h"
#include "macros.h"
#include "StorageDevice.h"
#include "declarations.h"
#include "common.h"
#include "globals.h"

NTSTATUS handleIoCtl(PDEVICE_OBJECT deviceObject, PIRP irp) {
	UNREFERENCED_PARAMETER(deviceObject);
	TRACE("StorageDevice::handleIoCtl\n");

	NTSTATUS status = STATUS_UNSUCCESSFUL;
	PMOUNTDEV_NAME mntDevName = NULL;
	PMOUNTDEV_UNIQUE_ID mntDevUID = NULL;
	UNICODE_STRING deviceUID;
	ULONG nameRetLen = 0;
	POBJECT_NAME_INFORMATION objNameInfo = NULL;
	objNameInfo = reinterpret_cast<POBJECT_NAME_INFORMATION>(ExAllocatePoolWithTag(NonPagedPool, sizeof(OBJECT_NAME_INFORMATION) + 256, POOL_TAG));
	if (objNameInfo == NULL) {
		TRACE("StorageDevice::ExAllocatePoolWithTag for objNameInfo failed\n");
		COMPLETE_IRP_WITH_STATUS(STATUS_INSUFFICIENT_RESOURCES);
	}

	PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
	UINT64 outputBufLen = stack->Parameters.DeviceIoControl.OutputBufferLength;

	switch (stack->Parameters.DeviceIoControl.IoControlCode) {

	case IOCTL_MOUNTDEV_QUERY_DEVICE_NAME:
		TRACE("StorageDevice::Handling IOCTL_MOUNTDEV_QUERY_DEVICE_NAME\n");
		CHECK_STATUS(ObQueryNameString(g_pdo, objNameInfo, 256, &nameRetLen));
		mntDevName = reinterpret_cast<PMOUNTDEV_NAME>(irp->AssociatedIrp.SystemBuffer);
		if (!NT_SUCCESS(validateOutputBufferLength(irp, outputBufLen, objNameInfo->Name.Length + sizeof(USHORT)))) {
			if (outputBufLen >= sizeof(MOUNTDEV_NAME)) {
				mntDevName->NameLength = objNameInfo->Name.Length;
				irp->IoStatus.Information = sizeof(MOUNTDEV_NAME);
				COMPLETE_IRP_WITH_STATUS(STATUS_BUFFER_OVERFLOW);
			} else {
				COMPLETE_IRP_WITH_STATUS(STATUS_INVALID_PARAMETER);
			}
		}
		RtlCopyBytes((PCHAR)mntDevName->Name, objNameInfo->Name.Buffer, objNameInfo->Name.Length);
		mntDevName->NameLength = objNameInfo->Name.Length;
		irp->IoStatus.Information = objNameInfo->Name.Length + sizeof(USHORT);
		break;

	case IOCTL_MOUNTDEV_QUERY_UNIQUE_ID:
		TRACE("StorageDevice::Handling IOCTL_MOUNTDEV_QUERY_UNIQUE_ID\n");
		deviceUID = RTL_CONSTANT_STRING(DEVICE_UID);
		mntDevUID = reinterpret_cast<PMOUNTDEV_UNIQUE_ID>(irp->AssociatedIrp.SystemBuffer);
		if (!NT_SUCCESS(validateOutputBufferLength(irp, outputBufLen, deviceUID.Length + sizeof(USHORT)))) {
			if (outputBufLen >= sizeof(MOUNTDEV_UNIQUE_ID)) {
				mntDevUID->UniqueIdLength = deviceUID.Length + sizeof(USHORT);
				irp->IoStatus.Information = sizeof(MOUNTDEV_UNIQUE_ID);
				COMPLETE_IRP_WITH_STATUS(STATUS_BUFFER_OVERFLOW);
			} else {
				COMPLETE_IRP_WITH_STATUS(STATUS_INVALID_PARAMETER);
			}
		}
		RtlCopyBytes((PCHAR)mntDevUID->UniqueId, deviceUID.Buffer, deviceUID.Length);
		mntDevUID->UniqueIdLength = deviceUID.Length;
		irp->IoStatus.Information = deviceUID.Length + sizeof(USHORT);
		break;

	default:
		TRACE("StorageDevice::Handling a non supported IOCTL - %lu\n", stack->Parameters.DeviceIoControl.IoControlCode);
		COMPLETE_IRP_WITH_STATUS(STATUS_NOT_SUPPORTED);
	}

	COMPLETE_IRP_WITH_STATUS(STATUS_SUCCESS);

cleanup:
	completeRequest(deviceObject, irp, &status);
	return status;
}
