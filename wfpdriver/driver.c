#include "public.h"
#include "driver.h"
#include "devctrl.h"
#include "datalinkctx.h"
#include "flowctl.h"
#include "callouts.h"

#include <fwpmk.h>

#include <in6addr.h>
#include <ip2string.h>
#include <stdlib.h>

DRIVER_INITIALIZE DriverEntry;
DRIVER_UNLOAD driverUnload;

static PDEVICE_OBJECT g_deviceControl;
static HANDLE g_bfeStateSubscribeHandle = NULL;

UNICODE_STRING u_devicename;
UNICODE_STRING u_devicesyslink;

NTSTATUS driver_init(
	IN  PDRIVER_OBJECT  driverObject,
	IN  PUNICODE_STRING registryPath)
{
	NTSTATUS status = STATUS_SUCCESS;

	RtlInitUnicodeString(&u_devicename, L"\\Device\\WFPDark");
	RtlInitUnicodeString(&u_devicesyslink, L"\\DosDevices\\WFPDark");

	status = IoCreateDevice(driverObject,
		0,
		&u_devicename,
		FILE_DEVICE_UNKNOWN,
		FILE_DEVICE_SECURE_OPEN,
		FALSE,
		&g_deviceControl);
	if (!NT_SUCCESS(status)){
		return status;
	}

	status = IoCreateSymbolicLink(&u_devicesyslink, &u_devicename);
	if(!NT_SUCCESS(status)){
		return status;
	}


	return status;
}

VOID
driverUnload(
	IN  PDRIVER_OBJECT driverObject
)
{
	UNREFERENCED_PARAMETER(driverObject);

	KdPrint((DPREFIX"driverUnload\n"));
	
	driver_clean();

	if (g_bfeStateSubscribeHandle)
	{
		FwpmBfeStateUnsubscribeChanges0(g_bfeStateSubscribeHandle);
		g_bfeStateSubscribeHandle = NULL;
	}

#ifdef _WPPTRACE
	WPP_CLEANUP(driverObject);
#endif
}

VOID NTAPI
bfeStateCallback(
	IN OUT void* context,
	IN FWPM_SERVICE_STATE  newState
)
{
	UNREFERENCED_PARAMETER(context);

	if (newState == FWPM_SERVICE_RUNNING)
	{
		NTSTATUS status = callout_init(&g_deviceControl);
		if (!NT_SUCCESS(status))
		{
			// LogOutput(1, DPREFIX"bfeStateCallback callouts_init failed, status=%x\n", status);
			KdPrint((DPREFIX"bfeStateCallback callouts_init failed, status=%x\n", status));
		}
	}
}

NTSTATUS
DriverEntry(
	IN  PDRIVER_OBJECT  driverObject,
	IN  PUNICODE_STRING registryPath
)
{
	NTSTATUS status = STATUS_SUCCESS;
	int i = 0;

	for (i = 0; i < IRP_MJ_MAXIMUM_FUNCTION; ++i)
	{
		driverObject->MajorFunction[i] = (PDRIVER_DISPATCH)devctrl_dispatch;
	}

	driverObject->DriverUnload = driverUnload;

	do 
	{
		// Init Driver 
		status = driver_init(driverObject, registryPath);
		if (!NT_SUCCESS(status))
		{
			return status;
		}

		// Init MAK Packet
		status = datalinkctx_init();
		if (!NT_SUCCESS(status))
		{
			break;
		}

		// Init Flowctl
		status = flowctl_init();
		if (!NT_SUCCESS(status))
		{
			break;
		}

		// Init IO Thread
		// PsCreateSystemThread();
		
		// Init WFP Callout
		if (FwpmBfeStateGet() == FWPM_SERVICE_RUNNING)
		{
			status = callout_init(g_deviceControl);
			if (!NT_SUCCESS(status))
			{
				break;
			}
		}
		else
		{
			status = FwpmBfeStateSubscribeChanges(
				g_deviceControl,
				bfeStateCallback,
				NULL,
				&g_bfeStateSubscribeHandle);
			if (!NT_SUCCESS(status))
			{
				KdPrint((DPREFIX"FwpmBfeStateSubscribeChanges\n"));
				break;
			}
		}


		return status;
	} while (FALSE);
	
	driver_clean();
	return status;
}


VOID driver_clean()
{
	NTSTATUS status = STATUS_SUCCESS;

	flowctl_free();
	datalinkctx_free();
	callout_free();
	devctrl_free();

	if (g_deviceControl)
		IoDeleteDevice(g_deviceControl);

	IoDeleteSymbolicLink(&u_devicesyslink);
};