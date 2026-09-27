#include "wrapper.h"

void apply_font_fixes(void)
{
	patch_bytes(0x443A2C, "\x88\x82\x29\x00", "\x78\x82\x29\x00", 4); /* 'ｈ' -> 'Ｙ', glyph 41 */
	patch_bytes(0x443A30, "\x89\x82\x2A\x00", "\x79\x82\x2A\x00", 4); /* 'ｉ' -> 'Ｚ', glyph 42 */
	patch_bytes(0x443C44, "\x87\x83\xC6\x00", "\x88\x83\xC6\x00", 4); /* 'ョ' -> 'ヨ', glyph 198 */
	patch_bytes(0x443DC8, "\x40\x8F\x41\x01", "\x45\x8F\x41\x01", 4); /* '宗' -> '拾', glyph 321 */
	patch_bytes(0x443E68, "\x80\x8C\x69\x01", "\x88\x8C\x69\x01", 4); /* '劇' -> '決', glyph 361 */
	patch_bytes(0x443E84, "\xE0\x8C\x70\x01", "\xE3\x8C\x70\x01", 4); /* '呉' -> '後', glyph 368 */
	patch_bytes(0x444058, "\x24\x00\x0B\x00", "\x66\x81\x2C\x00", 4); /* '$' (glyph 11) -> '’', glyph 44 */
	patch_bytes(0x444054, "\x23\x00\x0C\x00", "\x68\x81\x2B\x00", 4); /* '#' (glyph 12) -> '”', glyph 43 */
}

#define TB_SURFACE 0x28
#define TB_WIDTH 0x34
#define TB_HEIGHT 0x38
#define TB_DONE 0x7D

#define TEXT_FILL_R 254
#define TEXT_OUTLINE_R 51
#define KEY_PIXEL 0x00FF00FFu

WCHAR font_face[LF_FACESIZE] = L"Tahoma";
int font_size = 13, font_bold = 1;
static HFONT system_font;

static HFONT get_system_font(void)
{
	if (!system_font)
		system_font = CreateFontW(-font_size, 0, 0, 0, font_bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE,
								  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
								  DEFAULT_PITCH | FF_DONTCARE, font_face);

	return system_font;
}

static SIZE measure_system_text(const WCHAR *text)
{
	SIZE size = {0, 0};
	HDC dc = CreateCompatibleDC(NULL);
	RECT r = {0, 0, 0, 0};

	if (dc) {
		HGDIOBJ old = SelectObject(dc, get_system_font());
		DrawTextW(dc, text, -1, &r, DT_CALCRECT | DT_NOPREFIX | DT_LEFT | DT_TOP);
		SelectObject(dc, old);
		DeleteDC(dc);
	}
	size.cx = r.right + 3;
	size.cy = r.bottom + 2;
	return size;
}

/* Outline offsets: 1px all round plus a 2px bottom-right edge, like the bitmap font. */
static const signed char outline_offsets[][2] = {
	{-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1},
	{-2, 0}, {0, -2}, {-2, -1}, {-1, -2}, {-2, -2},
};

static void draw_system_text(void *textbox, const WCHAR *text)
{
	BYTE *tb = (BYTE *)textbox;
	void *surface = *(void **)(tb + TB_SURFACE);
	int w = *(int *)(tb + TB_WIDTH), h = *(int *)(tb + TB_HEIGHT), x, y, i;
	BITMAPINFO bi = {{sizeof(BITMAPINFOHEADER)}};
	HDC mem, sdc;
	HBITMAP bmp;
	HGDIOBJ old_bmp, old_font;
	DWORD *px = NULL;
	BYTE *cov;
	RECT r;

	if (!surface || w <= 0 || h <= 0 || w > 4096 || h > 4096)
		return;

	bi.bmiHeader.biWidth = w;
	bi.bmiHeader.biHeight = -h;
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	mem = CreateCompatibleDC(NULL);
	bmp = mem ? CreateDIBSection(mem, &bi, DIB_RGB_COLORS, (void **)&px, NULL, 0) : NULL;
	cov = (BYTE *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)w * h);
	if (!bmp || !px || !cov)
		goto done;

	old_bmp = SelectObject(mem, bmp);
	old_font = SelectObject(mem, get_system_font());
	memset(px, 0, (size_t)w * h * 4);
	SetTextColor(mem, RGB(255, 255, 255));
	SetBkMode(mem, TRANSPARENT);
	SetRect(&r, 1, 0, w, h);
	DrawTextW(mem, text, -1, &r, DT_NOPREFIX | DT_LEFT | DT_TOP);
	GdiFlush();
	for (i = 0; i < w * h; i++)
		cov[i] = (BYTE)(px[i] >> 8);

	for (y = 0; y < h; y++)
	{
		for (x = 0; x < w; x++)
		{
			BYTE c = cov[y * w + x];
			DWORD out = KEY_PIXEL;
			if (c)
			{
				DWORD v = (TEXT_FILL_R * c + TEXT_OUTLINE_R * (255 - c)) / 255;
				out = v << 16 | v << 8 | v;
			}
			else
			{
				for (i = 0; i < (int)(sizeof(outline_offsets) / sizeof(outline_offsets[0])); i++)
				{
					int nx = x + outline_offsets[i][0], ny = y + outline_offsets[i][1];
					if (nx >= 0 && ny >= 0 && nx < w && ny < h && cov[ny * w + nx])
					{
						out = TEXT_OUTLINE_R << 16 | TEXT_OUTLINE_R << 8 | TEXT_OUTLINE_R;
						break;
					}
				}
			}
			px[y * w + x] = out;
		}
	}

	/* Black magic, don't touch. */
	{
		void **vtbl = *(void ***)surface;
		typedef HRESULT(__stdcall * GetDC_fn)(void *, HDC *);
		typedef HRESULT(__stdcall * ReleaseDC_fn)(void *, HDC);
		if (((GetDC_fn)vtbl[17])(surface, &sdc) >= 0)
		{
			BitBlt(sdc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
			((ReleaseDC_fn)vtbl[26])(surface, sdc);
		}
	}
	SelectObject(mem, old_font);
	SelectObject(mem, old_bmp);
done:
	if (cov)
		HeapFree(GetProcessHeap(), 0, cov);
	if (bmp)
		DeleteObject(bmp);
	if (mem)
		DeleteDC(mem);
}

typedef void(__fastcall *StringMethod)(void *self, void *edx, void *cstr);
typedef void *(__fastcall *CStringFromCStr_fn)(void *self, void *edx, const char *s);
typedef void(__fastcall *CStringDtor_fn)(void *self, void *edx);

#define CString_FromCStr ((CStringFromCStr_fn)0x4076B0)
#define CString_Dtor ((CStringDtor_fn)0x4077F0)
#define BOX_CELL_W 16
#define BOX_LINE_H 18

static TranslationTable text_table;
static StringMethod orig_TextBox_SetText, orig_ActionMessage_SetText, orig_MsgBox_SetTextAutosize;
static int text_hook_depth;
static const WCHAR *pending_system_text;

static WCHAR to_fullwidth(WCHAR c)
{
	switch (c) {
	case L' ': return 0x3000;
	case L',': return 0x3001;  /* '、' (cell 82) */
	case L'\'': return 0x2019; /* '’' (cell 44, see apply_font_fixes) */
	case L'"': return 0x201D;  /* '”' (cell 43, see apply_font_fixes) */
	case L'*': return 0x2605;  /* '★' */
	}
	return (c > 0x20 && c < 0x7F) ? (WCHAR)(c + 0xFEE0) : c;
}

static BOOL prepare_game_text(const char *sjis, WCHAR **system_text, char **bitmap_text)
{
	WCHAR *wide = sjis_to_wide(sjis), *tr, *c;
	int render = RENDER_SYSTEM, n;

	*system_text = NULL;
	*bitmap_text = NULL;
	tr = wide ? translate(&text_table, wide, &render) : NULL;
	if (wide)
		HeapFree(GetProcessHeap(), 0, wide);

	if (!tr)
		return FALSE;

	if (render == RENDER_SYSTEM)
	{
		*system_text = tr;
		return TRUE;
	}
	for (c = tr; *c; c++)
		*c = to_fullwidth(*c);

	n = WideCharToMultiByte(GAME_CODEPAGE, 0, tr, -1, NULL, 0, NULL, NULL);
	if (n > 0 && (*bitmap_text = (char *)HeapAlloc(GetProcessHeap(), 0, n)) != NULL)
		WideCharToMultiByte(GAME_CODEPAGE, 0, tr, -1, *bitmap_text, n, NULL, NULL);

	HeapFree(GetProcessHeap(), 0, tr);
	return *bitmap_text != NULL;
}

static void call_with_string(StringMethod orig, void *self, const char *s)
{
	/* CString is 12 bytes, sizeof(void *) == 4 */
	void *tmp[4];

	CString_FromCStr(tmp, NULL, s);
	orig(self, NULL, tmp);
	CString_Dtor(tmp, NULL);
}

static void set_system_text(void *textbox, const WCHAR *text)
{
	call_with_string(orig_TextBox_SetText, textbox, "");
	*((BYTE *)textbox + TB_DONE) = 1;
	draw_system_text(textbox, text);
}

static char *sizing_placeholder(const WCHAR *text)
{
	SIZE size = measure_system_text(text);
	int lines = 1, cells = (size.cx + BOX_CELL_W - 1) / BOX_CELL_W, i;
	const WCHAR *c;
	char *p, *s;

	for (c = text; *c; c++)
		lines += *c == L'\n';

	if (cells < 1)
		cells = 1;

	s = p = (char *)HeapAlloc(GetProcessHeap(), 0, cells * 2 + lines);
	if (!s)
		return NULL;

	for (i = 0; i < cells; i++)
		*p++ = '\x81', *p++ = '\x40';

	for (i = 1; i < lines; i++)
		*p++ = '\n';

	*p = 0;
	return s;
}

/* Cheating: __thiscall -> __fastcall + unused edx */
static void __fastcall Hook_TextBox_SetText(void *self, void *edx, void *cstr)
{
	WCHAR *system_text;
	char *bitmap_text;

	(void)edx;
	if (pending_system_text)
	{
		const WCHAR *t = pending_system_text;
		pending_system_text = NULL;
		set_system_text(self, t);
		return;
	}
	if (text_hook_depth > 0 || !cstr || !prepare_game_text(*(const char **)((BYTE *)cstr + 4), &system_text, &bitmap_text))
	{
		orig_TextBox_SetText(self, NULL, cstr);
		return;
	}
	text_hook_depth++;
	if (system_text)
	{
		set_system_text(self, system_text);
		HeapFree(GetProcessHeap(), 0, system_text);
	}
	else
	{
		call_with_string(orig_TextBox_SetText, self, bitmap_text);
		HeapFree(GetProcessHeap(), 0, bitmap_text);
	}
	text_hook_depth--;
}

static void call_autosizing(StringMethod orig, void *self, void *cstr)
{
	WCHAR *system_text;
	char *bitmap_text, *placeholder;

	if (text_hook_depth > 0 || !cstr || !prepare_game_text(*(const char **)((BYTE *)cstr + 4), &system_text, &bitmap_text))
	{
		orig(self, NULL, cstr);
		return;
	}
	text_hook_depth++;
	if (system_text)
	{
		placeholder = sizing_placeholder(system_text);
		pending_system_text = system_text; /* consumed by the nested SetText */
		call_with_string(orig, self, placeholder ? placeholder : "");
		pending_system_text = NULL;
		if (placeholder)
			HeapFree(GetProcessHeap(), 0, placeholder);

		HeapFree(GetProcessHeap(), 0, system_text);
	}
	else
	{
		call_with_string(orig, self, bitmap_text);
		HeapFree(GetProcessHeap(), 0, bitmap_text);
	}
	text_hook_depth--;
}

/* Cheating: __thiscall -> __fastcall + unused edx */
static void __fastcall Hook_ActionMessage_SetText(void *self, void *edx, void *cstr)
{
	(void)edx;
	call_autosizing(orig_ActionMessage_SetText, self, cstr);
}

/* Cheating: __thiscall -> __fastcall + unused edx */
static void __fastcall Hook_MsgBox_SetTextAutosize(void *self, void *edx, void *cstr)
{
	(void)edx;
	call_autosizing(orig_MsgBox_SetTextAutosize, self, cstr);
}

void install_text_hooks(void)
{
	load_table(&text_table, L"translation.txt");
	if (!text_table.count)
		return;

	/* TextBox vtable slot 0xA0 */
	if (*(void **)0x444120 == (void *)0x409730)
	{
		orig_TextBox_SetText = (StringMethod)swap_pointer(0x444120, (void *)Hook_TextBox_SetText);
		/* QVText vtable slot 0xA0 */
		if (*(void **)0x445AD8 == (void *)0x409730)
			swap_pointer(0x445AD8, (void *)Hook_TextBox_SetText);
	}
	/* ActionMessage vtable slot 0x30 */
	if (*(void **)0x4443F4 == (void *)0x40D030)
		orig_ActionMessage_SetText = (StringMethod)swap_pointer(0x4443F4, (void *)Hook_ActionMessage_SetText);
	if (patch_call(0x41403F, 0x426C30, (void *)Hook_MsgBox_SetTextAutosize))
		orig_MsgBox_SetTextAutosize = (StringMethod)0x426C30;
}
