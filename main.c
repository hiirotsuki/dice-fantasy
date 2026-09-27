#include "wrapper.h"

WCHAR game_dir[MAX_PATH];

static BOOL emulation;
static BOOL all_computer;
static BOOL true_color = TRUE;
static BOOL unicode_message_box = TRUE;
static BOOL font_table_fix = TRUE;

static void load_settings(void)
{
	WCHAR ini[MAX_PATH], renderer[16], *slash;

	GetModuleFileNameW(NULL, game_dir, MAX_PATH);
	slash = wcsrchr(game_dir, L'\\');
	if (slash)
		slash[1] = 0;
	else
		game_dir[0] = 0;
	lstrcpynW(ini, game_dir, MAX_PATH - 16);
	lstrcatW(ini, L"DiceFantasy.ini"); /* missing file or keys: the defaults below */

#define INI_INT(section, key, def) GetPrivateProfileIntW(section, key, def, ini)
	emulation = INI_INT(L"Game", L"Emulation", emulation) != 0;
	all_computer = INI_INT(L"Game", L"AllComputer", all_computer) != 0;
	true_color = INI_INT(L"Wrapper", L"TrueColor", true_color) != 0;
	unicode_message_box = INI_INT(L"Wrapper", L"UnicodeMessageBox", unicode_message_box) != 0;
	font_table_fix = INI_INT(L"Wrapper", L"FontTableFix", font_table_fix) != 0;

	GetPrivateProfileStringW(L"Text", L"Renderer", L"system", renderer, 16, ini);
	default_renderer = lstrcmpiW(renderer, L"bitmap") == 0 ? RENDER_BITMAP : RENDER_SYSTEM;
	GetPrivateProfileStringW(L"Text", L"FontFace", L"Tahoma", font_face, LF_FACESIZE, ini);
	font_size = INI_INT(L"Text", L"FontSize", font_size);
	font_bold = INI_INT(L"Text", L"FontBold", font_bold) != 0;
	if (font_size < 6 || font_size > 64)
		font_size = 13;
#undef INI_INT
}

static void apply_game_flags(void)
{
	if (emulation) /* 0x42fc4b: je (skips mov [ebp-0x11],1) -> nop nop */
		patch_bytes(0x42FC4B, "\x74\x04", "\x90\x90", 2);
	if (all_computer)
		*(volatile BYTE *)0x451360 = 1; /* byte, 1 = -ALLCOM */
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
	(void)reserved;
	if (reason == DLL_PROCESS_ATTACH)
	{
		DisableThreadLibraryCalls(inst);
		load_settings();

		apply_game_flags();

		if (unicode_message_box)
			install_dialog_hook();
		if (true_color)
		{
			apply_true_color();
			install_color_key_fix();
		}
		if (font_table_fix)
			apply_font_fixes();
		install_text_hooks();
	}
	return TRUE;
}
