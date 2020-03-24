#pragma once
#include <wdm.h>

EXTERN_C
NTSTATUS ObQueryNameString(
	PVOID                    Object,
	POBJECT_NAME_INFORMATION ObjectNameInfo,
	ULONG                    Length,
	PULONG                   ReturnLength
);

EXTERN_C
NTSTATUS IoReportDetectedDevice(
	PDRIVER_OBJECT                 DriverObject,
	INTERFACE_TYPE                 LegacyBusType,
	ULONG                          BusNumber,
	ULONG                          SlotNumber,
	PCM_RESOURCE_LIST              ResourceList,
	PIO_RESOURCE_REQUIREMENTS_LIST ResourceRequirements,
	BOOLEAN                        ResourceAssigned,
	PDEVICE_OBJECT                 *DeviceObject
);
