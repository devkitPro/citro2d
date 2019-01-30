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
            tex->border = 0xFFFFFFFF;
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

C2D_Font C2Di_loadFont(u64 binary_lowpath)
{
    C2D_Font font = C2Di_FontAlloc();
    if (font)
    {
        int fontNum = (binary_lowpath & 0x300) >> 8;
        const u8 sizeMod = fontNum == 0 ? 0xA0 : 0xB0;
        u64 lowPath[] = { binary_lowpath, 0x00000001FFFFFE00 };
        Handle romfs_handle;
        u64    romfs_size        = 0;
        u32    romfs_bytes_read  = 0;
    
        FS_Path    savedatacheck_path       = { PATH_BINARY, 16, (u8*)lowPath };
        u8         file_binary_lowpath[20]  = {};
        FS_Path    romfs_path               = { PATH_BINARY, 20, file_binary_lowpath };
    
        if (R_FAILED(FSUSER_OpenFileDirectly(&romfs_handle, (FS_ArchiveID)0x2345678a, savedatacheck_path, romfs_path, FS_OPEN_READ, 0)))
        {
            free(font);
            return NULL;
        }
        if (R_FAILED(FSFILE_GetSize(romfs_handle, &romfs_size)))
        {
            free(font);
            FSFILE_Close(romfs_handle);
            return NULL;
        }
    
        u8* romfs_data_buffer = malloc(romfs_size);
        if (!romfs_data_buffer)
        {
            free(font);
            FSFILE_Close(romfs_handle);
            return NULL;
        }
        if (R_FAILED(FSFILE_Read(romfs_handle, &romfs_bytes_read, 0, romfs_data_buffer, romfs_size)))
        {
            free(romfs_data_buffer);
            free(font);
            FSFILE_Close(romfs_handle);
            return NULL;
        }
        FSFILE_Close(romfs_handle);
    
        u8* compFontData = romfs_data_buffer + sizeMod;
    
        u32 fontSize = *(u32*)(compFontData) >> 8;
        font->cfnt = linearAlloc(fontSize);
        if (font->cfnt)
        {
            if (!decompress_LZ11(font->cfnt, fontSize, NULL, compFontData + 4, romfs_size - sizeMod - 4))
            {
                C2D_FontFree(font);
                return NULL;
            }
        }

        free(romfs_data_buffer);

        font = C2Di_PostLoadFont(font);
    }

    return font;
}

C2D_Font C2D_FontLoadFromSystem(CFG_Region region)
{
    u8 systemRegion;
    if (R_FAILED(CFGU_SecureInfoGetRegion(&systemRegion)))
        return NULL;

    switch (region)
    {
        case 0:
        case 1:
        case 2:
        case 3:
            if (systemRegion > 3)
            {
                return C2Di_loadFont(0x0004009b00014002);
            }
            break;
        case 4:
            if (systemRegion == 4)
            {
                break;
            }
            return C2Di_loadFont(0x0004009b00014102);
        case 5:
            if (systemRegion == 5)
            {
                break;
            }
            return C2Di_loadFont(0x0004009b00014202);
        case 6:
            if (systemRegion == 6)
            {
                break;
            }
            return C2Di_loadFont(0x0004009b00014302);
    }
    return NULL;
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