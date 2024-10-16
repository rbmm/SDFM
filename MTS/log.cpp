#include "stdafx.h"

_NT_BEGIN

#include "log.h"

HRESULT GetLastHresult(ULONG dwError /*= GetLastError()*/)
{
	NTSTATUS status = RtlGetLastNtStatus();

	return RtlNtStatusToDosErrorNoTeb(status) == dwError ? HRESULT_FROM_NT(status) : HRESULT_FROM_WIN32(dwError);
}

CLogFile CLogFile::s_logfile;

NTSTATUS CLogFile::Init()
{
	STATIC_OBJECT_ATTRIBUTES(oa, "\\systemroot\\temp\\MTS");

	HANDLE hFile;
	IO_STATUS_BLOCK iosb;
	NTSTATUS status = NtCreateFile(&hFile, FILE_ADD_FILE, &oa, &iosb, 0, FILE_ATTRIBUTE_DIRECTORY,
		FILE_SHARE_READ|FILE_SHARE_WRITE, FILE_OPEN_IF, FILE_DIRECTORY_FILE, 0, 0);

	if (0 <= status)
	{
		NtClose(hFile);
	}

	return status;
}

NTSTATUS CLogFile::Init(_In_ PTIME_FIELDS tf)
{
	WCHAR lpFileName[128];

	if (0 >= swprintf_s(lpFileName, _countof(lpFileName), 
		L"\\systemroot\\temp\\MTS\\%u-%02u-%02u.log", tf->Year, tf->Month, tf->Day))
	{
		return STATUS_INTERNAL_ERROR;
	}

	IO_STATUS_BLOCK iosb;
	UNICODE_STRING ObjectName;
	OBJECT_ATTRIBUTES oa = { sizeof(oa), 0, &ObjectName, OBJ_CASE_INSENSITIVE };
	RtlInitUnicodeString(&ObjectName, lpFileName);

	NTSTATUS status = NtCreateFile(&oa.RootDirectory, FILE_APPEND_DATA|SYNCHRONIZE, &oa, &iosb, 0, 0,
		FILE_SHARE_READ|FILE_SHARE_WRITE, FILE_OPEN_IF, FILE_SYNCHRONOUS_IO_NONALERT, 0, 0);
	
	if (0 <= status)
	{
		_hFile = oa.RootDirectory;
	}

	return status;
}

void CLogFile::printf(_In_ PCSTR format, ...)
{
	union {
		FILETIME ft;
		LARGE_INTEGER time;
	};

	TIME_FIELDS tf;

	GetSystemTimeAsFileTime(&ft);
	RtlTimeToTimeFields(&time, &tf);

	va_list ap;
	va_start(ap, format);

	PSTR buf = 0;
	int len = 0;

	enum { tl = _countof("[hh:mm:ss] ") - 1};

	while (0 < (len = _vsnprintf(buf, len, format, ap)))
	{
		if (buf && tl - 1 == sprintf_s(buf -= tl, tl, "[%02u:%02u:%02u]", tf.Hour, tf.Minute, tf.Second))
		{
			buf[tl - 1] = ' ';

			HANDLE hFile;

			if (_day != tf.Day)
			{
				AcquireSRWLockExclusive(&_SRWLock);	

				if (_day != tf.Day)
				{
					if (hFile = _hFile)
					{
						NtClose(hFile);
						_hFile = 0;
					}

					if (0 <= Init(&tf))
					{
						_day = tf.Day;
					}
				}

				ReleaseSRWLockExclusive(&_SRWLock);
			}

			AcquireSRWLockShared(&_SRWLock);

			if (hFile = _hFile) WriteFile(hFile, buf, tl + len, &ft.dwLowDateTime, 0);

			ReleaseSRWLockShared(&_SRWLock);

			break;
		}

		buf = (PSTR)alloca(len + tl) + tl;
	}
}

HRESULT CLogFile::LogError(PCSTR prefix, HRESULT dwError/* = GetLastHresult()*/)
{
	LPCVOID lpSource = 0;

	PCSTR fmt = "%s: error[%x] %s";

	HRESULT hr = dwError;

	ULONG dwFlags = FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS|FORMAT_MESSAGE_ALLOCATE_BUFFER;

	if (dwError & FACILITY_NT_BIT)
	{
		dwError &= ~FACILITY_NT_BIT;

		dwFlags = FORMAT_MESSAGE_FROM_HMODULE|FORMAT_MESSAGE_IGNORE_INSERTS|FORMAT_MESSAGE_ALLOCATE_BUFFER;

		static HMODULE s_nt;

		if (!s_nt)
		{
			s_nt = GetModuleHandleW(L"ntdll");
		}

		lpSource = s_nt;
	}
	else if (0 <= dwError)
	{
		fmt = "%s: error[%u] %s";
	}

	PSTR msg;
	if (FormatMessageA(dwFlags, lpSource, dwError, 0, (PSTR)&msg, 0, 0))
	{
		printf(fmt, prefix, dwError, msg);
		LocalFree(msg);
	}

	return hr;
}

_NT_END