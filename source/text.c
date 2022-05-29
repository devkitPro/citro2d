#include "internal.h"
#include <alloca.h>
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
    u32 charNo;
	u32 wordNo;
} C2Di_Glyph;

struct C2D_TextBuf_s
{
	u32 reserved[2];
	size_t glyphCount;
	size_t glyphBufSize;
	C2Di_Glyph glyphs[0];
};

typedef struct C2Di_LineInfo_s
{
	u32 words;
	u32 wordStart;
} C2Di_LineInfo;

typedef struct C2Di_WordInfo_s
{
	C2Di_Glyph* start;
	C2Di_Glyph* end;
	float wrapXOffset;
	u32 newLineNumber;
} C2Di_WordInfo;

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
		tex->border = 0;
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
    u32 charNum = text->chars;

	bool lastWasWhitespace = true;
	while (buf->glyphCount < buf->glyphBufSize)
	{
        ++charNum;
		uint32_t code;
		ssize_t units = decode_utf8(&code, p);
		if (units == -1)
		{
			code = 0xFFFD;
			units = 1;
		} else if (code == 0 || code == '\n')
		{
			// If we last parsed non-whitespace, increment the word counter
			if (!lastWasWhitespace)
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
            glyph->charNo          = charNum - 1;
			glyph->width           = glyphData.width;
			glyph->texcoord.left   = glyphData.texcoord.left;
			glyph->texcoord.top    = glyphData.texcoord.top;
			glyph->texcoord.right  = glyphData.texcoord.right;
			glyph->texcoord.bottom = glyphData.texcoord.bottom;
			lastWasWhitespace = false;
		}
		else if (!lastWasWhitespace)
		{
			wordNum++;
			lastWasWhitespace = true;
		}
		text->width += glyphData.xAdvance;
	}
	text->end = buf->glyphCount;
	text->width *= s_textScale;
	text->lines = 1;
	text->words = wordNum;
    text->chars = charNum;
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
    text->chars  = 0;
	text->lines  = 0;

	for (;;)
	{
		C2D_Text temp;
        temp.chars = text->chars;
		str = C2D_TextFontParseLine(&temp, font, buf, str, text->lines++);
		text->words += temp.words;
        text->chars = temp.chars;
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

static inline void C2Di_CalcLineInfo(const C2D_Text* text, C2Di_LineInfo* lines, C2Di_WordInfo* words)
{
	C2Di_Glyph* begin = &text->buf->glyphs[text->begin];
	C2Di_Glyph* end   = &text->buf->glyphs[text->end];
	C2Di_Glyph* cur;
	// Get information about lines
	memset(lines, 0, sizeof(C2Di_LineInfo) * text->lines);
	for (cur = begin; cur != end; cur++)
		if (cur->wordNo >= lines[cur->lineNo].words)
			lines[cur->lineNo].words = cur->wordNo + 1;
	for (u32 i = 1; i < text->lines; i++)
		lines[i].wordStart = lines[i-1].wordStart + lines[i-1].words;

	// Get information about words
	for (u32 i = 0; i < text->words; i++)
	{
		words[i].start = NULL;
		words[i].end = NULL;
		words[i].wrapXOffset = 0;
		words[i].newLineNumber = 0;
	}
	for (cur = begin; cur != end; cur++)
	{
		u32 consecutiveWordNum = cur->wordNo + lines[cur->lineNo].wordStart;
		if (!words[consecutiveWordNum].end || cur->xPos + cur->width > words[consecutiveWordNum].end->xPos + words[consecutiveWordNum].end->width)
			words[consecutiveWordNum].end = cur;
		if (!words[consecutiveWordNum].start || cur->xPos < words[consecutiveWordNum].start->xPos)
			words[consecutiveWordNum].start = cur;
		words[consecutiveWordNum].newLineNumber = cur->lineNo;
	}
}

static inline void C2Di_CalcLineWidths(float* widths, const C2D_Text* text, const C2Di_WordInfo* words, bool wrap)
{
	u32 currentWord = 0;
	if (words)
	{
		while (currentWord != text->words)
		{
			u32 nextLineWord = currentWord + 1;
			// Advance nextLineWord to the next word that's on a different line, or the end
			if (wrap)
			{
				while (nextLineWord != text->words && words[nextLineWord].newLineNumber == words[currentWord].newLineNumber) nextLineWord++;
				// Finally, set the new line width
				widths[words[currentWord].newLineNumber] = words[nextLineWord-1].end->xPos + words[nextLineWord-1].end->width - words[currentWord].start->xPos;
			}
			else
			{
				while (nextLineWord != text->words && words[nextLineWord].start->lineNo == words[currentWord].start->lineNo) nextLineWord++;
				// Finally, set the new line width
				widths[words[currentWord].start->lineNo] = words[nextLineWord-1].end->xPos + words[nextLineWord-1].end->width - words[currentWord].start->xPos;
			}

			currentWord = nextLineWord;
		}
	}
	else
	{
		memset(widths, 0, sizeof(float) * text->lines);
		for (C2Di_Glyph* cur = &text->buf->glyphs[text->begin]; cur != &text->buf->glyphs[text->end]; cur++)
			if (cur->xPos + cur->width > widths[cur->lineNo])
				widths[cur->lineNo] = cur->xPos + cur->width;
	}
}

void C2D_DrawText(const C2D_Text* text, u32 flags, float x, float y, float z, float scaleX, float scaleY, ...)
{
	// If there are no words, we can't do the math calculations necessary with them. Just return; nothing would be drawn anyway.
	if (text->words == 0)
		return;
	C2Di_Glyph* begin = &text->buf->glyphs[text->begin];
	C2Di_Glyph* end   = &text->buf->glyphs[text->end];
	C2Di_Glyph* cur;
	CFNT_s* systemFont = fontGetSystemFont();

	float glyphZ = z;
	float glyphH;
	float dispY;
	if (text->font)
	{
		scaleX *= text->font->textScale;
		scaleY *= text->font->textScale;
		glyphH = scaleY*text->font->cfnt->finf.tglp->cellHeight;
		dispY = ceilf(scaleY*text->font->cfnt->finf.lineFeed);
	} else
	{
		scaleX *= s_textScale;
		scaleY *= s_textScale;
		glyphH = scaleY*fontGetGlyphInfo(systemFont)->cellHeight;
		dispY = ceilf(scaleY*fontGetInfo(systemFont)->lineFeed);
	}
	u32 color = 0xFF000000;
    u32* colors = NULL;
    u32 lenColors = 0;
	
	float maxWidth = scaleX*text->width;

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
	
	if (flags & C2D_MultiColor) 
	{
		colors = va_arg(va, u32*);
		lenColors = va_arg(va, u32);
	}

	if (flags & C2D_WordWrap)
		maxWidth = va_arg(va, double); // Passed as float, but varargs promotes to double.

	va_end(va);

	C2Di_SetMode(C2DiF_Mode_Text);

	C2Di_LineInfo* lines = NULL;
	C2Di_WordInfo* words = NULL;

	if (flags & C2D_WordWrap)
	{
		lines = alloca(sizeof(*lines)*text->lines);
		words = alloca(sizeof(*words)*text->words);
		C2Di_CalcLineInfo(text, lines, words);
		// The first word will never have a wrap offset in X or Y
		for (u32 i = 1; i < text->words; i++)
		{
			// If the current word was originally on a different line than the last one, only the difference between new line number and original line number should be the same
			if (words[i-1].start->lineNo != words[i].start->lineNo)
			{
				words[i].wrapXOffset = 0;
				words[i].newLineNumber = words[i].start->lineNo + (words[i-1].newLineNumber - words[i-1].start->lineNo);
			}
			// Otherwise, if the current word goes over the width, with the previous word's offset taken into account...
			else if (scaleX*(words[i-1].wrapXOffset + words[i].end->xPos + words[i].end->width) > maxWidth)
			{
				// Then set the X offset to the negative of the original position
				words[i].wrapXOffset = -words[i].start->xPos;
				// And set the new line number based off the last word's
				words[i].newLineNumber = words[i-1].newLineNumber + 1;
			}
			// Otherwise, both X offset and new line number should be the same as the last word's
			else
			{
				words[i].wrapXOffset = words[i-1].wrapXOffset;
				words[i].newLineNumber = words[i-1].newLineNumber;
			}
		}
	}

    u32 lastColorIdx = 0;
	switch (flags & C2D_AlignMask)
	{
		case C2D_AlignLeft:
			for (cur = begin; cur != end; ++cur)
			{
				float glyphW = scaleX*cur->width;
				float glyphX;
				float glyphY;
				if (flags & C2D_WordWrap)
				{
					u32 consecutiveWordNum = cur->wordNo + lines[cur->lineNo].wordStart;
					glyphX = x+scaleX*(cur->xPos + words[consecutiveWordNum].wrapXOffset);
					glyphY = y+dispY*words[consecutiveWordNum].newLineNumber;
				}
				else
				{
					glyphX = x+scaleX*cur->xPos;
					glyphY = y+dispY*cur->lineNo;
				}

                if (colors != NULL){
                    if(cur->charNo >= colors[lastColorIdx] && cur->charNo < colors[lastColorIdx+2]) {
                        color = colors[lastColorIdx+1];
                    }
                    else{
                        for(size_t i = 0; i < lenColors; i += 2) {
                            if (cur->charNo >= colors[i] && (i + 2 >= lenColors || cur->charNo < colors[i+2])) {
                                color = colors[i+1];
                                lastColorIdx = i;
                                break;
                            }
                        }
                    }
                }

				C2Di_SetTex(cur->sheet);
				C2Di_Update();
				C2Di_AppendQuad();
				C2Di_AppendVtx(glyphX,        glyphY,        glyphZ, cur->texcoord.left,  cur->texcoord.top,    0.0f, 1.0f, color);
				C2Di_AppendVtx(glyphX+glyphW, glyphY,        glyphZ, cur->texcoord.right, cur->texcoord.top,    0.0f, 1.0f, color);
				C2Di_AppendVtx(glyphX,        glyphY+glyphH, glyphZ, cur->texcoord.left,  cur->texcoord.bottom, 0.0f, 1.0f, color);
				C2Di_AppendVtx(glyphX+glyphW, glyphY+glyphH, glyphZ, cur->texcoord.right, cur->texcoord.bottom, 0.0f, 1.0f, color);
			}
			break;
		case C2D_AlignRight:
		{
			float finalLineWidths[flags & C2D_WordWrap ? words[text->words-1].newLineNumber + 1 : text->lines];
			C2Di_CalcLineWidths(finalLineWidths, text, words, flags & C2D_WordWrap);

			for (cur = begin; cur != end; cur++)
			{
				float glyphW = scaleX*cur->width;
				float glyphX;
				float glyphY;
				if (flags & C2D_WordWrap)
				{
					u32 consecutiveWordNum = cur->wordNo + lines[cur->lineNo].wordStart;
					glyphX = x + scaleX*(cur->xPos + words[consecutiveWordNum].wrapXOffset - finalLineWidths[words[consecutiveWordNum].newLineNumber]);
					glyphY = y + dispY*words[consecutiveWordNum].newLineNumber;
				}
				else
				{
					glyphX = x + scaleX*(cur->xPos - finalLineWidths[cur->lineNo]);
					glyphY = y + dispY*cur->lineNo;
				}

                if (colors != NULL){
                    if(cur->charNo >= colors[lastColorIdx] && cur->charNo < colors[lastColorIdx+2]) {
                        color = colors[lastColorIdx+1];
                    }
                    else{
                        for(size_t i = 0; i < lenColors; i += 2) {
                            if (cur->charNo >= colors[i] && (i + 2 >= lenColors || cur->charNo < colors[i+2])) {
                                color = colors[i+1];
                                lastColorIdx = i;
                                break;
                            }
                        }
                    }
                }

				C2Di_SetTex(cur->sheet);
				C2Di_Update();
				C2Di_AppendQuad();
				C2Di_AppendVtx(glyphX,        glyphY,        glyphZ, cur->texcoord.left,  cur->texcoord.top,    0.0f, 1.0f, color);
				C2Di_AppendVtx(glyphX+glyphW, glyphY,        glyphZ, cur->texcoord.right, cur->texcoord.top,    0.0f, 1.0f, color);
				C2Di_AppendVtx(glyphX,        glyphY+glyphH, glyphZ, cur->texcoord.left,  cur->texcoord.bottom, 0.0f, 1.0f, color);
				C2Di_AppendVtx(glyphX+glyphW, glyphY+glyphH, glyphZ, cur->texcoord.right, cur->texcoord.bottom, 0.0f, 1.0f, color);
			}
		}
		break;
		case C2D_AlignCenter:
		{
			float finalLineWidths[flags & C2D_WordWrap ? words[text->words-1].newLineNumber + 1 : text->lines];
			C2Di_CalcLineWidths(finalLineWidths, text, words, flags & C2D_WordWrap);

			for (cur = begin; cur != end; cur++)
			{
				float glyphW = scaleX*cur->width;
				float glyphX;
				float glyphY;
				if (flags & C2D_WordWrap)
				{
					u32 consecutiveWordNum = cur->wordNo + lines[cur->lineNo].wordStart;
					glyphX = x + scaleX*(cur->xPos + words[consecutiveWordNum].wrapXOffset - finalLineWidths[words[consecutiveWordNum].newLineNumber]/2);
					glyphY = y + dispY*words[consecutiveWordNum].newLineNumber;
				}
				else
				{
					glyphX = x + scaleX*(cur->xPos - finalLineWidths[cur->lineNo]/2);
					glyphY = y + dispY*cur->lineNo;
				}

                if (colors != NULL){
                    if(cur->charNo >= colors[lastColorIdx] && cur->charNo < colors[lastColorIdx+2]) {
                        color = colors[lastColorIdx+1];
                    }
                    else{
                        for(size_t i = 0; i < lenColors; i += 2) {
                            if (cur->charNo >= colors[i] && (i + 2 >= lenColors || cur->charNo < colors[i+2])) {
                                color = colors[i+1];
                                lastColorIdx = i;
                                break;
                            }
                        }
                    }
                }

				C2Di_SetTex(cur->sheet);
				C2Di_Update();
				C2Di_AppendQuad();
				C2Di_AppendVtx(glyphX,        glyphY,        glyphZ, cur->texcoord.left,  cur->texcoord.top,    0.0f, 1.0f, color);
				C2Di_AppendVtx(glyphX+glyphW, glyphY,        glyphZ, cur->texcoord.right, cur->texcoord.top,    0.0f, 1.0f, color);
				C2Di_AppendVtx(glyphX,        glyphY+glyphH, glyphZ, cur->texcoord.left,  cur->texcoord.bottom, 0.0f, 1.0f, color);
				C2Di_AppendVtx(glyphX+glyphW, glyphY+glyphH, glyphZ, cur->texcoord.right, cur->texcoord.bottom, 0.0f, 1.0f, color);
			}
		}
		break;
		case C2D_AlignJustified:
		{
			if (!(flags & C2D_WordWrap))
			{
				lines = alloca(sizeof(*lines)*text->lines);
				words = alloca(sizeof(*words)*text->words);
				C2Di_CalcLineInfo(text, lines, words);
			}
			// Get total width available for whitespace for all lines after wrapping
			struct
			{
				float whitespaceWidth;
				u32 wordStart;
				u32 words;
			} justifiedLineInfo[words[text->words - 1].newLineNumber + 1];
			for (u32 i = 0; i < words[text->words - 1].newLineNumber + 1; i++)
			{
				justifiedLineInfo[i].whitespaceWidth = 0;
				justifiedLineInfo[i].words = 0;
				justifiedLineInfo[i].wordStart = 0;
			}
			for (u32 i = 0; i < text->words; i++)
			{
				// Calculate the total text width
				justifiedLineInfo[words[i].newLineNumber].whitespaceWidth += words[i].end->xPos + words[i].end->width - words[i].start->xPos;
				// Increment amount of words
				justifiedLineInfo[words[i].newLineNumber].words++;
				// And set the word starts
				if (i > 0 && words[i-1].newLineNumber != words[i].newLineNumber)
					justifiedLineInfo[words[i].newLineNumber].wordStart = i;
			}
			for (u32 i = 0; i < words[text->words - 1].newLineNumber + 1; i++)
			{
				// Transform it from total text width to total whitespace width
				justifiedLineInfo[i].whitespaceWidth = maxWidth - scaleX*justifiedLineInfo[i].whitespaceWidth;
				// And then get the width of a single whitespace
				if (justifiedLineInfo[i].words > 1)
					justifiedLineInfo[i].whitespaceWidth /= justifiedLineInfo[i].words - 1;
			}

			// Set up final word beginnings and ends
			struct
			{
				float xBegin;
				float xEnd;
			} wordPositions[text->words];
			wordPositions[0].xBegin = 0;
			wordPositions[0].xEnd = wordPositions[0].xBegin + words[0].end->xPos + words[0].end->width - words[0].start->xPos;
			for (u32 i = 1; i < text->words; i++)
			{
				wordPositions[i].xBegin = words[i-1].newLineNumber != words[i].newLineNumber ? 0 : wordPositions[i-1].xEnd;
				wordPositions[i].xEnd = wordPositions[i].xBegin + words[i].end->xPos + words[i].end->width - words[i].start->xPos;
			}

			for (cur = begin; cur != end; cur++)
			{
				u32 consecutiveWordNum = cur->wordNo + lines[cur->lineNo].wordStart;
				float glyphW = scaleX*cur->width;
				// The given X position, plus the scaled beginning position for this word, plus the offset of this glyph within the word, plus the whitespace width for this line times the word number within the line
				float glyphX = x + scaleX*wordPositions[consecutiveWordNum].xBegin + scaleX*(cur->xPos - words[consecutiveWordNum].start->xPos) + justifiedLineInfo[words[consecutiveWordNum].newLineNumber].whitespaceWidth*(consecutiveWordNum - justifiedLineInfo[words[consecutiveWordNum].newLineNumber].wordStart);
				float glyphY = y + dispY*words[consecutiveWordNum].newLineNumber;

                if (colors != NULL){
                    if(cur->charNo >= colors[lastColorIdx] && cur->charNo < colors[lastColorIdx+2]) {
                        color = colors[lastColorIdx+1];
                    }
                    else{
                        for(size_t i = 0; i < lenColors; i += 2) {
                            if (cur->charNo >= colors[i] && (i + 2 >= lenColors || cur->charNo < colors[i+2])) {
                                color = colors[i+1];
                                lastColorIdx = i;
                                break;
                            }
                        }
                    }
                }

				C2Di_SetTex(cur->sheet);
				C2Di_Update();
				C2Di_AppendQuad();
				C2Di_AppendVtx(glyphX,        glyphY,        glyphZ, cur->texcoord.left,  cur->texcoord.top,    0.0f, 1.0f, color);
				C2Di_AppendVtx(glyphX+glyphW, glyphY,        glyphZ, cur->texcoord.right, cur->texcoord.top,    0.0f, 1.0f, color);
				C2Di_AppendVtx(glyphX,        glyphY+glyphH, glyphZ, cur->texcoord.left,  cur->texcoord.bottom, 0.0f, 1.0f, color);
				C2Di_AppendVtx(glyphX+glyphW, glyphY+glyphH, glyphZ, cur->texcoord.right, cur->texcoord.bottom, 0.0f, 1.0f, color);
			}
		}
		break;
	}
}
