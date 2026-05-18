#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "internal.h"
#include <c2d/font.h>

C2D_Font_s g_systemFont;

C2D_Font C2D_FontLoad(const char* filename)
{
	FILE* f = fopen(filename, "rb");
	if (!f) return NULL;
	C2D_Font ret = C2D_FontLoadFromHandle(f);
	fclose(f);
	return ret;
}

static inline C2D_Font C2Di_FontAlloc(void)
{
	return (C2D_Font)malloc(sizeof(struct C2D_Font_s));
}

static void fillSheet(C3D_Tex *tex, void *data, TGLP_s *glyphInfo)
{
	tex->data     = data;
	tex->fmt      = glyphInfo->sheetFmt;
	tex->size     = glyphInfo->sheetSize;
	tex->width    = glyphInfo->sheetWidth;
	tex->height   = glyphInfo->sheetHeight;
	tex->param    = GPU_TEXTURE_MAG_FILTER(GPU_LINEAR) | GPU_TEXTURE_MIN_FILTER(GPU_LINEAR)
		| GPU_TEXTURE_WRAP_S(GPU_CLAMP_TO_EDGE) | GPU_TEXTURE_WRAP_T(GPU_CLAMP_TO_EDGE);
	tex->border   = 0;
	tex->lodParam = 0;
}

static C2D_Font C2Di_PostLoadFont(C2D_Font font)
{
	if (!font->cfnt)
	{
		free(font);
		font = NULL;
	}
	else
	{
		if (font->cfnt != fontGetSystemFont())
		{
			fontFixPointers(font->cfnt);
		}

		TGLP_s* glyphInfo = font->cfnt->finf.tglp;
		font->textScale = 30.0f / glyphInfo->cellHeight;

		// The way TGLP_s is set up, all of a font's texture sheets are adjacent in memory and have the same size. We can
		// reinterpet the memory to describe a smaller set of much taller textures if we'd like. If we choose the right size,
		// we can get all of the ASCII glyphs under a single texture, which will massively improve performance by reducing
		// texture swaps within a piece of all-English text down to 0! We don't need any extra linear allocating to do this!
		// Let's combine as many sheets as it takes to get to a big sheet height of 1024, which is the maximum height a
		// texture can have.
		font->sheetsPerBigSheet = 1024U / glyphInfo->sheetHeight;
		u32 numSheetsBig   = glyphInfo->nSheets / font->sheetsPerBigSheet;
		u32 numSheetsSmall = glyphInfo->nSheets % font->sheetsPerBigSheet;
		u32 numSheetsTotal = numSheetsBig + numSheetsSmall;
		font->numSheetsCombined = glyphInfo->nSheets - numSheetsSmall;

		font->glyphSheets = malloc(sizeof(C3D_Tex)*numSheetsTotal);
		if (!font->glyphSheets)
		{
			C2D_FontFree(font);
			return NULL;
		}
		memset(font->glyphSheets, 0, sizeof(sizeof(C3D_Tex)*numSheetsTotal));
		for (u32 i = 0; i < numSheetsBig; i++)
		{
			C3D_Tex* tex = &font->glyphSheets[i];
			fillSheet(tex, fontGetGlyphSheetTex(font->cfnt, i * font->sheetsPerBigSheet), glyphInfo);
			tex->height = (uint16_t) (tex->height * font->sheetsPerBigSheet);
			tex->size   = tex->size * font->sheetsPerBigSheet;
		}

		for (u32 i = 0; i < numSheetsSmall; i++)
		{
			fillSheet(&font->glyphSheets[numSheetsBig + i], fontGetGlyphSheetTex(font->cfnt, numSheetsBig * font->sheetsPerBigSheet + i), glyphInfo);
		}

		for (u32 i = 0; i < NUM_ASCII_CHARACTERS; i++)
		{
			// This will readjust glyph UVs to account for being a part of the combined texture.
			C2D_FontCalcGlyphPos(NULL, &font->asciiCache[i], fontGlyphIndexFromCodePoint(font->cfnt, i), 0, 1.0, 1.0);
		}
	}
	return font;
}

C2D_Font C2D_FontLoadFromMem(const void* data, size_t size)
{
	C2D_Font font = C2Di_FontAlloc();
	if (font)
	{
		font->cfnt = linearAlloc(size);
		if (font->cfnt)
			memcpy(font->cfnt, data, size);
		font = C2Di_PostLoadFont(font);
	}
	return font;
}

C2D_Font C2D_FontLoadFromFD(int fd)
{
	C2D_Font font = C2Di_FontAlloc();
	if (font)
	{
		CFNT_s cfnt;
		read(fd, &cfnt, sizeof(CFNT_s));
		font->cfnt = linearAlloc(cfnt.fileSize);
		if (font->cfnt)
		{
			memcpy(font->cfnt, &cfnt, sizeof(CFNT_s));
			read(fd, (u8*)(font->cfnt) + sizeof(CFNT_s), cfnt.fileSize - sizeof(CFNT_s));
		}
		font = C2Di_PostLoadFont(font);
	}
	return font;
}

C2D_Font C2D_FontLoadFromHandle(FILE* handle)
{
	C2D_Font font = C2Di_FontAlloc();
	if (font)
	{
		CFNT_s cfnt;
		fread(&cfnt, 1, sizeof(CFNT_s), handle);
		font->cfnt = linearAlloc(cfnt.fileSize);
		if (font->cfnt)
		{
			memcpy(font->cfnt, &cfnt, sizeof(CFNT_s));
			fread((u8*)(font->cfnt) + sizeof(CFNT_s), 1, cfnt.fileSize - sizeof(CFNT_s), handle);
		}
		font = C2Di_PostLoadFont(font);
	}
	return font;
}

static C2D_Font C2Di_FontLoadFromArchive(u64 tid, const char* path)
{
	void* fontLzData = NULL;
	u32 fontLzSize = 0;

	Result rc = romfsMountFromTitle(tid, MEDIATYPE_NAND, "font");
	if (R_FAILED(rc))
		return NULL;

	FILE* f = fopen(path, "rb");
	if (f)
	{
		fseek(f, 0, SEEK_END);
		fontLzSize = ftell(f);
		rewind(f);

		fontLzData = malloc(fontLzSize);
		if (fontLzData)
			fread(fontLzData, 1, fontLzSize, f);

		fclose(f);
	}

	romfsUnmount("font");

	if (!fontLzData)
		return NULL;

	C2D_Font font = C2Di_FontAlloc();
	if (!font)
	{
		free(fontLzData);
		return NULL;
	}

	u32 fontSize = *(u32*)fontLzData >> 8;
	font->cfnt = linearAlloc(fontSize);
	if (font->cfnt && !decompress_LZ11(font->cfnt, fontSize, NULL, (u8*)fontLzData + 4, fontLzSize - 4))
	{
		linearFree(font->cfnt);
		font->cfnt = NULL;
	}
	free(fontLzData);

	return C2Di_PostLoadFont(font);
}

C2D_Font C2Di_LoadSystemFont(void)
{
	g_systemFont.cfnt = fontGetSystemFont();
	C2Di_PostLoadFont(&g_systemFont);
	return &g_systemFont;
}

static unsigned C2Di_RegionToFontIndex(CFG_Region region)
{
	switch (region)
	{
		default:
		case CFG_REGION_JPN:
		case CFG_REGION_USA:
		case CFG_REGION_EUR:
		case CFG_REGION_AUS:
			return 0;
		case CFG_REGION_CHN:
			return 1;
		case CFG_REGION_KOR:
			return 2;
		case CFG_REGION_TWN:
			return 3;
	}
}

static const char* const C2Di_FontPaths[] =
{
	"font:/cbf_std.bcfnt.lz",
	"font:/cbf_zh-Hans-CN.bcfnt.lz",
	"font:/cbf_ko-Hang-KR.bcfnt.lz",
	"font:/cbf_zh-Hant-TW.bcfnt.lz",
};

C2D_Font C2D_FontLoadSystem(CFG_Region region)
{
	unsigned fontIdx = C2Di_RegionToFontIndex(region);

	u8 systemRegion = 0;
	Result rc = CFGU_SecureInfoGetRegion(&systemRegion);
	if (R_FAILED(rc) || fontIdx == C2Di_RegionToFontIndex((CFG_Region)systemRegion))
	{
		fontEnsureMapped();
		return NULL;
	}

	// Load the font
	return C2Di_FontLoadFromArchive(0x0004009b00014002ULL | (fontIdx<<8), C2Di_FontPaths[fontIdx]);
}

void C2D_FontFree(C2D_Font font)
{
	if (font && font != &g_systemFont)
	{
		if (font->cfnt)
			linearFree(font->cfnt);
		free(font->glyphSheets);
	}
}

void C2D_FontSetFilter(C2D_Font font, GPU_TEXTURE_FILTER_PARAM magFilter, GPU_TEXTURE_FILTER_PARAM minFilter)
{
	if (!font || font == &g_systemFont)
		return;

	TGLP_s* glyphInfo = font->cfnt->finf.tglp;

	int i;
	for (i = 0; i < glyphInfo->nSheets; i++)
	{
		C3D_Tex* tex = &font->glyphSheets[i];
		C3D_TexSetFilter(tex, magFilter, minFilter);
	}
}

int C2D_FontGlyphIndexFromCodePoint(C2D_Font font, u32 codepoint)
{
	if (!font) font = &g_systemFont;
	return fontGlyphIndexFromCodePoint(font->cfnt, codepoint);
}

charWidthInfo_s* C2D_FontGetCharWidthInfo(C2D_Font font, int glyphIndex)
{
	if (!font) font = &g_systemFont;
	return fontGetCharWidthInfo(font->cfnt, glyphIndex);
}

void C2D_FontCalcGlyphPos(C2D_Font font, fontGlyphPos_s* out, int glyphIndex, u32 flags, float scaleX, float scaleY)
{
	if (!font) font = &g_systemFont;
	fontCalcGlyphPos(out, font->cfnt, glyphIndex, flags, scaleX, scaleY);

	if (out->sheetIndex < font->numSheetsCombined)
	{
		u32 indexWithinBigSheet = out->sheetIndex % font->sheetsPerBigSheet;
		out->sheetIndex /= font->sheetsPerBigSheet;

		// Readjust glyph UVs to account for being a part of the combined texture.
		out->texcoord.top    = (out->texcoord.top    + (font->sheetsPerBigSheet - indexWithinBigSheet - 1)) / (float) font->sheetsPerBigSheet;
		out->texcoord.bottom = (out->texcoord.bottom + (font->sheetsPerBigSheet - indexWithinBigSheet - 1)) / (float) font->sheetsPerBigSheet;
	}
	else
	{
		out->sheetIndex = out->sheetIndex - font->numSheetsCombined + font->numSheetsCombined / font->sheetsPerBigSheet;
	}
}

void C2D_FontCalcGlyphPosFromCodePoint(C2D_Font font, fontGlyphPos_s* out, u32 codepoint, u32 flags, float scaleX, float scaleY)
{
	if (!font) font = &g_systemFont;

	// Building glyph positions is pretty expensive, but we could just store the results for plain ASCII.
	if (codepoint < NUM_ASCII_CHARACTERS && flags == 0 && scaleX == 1 && scaleY == 1)
	{
		*out = font->asciiCache[codepoint];
	}
	else
	{
		C2D_FontCalcGlyphPos(font, out, C2D_FontGlyphIndexFromCodePoint(font, codepoint), 0, 1.0f, 1.0f);
	}
}

FINF_s* C2D_FontGetInfo(C2D_Font font)
{
	if (!font) font = &g_systemFont;
	return fontGetInfo(font->cfnt);
}
