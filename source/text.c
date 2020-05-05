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
	u32 wordNo : 31;
	bool isWhitespace : 1;
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
	CFNT_s* font = fontGetSystemFont();
	TGLP_s* glyphInfo = fontGetGlyphInfo(font);
	s_glyphSheets = malloc(sizeof(C3D_Tex)*glyphInfo->nSheets);
	s_textScale = 30.0f / glyphInfo->cellHeight;
	if (!s_glyphSheets)
		svcBreak(USERBREAK_PANIC);

	int i;
	for (i = 0; i < glyphInfo->nSheets; i ++)
	{
		C3D_Tex* tex = &s_glyphSheets[i];
		tex->data = fontGetGlyphSheetTex(font, i);
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
	for (C2Di_Glyph* g = buf->glyphs; g != buf->glyphs + buf->glyphBufSize; g++)
	{
		g->isWhitespace = false;
	}
}

size_t C2D_TextBufGetNumGlyphs(C2D_TextBuf buf)
{
	return buf->glyphCount;
}

const char* C2D_TextParseLine(C2D_Text* text, C2D_TextBuf buf, const char* str, u32 lineNo)
{
	return C2D_TextFontParseLine(text, NULL, buf, str, lineNo);
}

const char* C2D_TextFontParseLine(C2D_Text* text, C2D_Font font, C2D_TextBuf buf, const char* str, u32 lineNo)
{
	const uint8_t* p = (const uint8_t*)str;
	text->font  = font;
	text->buf   = buf;
	text->begin = buf->glyphCount;
	text->width = 0.0f;
	u32 wordNum = 0;
	// Somewhat hacky initialization to prevent problems with immediately parsed whitespace
	if (buf->glyphCount < buf->glyphBufSize)
	{
		buf->glyphs[buf->glyphCount].isWhitespace = true;
	}
	while (buf->glyphCount < buf->glyphBufSize)
	{
		uint32_t code;
		ssize_t units = decode_utf8(&code, p);
		if (units == -1)
		{
			code = 0xFFFD;
			units = 1;
		} else if (code == 0 || code == '\n')
		{
			// If we last parsed whitespace, increment the word counter
			if (!buf->glyphs[buf->glyphCount].isWhitespace)
				wordNum++;
			break;
		}
		p += units;

		fontGlyphPos_s glyphData;
		C2D_FontCalcGlyphPos(font, &glyphData, C2D_FontGlyphIndexFromCodePoint(font, code), 0, 1.0f, 1.0f);
		if (glyphData.width > 0.0f)
		{
			C2Di_Glyph* glyph = &buf->glyphs[buf->glyphCount++];
			if (font)
				glyph->sheet = &font->glyphSheets[glyphData.sheetIndex];
			else
				glyph->sheet = &s_glyphSheets[glyphData.sheetIndex];
			glyph->xPos            = text->width + glyphData.xOffset;
			glyph->lineNo          = lineNo;
			glyph->wordNo          = wordNum;
			glyph->width           = glyphData.width;
			glyph->texcoord.left   = glyphData.texcoord.left;
			glyph->texcoord.top    = glyphData.texcoord.top;
			glyph->texcoord.right  = glyphData.texcoord.right;
			glyph->texcoord.bottom = glyphData.texcoord.bottom;
			glyph->isWhitespace    = false;
		}
		else
		{
			// Intentionally doesn't advance the buffer. Just uses the next glyph as a placeholder for "whitespace happened" until the next nonwhitespace
			if (!buf->glyphs[buf->glyphCount].isWhitespace)
			{
				wordNum++;
				buf->glyphs[buf->glyphCount].isWhitespace = true;
			}
		}
		text->width += glyphData.xAdvance;
	}
	text->end = buf->glyphCount;
	text->width *= s_textScale;
	text->lines = 1;
	text->words = wordNum;
	return (const char*)p;
}

const char* C2D_TextParse(C2D_Text* text, C2D_TextBuf buf, const char* str)
{
	return C2D_TextFontParse(text, NULL, buf, str);
}

const char* C2D_TextFontParse(C2D_Text* text, C2D_Font font, C2D_TextBuf buf, const char* str)
{
	text->font   = font;
	text->buf    = buf;
	text->begin  = buf->glyphCount;
	text->width  = 0.0f;
	text->words  = 0;
	text->lines  = 0;

	for (;;)
	{
		C2D_Text temp;
		str = C2D_TextFontParseLine(&temp, font, buf, str, text->lines++);
		text->words += temp.words;
		if (temp.width > text->width)
			text->width = temp.width;
		if (!str || *str != '\n')
			break;
		str++;
	}

	text->end = buf->glyphCount;
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
	{
		if (text->font)
			*outHeight = ceilf(scaleY*text->font->textScale*text->font->cfnt->finf.lineFeed)*text->lines;
		else
			*outHeight = ceilf(scaleY*s_textScale*fontGetInfo(fontGetSystemFont())->lineFeed)*text->lines;
	}
}

void C2D_DrawText(const C2D_Text* text, u32 flags, float x, float y, float z, float scaleX, float scaleY, ...)
{
	C2Di_Glyph* begin = &text->buf->glyphs[text->begin];
	C2Di_Glyph* end   = &text->buf->glyphs[text->end];
	C2Di_Glyph* cur;
	CFNT_s* systemFont = fontGetSystemFont();

	scaleX *= s_textScale;
	scaleY *= s_textScale;

	float glyphZ = z;
	float glyphH;
	float dispY;
	if (text->font)
	{
		glyphH = scaleY*text->font->cfnt->finf.tglp->cellHeight;
		dispY = ceilf(scaleY*text->font->cfnt->finf.lineFeed);
	} else
	{
		glyphH = scaleY*fontGetGlyphInfo(systemFont)->cellHeight;
		dispY = ceilf(scaleY*fontGetInfo(systemFont)->lineFeed);
	}
	u32 color = 0xFF000000;

	va_list va;
	va_start(va, scaleY);

	if (flags & C2D_AtBaseline)
	{
		if (text->font)
			y -= scaleY*text->font->cfnt->finf.tglp->baselinePos;
		else
			y -= scaleY*fontGetGlyphInfo(systemFont)->baselinePos;
	}
	if (flags & C2D_WithColor)
		color = va_arg(va, u32);

	va_end(va);

	C2Di_SetCircle(false);

	switch (flags & C2D_AlignMask)
	{
		case C2D_AlignLeft:
			for (cur = begin; cur != end; ++cur)
			{
				float glyphW = scaleX*cur->width;
				float glyphX = x+scaleX*cur->xPos;
				float glyphY = y+dispY*cur->lineNo;

				C2Di_SetTex(cur->sheet);
				C2Di_Update();
				C2Di_AppendVtx(glyphX,        glyphY,        glyphZ, cur->texcoord.left,  cur->texcoord.top,    0.0f, 1.0f, color);
				C2Di_AppendVtx(glyphX,        glyphY+glyphH, glyphZ, cur->texcoord.left,  cur->texcoord.bottom, 0.0f, 1.0f, color);
				C2Di_AppendVtx(glyphX+glyphW, glyphY,        glyphZ, cur->texcoord.right, cur->texcoord.top,    0.0f, 1.0f, color);
				C2Di_AppendVtx(glyphX+glyphW, glyphY,        glyphZ, cur->texcoord.right, cur->texcoord.top,    0.0f, 1.0f, color);
				C2Di_AppendVtx(glyphX,        glyphY+glyphH, glyphZ, cur->texcoord.left,  cur->texcoord.bottom, 0.0f, 1.0f, color);
				C2Di_AppendVtx(glyphX+glyphW, glyphY+glyphH, glyphZ, cur->texcoord.right, cur->texcoord.bottom, 0.0f, 1.0f, color);
			}
			break;
		case C2D_AlignRight:
			{
				float lineWidths[text->lines];
				for (cur = begin; cur != end; cur++)
				{
					if (cur->xPos + cur->width > lineWidths[cur->lineNo])
					{
						lineWidths[cur->lineNo] = cur->xPos + cur->width;
					}
				}

				for (cur = begin; cur != end; cur++)
				{
					float glyphW = scaleX*cur->width;
					float glyphX = x - scaleX*lineWidths[cur->lineNo] + scaleX*cur->xPos;
					float glyphY = y + dispY*cur->lineNo;

					C2Di_SetTex(cur->sheet);
					C2Di_Update();
					C2Di_AppendVtx(glyphX,        glyphY,        glyphZ, cur->texcoord.left,  cur->texcoord.top,    0.0f, 1.0f, color);
					C2Di_AppendVtx(glyphX,        glyphY+glyphH, glyphZ, cur->texcoord.left,  cur->texcoord.bottom, 0.0f, 1.0f, color);
					C2Di_AppendVtx(glyphX+glyphW, glyphY,        glyphZ, cur->texcoord.right, cur->texcoord.top,    0.0f, 1.0f, color);
					C2Di_AppendVtx(glyphX+glyphW, glyphY,        glyphZ, cur->texcoord.right, cur->texcoord.top,    0.0f, 1.0f, color);
					C2Di_AppendVtx(glyphX,        glyphY+glyphH, glyphZ, cur->texcoord.left,  cur->texcoord.bottom, 0.0f, 1.0f, color);
					C2Di_AppendVtx(glyphX+glyphW, glyphY+glyphH, glyphZ, cur->texcoord.right, cur->texcoord.bottom, 0.0f, 1.0f, color);
				}
			}
			break;
		case C2D_AlignCenter:
			{
				float lineWidths[text->lines];
				for (cur = begin; cur != end; cur++)
				{
					if (cur->xPos + cur->width > lineWidths[cur->lineNo])
					{
						lineWidths[cur->lineNo] = cur->xPos + cur->width;
					}
				}

				for (cur = begin; cur != end; cur++)
				{
					float glyphW = scaleX*cur->width;
					float glyphX = x - scaleX*lineWidths[cur->lineNo]/2 + scaleX*cur->xPos;
					float glyphY = y + dispY*cur->lineNo;

					C2Di_SetTex(cur->sheet);
					C2Di_Update();
					C2Di_AppendVtx(glyphX,        glyphY,        glyphZ, cur->texcoord.left,  cur->texcoord.top,    0.0f, 1.0f, color);
					C2Di_AppendVtx(glyphX,        glyphY+glyphH, glyphZ, cur->texcoord.left,  cur->texcoord.bottom, 0.0f, 1.0f, color);
					C2Di_AppendVtx(glyphX+glyphW, glyphY,        glyphZ, cur->texcoord.right, cur->texcoord.top,    0.0f, 1.0f, color);
					C2Di_AppendVtx(glyphX+glyphW, glyphY,        glyphZ, cur->texcoord.right, cur->texcoord.top,    0.0f, 1.0f, color);
					C2Di_AppendVtx(glyphX,        glyphY+glyphH, glyphZ, cur->texcoord.left,  cur->texcoord.bottom, 0.0f, 1.0f, color);
					C2Di_AppendVtx(glyphX+glyphW, glyphY+glyphH, glyphZ, cur->texcoord.right, cur->texcoord.bottom, 0.0f, 1.0f, color);
				}
			}
			break;
		case C2D_AlignJustified:
			{
				struct line {
					u32 words;
					u32 wordStart;
				};
				struct line lines[text->lines];
				memset(lines, 0, sizeof(struct line) * text->lines);
				for (cur = begin; cur != end; cur++)
				{
					if (cur->wordNo >= lines[cur->lineNo].words)
						lines[cur->lineNo].words = cur->wordNo + 1;
				}
				lines[0].wordStart = 0;
				for (u32 i = 1; i < text->lines; i++)
				{
					lines[i].wordStart = lines[i-1].wordStart + lines[i-1].words;
				}
				struct word {
					C2Di_Glyph* start;
					C2Di_Glyph* end;
					float xBegin;
					float xEnd;
				};
				struct word words[text->words];
				for (u32 i = 0; i < text->words; i++)
				{
					words[i].start = NULL;
					words[i].end = NULL;
					words[i].xBegin = 0;
					words[i].xEnd = 0;
				}
				for (cur = begin; cur != end; cur++)
				{
					u32 consecutiveWordNum = cur->wordNo + lines[cur->lineNo].wordStart;
					if (words[consecutiveWordNum].end == NULL || cur->xPos + cur->width > words[consecutiveWordNum].end->xPos + words[consecutiveWordNum].end->width)
						words[consecutiveWordNum].end = cur;
					if (words[consecutiveWordNum].start == NULL || cur->xPos < words[consecutiveWordNum].start->xPos)
						words[consecutiveWordNum].start = cur;
				}

				// Get total width available for whitespace
				float whitespaceWidth[text->lines];
				for (u32 i = 0; i < text->lines; i++)
				{
					whitespaceWidth[i] = 0;
				}
				for (u32 i = 0; i < text->words; i++)
				{
					// If there's at least one word, calculate the total text width
					// if (words[i].start != NULL && words[i].end != NULL)
						whitespaceWidth[words[i].start->lineNo] += words[i].end->xPos + words[i].end->width - words[i].start->xPos;
				}
				for (u32 i = 0; i < text->lines; i++)
				{
					// Transform it from total text width to total whitespace width
					whitespaceWidth[i] = text->width - whitespaceWidth[i];
					// And then get the width of a single whitespace
					if (lines[i].words > 1)
						whitespaceWidth[i] /= lines[i].words - 1;
				}

				// Set up final word beginnings and ends
				words[0].xBegin = x;
				words[0].xEnd = words[0].xBegin + words[0].end->xPos + words[0].end->width - words[0].start->xPos;
				for (u32 i = 1; i < text->words; i++)
				{
					if (words[i-1].start->lineNo != words[i].start->lineNo)
					{
						words[i].xBegin = x;
					}
					else
					{
						words[i].xBegin = words[i-1].xEnd + whitespaceWidth[words[i].start->lineNo];
					}
					words[i].xEnd = words[i].xBegin + words[i].end->xPos + words[i].end->width - words[i].start->xPos;
				}

				for (cur = begin; cur != end; cur++)
				{
					u32 consecutiveWordNum = cur->wordNo + lines[cur->lineNo].wordStart;
					float glyphW = scaleX*cur->width;
					// Specified X value plus the scaled beginning offset plus the scaled offset from the beginning plus the calculated whitespace width
					float glyphX = scaleX*words[consecutiveWordNum].xBegin + scaleX*(cur->xPos - words[consecutiveWordNum].start->xPos); // scaleX*cur->xPos + scaleX*whitespaceWidth[cur->lineNo]*cur->wordNo
					float glyphY = y + dispY*cur->lineNo;

					C2Di_SetTex(cur->sheet);
					C2Di_Update();
					C2Di_AppendVtx(glyphX,        glyphY,        glyphZ, cur->texcoord.left,  cur->texcoord.top,    0.0f, 1.0f, color);
					C2Di_AppendVtx(glyphX,        glyphY+glyphH, glyphZ, cur->texcoord.left,  cur->texcoord.bottom, 0.0f, 1.0f, color);
					C2Di_AppendVtx(glyphX+glyphW, glyphY,        glyphZ, cur->texcoord.right, cur->texcoord.top,    0.0f, 1.0f, color);
					C2Di_AppendVtx(glyphX+glyphW, glyphY,        glyphZ, cur->texcoord.right, cur->texcoord.top,    0.0f, 1.0f, color);
					C2Di_AppendVtx(glyphX,        glyphY+glyphH, glyphZ, cur->texcoord.left,  cur->texcoord.bottom, 0.0f, 1.0f, color);
					C2Di_AppendVtx(glyphX+glyphW, glyphY+glyphH, glyphZ, cur->texcoord.right, cur->texcoord.bottom, 0.0f, 1.0f, color);
				}
			}
			break;
	}
}
