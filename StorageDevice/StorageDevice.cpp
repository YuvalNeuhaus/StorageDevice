#include <wdm.h>
#include <ntdddisk.h>
#include <initguid.h>
#include <mountmgr.h>
#include <mountdev.h>
#include "FAT32Sectors.h"

#define CHECK_STATUS(retval) status=retval; \
							 if (!NT_SUCCESS(status)) goto cleanup;
#define TRACE(...) DbgPrintEx(DPFLTR_IHVDRIVER_ID, 0xFFFFFFFF, __VA_ARGS__)
#define RETURN_STATUS(ret) status=ret;goto cleanup;

#define STORAGE_SIZE (1024*1024*50)
#define SECTOR_SIZE (512)
#define CYLINDERS_NUM (242)
#define SECTORS_PER_TRACK (63)
#define TRACKS_PER_CYLINDER (255)
#define POOL_TAG (0)
#define DEVICE_NAME L"\\Device\\MyStorageDevice"
#define DEVICE_UID L"BlaBlaUniqueID"

PDEVICE_OBJECT g_deviceObject = NULL;
PCHAR g_storage = NULL;
BOOLEAN g_mounted = FALSE;


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
	//TODO:Unallocate the storage
	
}

NTSTATUS handleOperationSuccess(PDEVICE_OBJECT deviceObject, PIRP irp) {
	UNREFERENCED_PARAMETER(deviceObject);
	TRACE("StorageDevice::handleOperationSuccess\n");
	irp->IoStatus.Information = 0;
	irp->IoStatus.Status = STATUS_SUCCESS;
	IoCompleteRequest(irp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
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
		if (userBuffer == NULL) {
			TRACE("StorageDevice::Address from MDL is NULL\n");
			irp->IoStatus.Information = 0;
			irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
			RETURN_STATUS(STATUS_INVALID_PARAMETER);
		}
	} else {
		TRACE("StorageDevice::Received an IRP that uses NEITHER_IO which is not supported\n");
		irp->IoStatus.Information = 0;
		irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
		RETURN_STATUS(STATUS_NOT_SUPPORTED);
	}
	

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

	PVOID dst = isWrite ? &g_storage[offset] : userBuffer;
	PVOID src = isWrite ? userBuffer : &g_storage[offset];
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
	RETURN_STATUS(readWrite(irp, FALSE));

cleanup:
	IoCompleteRequest(irp, IO_NO_INCREMENT);
	return status;
}

NTSTATUS mount() {
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	UNICODE_STRING mountMgrName;
	PFILE_OBJECT fileObject = NULL;
	PDEVICE_OBJECT mountMgrDev = NULL;
	KEVENT event;
	IO_STATUS_BLOCK iosb;
	PIRP newIrp;
	UNICODE_STRING deviceName = RTL_CONSTANT_STRING(DEVICE_NAME);
	PMOUNTMGR_TARGET_NAME mountPoint = NULL;

	if (g_mounted) {
		RETURN_STATUS(STATUS_SUCCESS);
	}

	
	UINT32 mountPointSize = sizeof(MOUNTMGR_TARGET_NAME) + deviceName.Length - 1;
	mountPoint = reinterpret_cast<PMOUNTMGR_TARGET_NAME>(ExAllocatePoolWithTag(NonPagedPool, mountPointSize, POOL_TAG));
	mountPoint->DeviceNameLength = deviceName.Length;
	RtlCopyMemory((PCHAR)mountPoint->DeviceName, deviceName.Buffer, deviceName.Length);
	mountPoint->DeviceName[deviceName.Length - 1] = 0;
	RtlInitUnicodeString(&mountMgrName, MOUNTMGR_DEVICE_NAME);
	CHECK_STATUS(IoGetDeviceObjectPointer(&mountMgrName, FILE_READ_ATTRIBUTES, &fileObject, &mountMgrDev));
	// announce the volume's arrival to Mount-manager
	KeInitializeEvent(&event, NotificationEvent, FALSE);
	newIrp = IoBuildDeviceIoControlRequest(IOCTL_MOUNTMGR_VOLUME_ARRIVAL_NOTIFICATION,
										   mountMgrDev,
										   mountPoint,
										   mountPointSize,
										   NULL,
										   0,
										   FALSE,
										   &event,
										   &iosb);
	if (newIrp == NULL) {
		TRACE("StorageDevice::IoBuildDeviceIoControlRequest failed with status %lu\n", status);
		RETURN_STATUS(STATUS_INSUFFICIENT_RESOURCES);
	}

	g_mounted = TRUE;
	status = IoCallDriver(mountMgrDev, newIrp);

	if (status == STATUS_PENDING) {
		status = KeWaitForSingleObject(&event,
									   Executive,
									   KernelMode,
									   FALSE,
									   NULL);
	}
	if (!NT_SUCCESS(status)) {
		TRACE("StorageDevice::IoCallDriver failed with status %lu\n", status);
		RETURN_STATUS(status);
	}
	
	RETURN_STATUS(STATUS_SUCCESS);

cleanup:
	return status;
}

NTSTATUS handleWrite(PDEVICE_OBJECT deviceObject, PIRP irp) {
	UNREFERENCED_PARAMETER(deviceObject);
	TRACE("StorageDevice::handleWrite\n");
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	//if (g_mounted) {
//		RETURN_STATUS(handleOperationSuccess(deviceObject, irp));
//	}
	//CHECK_STATUS(mount());
	RETURN_STATUS(readWrite(irp, TRUE));

cleanup:
	IoCompleteRequest(irp, IO_NO_INCREMENT);
	return status;
}

NTSTATUS handleCreate(PDEVICE_OBJECT deviceObject, PIRP irp) {
	TRACE("StorageDevice::handleCreate\n");
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	//if (g_mounted) {
	RETURN_STATUS(handleOperationSuccess(deviceObject, irp));
//	}
	//RETURN_STATUS(mount());

cleanup:
	IoCompleteRequest(irp, IO_NO_INCREMENT);
	return status;
}

NTSTATUS validateOutputBufferLength(PIRP irp, UINT64 outputBufferLen, UINT64 sizeNeeded) {
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	if (outputBufferLen < sizeNeeded) {
		TRACE("StorageDevice::OutputBufferLength is too small. Got %llu, expected %llu\n", outputBufferLen, sizeNeeded);
		irp->IoStatus.Information = 0;
		irp->IoStatus.Status = STATUS_BUFFER_TOO_SMALL;
		RETURN_STATUS(STATUS_BUFFER_TOO_SMALL);
	}

	RETURN_STATUS(STATUS_SUCCESS);

cleanup:
	return status;
}

NTSTATUS handleIoCtl(PDEVICE_OBJECT deviceObject, PIRP irp) {
	UNREFERENCED_PARAMETER(deviceObject);
	TRACE("StorageDevice::handleCreate\n");
	
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	PDISK_GEOMETRY diskGeo = NULL;
	PMOUNTDEV_NAME mntDevName = NULL;
	PMOUNTDEV_UNIQUE_ID mntDevUID = NULL;
	PSTORAGE_HOTPLUG_INFO hotplugInfo = NULL;
	PSTORAGE_PROPERTY_QUERY propQuery = NULL;
	PSTORAGE_DEVICE_DESCRIPTOR storageDevDescriptor = NULL;
	PPARTITION_INFORMATION_EX partInfo = NULL;
	//PSTORAGE_DESCRIPTOR_HEADER descriptorHeader = NULL;
	UNICODE_STRING deviceName;
	UNICODE_STRING deviceUID;
	UINT64 diskSize = 0;
	PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
	UINT64 outputBufLen = stack->Parameters.DeviceIoControl.OutputBufferLength;
	if (irp->AssociatedIrp.SystemBuffer == NULL && stack->Parameters.DeviceIoControl.IoControlCode != IOCTL_DISK_IS_WRITABLE) {
		TRACE("StorageDevice::SystemBuffer is NULL\n");
		__debugbreak();
	}

	switch (stack->Parameters.DeviceIoControl.IoControlCode) {
	case IOCTL_DISK_GET_LENGTH_INFO:
		TRACE("StorageDevice::Handling IOCTL_DISK_GET_LENGTH_INFO\n");
		CHECK_STATUS(validateOutputBufferLength(irp, outputBufLen, sizeof(UINT64)));
		diskSize = STORAGE_SIZE;
		RtlCopyBytes(irp->AssociatedIrp.SystemBuffer, &diskSize, sizeof(UINT64));
		irp->IoStatus.Information = sizeof(UINT64);
		break;

	case IOCTL_DISK_GET_DRIVE_GEOMETRY:
		TRACE("StorageDevice::Handling IOCTL_DISK_GET_DRIVE_GEOMETRY\n");
		CHECK_STATUS(validateOutputBufferLength(irp, outputBufLen, sizeof(DISK_GEOMETRY)));
		diskGeo = reinterpret_cast<PDISK_GEOMETRY>(irp->AssociatedIrp.SystemBuffer);
		diskGeo->BytesPerSector = SECTOR_SIZE;
		diskGeo->Cylinders.QuadPart = CYLINDERS_NUM;
		diskGeo->MediaType = FixedMedia;
		diskGeo->SectorsPerTrack = SECTORS_PER_TRACK;
		diskGeo->TracksPerCylinder = TRACKS_PER_CYLINDER;
		irp->IoStatus.Information = sizeof(DISK_GEOMETRY);
		break;

	case IOCTL_DISK_CHECK_VERIFY:
	case IOCTL_DISK_IS_WRITABLE:
		TRACE("StorageDevice::Handling IOCTL_DISK_IS_WRITABLE\n");
		irp->IoStatus.Information = 0;
		break;

	case IOCTL_MOUNTDEV_QUERY_DEVICE_NAME:
		TRACE("StorageDevice::Handling IOCTL_MOUNTDEV_QUERY_DEVICE_NAME\n");
		deviceName = RTL_CONSTANT_STRING(DEVICE_NAME);
		mntDevName = reinterpret_cast<PMOUNTDEV_NAME>(irp->AssociatedIrp.SystemBuffer);
		if (!NT_SUCCESS(validateOutputBufferLength(irp, outputBufLen, deviceName.Length + sizeof(USHORT)))) {
			if (outputBufLen >= sizeof(MOUNTDEV_NAME)) {
				mntDevName->NameLength = deviceName.Length + sizeof(USHORT);
				irp->IoStatus.Information = sizeof(MOUNTDEV_NAME);
				irp->IoStatus.Status = STATUS_BUFFER_OVERFLOW;
				RETURN_STATUS(STATUS_BUFFER_OVERFLOW);
			} else {
				irp->IoStatus.Information = 0;
				irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
				RETURN_STATUS(STATUS_INVALID_PARAMETER);
			}
		}
		RtlCopyBytes((PCHAR)mntDevName->Name, deviceName.Buffer, deviceName.Length);
		mntDevName->NameLength = deviceName.Length;
		irp->IoStatus.Information = deviceName.Length + sizeof(USHORT);
		break;

	case IOCTL_MOUNTDEV_QUERY_UNIQUE_ID:
		TRACE("StorageDevice::Handling IOCTL_MOUNTDEV_QUERY_UNIQUE_ID\n");
		deviceUID = RTL_CONSTANT_STRING(DEVICE_UID);
		mntDevUID = reinterpret_cast<PMOUNTDEV_UNIQUE_ID>(irp->AssociatedIrp.SystemBuffer);
		if (!NT_SUCCESS(validateOutputBufferLength(irp, outputBufLen, deviceUID.Length + sizeof(USHORT)))) {
			if (outputBufLen >= sizeof(MOUNTDEV_UNIQUE_ID)) {
				mntDevUID->UniqueIdLength = deviceUID.Length + sizeof(USHORT);
				irp->IoStatus.Information = sizeof(MOUNTDEV_UNIQUE_ID);
				irp->IoStatus.Status = STATUS_BUFFER_OVERFLOW;
				RETURN_STATUS(STATUS_BUFFER_OVERFLOW);
			} else {
				irp->IoStatus.Information = 0;
				irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
				RETURN_STATUS(STATUS_INVALID_PARAMETER);
			}
		}
		RtlCopyBytes((PCHAR)mntDevUID->UniqueId, deviceUID.Buffer, deviceUID.Length);
		mntDevUID->UniqueIdLength = deviceUID.Length;
		irp->IoStatus.Information = deviceUID.Length + sizeof(USHORT);
		break;

	case IOCTL_STORAGE_GET_HOTPLUG_INFO:
		TRACE("StorageDevice::Handling IOCTL_STORAGE_GET_HOTPLUG_INFO\n");
		CHECK_STATUS(validateOutputBufferLength(irp, outputBufLen, sizeof(STORAGE_HOTPLUG_INFO)));
		hotplugInfo = reinterpret_cast<PSTORAGE_HOTPLUG_INFO>(irp->AssociatedIrp.SystemBuffer);
		hotplugInfo->Size = sizeof(STORAGE_HOTPLUG_INFO);
		hotplugInfo->DeviceHotplug = FALSE;
		hotplugInfo->MediaHotplug = 1; // Non Lockable
		hotplugInfo->MediaRemovable = FALSE;
		hotplugInfo->WriteCacheEnableOverride = NULL;
		irp->IoStatus.Information = sizeof(STORAGE_HOTPLUG_INFO);
		break;

	case IOCTL_STORAGE_QUERY_PROPERTY:
		TRACE("StorageDevice::Handling IOCTL_STORAGE_QUERY_PROPERTY\n");
		CHECK_STATUS(validateOutputBufferLength(irp, outputBufLen, sizeof(STORAGE_DEVICE_DESCRIPTOR)));
		//if (outputBufLen == sizeof(STORAGE_DESCRIPTOR_HEADER)) {
//			descriptorHeader = reinterpret_cast<PSTORAGE_DESCRIPTOR_HEADER>(irp->AssociatedIrp.SystemBuffer);
			//descriptorHeader->Version = 1;
			//descriptorHeader->Size = 
		//}
		propQuery = reinterpret_cast<PSTORAGE_PROPERTY_QUERY>(irp->AssociatedIrp.SystemBuffer);
		switch (propQuery->PropertyId) {
		case StorageDeviceProperty:
			if (propQuery->QueryType == PropertyExistsQuery) {
				irp->IoStatus.Information = 0;
			} else {
				storageDevDescriptor = reinterpret_cast<PSTORAGE_DEVICE_DESCRIPTOR>(irp->AssociatedIrp.SystemBuffer);
				RtlZeroMemory(storageDevDescriptor, sizeof(STORAGE_DEVICE_DESCRIPTOR));
				storageDevDescriptor->Version = sizeof(STORAGE_DEVICE_DESCRIPTOR);
				storageDevDescriptor->Size = sizeof(STORAGE_DEVICE_DESCRIPTOR);
				storageDevDescriptor->DeviceType = FILE_DEVICE_DISK;
				storageDevDescriptor->DeviceTypeModifier = 0;
				storageDevDescriptor->RemovableMedia = FALSE;
				storageDevDescriptor->CommandQueueing = FALSE;
				storageDevDescriptor->VendorIdOffset = 0;
				storageDevDescriptor->ProductIdOffset = 0;
				storageDevDescriptor->ProductRevisionOffset = 0;
				storageDevDescriptor->SerialNumberOffset = 0;
				storageDevDescriptor->BusType = BusTypeVirtual;
				storageDevDescriptor->RawPropertiesLength = 0;
				irp->IoStatus.Information = sizeof(STORAGE_DEVICE_DESCRIPTOR);
			}
			break;

		default:
			TRACE("StorageDevice::Property ID not supported - %lu (%s)\n", propQuery->PropertyId,
																		   propQuery->QueryType ? "PropertyExistsQuery" : "PropertyStandardQuery");
			irp->IoStatus.Information = 0;
			irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
			RETURN_STATUS(STATUS_NOT_SUPPORTED);
		}

		break;

	case IOCTL_DISK_GET_PARTITION_INFO_EX:
		TRACE("StorageDevice::Handling IOCTL_DISK_GET_PARTITION_INFO_EX\n");
		CHECK_STATUS(validateOutputBufferLength(irp, outputBufLen, sizeof(PARTITION_INFORMATION_EX)));
		partInfo = reinterpret_cast<PPARTITION_INFORMATION_EX>(irp->AssociatedIrp.SystemBuffer);
		partInfo->PartitionStyle = PARTITION_STYLE_RAW;
		partInfo->StartingOffset.QuadPart = 0;
		partInfo->PartitionLength.QuadPart = STORAGE_SIZE;
		partInfo->PartitionNumber = 1;
		partInfo->RewritePartition = TRUE;
		partInfo->Mbr.PartitionType = PARTITION_FAT32;
		partInfo->Mbr.BootIndicator = FALSE;
		partInfo->Mbr.RecognizedPartition = TRUE;
		partInfo->Mbr.HiddenSectors = 0;
		irp->IoStatus.Information = sizeof(PARTITION_INFORMATION_EX);
		break;

	default:
		TRACE("StorageDevice::Handling a non supported IOCTL - %lu\n", stack->Parameters.DeviceIoControl.IoControlCode);
		irp->IoStatus.Information = 0;
		irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
		RETURN_STATUS(STATUS_NOT_SUPPORTED);
	}

	irp->IoStatus.Status = STATUS_SUCCESS;
	RETURN_STATUS(STATUS_SUCCESS);

cleanup:
	IoCompleteRequest(irp, IO_NO_INCREMENT);
	return status;
}
/*
NTSTATUS DriverAddDevice(PDRIVER_OBJECT driverObject, PDEVICE_OBJECT physicalDeviceObject) {
	UNREFERENCED_PARAMETER(driverObject);
	UNREFERENCED_PARAMETER(physicalDeviceObject);
	TRACE("StorageDevice::DriverAddDevice\n");
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	RETURN_STATUS(STATUS_SUCCESS);
cleanup:
	return status;
}
*/

NTSTATUS handleUnsupporeted(PDEVICE_OBJECT deviceObject, PIRP irp) {
	UNREFERENCED_PARAMETER(deviceObject);
	PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
	TRACE("StorageDevice::handleUnsupporeted - %lu, %lu\n", stack->MajorFunction, stack->MinorFunction);
	irp->IoStatus.Information = 0;
	irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
	return STATUS_NOT_SUPPORTED;
}

EXTERN_C
NTSTATUS DriverEntry(PDRIVER_OBJECT driverObject, PUNICODE_STRING) {
	UNREFERENCED_PARAMETER(driverObject);
	TRACE("StorageDevice::DriverEntry\n");

	NTSTATUS status = STATUS_UNSUCCESSFUL;

	for (UINT32 i = 0; i < IRP_MJ_MAXIMUM_FUNCTION; i++) {
		driverObject->MajorFunction[i] = handleUnsupporeted;
	}

	driverObject->DriverUnload = driverUnload;
	driverObject->MajorFunction[IRP_MJ_READ] = handleRead;
	driverObject->MajorFunction[IRP_MJ_WRITE] = handleWrite;
	driverObject->MajorFunction[IRP_MJ_CREATE] = handleCreate;
	driverObject->MajorFunction[IRP_MJ_CLOSE] = handleOperationSuccess;
	driverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = handleIoCtl;
	driverObject->MajorFunction[IRP_MJ_FLUSH_BUFFERS] = handleOperationSuccess;
//	driverObject->DriverExtension->AddDevice = DriverAddDevice;


	UNICODE_STRING deviceName = RTL_CONSTANT_STRING(DEVICE_NAME);
	UNICODE_STRING deviceSymLink = RTL_CONSTANT_STRING(L"\\DosDevices\\MyStorageDeviceSymLink");
//	UNICODE_STRING symLink;
//	RtlZeroMemory(&symLink, sizeof(UNICODE_STRING));

	g_storage = reinterpret_cast<PCHAR>(ExAllocatePoolWithTag(NonPagedPool, STORAGE_SIZE, POOL_TAG));
	if (g_storage == NULL) {
		TRACE("StorageDevice::ExAllocatePoolWithTag for g_storage failed\n");
		RETURN_STATUS(STATUS_INSUFFICIENT_RESOURCES);
	}
	RtlZeroMemory(g_storage, STORAGE_SIZE);
	/*
	PCHAR sector = reinterpret_cast<PCHAR>(ExAllocatePoolWithTag(NonPagedPool, SECTOR_SIZE, POOL_TAG));
	if (g_storage == NULL) {
		TRACE("StorageDevice::ExAllocatePoolWithTag for sector failed\n");
		RETURN_STATUS(STATUS_INSUFFICIENT_RESOURCES);
	}
	sector = FAT32_BOOT_SECTOR;
	RtlCopyMemory(&g_storage[0 * SECTOR_SIZE], sector, SECTOR_SIZE);
	sector = FAT32_TABLE;
	RtlCopyMemory(&g_storage[0x1A3E * SECTOR_SIZE], sector, SECTOR_SIZE);
	RtlCopyMemory(&g_storage[0x1D1F * SECTOR_SIZE], sector, SECTOR_SIZE);
	sector = FAT32_ROOT_DIR;
	RtlCopyMemory(&g_storage[0x2000 * SECTOR_SIZE], sector, SECTOR_SIZE);
	*/

	CHECK_STATUS(IoCreateDevice(driverObject,
								//sizeof(DEVOBJ_EXTENSION),
								0,
								&deviceName,
								//NULL,
								FILE_DEVICE_DISK,
								FILE_DEVICE_SECURE_OPEN,
								FALSE,
								&g_deviceObject));
	CHECK_STATUS(IoCreateSymbolicLink(&deviceSymLink, &deviceName));

	//DEVOBJ_EXTENSION deviceExtension;
	//g_deviceObject->DeviceObjectExtension->DeviceNode
	//GUID guid = MOUNTDEV_MOUNTED_DEVICE_GUID;
	//CHECK_STATUS(IoRegisterDeviceInterface(g_deviceObject, &guid, NULL, &symLink));

	g_deviceObject->Flags |= DO_BUFFERED_IO;
	g_deviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
	RETURN_STATUS(STATUS_SUCCESS);

cleanup:
	//if (symLink.Buffer != NULL && symLink.Length != 0) {
//		RtlFreeUnicodeString(&symLink);
//	}
	if (!NT_SUCCESS(status)) {
		TRACE("StorageDevice::DriverEntry failed with status %lu\n", status);
	}
	return status;
}