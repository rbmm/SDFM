#include "stdafx.h"

_NT_BEGIN
#include "log.h"
#include "../inc/initterm.h"
#include "SvcBase.h"

void WINAPI StartNotepadInSession2(ULONG dwSessionId)
{
	HANDLE hToken;
	WCHAR sz[MAX_PATH];

	if (GetEnvironmentVariableW(L"comspec", sz, _countof(sz)))
	{
		DbgPrint("try start %S in %x\n", sz, dwSessionId);

		if (WTSQueryUserToken(dwSessionId, &hToken))
		{
			PVOID lpEnvironment;
			if (CreateEnvironmentBlock(&lpEnvironment, hToken, FALSE))
			{
				PROCESS_INFORMATION pi;
				STARTUPINFOW si = { sizeof(si) };
				//Sleep(2000);
				if (CreateProcessAsUserW(hToken, sz, 0, 0, 0, 0, 
					CREATE_UNICODE_ENVIRONMENT, lpEnvironment, 0, &si, &pi))
				{
					DbgPrint("started !\r\n");
					NtClose(pi.hThread);
					NtClose(pi.hProcess);
				}
				else
				{
					DbgPrint("nt=%x, %u\r\n", RtlGetLastNtStatus(), GetLastError());
					LOG(LogError("CreateProcessAsUserW"));
				}
				DestroyEnvironmentBlock(lpEnvironment);
			}
			else
			{
				LOG(LogError("CreateEnvironmentBlock"));
			}
			NtClose(hToken);
		}
		else
		{
			LOG(LogError("WTSQueryUserToken"));
		}
	}
	else
	{
		LOG(LogError("SearchPathW"));
	}

	//RtlExitUserThread(0);
}

void StartNotepadInSession(ULONG dwSessionId)
{
	RtlCreateUserThread(NtCurrentProcess(), 0, 0, 0, 0, 0, StartNotepadInSession2, (void*)(ULONG_PTR)dwSessionId, 0, 0);
}

void StartNotepadInSessions()
{
	PWTS_SESSION_INFOW pSessionInfo;
	ULONG Count;
	if (WTSEnumerateSessionsW(WTS_CURRENT_SERVER_HANDLE, 0, 1, &pSessionInfo, &Count))
	{
		DbgPrint("Sessions = %x\r\n", Count);
		if (Count)
		{
			pSessionInfo += Count;
			do 
			{
				--pSessionInfo;

				DbgPrint("SESSION_INFO<%x>: %x %S\r\n", pSessionInfo->SessionId, pSessionInfo->State, pSessionInfo->pWinStationName);

				if (pSessionInfo->SessionId)
				{
					switch (pSessionInfo->State)
					{
					case WTSDisconnected:
					case WTSActive:
						StartNotepadInSession2(pSessionInfo->SessionId);
						break;
					}
				}

			} while (--Count);
		}
		WTSFreeMemory(pSessionInfo);
	}
	else
	{
		LOG(LogError("WTSEnumerateSessions"));
	}
}

class CSvc : public CSvcBase
{
	HANDLE _hThread = 0;

	virtual ULONG OnSessionChange(ULONG dwSessionId, ULONG dwEventType )
	{
		DbgPrint("OnSessionChange(%u, %x)\r\n", dwSessionId, dwEventType);

		ULONG size;
		PWTSINFOW pp;

		if (WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, 
			dwSessionId, WTSSessionInfo, (PWSTR*)&pp, &size))
		{
			DbgPrint("<%x: %x:%S %S\\%S >\r\n", dwSessionId, pp->State, pp->WinStationName, pp->Domain, pp->UserName);
			WTSFreeMemory(pp);
		}
		else
		{
			LOG(LogError("WTSQuerySessionInformationW"));
		}

		switch (dwEventType)
		{
		case WTS_SESSION_LOGON:
		case WTS_SESSION_UNLOCK:
			StartNotepadInSession2(dwSessionId);
			break;
		}

		return NOERROR;
	}

	virtual HRESULT Run()
	{
		if (!DuplicateHandle(NtCurrentProcess(), NtCurrentThread(), NtCurrentProcess(), &_hThread, 0, FALSE, DUPLICATE_SAME_ACCESS))
		{
			return GetLastHr();
		}
		
		DbgPrint("+++ Run\r\n");

		StartNotepadInSessions();

		do 
		{
			ULONG dwState = m_dwTargetState;

			DbgPrint("state:= %x\r\n", dwState);

			SetState(dwState, dwState == SERVICE_RUNNING 
				? SERVICE_ACCEPT_PAUSE_CONTINUE | SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SESSIONCHANGE
				: SERVICE_ACCEPT_PAUSE_CONTINUE | SERVICE_ACCEPT_STOP);

			if (dwState == SERVICE_RUNNING)
			{
			}
			else
			{
			}

			static const LARGE_INTEGER Interval = { 0, (LONG)MINLONG };

			ZwDelayExecution( TRUE, const_cast<PLARGE_INTEGER>(&Interval));

		} while (m_dwTargetState != SERVICE_STOPPED);

		DbgPrint("--- Run\r\n");

		return S_OK;
	}

	virtual ULONG Handler(
		ULONG    dwControl,
		ULONG    /*dwEventType*/,
		PVOID   /*lpEventData*/
		)
	{
		switch (dwControl)
		{
		case SERVICE_CONTROL_CONTINUE:
		case SERVICE_CONTROL_PAUSE:
		case SERVICE_CONTROL_STOP:
			return RtlNtStatusToDosErrorNoTeb(ZwAlertThread(_hThread));
		}

		return ERROR_SERVICE_CANNOT_ACCEPT_CTRL;
	}

public:

	~CSvc()
	{
		if (HANDLE hThread = _hThread) NtClose(_hThread);
	}
};

void NTAPI ServiceMain(DWORD argc, PWSTR argv[])
{
	if (argc)
	{
		CSvc o;
		o.ServiceMain(argv[0]);
	}
}

HRESULT UnInstallService();
HRESULT InstallService();

void CALLBACK ep(void*)
{
	initterm();
	LOG(Init());

	PCWSTR cmd = GetCommandLineW();
	if (wcschr(cmd, '\n'))
	{
		const static SERVICE_TABLE_ENTRY ste[] = { 
			{ const_cast<PWSTR>(L"MTS"), ServiceMain }, {} 
		};

		if (!StartServiceCtrlDispatcher(ste))
		{
			LOG(LogError("SERVICE_CONTROL_STOP"));
		}

		DbgPrint("Service Exit\r\n");
	}
	else if (cmd = wcschr(cmd, '*'))
	{
		switch (cmd[1])
		{
		case 'i':
			InstallService();
			break;
		case 'u':
			UnInstallService();
			break;
		}
	}

	destroyterm();
	ExitProcess(0);
}

_NT_END