#include <ntifs.h>
#include <ntddk.h>
#include <ntddvol.h>
#include <ntdddisk.h>
#include <mountdev.h>
#include "deviceControl.h"
#include "StorageDevice.h"
#include "macros.h"
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
	PPARTITION_INFORMATION_EX partInfo = NULL;
	PSTORAGE_DEVICE_DESCRIPTOR storageDevDescriptor = NULL;
	PSTORAGE_PROPERTY_QUERY propQuery = NULL;
	PGET_LENGTH_INFORMATION lenInfo = NULL;
	PSTORAGE_HOTPLUG_INFO hotplugInfo = NULL;
	PVOLUME_GET_GPT_ATTRIBUTES_INFORMATION gptInfo = NULL;
	PGET_DISK_ATTRIBUTES diskAttributes = NULL;
	PDISK_GEOMETRY diskGeo = NULL;
	//PDISK_GEOMETRY_EX diskGeoEx = NULL;
	PGET_MEDIA_TYPES mediaTypes = NULL;
	PSTORAGE_DEVICE_NUMBER storDevNum = NULL;
	PVOLUME_DISK_EXTENTS volDiskEx = NULL;
	PMOUNTDEV_NAME mntDevName = NULL;
	PMOUNTDEV_UNIQUE_ID mntDevUID = NULL;
	UNICODE_STRING deviceUID;

	PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
	UINT64 outputBufLen = stack->Parameters.DeviceIoControl.OutputBufferLength;
	UNICODE_STRING devName = RTL_CONSTANT_STRING(DEVICE_NAME);

	switch (stack->Parameters.DeviceIoControl.IoControlCode) {

	case IOCTL_MOUNTDEV_QUERY_DEVICE_NAME:
		TRACE("StorageDevice::Handling IOCTL_MOUNTDEV_QUERY_DEVICE_NAME\n");
		mntDevName = reinterpret_cast<PMOUNTDEV_NAME>(irp->AssociatedIrp.SystemBuffer);
		if (!NT_SUCCESS(validateOutputBufferLength(irp, outputBufLen, devName.Length + sizeof(USHORT)))) {
			if (outputBufLen >= sizeof(MOUNTDEV_NAME)) {
				mntDevName->NameLength = devName.Length;
				irp->IoStatus.Information = sizeof(MOUNTDEV_NAME);
				COMPLETE_IRP_WITH_STATUS(STATUS_BUFFER_OVERFLOW);
			} else {
				COMPLETE_IRP_WITH_STATUS(STATUS_INVALID_PARAMETER);
			}
		}
		RtlCopyBytes((PCHAR)mntDevName->Name, devName.Buffer, devName.Length);
		mntDevName->NameLength = devName.Length;
		irp->IoStatus.Information = devName.Length + sizeof(USHORT);
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

	case IOCTL_MOUNTDEV_LINK_CREATED:
		TRACE("StorageDevice::Handling IOCTL_MOUNTDEV_LINK_CREATED\n");
		break;
	
	case IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS:
		TRACE("StorageDevice::Handling IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS\n");
		volDiskEx = reinterpret_cast<PVOLUME_DISK_EXTENTS>(irp->AssociatedIrp.SystemBuffer);
		volDiskEx->NumberOfDiskExtents = 1;
		volDiskEx->Extents[0].DiskNumber = 1;
		volDiskEx->Extents[0].StartingOffset.QuadPart = 0;
		volDiskEx->Extents[0].ExtentLength.QuadPart = STORAGE_SIZE;
		irp->IoStatus.Information = sizeof(VOLUME_DISK_EXTENTS);
		break;

	case IOCTL_STORAGE_GET_DEVICE_NUMBER:
		TRACE("StorageDevice::Handling IOCTL_STORAGE_GET_DEVICE_NUMBER\n");
		storDevNum = reinterpret_cast<PSTORAGE_DEVICE_NUMBER>(irp->AssociatedIrp.SystemBuffer);
		storDevNum->DeviceNumber = 0;
		storDevNum->DeviceType = FILE_DEVICE_DISK;
		storDevNum->PartitionNumber = static_cast<ULONG>(-1);
		irp->IoStatus.Information = sizeof(STORAGE_DEVICE_NUMBER);
		break;

	case IOCTL_VOLUME_IS_DYNAMIC:
		TRACE("StorageDevice::Handling IOCTL_VOLUME_IS_DYNAMIC\n");
		CHECK_STATUS(validateOutputBufferLength(irp, outputBufLen, sizeof(BOOLEAN)));
		*reinterpret_cast<PBOOLEAN>(irp->AssociatedIrp.SystemBuffer) = FALSE;
		break;

	case IOCTL_VOLUME_ONLINE:
		TRACE("StorageDevice::Handling IOCTL_VOLUME_ONLINE\n");
		break;

	case IOCTL_STORAGE_CHECK_VERIFY:
		TRACE("StorageDevice::Handling IOCTL_STORAGE_CHECK_VERIFY\n");
		break;

	case IOCTL_STORAGE_CHECK_VERIFY2:
		TRACE("StorageDevice::Handling IOCTL_STORAGE_CHECK_VERIFY2\n");
		break;

	case IOCTL_DISK_UPDATE_DRIVE_SIZE:
		TRACE("StorageDevice::Handling IOCTL_DISK_UPDATE_DRIVE_SIZE\n");
		diskGeo = reinterpret_cast<PDISK_GEOMETRY>(irp->AssociatedIrp.SystemBuffer);
		diskGeo->BytesPerSector = SECTOR_SIZE;
		diskGeo->Cylinders.QuadPart = CYLINDERS_NUM;
		diskGeo->MediaType = MEDIA_TYPE::FixedMedia;
		diskGeo->SectorsPerTrack = SECTORS_PER_TRACK;
		diskGeo->TracksPerCylinder = TRACKS_PER_CYLINDER;
		irp->IoStatus.Information = sizeof(DISK_GEOMETRY);
		break;

	/*case IOCTL_STORAGE_GET_MEDIA_TYPES:
		TRACE("StorageDevice::Handling IOCTL_STORAGE_GET_MEDIA_TYPES\n");
		diskGeo = reinterpret_cast<PDISK_GEOMETRY>(irp->AssociatedIrp.SystemBuffer);
		diskGeo->BytesPerSector = SECTOR_SIZE;
		diskGeo->Cylinders.QuadPart = CYLINDERS_NUM;
		diskGeo->MediaType = MEDIA_TYPE::RemovableMedia;
		diskGeo->SectorsPerTrack = SECTORS_PER_TRACK;
		diskGeo->TracksPerCylinder = TRACKS_PER_CYLINDER;
		irp->IoStatus.Information = sizeof(DISK_GEOMETRY);
		break;*/

	case IOCTL_STORAGE_GET_MEDIA_TYPES_EX:
		TRACE("StorageDevice::Handling IOCTL_STORAGE_GET_MEDIA_TYPES_EX\n");
		mediaTypes = reinterpret_cast<PGET_MEDIA_TYPES>(irp->AssociatedIrp.SystemBuffer);
		mediaTypes->DeviceType = FILE_DEVICE_DISK;
		mediaTypes->MediaInfoCount = 1;
		mediaTypes->MediaInfo[0].DeviceSpecific.DiskInfo.Cylinders.QuadPart = CYLINDERS_NUM;
		mediaTypes->MediaInfo[0].DeviceSpecific.DiskInfo.MediaType = static_cast<STORAGE_MEDIA_TYPE>(MEDIA_TYPE::FixedMedia);
		mediaTypes->MediaInfo[0].DeviceSpecific.DiskInfo.TracksPerCylinder = TRACKS_PER_CYLINDER;
		mediaTypes->MediaInfo[0].DeviceSpecific.DiskInfo.SectorsPerTrack = SECTORS_PER_TRACK;
		mediaTypes->MediaInfo[0].DeviceSpecific.DiskInfo.BytesPerSector = SECTOR_SIZE;
		mediaTypes->MediaInfo[0].DeviceSpecific.DiskInfo.NumberMediaSides = 1;
		mediaTypes->MediaInfo[0].DeviceSpecific.DiskInfo.MediaCharacteristics = MEDIA_CURRENTLY_MOUNTED | MEDIA_READ_WRITE;
		irp->IoStatus.Information = sizeof(GET_MEDIA_TYPES);
		break;

	case IOCTL_DISK_GET_LENGTH_INFO:
		TRACE("StorageDevice::Handling IOCTL_DISK_GET_LENGTH_INFO\n");
		CHECK_STATUS(validateOutputBufferLength(irp, outputBufLen, sizeof(UINT64)));
		lenInfo = reinterpret_cast<PGET_LENGTH_INFORMATION>(irp->AssociatedIrp.SystemBuffer);
		lenInfo->Length.QuadPart = STORAGE_SIZE;
		irp->IoStatus.Information = sizeof(GET_LENGTH_INFORMATION);
		break;

	case IOCTL_STORAGE_GET_HOTPLUG_INFO:
		TRACE("StorageDevice::Handling IOCTL_STORAGE_GET_HOTPLUG_INFO\n");
		CHECK_STATUS(validateOutputBufferLength(irp, outputBufLen, sizeof(STORAGE_HOTPLUG_INFO)));
		hotplugInfo = reinterpret_cast<PSTORAGE_HOTPLUG_INFO>(irp->AssociatedIrp.SystemBuffer);
		hotplugInfo->Size = sizeof(STORAGE_HOTPLUG_INFO);
		hotplugInfo->DeviceHotplug = FALSE;
		hotplugInfo->MediaHotplug = FALSE;
		hotplugInfo->MediaRemovable = FALSE;
		hotplugInfo->WriteCacheEnableOverride = NULL;
		irp->IoStatus.Information = sizeof(STORAGE_HOTPLUG_INFO);
		break;

	//case IOCTL_DISK_GET_DRIVE_GEOMETRY_EX:
	//	TRACE("StorageDevice::Handling IOCTL_DISK_GET_DRIVE_GEOMETRY_EX\n");
	//	//CHECK_STATUS(validateOutputBufferLength(irp, outputBufLen, FIELD_OFFSET(DISK_GEOMETRY_EX, Data) + sizeof(DISK_PARTITION_INFO) + sizeof(DISK_DETECTION_INFO)));
	//	CHECK_STATUS(validateOutputBufferLength(irp, outputBufLen, sizeof(DISK_GEOMETRY_EX)));
	//	diskGeoEx = reinterpret_cast<PDISK_GEOMETRY_EX>(irp->AssociatedIrp.SystemBuffer);
	//	diskGeoEx->Geometry.BytesPerSector = SECTOR_SIZE;
	//	diskGeoEx->Geometry.Cylinders.QuadPart = CYLINDERS_NUM;
	//	diskGeoEx->Geometry.MediaType = RemovableMedia;
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

	case IOCTL_DISK_GET_DISK_ATTRIBUTES:
		TRACE("StorageDevice::Handling IOCTL_DISK_GET_DISK_ATTRIBUTES\n");
		diskAttributes = reinterpret_cast<PGET_DISK_ATTRIBUTES>(irp->AssociatedIrp.SystemBuffer);
		diskAttributes->Version = sizeof(GET_DISK_ATTRIBUTES);
		irp->IoStatus.Information = sizeof(GET_DISK_ATTRIBUTES);
		break;

	case IOCTL_VOLUME_GET_GPT_ATTRIBUTES:
		TRACE("StorageDevice::Handling IOCTL_VOLUME_GET_GPT_ATTRIBUTES\n");
		gptInfo = reinterpret_cast<PVOLUME_GET_GPT_ATTRIBUTES_INFORMATION>(irp->AssociatedIrp.SystemBuffer);
		gptInfo->GptAttributes = 0;
		irp->IoStatus.Information = sizeof(VOLUME_GET_GPT_ATTRIBUTES_INFORMATION);
		break;

	case IOCTL_DISK_IS_WRITABLE:
		TRACE("StorageDevice::Handling IOCTL_DISK_IS_WRITABLE\n");
		break;

	case IOCTL_STORAGE_QUERY_PROPERTY:
		TRACE("StorageDevice::Handling IOCTL_STORAGE_QUERY_PROPERTY\n");
		CHECK_STATUS(validateOutputBufferLength(irp, outputBufLen, sizeof(STORAGE_DEVICE_DESCRIPTOR)));
		propQuery = reinterpret_cast<PSTORAGE_PROPERTY_QUERY>(irp->AssociatedIrp.SystemBuffer);
		switch (propQuery->PropertyId) {
		case StorageDeviceProperty:
			if (propQuery->QueryType == PropertyStandardQuery) {
				storageDevDescriptor = reinterpret_cast<PSTORAGE_DEVICE_DESCRIPTOR>(irp->AssociatedIrp.SystemBuffer);
				storageDevDescriptor->Version = sizeof(STORAGE_DEVICE_DESCRIPTOR);
				storageDevDescriptor->Size = 0;
				storageDevDescriptor->DeviceType = FILE_DEVICE_DISK;
				storageDevDescriptor->DeviceTypeModifier = 0;
				storageDevDescriptor->RemovableMedia = FALSE;
				storageDevDescriptor->CommandQueueing = FALSE;
				storageDevDescriptor->VendorIdOffset = 0;
				storageDevDescriptor->ProductIdOffset = 0;
				storageDevDescriptor->ProductRevisionOffset = 0;
				storageDevDescriptor->SerialNumberOffset = 0;
				storageDevDescriptor->BusType = BusTypeUnknown;
				storageDevDescriptor->RawPropertiesLength = 0;
				irp->IoStatus.Information = sizeof(STORAGE_DEVICE_DESCRIPTOR);
			}
			break;

		default:
			TRACE("StorageDevice::Property ID not supported - %lu (%s)\n", propQuery->PropertyId,
				  propQuery->QueryType ? "PropertyExistsQuery" : "PropertyStandardQuery");
			COMPLETE_IRP_WITH_STATUS(STATUS_NOT_SUPPORTED);
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
		partInfo->Mbr.PartitionType = PARTITION_ENTRY_UNUSED;
		partInfo->Mbr.BootIndicator = FALSE;
		partInfo->Mbr.RecognizedPartition = TRUE;
		partInfo->Mbr.HiddenSectors = 0;
		irp->IoStatus.Information = sizeof(PARTITION_INFORMATION_EX);
		break;

	case IOCTL_DISK_CHECK_VERIFY:
		TRACE("StorageDevice::Handling IOCTL_DISK_CHECK_VERIFY\n");
		break;

	case IOCTL_DISK_SET_PARTITION_INFO:
		TRACE("StorageDevice::Handling IOCTL_DISK_SET_PARTITION_INFO\n");
		break;

	case IOCTL_DISK_MEDIA_REMOVAL:
		TRACE("StorageDevice::Handling IOCTL_DISK_MEDIA_REMOVAL\n");
		break;

	case IOCTL_STORAGE_EJECT_MEDIA:
		TRACE("StorageDevice::Handling IOCTL_STORAGE_EJECT_MEDIA\n");
		break;

	default:
		TRACE("StorageDevice::Handling a non supported IOCTL - %lu\n", stack->Parameters.DeviceIoControl.IoControlCode);
		COMPLETE_IRP_WITH_STATUS(STATUS_NOT_SUPPORTED);
	}

	COMPLETE_IRP_WITH_STATUS(STATUS_SUCCESS);

cleanup:
	IoCompleteRequest(irp, IO_NO_INCREMENT);
	return status;
}
