#pragma once

#include <wdm.h>
#include <classpnp.h>
#include <initguid.h>
#include <mountdev.h>
#include <guiddef.h>

#define STORAGE_SIZE (1024*1024*50)
#define DEVICE_NAME L"\\Device\\MyStorageDevice"
#define SYMBOLIC_LINK L"\\DosDevices\\MyStorageDeviceSymLink"
#define DEVICE_UID L"\\??\\Global\\{53f56307-b6bf-11d0-94f2-00a0c91efb8b}"
#define VOL_LABEL L"YuVOL"
// {1B9A48C2-DE06-4F93-ABFB-642371B005A0}
DEFINE_GUID(MY_DEVICE_CLASS_GUID, 0x1b9a48c2, 0xde06, 0x4f93, 0xab, 0xfb, 0x64, 0x23, 0x71, 0xb0, 0x5, 0xa0);

