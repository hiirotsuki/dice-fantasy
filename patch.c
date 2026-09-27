#include "wrapper.h"

BOOL patch_bytes(DWORD va, const void *orig, const void *repl, size_t size)
{
	BYTE *at = (BYTE *)va;
	DWORD old;

	if (memcmp(at, orig, size) != 0 || !VirtualProtect(at, size, PAGE_EXECUTE_READWRITE, &old))
		return FALSE;

	memcpy(at, repl, size);
	VirtualProtect(at, size, old, &old);
	FlushInstructionCache(GetCurrentProcess(), at, size);
	return TRUE;
}

void *swap_pointer(DWORD va, void *value)
{
	void **slot = (void **)va, *prev = *slot;
	DWORD old;

	if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old))
		return NULL;

	*slot = value;
	VirtualProtect(slot, sizeof(*slot), old, &old);
	return prev;
}

void patch_import(DWORD slot_va, DWORD name_str_va, const char *name, DWORD name_len, void *hook)
{
	if (memcmp((const void *)name_str_va, name, name_len + 1) == 0)
		swap_pointer(slot_va, hook);
}

BOOL patch_call(DWORD va, DWORD expected, void *replacement)
{
	BYTE *at = (BYTE *)va, call[5];

	memcpy(call, at, 5);
	if (call[0] != 0xE8 || va + 5 + *(LONG *)(call + 1) != expected)
		return FALSE;

	*(LONG *)(call + 1) = (LONG)((BYTE *)replacement - (at + 5));
	return patch_bytes(va, at, call, 5);
}
