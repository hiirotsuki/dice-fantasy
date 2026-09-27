#include "wrapper.h"

static TranslationTable message_table;

static void translate_in_place(WCHAR **text)
{
	WCHAR *tr = *text ? translate(&message_table, *text, NULL) : NULL;
	if (tr)
	{
		HeapFree(GetProcessHeap(), 0, *text);
		*text = tr;
	}
}

static int WINAPI Hook_MessageBoxA(HWND owner, LPCSTR text, LPCSTR caption, UINT type)
{
	WCHAR *wtext = sjis_to_wide(text);
	WCHAR *wcaption = sjis_to_wide(caption);
	int result;

	translate_in_place(&wtext);
	translate_in_place(&wcaption);

	if ((text && !wtext) || (caption && !wcaption))
		result = MessageBoxA(owner, text, caption, type); /* conversion failed */
	else
		result = MessageBoxW(owner, wtext, wcaption, type);

	if (wtext)
		HeapFree(GetProcessHeap(), 0, wtext);
	if (wcaption)
		HeapFree(GetProcessHeap(), 0, wcaption);
	return result;
}

void install_dialog_hook(void)
{
	load_table(&message_table, L"dialog.txt");
	patch_import(0x443178, 0x449B62, "MessageBoxA", /*strlen*/11, (void *)Hook_MessageBoxA);
}
