#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atlstr.h>

bool GetDLLPath(CStringA& path)
{
	const auto path_len = GetModuleFileNameA(NULL, path.GetBuffer(MAX_PATH), MAX_PATH);
	path.ReleaseBuffer();

	if (path.IsEmpty())
		return false;

	const auto last_slash_pos = path.ReverseFind('\\');
	if (last_slash_pos == -1)
		return false;

	path = path.Left(last_slash_pos) + "\\discoplus.dll";
	
	return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

bool InjectDLL(HANDLE proc, CStringA& path)
{
	const auto path_len = path.GetLength() + 1;

	const auto load_lib_addr = GetProcAddress(GetModuleHandleA("Kernel32"), "LoadLibraryA");
	if (!load_lib_addr)
		return false;

	const auto lib_name_addr = VirtualAllocEx(proc, NULL, path_len, MEM_COMMIT, PAGE_READWRITE);
	if (!lib_name_addr)
		return false;

	if (!WriteProcessMemory(proc, lib_name_addr, path, path_len, NULL))
	{
		VirtualFreeEx(proc, lib_name_addr, path_len, MEM_RELEASE);
		return false;
	}

	const auto thread = CreateRemoteThread(proc, NULL, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(load_lib_addr), lib_name_addr, 0, NULL);
	if (!thread)
	{
		VirtualFreeEx(proc, lib_name_addr, path_len, MEM_RELEASE);
		return false;
	}

	Sleep(100);
	CloseHandle(thread);
	VirtualFreeEx(proc, lib_name_addr, path_len, MEM_RELEASE);

	return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH || reason == DLL_PROCESS_DETACH)
	{
		const auto(*const signature)[2] = (DWORD(*)[2])0x43537E;
		if ((*signature)[0] != 0xffffe281 || (*signature)[1] != 0x8b5200ff)
			return FALSE;

		CStringA path;
		if (!GetDLLPath(path))
			return FALSE;

		const auto pid = GetCurrentProcessId();
		const auto proc = OpenProcess(PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION, 0, pid);
		if (!proc)
			return S_FALSE;

		auto ret = InjectDLL(proc, path);

		CloseHandle(proc);
		return ret ? S_OK : S_FALSE;
	}

	return TRUE;
}

