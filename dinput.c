#include "wrapper.h"

#pragma comment(linker, "/EXPORT:DirectInputCreateA=_DirectInputCreateA@16")

typedef HRESULT(WINAPI *DirectInputCreateA_fn)(HINSTANCE, DWORD, void **, void *);

static DirectInputCreateA_fn real_DirectInputCreateA;

static DirectInputCreateA_fn load_real_DirectInputCreateA(void)
{
	WCHAR path[MAX_PATH];
	UINT len = GetSystemDirectoryW(path, MAX_PATH);
	HMODULE real;

	if (len == 0 || len + 12 >= MAX_PATH)
		return NULL;

	lstrcpyW(path + len, L"\\dinput.dll");
	real = LoadLibraryW(path);
	return real ? (DirectInputCreateA_fn)GetProcAddress(real, "DirectInputCreateA") : NULL;
}

HRESULT WINAPI DirectInputCreateA(HINSTANCE inst, DWORD version, void **out, void *outer)
{
	real_DirectInputCreateA = load_real_DirectInputCreateA();
	if (!real_DirectInputCreateA)
		return E_FAIL;
	return real_DirectInputCreateA(inst, version, out, outer);
}
