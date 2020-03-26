#include <ntifs.h>
#include <ntddk.h>
#include "StorageDevice.h"
#include "macros.h"
#include "deviceControl.h"
#include "common.h"
#include "globals.h"

DEFINE_GUID(GUID_DEVINTERFACE_DISK, 0x53f56307L, 0xb6bf, 0x11d0, 0x94, 0xf2, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b);
DEFINE_GUID(GUID_DEVINTERFACE_VOLUME, 0x53f5630dL, 0xb6bf, 0x11d0, 0x94, 0xf2, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b);

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
		// Update the global pointer to the EndOfFile
		g_eof += bytesToWrite;
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

NTSTATUS handleQueryInfo(PDEVICE_OBJECT deviceObject, PIRP irp) {
	TRACE("StorageDevice::handleQueryInfo\n");
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
	UINT64 outputBufLen = stack->Parameters.DeviceIoControl.OutputBufferLength;
	UNICODE_STRING fileName = RTL_CONSTANT_STRING(L"\\");
	PFILE_NAME_INFORMATION fileNameInfo = NULL;
	PFILE_STANDARD_INFORMATION fileStandardInfo = NULL;
	PFILE_BASIC_INFORMATION fileBasicInfo = NULL;
	switch (stack->Parameters.QueryFile.FileInformationClass) {

	case FileNameInformation:
		CHECK_STATUS(validateOutputBufferLength(irp, outputBufLen, fileName.Length + sizeof(ULONG)));
		fileNameInfo = reinterpret_cast<PFILE_NAME_INFORMATION>(irp->AssociatedIrp.SystemBuffer);
		fileNameInfo->FileNameLength = fileName.Length;
		RtlCopyMemory((PCHAR)fileNameInfo, fileName.Buffer, fileName.Length);
		irp->IoStatus.Information = fileName.Length + sizeof(ULONG);
		break;

	case FileStandardInformation:
		CHECK_STATUS(validateOutputBufferLength(irp, outputBufLen, sizeof(FILE_STANDARD_INFORMATION)));
		fileStandardInfo = reinterpret_cast<PFILE_STANDARD_INFORMATION>(irp->AssociatedIrp.SystemBuffer);
		fileStandardInfo->AllocationSize.QuadPart = 512;
		fileStandardInfo->EndOfFile.QuadPart = g_eof;
		fileStandardInfo->NumberOfLinks = 1;
		fileStandardInfo->DeletePending = FALSE;
		fileStandardInfo->Directory = TRUE;
		irp->IoStatus.Information = sizeof(FILE_STANDARD_INFORMATION);
		break;

	case FileBasicInformation:
		CHECK_STATUS(validateOutputBufferLength(irp, outputBufLen, sizeof(FILE_BASIC_INFORMATION)));
		fileBasicInfo = reinterpret_cast<PFILE_BASIC_INFORMATION>(irp->AssociatedIrp.SystemBuffer);
		fileBasicInfo->CreationTime.QuadPart = 0;
		fileBasicInfo->LastAccessTime.QuadPart = 0;
		fileBasicInfo->LastWriteTime.QuadPart = 0;
		fileBasicInfo->ChangeTime.QuadPart = 0;
		fileBasicInfo->FileAttributes = FILE_ATTRIBUTE_DIRECTORY;
		irp->IoStatus.Information = sizeof(FILE_BASIC_INFORMATION);
		break;

	default:
		TRACE("StorageDevice::Unsupprted Information type - %lu\n", stack->Parameters.QueryFile.FileInformationClass);
		COMPLETE_IRP_WITH_STATUS(STATUS_NOT_SUPPORTED);
	}

	COMPLETE_IRP_WITH_STATUS(STATUS_SUCCESS);

cleanup:
	completeRequest(deviceObject, irp, &status);
	return status;

}

NTSTATUS handleDirCtrl(PDEVICE_OBJECT deviceObject, PIRP irp) {
	TRACE("StorageDevice::handleDirCtrl\n");
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
	PFILE_ID_BOTH_DIR_INFORMATION fileIdBothDirInfo = NULL;
	UNICODE_STRING fileName = RTL_CONSTANT_STRING(L".");
	if (stack->MinorFunction == IRP_MN_NOTIFY_CHANGE_DIRECTORY) {
		TRACE("StorageDevice::IRP_MN_NOTIFY_CHANGE_DIRECTORY is not supported\n");
			COMPLETE_IRP_WITH_STATUS(STATUS_NOT_SUPPORTED);
	}
	
	if (stack->Parameters.QueryDirectory.FileInformationClass != FileIdBothDirectoryInformation) {
		TRACE("StorageDevice::Unsupported FileInformationClass - %lu\n", stack->Parameters.QueryDirectory.FileInformationClass);
		COMPLETE_IRP_WITH_STATUS(STATUS_NOT_SUPPORTED);
	}

	if (stack->Flags == SL_RESTART_SCAN || stack->Flags == SL_RETURN_SINGLE_ENTRY) {
		fileIdBothDirInfo = reinterpret_cast<PFILE_ID_BOTH_DIR_INFORMATION>(irp->AssociatedIrp.SystemBuffer);
		fileIdBothDirInfo->NextEntryOffset = 0;
		fileIdBothDirInfo->FileIndex = 0;
		fileIdBothDirInfo->CreationTime.QuadPart = 0;
		fileIdBothDirInfo->LastAccessTime.QuadPart = 0;
		fileIdBothDirInfo->LastWriteTime.QuadPart = 0;
		fileIdBothDirInfo->ChangeTime.QuadPart = 0;
		fileIdBothDirInfo->EndOfFile.QuadPart = 1;
		fileIdBothDirInfo->AllocationSize.QuadPart = 512;
		fileIdBothDirInfo->FileAttributes = FILE_ATTRIBUTE_DIRECTORY;
		fileIdBothDirInfo->FileNameLength = fileName.Length;
		fileIdBothDirInfo->EaSize = 0;
		fileIdBothDirInfo->ShortNameLength = fileName.Length;
		RtlCopyMemory((PCHAR)fileIdBothDirInfo->ShortName, fileName.Buffer, fileName.Length);
		fileIdBothDirInfo->FileId.QuadPart = 0;
		RtlCopyMemory((PCHAR)fileIdBothDirInfo->FileName, fileName.Buffer, fileName.Length);
		irp->IoStatus.Information = sizeof(FILE_ID_BOTH_DIR_INFORMATION);
	}

	
	RETURN_STATUS(STATUS_SUCCESS);

cleanup:
	completeRequest(deviceObject, irp, &status);
	return status;

}

NTSTATUS handleQueryVolInfo(PDEVICE_OBJECT deviceObject, PIRP irp) {
	TRACE("StorageDevice::handleQueryVolInfo\n");
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
	UINT64 outputBufLen = stack->Parameters.DeviceIoControl.OutputBufferLength;
	PFILE_FS_VOLUME_INFORMATION fsVolInfo = NULL;
	UNICODE_STRING volLabel = RTL_CONSTANT_STRING(VOL_LABEL);
	PFILE_FS_ATTRIBUTE_INFORMATION fsAttrInfo = NULL;
	UNICODE_STRING fsName = RTL_CONSTANT_STRING(L"RAW");
	PFILE_FS_FULL_SIZE_INFORMATION fsFullSizeInfo = NULL;
	PFILE_FS_SIZE_INFORMATION fsSizeInfo = NULL;
	switch (stack->Parameters.QueryVolume.FsInformationClass) {
	case FileFsVolumeInformation:
		CHECK_STATUS(validateOutputBufferLength(irp, outputBufLen, FIELD_OFFSET(FILE_FS_VOLUME_INFORMATION, VolumeLabel) + volLabel.Length));
		fsVolInfo = reinterpret_cast<PFILE_FS_VOLUME_INFORMATION>(irp->AssociatedIrp.SystemBuffer);
		fsVolInfo->VolumeCreationTime.QuadPart = 0;
		fsVolInfo->VolumeSerialNumber = 0;
		fsVolInfo->VolumeLabelLength = volLabel.Length;
		fsVolInfo->SupportsObjects = TRUE;
		RtlCopyMemory((PCHAR)fsVolInfo->VolumeLabel, volLabel.Buffer, volLabel.Length);
		irp->IoStatus.Information = FIELD_OFFSET(FILE_FS_VOLUME_INFORMATION, VolumeLabel) + volLabel.Length;
		break;

	case FileFsAttributeInformation:
		CHECK_STATUS(validateOutputBufferLength(irp, outputBufLen, FIELD_OFFSET(FILE_FS_ATTRIBUTE_INFORMATION, FileSystemName) + fsName.Length));
		fsAttrInfo = reinterpret_cast<PFILE_FS_ATTRIBUTE_INFORMATION>(irp->AssociatedIrp.SystemBuffer);
		fsAttrInfo->FileSystemAttributes = 0;
		fsAttrInfo->MaximumComponentNameLength = 10;
		fsAttrInfo->FileSystemNameLength = fsName.Length;
		RtlCopyMemory((PCHAR)fsAttrInfo->FileSystemName, fsName.Buffer, fsName.Length);
		irp->IoStatus.Information = FIELD_OFFSET(FILE_FS_ATTRIBUTE_INFORMATION, FileSystemName) + fsName.Length;
		break;

	case FileFsFullSizeInformation:
		CHECK_STATUS(validateOutputBufferLength(irp, outputBufLen, sizeof(FILE_FS_FULL_SIZE_INFORMATION)));
		fsFullSizeInfo = reinterpret_cast<PFILE_FS_FULL_SIZE_INFORMATION>(irp->AssociatedIrp.SystemBuffer);
		fsFullSizeInfo->TotalAllocationUnits.QuadPart = STORAGE_SIZE / 512;
		fsFullSizeInfo->CallerAvailableAllocationUnits.QuadPart = STORAGE_SIZE / 512;
		fsFullSizeInfo->ActualAvailableAllocationUnits.QuadPart = STORAGE_SIZE / 512;
		fsFullSizeInfo->SectorsPerAllocationUnit = 1;
		fsFullSizeInfo->BytesPerSector = 512;
		irp->IoStatus.Information = sizeof(FILE_FS_FULL_SIZE_INFORMATION);
		break;

	case FileFsSizeInformation:
		CHECK_STATUS(validateOutputBufferLength(irp, outputBufLen, sizeof(FILE_FS_SIZE_INFORMATION)));
		fsSizeInfo = reinterpret_cast<PFILE_FS_SIZE_INFORMATION>(irp->AssociatedIrp.SystemBuffer);
		fsSizeInfo->TotalAllocationUnits.QuadPart = STORAGE_SIZE / 512;
		fsSizeInfo->AvailableAllocationUnits.QuadPart = STORAGE_SIZE / 512;
		fsSizeInfo->SectorsPerAllocationUnit = 1;
		fsSizeInfo->BytesPerSector = 512;
		irp->IoStatus.Information = sizeof(FILE_FS_SIZE_INFORMATION);
		break;

	default:
		TRACE("StorageDevice::Unsupported FsInformationClass - %lu\n", stack->Parameters.QueryVolume.FsInformationClass);
		COMPLETE_IRP_WITH_STATUS(STATUS_NOT_SUPPORTED);
		break;
	}
	
	COMPLETE_IRP_WITH_STATUS(STATUS_SUCCESS);

cleanup:
	completeRequest(deviceObject, irp, &status);
	return status;
}

NTSTATUS handleFSCtrl(PDEVICE_OBJECT deviceObject, PIRP irp) {
	TRACE("StorageDevice::handleFSCtrl\n");
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
	COMPLETE_IRP_WITH_STATUS(STATUS_INVALID_DEVICE_REQUEST);

cleanup:
	completeRequest(deviceObject, irp, &status);
	return status;
}

EXTERN_C
NTSTATUS DriverEntry(PDRIVER_OBJECT driverObject, PUNICODE_STRING) {
	TRACE("StorageDevice::DriverEntry\n");
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	UNICODE_STRING deviceName = RTL_CONSTANT_STRING(DEVICE_NAME);
	UNICODE_STRING deviceSymLink = RTL_CONSTANT_STRING(SYMBOLIC_LINK);
	UNICODE_STRING interfaceSymLink;
	//UNICODE_STRING interfaceSymLink2;
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
	driverObject->MajorFunction[IRP_MJ_CLEANUP] = completeOrPassToLower;

	//driverObject->MajorFunction[IRP_MJ_QUERY_INFORMATION] = handleQueryInfo;
	driverObject->MajorFunction[IRP_MJ_DIRECTORY_CONTROL] = handleDirCtrl;
	//driverObject->MajorFunction[IRP_MJ_QUERY_VOLUME_INFORMATION] = handleQueryVolInfo;
	//driverObject->MajorFunction[IRP_MJ_FILE_SYSTEM_CONTROL] = handleFSCtrl;

	// Create to storage memory
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

	// Register to the GUID_DEVINTERFACE_DISK interface
	/*CHECK_STATUS(IoRegisterDeviceInterface(g_pdo, &GUID_DEVINTERFACE_DISK, NULL, &interfaceSymLink2));
	CHECK_STATUS(IoSetDeviceInterfaceState(&interfaceSymLink2, TRUE));*/

	// Register to the MOUNTDEV_MOUNTED_DEVICE_GUID interface
	CHECK_STATUS(IoRegisterDeviceInterface(g_pdo, &MOUNTDEV_MOUNTED_DEVICE_GUID, NULL, &interfaceSymLink));
	reinterpret_cast<PCOMMON_DEVICE_EXTENSION>(g_pdo->DeviceExtension)->MountedDeviceInterfaceName = interfaceSymLink;
	CHECK_STATUS(IoSetDeviceInterfaceState(&interfaceSymLink, TRUE));
	RETURN_STATUS(STATUS_SUCCESS);

cleanup:
	if (interfaceSymLink.Buffer != NULL) {
		RtlFreeUnicodeString(&interfaceSymLink);
	}
	/*if (interfaceSymLink2.Buffer != NULL) {
		RtlFreeUnicodeString(&interfaceSymLink2);
	}*/

	if (!NT_SUCCESS(status)) {
		TRACE("StorageDevice::DriverEntry failed with status %lu\n", status);
	}
	return status;
}
