#include "wrapper.h"

/* Fixup MMX induced difference between DirectDraw drivers */
/* Seen as magenta background on some bitmaps */
static const BYTE blend_key_compare_stub[] = \
{
	0x0F, 0x7F, 0xC4,	   /* movq	  mm4, mm0   ; original */
	0x0F, 0x76, 0xFF,	   /* pcmpeqd   mm7, mm7   ; all ones */
	0x0F, 0x72, 0xD7, 0x08, /* psrld	 mm7, 8	 ; 0x00FFFFFF per pixel */
	0x0F, 0xDB, 0xE7,	   /* pand	  mm4, mm7   ; drop the top byte */
	0x0F, 0x60, 0xEB,	   /* punpcklbw mm5, mm3   ; original */
	0xC3,				   /* ret */
};

static void patch_blend_key_compare(void)
{
	BYTE *stub, call[6];
	DWORD old;

	stub = (BYTE *)VirtualAlloc(NULL, sizeof(blend_key_compare_stub), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!stub)
		return;
	memcpy(stub, blend_key_compare_stub, sizeof(blend_key_compare_stub));

	/* 0x4046d5: call stub (5 bytes) + nop; 0x4046db (movq mm6,mm1) follows unchanged. */
	call[0] = 0xE8;
	*(LONG *)(call + 1) = (LONG)(stub - (BYTE *)(0x4046D5 + 5));
	call[5] = 0x90;
	if (!VirtualProtect(stub, sizeof(blend_key_compare_stub), PAGE_EXECUTE_READ, &old) ||
		!patch_bytes(0x4046D5, "\x0F\x7F\xC4\x0F\x60\xEB", call, sizeof(call)))
		VirtualFree(stub, 0, MEM_RELEASE);
}

void apply_true_color(void)
{
	/* cmp eax,2 / je ok  -> cmp eax,4 / jbe ok */
	patch_bytes(0x408187, "\x83\xF8\x02\x74\x6E", "\x83\xF8\x04\x76\x6E", 5);
	patch_blend_key_compare();
}

static BOOL is_near_key(BYTE r, BYTE g, BYTE b)
{
	return (r >> 3) == 31 && (g >> 2) == 0 && (b >> 3) == 31 && !(r == 0xFF && g == 0 && b == 0xFF);
}

static int WINAPI Hook_StretchDIBits(HDC dc, int xd, int yd, int wd, int hd, int xs, int ys, int ws, int hs,
									 const VOID *bits, const BITMAPINFO *bmi, UINT usage, DWORD rop)
{
	const BITMAPINFOHEADER *h = bmi ? &bmi->bmiHeader : NULL;
	int width, rows, bpp, x0, x1, y0, y1, x, y;
	DWORD stride, sub_stride;
	const BYTE *src;
	BYTE *sub;
	BITMAPINFOHEADER sub_hdr;
	BOOL any = FALSE;
	int result;

	if (!h || !bits || usage != DIB_RGB_COLORS || h->biCompression != BI_RGB ||
		(h->biBitCount != 24 && h->biBitCount != 32) || h->biHeight <= 0 || h->biWidth <= 0)
		return StretchDIBits(dc, xd, yd, wd, hd, xs, ys, ws, hs, bits, bmi, usage, rop);

	width = h->biWidth;
	rows = h->biHeight;
	bpp = h->biBitCount / 8;
	stride = ((DWORD)width * bpp + 3) & ~3u;
	src = (const BYTE *)bits;

	if (ws > 0 && hs > 0)
	{
		x0 = max(xs, 0), x1 = min(xs + ws, width);
		y0 = max(ys, 0), y1 = min(ys + hs, rows);
	}
	else
	{
		x0 = 0, x1 = width, y0 = 0, y1 = rows;
	}
	if (x0 >= x1 || y0 >= y1)
		return StretchDIBits(dc, xd, yd, wd, hd, xs, ys, ws, hs, bits, bmi, usage, rop);

	for (y = y0; y < y1 && !any; y++)
	{
		const BYTE *px = src + y * stride + x0 * bpp;
		for (x = x0; x < x1 && !any; x++, px += bpp)
			any = is_near_key(px[2], px[1], px[0]);
	}
	if (!any)
		return StretchDIBits(dc, xd, yd, wd, hd, xs, ys, ws, hs, bits, bmi, usage, rop);

	sub_stride = ((DWORD)(x1 - x0) * bpp + 3) & ~3u;
	sub = (BYTE *)HeapAlloc(GetProcessHeap(), 0, sub_stride * (y1 - y0));
	if (!sub)
		return StretchDIBits(dc, xd, yd, wd, hd, xs, ys, ws, hs, bits, bmi, usage, rop);

	for (y = y0; y < y1; y++)
	{
		BYTE *px = sub + (y - y0) * sub_stride;
		memcpy(px, src + y * stride + x0 * bpp, (x1 - x0) * bpp);
		for (x = x0; x < x1; x++, px += bpp)
		{
			if (is_near_key(px[2], px[1], px[0]))
			{
				px[0] = 0xFF;
				px[1] = 0;
				px[2] = 0xFF;
			}
		}
	}
	sub_hdr = *h;
	sub_hdr.biSize = sizeof(sub_hdr);
	sub_hdr.biWidth = x1 - x0;
	sub_hdr.biHeight = y1 - y0;
	sub_hdr.biSizeImage = 0;
	sub_hdr.biClrUsed = 0;
	sub_hdr.biClrImportant = 0;
	result = StretchDIBits(dc, xd, yd, wd, hd, xs - x0, ys - y0, ws, hs, sub, (BITMAPINFO *)&sub_hdr, usage, rop);
	HeapFree(GetProcessHeap(), 0, sub);
	return result;
}

void install_color_key_fix(void)
{
	patch_import(0x443018, 0x449CF8, "StretchDIBits", /*strlen*/13, (void *)Hook_StretchDIBits);
}
