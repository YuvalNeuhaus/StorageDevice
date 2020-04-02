#include <ntifs.h>
#include <ntddk.h>
#include "dispatchRoutines.h"
#include "macros.h"
#include "StorageDevice.h"
#include "globals.h"
#include "common.h"

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
	TRACE_FUNC;
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	RETURN_STATUS(readWrite(irp, FALSE));

cleanup:
	IoCompleteRequest(irp, IO_NO_INCREMENT);
	return status;
}

NTSTATUS handleWrite(PDEVICE_OBJECT deviceObject, PIRP irp) {
	UNREFERENCED_PARAMETER(deviceObject);
	TRACE_FUNC;
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	RETURN_STATUS(readWrite(irp, TRUE));

cleanup:
	IoCompleteRequest(irp, IO_NO_INCREMENT);
	return status;
}

NTSTATUS handleUnsupporeted(PDEVICE_OBJECT deviceObject, PIRP irp) {
	UNREFERENCED_PARAMETER(deviceObject);
	PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
	TRACE("StorageDevice::%s - %lu, %lu\n", __FUNCTION__, stack->MajorFunction, stack->MinorFunction);
	irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
	return STATUS_NOT_SUPPORTED;
}

NTSTATUS handleQueryInfo(PDEVICE_OBJECT deviceObject, PIRP irp) {
	UNREFERENCED_PARAMETER(deviceObject);
	TRACE_FUNC;
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
	UINT64 outputBufLen = stack->Parameters.QueryFile.Length;
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
		irp->IoStatus.Information = FIELD_OFFSET(FILE_NAME_INFORMATION, FileName) + fileName.Length;
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
		COMPLETE_IRP_WITH_STATUS(STATUS_INVALID_DEVICE_REQUEST);
	}

	COMPLETE_IRP_WITH_STATUS(STATUS_SUCCESS);

cleanup:
	IoCompleteRequest(irp, IO_NO_INCREMENT);
	return status;

}

NTSTATUS handleQueryVolInfo(PDEVICE_OBJECT deviceObject, PIRP irp) {
	UNREFERENCED_PARAMETER(deviceObject);
	TRACE_FUNC;
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
	UINT64 outputBufLen = stack->Parameters.DeviceIoControl.OutputBufferLength;
	PFILE_FS_VOLUME_INFORMATION fsVolInfo = NULL;
	UNICODE_STRING volLabel = RTL_CONSTANT_STRING(VOL_LABEL);
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
	IoCompleteRequest(irp, IO_NO_INCREMENT);
	return status;
}

NTSTATUS handleCreate(PDEVICE_OBJECT deviceObject, PIRP irp) {
	UNREFERENCED_PARAMETER(deviceObject);
	TRACE_FUNC;
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	irp->IoStatus.Information = FILE_EXISTS;
	COMPLETE_IRP_WITH_STATUS(STATUS_SUCCESS);

cleanup:
	IoCompleteRequest(irp, IO_NO_INCREMENT);
	return status;

}

NTSTATUS handleClose(PDEVICE_OBJECT deviceObject, PIRP irp) {
	UNREFERENCED_PARAMETER(deviceObject);
	TRACE_FUNC;
	NTSTATUS status = STATUS_SUCCESS;
	COMPLETE_IRP_WITH_STATUS(STATUS_SUCCESS);

cleanup:
	IoCompleteRequest(irp, IO_NO_INCREMENT);
	return status;
}

NTSTATUS handleCleanup(PDEVICE_OBJECT deviceObject, PIRP irp) {
	UNREFERENCED_PARAMETER(deviceObject);
	TRACE_FUNC;
	NTSTATUS status = STATUS_SUCCESS;
	COMPLETE_IRP_WITH_STATUS(STATUS_SUCCESS);

cleanup:
	IoCompleteRequest(irp, IO_NO_INCREMENT);
	return status;
}

NTSTATUS handlePower(PDEVICE_OBJECT deviceObject, PIRP irp) {
	UNREFERENCED_PARAMETER(deviceObject);
	TRACE_FUNC;
	NTSTATUS status = STATUS_SUCCESS;
	COMPLETE_IRP_WITH_STATUS(STATUS_SUCCESS);

	cleanup:
	IoCompleteRequest(irp, IO_NO_INCREMENT);
	return status;
}
