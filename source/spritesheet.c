#include <stdio.h>
#include <stdlib.h>
#include "internal.h"
#include <c2d/spritesheet.h>

struct C2D_SpriteSheet_s
{
	Tex3DS_Texture t3x;
	C3D_Tex        tex;
};

C2D_SpriteSheet C2D_SpriteSheetLoad(const char* filename)
{
	FILE* f = fopen(filename, "rb");
	if (!f) return NULL;
	setvbuf(f, NULL, _IOFBF, 64*1024);
	C2D_SpriteSheet ret = C2D_SpriteSheetLoadFromHandle(f);
	fclose(f);
	return ret;
}

static inline C2D_SpriteSheet C2Di_SpriteSheetAlloc(void)
{
	return (C2D_SpriteSheet)malloc(sizeof(struct C2D_SpriteSheet_s));
}

static inline C2D_SpriteSheet C2Di_PostLoadSheet(C2D_SpriteSheet sheet)
{
	if (!sheet->t3x)
	{
		free(sheet);
		sheet = NULL;
	} else
	{
		// Configure transparent border around texture
		sheet->tex.border = 0;
		C3D_TexSetWrap(&sheet->tex, GPU_CLAMP_TO_BORDER, GPU_CLAMP_TO_BORDER);
	}
	return sheet;
}

C2D_SpriteSheet C2D_SpriteSheetLoadFromMem(const void* data, size_t size)
{
	C2D_SpriteSheet sheet = C2Di_SpriteSheetAlloc();
	if (sheet)
	{
		sheet->t3x = Tex3DS_TextureImport(data, size, &sheet->tex, NULL, false);
		sheet = C2Di_PostLoadSheet(sheet);
	}
	return sheet;
}

C2D_SpriteSheet C2D_SpriteSheetFromFD(int fd)
{
	C2D_SpriteSheet sheet = C2Di_SpriteSheetAlloc();
	if (sheet)
	{
		sheet->t3x = Tex3DS_TextureImportFD(fd, &sheet->tex, NULL, false);
		sheet = C2Di_PostLoadSheet(sheet);
	}
	return sheet;
}

C2D_SpriteSheet C2D_SpriteSheetLoadFromHandle(FILE* f)
{
	C2D_SpriteSheet sheet = C2Di_SpriteSheetAlloc();
	if (sheet)
	{
		sheet->t3x = Tex3DS_TextureImportStdio(f, &sheet->tex, NULL, false);
		sheet = C2Di_PostLoadSheet(sheet);
	}
	return sheet;
}

void C2D_SpriteSheetFree(C2D_SpriteSheet sheet)
{
	Tex3DS_TextureFree(sheet->t3x);
	C3D_TexDelete(&sheet->tex);
	free(sheet);
}

size_t C2D_SpriteSheetCount(C2D_SpriteSheet sheet)
{
	return Tex3DS_GetNumSubTextures(sheet->t3x);
}

C2D_Image C2D_SpriteSheetGetImage(C2D_SpriteSheet sheet, size_t index)
{
	C2D_Image ret = { &sheet->tex, Tex3DS_GetSubTexture(sheet->t3x, index) };
	return ret;
}
