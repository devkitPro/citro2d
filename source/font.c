#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "internal.h"
#include <c2d/font.h>

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

static C2D_Font C2Di_PostLoadFont(C2D_Font font)
{
	if (!font->cfnt)
	{
		free(font);
		font = NULL;
	} else
	{
		fontFixPointers(font->cfnt);

		TGLP_s* glyphInfo = font->cfnt->finf.tglp;
		font->glyphSheets = malloc(sizeof(C3D_Tex)*glyphInfo->nSheets);
		font->textScale = 30.0f / glyphInfo->cellHeight;
		if (!font->glyphSheets)
		{
			C2D_FontFree(font);
			return NULL;
		}

		int i;
		for (i = 0; i < glyphInfo->nSheets; i++)
		{
			C3D_Tex* tex = &font->glyphSheets[i];
			tex->data = &glyphInfo->sheetData[glyphInfo->sheetSize*i];
			tex->fmt = glyphInfo->sheetFmt;
			tex->size = glyphInfo->sheetSize;
			tex->width = glyphInfo->sheetWidth;
			tex->height = glyphInfo->sheetHeight;
			tex->param = GPU_TEXTURE_MAG_FILTER(GPU_LINEAR) | GPU_TEXTURE_MIN_FILTER(GPU_LINEAR)
				| GPU_TEXTURE_WRAP_S(GPU_CLAMP_TO_BORDER) | GPU_TEXTURE_WRAP_T(GPU_CLAMP_TO_BORDER);
			tex->border = 0;
			tex->lodParam = 0;
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
	if (font)
	{
		if (font->cfnt)
			linearFree(font->cfnt);
		free(font->glyphSheets);
	}
}

void C2D_FontSetFilter(C2D_Font font, GPU_TEXTURE_FILTER_PARAM magFilter, GPU_TEXTURE_FILTER_PARAM minFilter)
{
	if (!font)
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
	if (!font)
		return fontGlyphIndexFromCodePoint(fontGetSystemFont(), codepoint);
	else
		return fontGlyphIndexFromCodePoint(font->cfnt, codepoint);
}

charWidthInfo_s* C2D_FontGetCharWidthInfo(C2D_Font font, int glyphIndex)
{
	if (!font)
		return fontGetCharWidthInfo(fontGetSystemFont(), glyphIndex);
	else
		return fontGetCharWidthInfo(font->cfnt, glyphIndex);
}

void C2D_FontCalcGlyphPos(C2D_Font font, fontGlyphPos_s* out, int glyphIndex, u32 flags, float scaleX, float scaleY)
{
	if (!font)
		fontCalcGlyphPos(out, fontGetSystemFont(), glyphIndex, flags, scaleX, scaleY);
	else
		fontCalcGlyphPos(out, font->cfnt, glyphIndex, flags, scaleX, scaleY);
}

FINF_s* C2D_FontGetInfo(C2D_Font font)
{
	if (!font)
		return fontGetInfo(NULL);
	else
		return fontGetInfo(font->cfnt);
}
