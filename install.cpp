#include "stdafx.h"

_NT_BEGIN

#include "log.h"

extern volatile const UCHAR guz = 0;

struct ServiceData : SERVICE_NOTIFY
{
	ServiceData() { 
		RtlZeroMemory(this, sizeof(ServiceData)); 
		dwVersion = SERVICE_NOTIFY_STATUS_CHANGE;
		pfnNotifyCallback = ScNotifyCallback;
		pContext = this;
	}

	void OnScNotify()
	{
		DbgPrint("ScNotifyCallback(%u %08x %x %x)\r\n", 
			dwNotificationStatus, dwNotificationTriggered, 
			ServiceStatus.dwCurrentState, ServiceStatus.dwCheckPoint );
	}

	static VOID CALLBACK ScNotifyCallback (_In_ PVOID pParameter)
	{
		reinterpret_cast<ServiceData*>(pParameter)->OnScNotify();
	}
};

STATIC_WSTRING_(MTS);

HRESULT UnInstallService() 
{
	DbgPrint("UnInstallService()\r\n");

	if (SC_HANDLE scm = OpenSCManagerW(0, 0, 0))
	{
		HRESULT hr = S_OK;

		SC_HANDLE svc = OpenServiceW(scm, MTS, DELETE|SERVICE_STOP|SERVICE_QUERY_STATUS);

		CloseServiceHandle(scm);

		if (svc)					
		{
			ServiceData sd;

			if (ControlService(svc, SERVICE_CONTROL_STOP, (SERVICE_STATUS*)&sd.ServiceStatus))
			{
				ULONG64 t_end = GetTickCount64() + 4000, t;

				while (sd.ServiceStatus.dwCurrentState != SERVICE_STOPPED)
				{
					if (sd.dwNotificationStatus = NotifyServiceStatusChangeW(svc, 
						SERVICE_NOTIFY_CONTINUE_PENDING|
						SERVICE_NOTIFY_DELETE_PENDING|
						SERVICE_NOTIFY_PAUSE_PENDING|
						SERVICE_NOTIFY_PAUSED|
						SERVICE_NOTIFY_RUNNING|
						SERVICE_NOTIFY_START_PENDING|
						SERVICE_NOTIFY_STOP_PENDING|
						SERVICE_NOTIFY_STOPPED, &sd))
					{
						LOG(LogError("SERVICE_CONTROL_STOP", sd.dwNotificationStatus));
						break;
					}

					sd.dwNotificationStatus = ERROR_TIMEOUT;

					if ((t = GetTickCount64()) >= t_end ||
						WAIT_IO_COMPLETION != SleepEx((ULONG)(t_end - t), TRUE) ||
						sd.dwNotificationStatus != NOERROR)
					{
						break;
					}
				}

				if (sd.ServiceStatus.dwCurrentState != SERVICE_STOPPED)
				{
					LOG(LogError("dwNotificationStatus", sd.dwNotificationStatus));
				}
			}
			else
			{
				LOG(LogError("SERVICE_CONTROL_STOP"));
			}

			if (!DeleteService(svc))
			{
				hr = LOG(LogError("DeleteService"));
			}

			CloseServiceHandle(svc);
			ZwTestAlert();
		}
		else
		{
			hr = LOG(LogError("OpenService"));
		}
		return hr;
	}

	return LOG(LogError("OpenSCManager"));
}

HRESULT InstallService()
{
	HRESULT hr;
	PVOID stack = alloca(guz);

	union {
		PVOID buf;
		PWSTR lpBinaryPathName;
	};

	ULONG cch, nSize;
	do 
	{
		nSize = RtlPointerToOffset(buf = alloca(0x20), stack) / sizeof(WCHAR);

		cch = GetModuleFileNameW(0, lpBinaryPathName + 1, nSize - 3);

	} while ((hr = GetLastError()) == ERROR_INSUFFICIENT_BUFFER);

	if (hr)
	{
		return HRESULT_FROM_WIN32(hr);
	}

	DbgPrint("InstallService(%S)...\r\n", lpBinaryPathName + 1);

	*lpBinaryPathName = '\"', lpBinaryPathName[cch + 1] = '\"', lpBinaryPathName[cch + 2] = '\n', lpBinaryPathName[cch + 3] = 0;

	if (SC_HANDLE scm = OpenSCManagerW(0, 0, SC_MANAGER_CREATE_SERVICE))
	{
		hr = S_OK;

		if (SC_HANDLE svc = CreateServiceW(
			scm,						// SCM database
			MTS,					// name of service
			L"service name to display",		// service name to display
			SERVICE_ALL_ACCESS,			// desired access
			SERVICE_WIN32_OWN_PROCESS,// service type
			SERVICE_AUTO_START,			// start type
			SERVICE_ERROR_NORMAL,		// error control type
			lpBinaryPathName,	// path to service's binary
			0,					// no load ordering group
			0,					// no tag identifier
			0,					// no dependencies
			0,					// LocalSystem account
			0))					// no password
		{

			static const SERVICE_DESCRIPTION sd = {
				const_cast<PWSTR>(L"@@SERVICE_DESCRIPTION")
			};

			if (!ChangeServiceConfig2W(
				svc,						// handle to service
				SERVICE_CONFIG_DESCRIPTION, // change: description
				const_cast<SERVICE_DESCRIPTION*>(&sd)))			// new description
			{
				LOG(LogError("ChangeServiceConfig2"));
			}

			if (!StartServiceW(svc, 0, 0))
			{
				hr = LOG(LogError("StartServiceW"));
			}

			CloseServiceHandle(svc);
		}
		else
		{
			hr = LOG(LogError("CreateService"));
		}

		CloseServiceHandle(scm);
		return hr;
	}

	return LOG(LogError("OpenSCManager"));
}

_NT_END