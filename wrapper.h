#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define GAME_CODEPAGE 932 /* Shift-JIS */

extern WCHAR game_dir[MAX_PATH];

BOOL patch_bytes(DWORD va, const void *orig, const void *repl, size_t size);
void *swap_pointer(DWORD va, void *value);
void patch_import(DWORD slot_va, DWORD name_str_va, const char *name, DWORD name_len, void *hook);
BOOL patch_call(DWORD va, DWORD expected, void *replacement);

enum { RENDER_SYSTEM, RENDER_BITMAP };

typedef struct
{
	const WCHAR *src;
	const WCHAR *dst;
	int weight;
	int render;
} TableEntry;

typedef struct
{
	TableEntry *entries;
	int count;
} TranslationTable;

extern int default_renderer;

WCHAR *sjis_to_wide(LPCSTR s);
void load_table(TranslationTable *t, LPCWSTR name);
WCHAR *translate(TranslationTable *t, const WCHAR *text, int *render);

void install_dialog_hook(void);

void apply_true_color(void);
void install_color_key_fix(void);

extern WCHAR font_face[LF_FACESIZE];
extern int font_size, font_bold;

void apply_font_fixes(void);
void install_text_hooks(void);
