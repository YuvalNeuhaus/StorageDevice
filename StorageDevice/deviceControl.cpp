#include <ntifs.h>
#include <ntddk.h>
#include <ntddvol.h>
#include "deviceControl.h"
#include "macros.h"
#include "StorageDevice.h"
#include "declarations.h"
#include "common.h"
#include "globals.h"

#define SECTOR_SIZE (512)
#define CYLINDERS_NUM (242)
#define SECTORS_PER_TRACK (63)
#define TRACKS_PER_CYLINDER (255)

NTSTATUS handleIoCtl(PDEVICE_OBJECT deviceObject, PIRP irp) {
	UNREFERENCED_PARAMETER(deviceObject);
	TRACE("StorageDevice::handleIoCtl\n");

	NTSTATUS status = STATUS_UNSUCCESSFUL;
	PGET_DISK_ATTRIBUTES diskAttributes = NULL;
	PDISK_GEOMETRY_EX diskGeoEx = NULL;
	PGET_MEDIA_TYPES mediaTypes = NULL;
	PSTORAGE_DEVICE_NUMBER storDevNum = NULL;
	PVOLUME_DISK_EXTENTS volDiskEx = NULL;
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

	
	//case IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS:
	//	TRACE("StorageDevice::Handling IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS\n");
	//	volDiskEx = reinterpret_cast<PVOLUME_DISK_EXTENTS>(irp->AssociatedIrp.SystemBuffer);
	//	volDiskEx->NumberOfDiskExtents = 1;
	//	volDiskEx->Extents[0].DiskNumber = 1;
	//	volDiskEx->Extents[0].StartingOffset.QuadPart = 0;
	//	volDiskEx->Extents[0].ExtentLength.QuadPart = STORAGE_SIZE;
	//	irp->IoStatus.Information = sizeof(VOLUME_DISK_EXTENTS);
	//	break;

	//case IOCTL_STORAGE_GET_DEVICE_NUMBER:
	//	TRACE("StorageDevice::Handling IOCTL_STORAGE_GET_DEVICE_NUMBER\n");
	//	storDevNum = reinterpret_cast<PSTORAGE_DEVICE_NUMBER>(irp->AssociatedIrp.SystemBuffer);
	//	storDevNum->DeviceNumber = 10;
	//	storDevNum->DeviceType = FILE_DEVICE_DISK;
	//	storDevNum->PartitionNumber = 10;
	//	irp->IoStatus.Information = sizeof(STORAGE_DEVICE_NUMBER);
	//	break;

	//case IOCTL_VOLUME_IS_DYNAMIC:
	//	TRACE("StorageDevice::Handling IOCTL_VOLUME_IS_DYNAMIC\n");
	//	COMPLETE_IRP_WITH_STATUS(STATUS_UNSUCCESSFUL);
	//	break;

	//case IOCTL_VOLUME_ONLINE:
	//	TRACE("StorageDevice::Handling IOCTL_VOLUME_ONLINE\n");
	//	COMPLETE_IRP_WITH_STATUS(STATUS_SUCCESS);
	//	break;

	//case IOCTL_STORAGE_GET_MEDIA_TYPES_EX:
	//	TRACE("StorageDevice::Handling IOCTL_STORAGE_GET_MEDIA_TYPES_EX\n");
	//	mediaTypes = reinterpret_cast<PGET_MEDIA_TYPES>(irp->AssociatedIrp.SystemBuffer);
	//	mediaTypes->DeviceType = FILE_DEVICE_DISK;
	//	mediaTypes->MediaInfoCount = 1;
	//	mediaTypes->MediaInfo[0].DeviceSpecific.DiskInfo.Cylinders.QuadPart = CYLINDERS_NUM;
	//	mediaTypes->MediaInfo[0].DeviceSpecific.DiskInfo.MediaType = static_cast<STORAGE_MEDIA_TYPE>(MEDIA_TYPE::Unknown);
	//	mediaTypes->MediaInfo[0].DeviceSpecific.DiskInfo.TracksPerCylinder = TRACKS_PER_CYLINDER;
	//	mediaTypes->MediaInfo[0].DeviceSpecific.DiskInfo.SectorsPerTrack = SECTORS_PER_TRACK;
	//	mediaTypes->MediaInfo[0].DeviceSpecific.DiskInfo.BytesPerSector = SECTOR_SIZE;
	//	mediaTypes->MediaInfo[0].DeviceSpecific.DiskInfo.NumberMediaSides = 1;
	//	mediaTypes->MediaInfo[0].DeviceSpecific.DiskInfo.MediaCharacteristics = MEDIA_CURRENTLY_MOUNTED | MEDIA_READ_WRITE;
	//	irp->IoStatus.Information = sizeof(GET_MEDIA_TYPES);
	//	break;

	//case IOCTL_DISK_GET_DRIVE_GEOMETRY_EX:
	//	TRACE("StorageDevice::Handling IOCTL_DISK_GET_DRIVE_GEOMETRY_EX\n");
	//	//CHECK_STATUS(validateOutputBufferLength(irp, outputBufLen, FIELD_OFFSET(DISK_GEOMETRY_EX, Data) + sizeof(DISK_PARTITION_INFO) + sizeof(DISK_DETECTION_INFO)));
	//	CHECK_STATUS(validateOutputBufferLength(irp, outputBufLen, sizeof(DISK_GEOMETRY_EX)));
	//	diskGeoEx = reinterpret_cast<PDISK_GEOMETRY_EX>(irp->AssociatedIrp.SystemBuffer);
	//	diskGeoEx->Geometry.BytesPerSector = SECTOR_SIZE;
	//	diskGeoEx->Geometry.Cylinders.QuadPart = CYLINDERS_NUM;
	//	diskGeoEx->Geometry.MediaType = FixedMedia;
	//	diskGeoEx->Geometry.SectorsPerTrack = SECTORS_PER_TRACK;
	//	diskGeoEx->Geometry.TracksPerCylinder = TRACKS_PER_CYLINDER;
	//	diskGeoEx->DiskSize.QuadPart = STORAGE_SIZE;
	//	/*DiskGeometryGetDetect(diskGeoEx)->SizeOfDetectInfo = sizeof(DISK_DETECTION_INFO);
	//	DiskGeometryGetDetect(diskGeoEx)->DetectionType = DetectNone;
	//	DiskGeometryGetPartition(diskGeoEx)->SizeOfPartitionInfo = sizeof(DISK_PARTITION_INFO);
	//	DiskGeometryGetPartition(diskGeoEx)->PartitionStyle = PARTITION_STYLE::PARTITION_STYLE_RAW;*/
	//	//irp->IoStatus.Information = FIELD_OFFSET(DISK_GEOMETRY_EX, Data) + sizeof(DISK_PARTITION_INFO) + sizeof(DISK_DETECTION_INFO);
	//	irp->IoStatus.Information = sizeof(DISK_GEOMETRY_EX);
	//	break;

	//case IOCTL_DISK_GET_DISK_ATTRIBUTES:
	//	TRACE("StorageDevice::Handling IOCTL_DISK_GET_DISK_ATTRIBUTES\n");
	//	diskAttributes = reinterpret_cast<PGET_DISK_ATTRIBUTES>(irp->AssociatedIrp.SystemBuffer);
	//	diskAttributes->Version = sizeof(GET_DISK_ATTRIBUTES);
	//	irp->IoStatus.Information = sizeof(GET_DISK_ATTRIBUTES);
	//	break;

	default:
		TRACE("StorageDevice::Handling a non supported IOCTL - %lu\n", stack->Parameters.DeviceIoControl.IoControlCode);
		COMPLETE_IRP_WITH_STATUS(STATUS_NOT_SUPPORTED);
	}

	COMPLETE_IRP_WITH_STATUS(STATUS_SUCCESS);

cleanup:
	completeRequest(deviceObject, irp, &status);
	return status;
}
