#pragma once

HRESULT GetLastHresult(ULONG dwError = GetLastError());

class CLogFile
{
private:
	HANDLE _hFile = 0;
	SRWLOCK _SRWLock = SRWLOCK_INIT;
	CSHORT _day = MAXSHORT;

	NTSTATUS Init(_In_ PTIME_FIELDS tf);
public:
	static CLogFile s_logfile;

	~CLogFile()
	{
		if (HANDLE hFile = _hFile)
		{
			NtClose(hFile);
		}
	}

	NTSTATUS Init();

	void printf(_In_ PCSTR format, ...);

	HRESULT LogError(PCSTR prefix, HRESULT dwError = GetLastHresult());
};

#define DbgPrint CLogFile::s_logfile.printf

#define LOG(args)  CLogFile::s_logfile.args

#pragma message("!!! LOG >>>>>>")
