#include "internal.h"
#include <c2d/text.h>
#include <stdlib.h>
#include <stdarg.h>

static C3D_Tex* s_glyphSheets;
static float s_textScale;

typedef struct C2Di_Glyph_s
{
	u32 lineNo;
	C3D_Tex* sheet;
	float xPos;
	float width;
	struct
	{
		float left, top, right, bottom;
	} texcoord;
} C2Di_Glyph;

struct C2D_TextBuf_s
{
	u32 reserved[2];
	size_t glyphCount;
	size_t glyphBufSize;
	C2Di_Glyph glyphs[0];
};

static size_t C2Di_TextBufBufferSize(size_t maxGlyphs)
{
	return sizeof(struct C2D_TextBuf_s) + maxGlyphs*sizeof(C2Di_Glyph);
}

static int C2Di_GlyphComp(const void* _g1, const void* _g2)
{
	const C2Di_Glyph* g1 = (C2Di_Glyph*)_g1;
	const C2Di_Glyph* g2 = (C2Di_Glyph*)_g2;
	int ret = (int)g1->sheet - (int)g2->sheet;
	if (ret == 0)
		ret = (int)g1 - (int)g2;
	return ret;
}

static void C2Di_TextEnsureLoad(void)
{
	// Skip if already loaded
	if (s_glyphSheets)
		return;

	// Ensure the shared system font is mapped
	if (R_FAILED(fontEnsureMapped()))
		svcBreak(USERBREAK_PANIC);

	// Load the glyph texture sheets
	TGLP_s* glyphInfo = fontGetGlyphInfo();
	s_glyphSheets = malloc(sizeof(C3D_Tex)*glyphInfo->nSheets);
	s_textScale = 30.0f / glyphInfo->cellHeight;
	if (!s_glyphSheets)
		svcBreak(USERBREAK_PANIC);

	int i;
	for (i = 0; i < glyphInfo->nSheets; i ++)
	{
		C3D_Tex* tex = &s_glyphSheets[i];
		tex->data = fontGetGlyphSheetTex(i);
		tex->fmt = glyphInfo->sheetFmt;
		tex->size = glyphInfo->sheetSize;
		tex->width = glyphInfo->sheetWidth;
		tex->height = glyphInfo->sheetHeight;
		tex->param = GPU_TEXTURE_MAG_FILTER(GPU_LINEAR) | GPU_TEXTURE_MIN_FILTER(GPU_LINEAR)
			| GPU_TEXTURE_WRAP_S(GPU_CLAMP_TO_BORDER) | GPU_TEXTURE_WRAP_T(GPU_CLAMP_TO_BORDER);
		tex->border = 0xFFFFFFFF;
		tex->lodParam = 0;
	}
}

C2D_TextBuf C2D_TextBufNew(size_t maxGlyphs)
{
	C2Di_TextEnsureLoad();

	C2D_TextBuf buf = (C2D_TextBuf)malloc(C2Di_TextBufBufferSize(maxGlyphs));
	if (!buf) return NULL;
	memset(buf, 0, sizeof(struct C2D_TextBuf_s));
	buf->glyphBufSize = maxGlyphs;
	return buf;
}

C2D_TextBuf C2D_TextBufResize(C2D_TextBuf buf, size_t maxGlyphs)
{
	size_t oldMax = buf->glyphBufSize;
	C2D_TextBuf newBuf = (C2D_TextBuf)realloc(buf, C2Di_TextBufBufferSize(maxGlyphs));
	if (!newBuf) return NULL;

	// zero out new glyphs
	if (maxGlyphs > oldMax)
		memset(&newBuf->glyphs[oldMax], 0, (maxGlyphs-oldMax)*sizeof(C2Di_Glyph));

	newBuf->glyphBufSize = maxGlyphs;
	return newBuf;

}

void C2D_TextBufDelete(C2D_TextBuf buf)
{
	free(buf);
}

void C2D_TextBufClear(C2D_TextBuf buf)
{
	buf->glyphCount = 0;
}

size_t C2D_TextBufGetNumGlyphs(C2D_TextBuf buf)
{
	return buf->glyphCount;
}

const char* C2D_TextParseLine(C2D_Text* text, C2D_TextBuf buf, const char* str, u32 lineNo)
{
	const uint8_t* p = (const uint8_t*)str;
	text->buf   = buf;
	text->begin = buf->glyphCount;
	text->width = 0.0f;
	while (buf->glyphCount < buf->glyphBufSize)
	{
		uint32_t code;
		ssize_t units = decode_utf8(&code, p);
		if (units == -1)
		{
			code = 0xFFFD;
			units = 1;
		} else if (code == 0 || code == '\n')
			break;
		p += units;

		fontGlyphPos_s glyphData;
		fontCalcGlyphPos(&glyphData, fontGlyphIndexFromCodePoint(code), 0, 1.0f, 1.0f);
		if (glyphData.width > 0.0f)
		{
			C2Di_Glyph* glyph      = &buf->glyphs[buf->glyphCount++];
			glyph->sheet           = &s_glyphSheets[glyphData.sheetIndex];
			glyph->xPos            = text->width + glyphData.xOffset;
			glyph->lineNo          = lineNo;
			glyph->width           = glyphData.width;
			glyph->texcoord.left   = glyphData.texcoord.left;
			glyph->texcoord.top    = glyphData.texcoord.top;
			glyph->texcoord.right  = glyphData.texcoord.right;
			glyph->texcoord.bottom = glyphData.texcoord.bottom;
		}
		text->width += glyphData.xAdvance;
	}
	text->end = buf->glyphCount;
	text->width *= s_textScale;
	text->lines = 1;
	return (const char*)p;
}

const char* C2D_TextParse(C2D_Text* text, C2D_TextBuf buf, const char* str)
{
	u32 lineNo  = 0;
	text->buf   = buf;
	text->begin = buf->glyphCount;
	text->width = 0.0f;

	for (;;)
	{
		C2D_Text temp;
		str = C2D_TextParseLine(&temp, buf, str, lineNo++);
		if (temp.width > text->width)
			text->width = temp.width;
		if (!str || *str != '\n')
			break;
		str++;
	}

	text->end = buf->glyphCount;
	text->lines = lineNo;
	return str;
}

void C2D_TextOptimize(const C2D_Text* text)
{
	// Dirty and probably not very efficient/overkill, but it should work
	qsort(&text->buf->glyphs[text->begin], text->end-text->begin, sizeof(C2Di_Glyph), C2Di_GlyphComp);
}

void C2D_TextGetDimensions(const C2D_Text* text, float scaleX, float scaleY, float* outWidth, float* outHeight)
{
	if (outWidth)
		*outWidth  = scaleX*text->width;
	if (outHeight)
		*outHeight = ceilf(scaleY*s_textScale*fontGetInfo()->lineFeed)*text->lines;
}

void C2D_DrawText(const C2D_Text* text, u32 flags, float x, float y, float z, float scaleX, float scaleY, ...)
{
	C2Di_Glyph* begin = &text->buf->glyphs[text->begin];
	C2Di_Glyph* end   = &text->buf->glyphs[text->end];
	C2Di_Glyph* cur;

	scaleX *= s_textScale;
	scaleY *= s_textScale;

	float glyphZ = z;
	float glyphH = scaleY*fontGetGlyphInfo()->cellHeight;
	float dispY = ceilf(scaleY*fontGetInfo()->lineFeed);
	u32 color = 0xFF000000;

	va_list va;
	va_start(va, scaleY);

	if (flags & C2D_AtBaseline)
		y -= scaleY*fontGetGlyphInfo()->baselinePos;
	if (flags & C2D_WithColor)
		color = va_arg(va, u32);

	va_end(va);

	for (cur = begin; cur != end; ++cur)
	{
		float glyphW = scaleX*cur->width;
		float glyphX = x+scaleX*cur->xPos;
		float glyphY = y+dispY*cur->lineNo;

		C2Di_SetTex(cur->sheet);
		C2Di_Update();
		C2Di_AppendVtx(glyphX,        glyphY,        glyphZ, cur->texcoord.left,  cur->texcoord.top,    1.0f, color);
		C2Di_AppendVtx(glyphX,        glyphY+glyphH, glyphZ, cur->texcoord.left,  cur->texcoord.bottom, 1.0f, color);
		C2Di_AppendVtx(glyphX+glyphW, glyphY,        glyphZ, cur->texcoord.right, cur->texcoord.top,    1.0f, color);
		C2Di_AppendVtx(glyphX+glyphW, glyphY,        glyphZ, cur->texcoord.right, cur->texcoord.top,    1.0f, color);
		C2Di_AppendVtx(glyphX,        glyphY+glyphH, glyphZ, cur->texcoord.left,  cur->texcoord.bottom, 1.0f, color);
		C2Di_AppendVtx(glyphX+glyphW, glyphY+glyphH, glyphZ, cur->texcoord.right, cur->texcoord.bottom, 1.0f, color);
	}
}
