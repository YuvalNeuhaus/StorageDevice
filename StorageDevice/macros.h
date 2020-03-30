#pragma once

#define CHECK_STATUS(retval) status=retval; \
							 if (!NT_SUCCESS(status)) goto cleanup;
#define TRACE(...) DbgPrintEx(DPFLTR_IHVDRIVER_ID, 0xFFFFFFFF, __VA_ARGS__)
#define RETURN_STATUS(ret) status=ret;goto cleanup;
#define COMPLETE_IRP_WITH_STATUS(ret) irp->IoStatus.Status=ret;RETURN_STATUS(ret);
#define TRACE_FUNC (TRACE("StorageDevice::%s\n", __FUNCTION__))
