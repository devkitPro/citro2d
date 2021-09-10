#pragma once
#include <c2d/base.h>

typedef struct
{
	float pos[3];
	float texcoord[2];
	float ptcoord[2];
	u32 color;
} C2Di_Vertex;

typedef struct
{
	DVLB_s* shader;
	shaderProgram_s program;
	C3D_AttrInfo attrInfo;
	C3D_BufInfo bufInfo;
	C3D_ProcTex ptBlend;
	C3D_ProcTex ptCircle;
	C3D_ProcTexLut ptBlendLut;
	C3D_ProcTexLut ptCircleLut;
	u32 sceneW, sceneH;

	C2Di_Vertex* vtxBuf;
	u16* idxBuf;

	size_t vtxBufSize;
	size_t vtxBufPos;

	size_t idxBufSize;
	size_t idxBufPos;
	size_t idxBufLastPos;

	u32 flags;
	C3D_Mtx projMtx;
	C3D_Mtx mdlvMtx;
	C3D_Tex* curTex;
	u32 fadeClr;
} C2Di_Context;

enum
{
	C2DiF_Active       = BIT(0),
	C2DiF_DirtyProj    = BIT(1),
	C2DiF_DirtyMdlv    = BIT(2),
	C2DiF_DirtyTex     = BIT(3),
	C2DiF_DirtyMode    = BIT(4),
	C2DiF_DirtyFade    = BIT(5),

	C2DiF_Mode_Shift      = 8,
	C2DiF_Mode_Mask       = 0xf << C2DiF_Mode_Shift,
	C2DiF_Mode_Solid      = 0   << C2DiF_Mode_Shift,
	C2DiF_Mode_Circle     = 1   << C2DiF_Mode_Shift,
	C2DiF_Mode_Text       = 2   << C2DiF_Mode_Shift,
	C2DiF_Mode_ImageSolid = 3   << C2DiF_Mode_Shift,
	C2DiF_Mode_ImageMult  = 4   << C2DiF_Mode_Shift,
	C2DiF_Mode_ImageLuma  = 5   << C2DiF_Mode_Shift,

	C2DiF_ProcTex_Shift  = 12,
	C2DiF_ProcTex_Mask   = 0xf << C2DiF_ProcTex_Shift,
	C2DiF_ProcTex_None   = 0   << C2DiF_ProcTex_Shift,
	C2DiF_ProcTex_Blend  = 1   << C2DiF_ProcTex_Shift,
	C2DiF_ProcTex_Circle = 2   << C2DiF_ProcTex_Shift,

	C2DiF_TintMode_Shift = 16,
	C2DiF_TintMode_Mask  = 0xf << C2DiF_TintMode_Shift,

	C2DiF_DirtyAny = C2DiF_DirtyProj | C2DiF_DirtyMdlv | C2DiF_DirtyTex | C2DiF_DirtyMode | C2DiF_DirtyFade,
};

struct C2D_Font_s
{
	CFNT_s* cfnt;
	C3D_Tex* glyphSheets;
	float textScale;
};

static inline C2Di_Context* C2Di_GetContext(void)
{
	extern C2Di_Context __C2Di_Context;
	return &__C2Di_Context;
}

static inline void C2Di_SetMode(u32 mode)
{
	C2Di_Context* ctx = C2Di_GetContext();
	mode &= C2DiF_Mode_Mask;
	if ((ctx->flags & C2DiF_Mode_Mask) != mode)
		ctx->flags = C2DiF_DirtyMode | (ctx->flags &~ C2DiF_Mode_Mask) | mode;
}

static inline void C2Di_SetTex(C3D_Tex* tex)
{
	C2Di_Context* ctx = C2Di_GetContext();
	if (tex != ctx->curTex)
	{
		ctx->flags |= C2DiF_DirtyTex;
		ctx->curTex = tex;
	}
}

typedef struct
{
	float topLeft[2];
	float topRight[2];
	float botLeft[2];
	float botRight[2];
} C2Di_Quad;

void C2Di_CalcQuad(C2Di_Quad* quad, const C2D_DrawParams* params);
void C2Di_AppendTri(void);
void C2Di_AppendQuad(void);
void C2Di_AppendVtx(float x, float y, float z, float u, float v, float ptx, float pty, u32 color);
void C2Di_FlushVtxBuf(void);
void C2Di_Update(void);
