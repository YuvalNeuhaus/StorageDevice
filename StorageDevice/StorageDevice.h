#pragma once

#include <wdm.h>
#include <classpnp.h>
#include <initguid.h>
#include <mountdev.h>

#define STORAGE_SIZE (1024*1024*50)
#define POOL_TAG (0)
#define DEVICE_NAME L"\\Device\\MyStorageDevice"
#define SYMBOLIC_LINK L"\\DosDevices\\MyStorageDeviceSymLink"
#define DEVICE_UID L"\\??\\Global\\{53f56307-b6bf-11d0-94f2-00a0c91efb8b}"
#define VOL_LABEL L"YuVOL"
