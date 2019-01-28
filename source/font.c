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

static inline C2D_Font C2Di_PostLoadFont(C2D_Font font)
{
    if (!font->cfnt)
    {
        free(font);
        font = NULL;
    } else
    {
        font->cfnt->finf.tglp = (TGLP_s*)((u32)(font->cfnt->finf.tglp) + (u32) font->cfnt);
        font->cfnt->finf.tglp->sheetData = (u8*)((u32)(font->cfnt->finf.tglp->sheetData) + (u32) font->cfnt);

        font->cfnt->finf.cmap = (CMAP_s*)((u32)(font->cfnt->finf.cmap) + (u32) font->cfnt);
        CMAP_s* cmap = font->cfnt->finf.cmap;
        for (; cmap->next; cmap = cmap->next)
            cmap->next = (CMAP_s*)((u32)(cmap->next) + (u32) font->cfnt);

        font->cfnt->finf.cwdh = (CWDH_s*)((u32)(font->cfnt->finf.cwdh) + (u32) font->cfnt);
        CWDH_s* cwdh = font->cfnt->finf.cwdh;
        for (; cwdh->next; cwdh = cwdh->next)
            cwdh->next = (CWDH_s*)((u32)(cwdh->next) + (u32) font->cfnt);

        TGLP_s* glyphInfo = font->cfnt->finf.tglp;
        font->glyphSheets = malloc(sizeof(C3D_Tex)*glyphInfo->nSheets);
        font->textScale = 30.0f / glyphInfo->cellHeight;
        if (!font->glyphSheets)
            svcBreak(USERBREAK_PANIC);

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
            tex->border = 0xFFFFFFFF;
            tex->lodParam = 0;
        }

        font->charPerSheet = glyphInfo->nRows * glyphInfo->nLines;
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
        {
            memcpy(font->cfnt, data, size);
        }
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

void C2D_FontFree(C2D_Font font)
{
    if (font)
    {
        if (font->cfnt)
            free(font->cfnt);
        free(font->glyphSheets);
    }
}

int C2D_FontGlyphIndexFromCodePoint(C2D_Font font, u32 codepoint)
{
    if (!font)
        return fontGlyphIndexFromCodePoint(codepoint);

    int ret = font->cfnt->finf.alterCharIndex;
    if (codepoint < 0x10000)
    {
        CMAP_s* cmap;
        for (cmap = font->cfnt->finf.cmap; cmap; cmap = cmap->next)
        {
            if (codepoint < cmap->codeBegin || codepoint > cmap->codeEnd)
                continue;

            if (cmap->mappingMethod == CMAP_TYPE_DIRECT)
            {
                ret = cmap->indexOffset + (codepoint - cmap->codeBegin);
                break;
            }

            if (cmap->mappingMethod == CMAP_TYPE_TABLE)
            {
                ret = cmap->indexTable[codepoint - cmap->codeBegin];
                break;
            }

            int j;
            for (j = 0; j < cmap->nScanEntries; j++)
                if (cmap->scanEntries[j].code == codepoint)
                    break;
            if (j < cmap->nScanEntries)
            {
                ret = cmap->scanEntries[j].glyphIndex;
                break;
            }
        }
    }
    return ret;
}

charWidthInfo_s* C2D_FontGetCharWidthInfo(C2D_Font font, int glyphIndex)
{
    if (!font)
        return fontGetCharWidthInfo(glyphIndex);
    
    charWidthInfo_s* info = NULL;
    CWDH_s* cwdh;
    for (cwdh = font->cfnt->finf.cwdh; cwdh && !info; cwdh = cwdh->next)
    {
        if (glyphIndex < cwdh->startIndex || glyphIndex > cwdh->endIndex)
            continue;
        info = &cwdh->widths[glyphIndex - cwdh->startIndex];
    }
    if (!info)
        info = &font->cfnt->finf.defaultWidth;
    return info;
}

void C2D_FontCalcGlyphPos(C2D_Font font, fontGlyphPos_s* out, int glyphIndex, u32 flags, float scaleX, float scaleY)
{
    if (!font)
        return fontCalcGlyphPos(out, glyphIndex, flags, scaleX, scaleY);
    FINF_s* finf = &font->cfnt->finf;
    TGLP_s* tglp = finf->tglp;
    charWidthInfo_s* cwi = C2D_FontGetCharWidthInfo(font, glyphIndex);

    int sheetId = glyphIndex / font->charPerSheet;
    int glInSheet = glyphIndex % font->charPerSheet;
    out->sheetIndex = sheetId;
    out->xOffset = scaleX*cwi->left;
    out->xAdvance = scaleX*cwi->charWidth;
    out->width = scaleX*cwi->glyphWidth;

    int lineId = glInSheet / tglp->nRows;
    int rowId = glInSheet % tglp->nRows;

    float tx = (float)(rowId*(tglp->cellWidth+1)+1) / tglp->sheetWidth;
    float ty = 1.0f - (float)(lineId*(tglp->cellHeight+1)+1) / tglp->sheetHeight;
    float tw = (float)cwi->glyphWidth / tglp->sheetWidth;
    float th = (float)tglp->cellHeight / tglp->sheetHeight;
    out->texcoord.left = tx;
    out->texcoord.top = ty;
    out->texcoord.right = tx+tw;
    out->texcoord.bottom = ty-th;

    if (flags & GLYPH_POS_CALC_VTXCOORD)
    {
        float vx = out->xOffset;
        float vy = (flags & GLYPH_POS_AT_BASELINE) ? (scaleY*tglp->baselinePos) : 0;
        float vw = out->width;
        float vh = scaleY*tglp->cellHeight;
        if (flags & GLYPH_POS_Y_POINTS_UP)
        {
            vy = -(vh-vy);
            out->vtxcoord.left = vx;
            out->vtxcoord.top = vy+vh;
            out->vtxcoord.right = vx+vw;
            out->vtxcoord.bottom = vy;
        } else
        {
            vy = -vy;
            out->vtxcoord.left = vx;
            out->vtxcoord.top = vy;
            out->vtxcoord.right = vx+vh;
            out->vtxcoord.bottom = vy+vh;
        }
    }
}