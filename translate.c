#include "wrapper.h"

#define MAX_CAPTURES 4

int default_renderer = RENDER_SYSTEM;

WCHAR *sjis_to_wide(LPCSTR s)
{
	int n;
	WCHAR *w;

	if (!s)
		return NULL;
	n = MultiByteToWideChar(GAME_CODEPAGE, 0, s, -1, NULL, 0);
	if (n <= 0)
		return NULL;

	w = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, n * sizeof(WCHAR));
	if (w && MultiByteToWideChar(GAME_CODEPAGE, 0, s, -1, w, n) != n)
	{
		HeapFree(GetProcessHeap(), 0, w);
		w = NULL;
	}
	return w;
}

static void unescape(WCHAR *s)
{
	WCHAR *in = s, *out = s;

	for (; *in; in++)
	{
		if (*in == L'\\' && in[1])
		{
			in++;
			*out++ = *in == L'n' ? L'\n' : *in == L't' ? L'\t' : *in;
		}
		else
		{
			*out++ = *in;
		}
	}
	*out = 0;
}

static int compare_entries(const void *a, const void *b)
{
	return ((const TableEntry *)b)->weight - ((const TableEntry *)a)->weight;
}

void load_table(TranslationTable *t, LPCWSTR name)
{
	WCHAR path[MAX_PATH];
	HANDLE f;
	DWORD size, got;
	BYTE *utf8;
	WCHAR *text, *line, *next;
	int wlen, lines = 0, i;

	if (lstrlenW(game_dir) + lstrlenW(name) >= MAX_PATH)
		return;

	lstrcpyW(path, game_dir);
	lstrcatW(path, name);
	f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (f == INVALID_HANDLE_VALUE)
		return;

	size = GetFileSize(f, NULL);
	utf8 = (size != INVALID_FILE_SIZE && size < (16u << 20)) ? (BYTE *)HeapAlloc(GetProcessHeap(), 0, size) : NULL;
	if (!utf8 || !ReadFile(f, utf8, size, &got, NULL) || got != size)
	{
		CloseHandle(f);
		return;
	}
	CloseHandle(f);

	i = (size >= 3 && utf8[0] == 0xEF && utf8[1] == 0xBB && utf8[2] == 0xBF) ? 3 : 0;
	wlen = MultiByteToWideChar(CP_UTF8, 0, (LPCSTR)utf8 + i, size - i, NULL, 0);
	text = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, (wlen + 1) * sizeof(WCHAR));
	if (!text)
	{
		HeapFree(GetProcessHeap(), 0, utf8);
		return;
	}
	MultiByteToWideChar(CP_UTF8, 0, (LPCSTR)utf8 + i, size - i, text, wlen);
	text[wlen] = 0;
	HeapFree(GetProcessHeap(), 0, utf8);

	for (i = 0; i < wlen; i++)
		lines += text[i] == L'\n';

	t->entries = (TableEntry *)HeapAlloc(GetProcessHeap(), 0, (lines + 1) * sizeof(TableEntry));
	if (!t->entries)
		return;

	for (line = text; line; line = next)
	{
		TableEntry *e = &t->entries[t->count];
		WCHAR *tab, *end, *c;
		int weight = 0;

		next = wcschr(line, L'\n');
		if (next)
			*next++ = 0;

		end = line + wcslen(line);
		if (end > line && end[-1] == L'\r')
			*--end = 0;

		if (*line == L'#' || !(tab = wcschr(line, L'\t')))
			continue;

		*tab = 0;
		unescape(line);
		unescape(tab + 1);
		for (c = line; *c; c++)
		{
			if (c[0] == L'%' && c[1] == L'd')
				c++; /* placeholder */
			else
				weight++;
		}
		if (weight == 0)
			continue;

		e->src = line;
		e->dst = tab + 1;
		e->weight = weight;
		e->render = default_renderer;
		if (wcsncmp(e->dst, L"{system}", 8) == 0)
			e->dst += 8, e->render = RENDER_SYSTEM;
		else if (wcsncmp(e->dst, L"{bitmap}", 8) == 0)
			e->dst += 8, e->render = RENDER_BITMAP;
		t->count++;
	}
	qsort(t->entries, t->count, sizeof(TableEntry), compare_entries);
}

static size_t match_pattern(const WCHAR *text, const WCHAR *pat, const WCHAR **caps, size_t *cap_lens, int *ncaps)
{
	const WCHAR *start = text;

	*ncaps = 0;
	while (*pat)
	{
		if (pat[0] == L'%' && pat[1] == L'd')
		{
			const WCHAR *digits = text;
			while (*text >= L'0' && *text <= L'9')
				text++;

			if (text == digits)
				return 0;

			if (*ncaps < MAX_CAPTURES)
			{
				caps[*ncaps] = digits;
				cap_lens[*ncaps] = text - digits;
				(*ncaps)++;
			}
			pat += 2;
		}
		else if (*pat++ != *text++)
		{
			return 0;
		}
	}
	return text - start;
}

typedef struct
{
	WCHAR *buf;
	size_t len, cap;
} WideBuf;

static BOOL wb_append(WideBuf *b, const WCHAR *s, size_t n)
{
	if (b->len + n + 1 > b->cap)
	{
		size_t cap = (b->len + n + 1) * 2;
		WCHAR *bigger = b->buf ? (WCHAR *)HeapReAlloc(GetProcessHeap(), 0, b->buf, cap * sizeof(WCHAR))
							   : (WCHAR *)HeapAlloc(GetProcessHeap(), 0, cap * sizeof(WCHAR));

		if (!bigger)
			return FALSE;

		b->buf = bigger;
		b->cap = cap;
	}
	memcpy(b->buf + b->len, s, n * sizeof(WCHAR));
	b->len += n;
	b->buf[b->len] = 0;
	return TRUE;
}

static BOOL emit_translation(WideBuf *out, const WCHAR *dst, const WCHAR **caps, const size_t *cap_lens, int ncaps)
{
	const WCHAR *lit = dst;
	int next_cap = 0;

	for (; *dst; dst++)
	{
		if (dst[0] != L'%' || dst[1] != L'd')
			continue;

		if (!wb_append(out, lit, dst - lit))
			return FALSE;

		if (next_cap < ncaps && !wb_append(out, caps[next_cap], cap_lens[next_cap]))
			return FALSE;

		next_cap++;
		lit = ++dst + 1;
	}
	return wb_append(out, lit, dst - lit);
}

WCHAR *translate(TranslationTable *t, const WCHAR *text, int *render)
{
	WideBuf out = {0};
	BOOL changed = FALSE, ok = TRUE;

	if (render)
		*render = RENDER_SYSTEM;

	if (!text || t->count == 0)
		return NULL;

	while (ok && *text)
	{
		const WCHAR *caps[MAX_CAPTURES];
		size_t cap_lens[MAX_CAPTURES], used = 0;
		int i, ncaps = 0;

		for (i = 0; i < t->count && !used; i++)
			used = match_pattern(text, t->entries[i].src, caps, cap_lens, &ncaps);
		if (!used)
		{
			ok = wb_append(&out, text++, 1);
			continue;
		}
		ok = emit_translation(&out, t->entries[i - 1].dst, caps, cap_lens, ncaps);
		if (render && t->entries[i - 1].render == RENDER_BITMAP)
			*render = RENDER_BITMAP;
		text += used;
		changed = TRUE;
	}
	if (!ok || !changed)
	{
		if (out.buf)
			HeapFree(GetProcessHeap(), 0, out.buf);
		return NULL;
	}
	return out.buf;
}
