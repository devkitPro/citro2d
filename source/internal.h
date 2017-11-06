#pragma once
#include <c2d/base.h>

typedef struct
{
	float pos[3];
	float texcoord[2];
	float blend[2];
	u32 color;
} C2Di_Vertex;

typedef struct
{
	DVLB_s* shader;
	shaderProgram_s program;
	C3D_AttrInfo attrInfo;
	C3D_BufInfo bufInfo;
	C3D_ProcTex ptBlend;
	C3D_ProcTexLut ptBlendLut;
	u32 sceneW, sceneH;

	C2Di_Vertex* vtxBuf;
	size_t vtxBufSize;
	size_t vtxBufPos;
	size_t vtxBufLastPos;

	u32 flags;
	C3D_Mtx projMtx;
	C3D_Mtx mdlvMtx;
	C3D_Tex* curTex;
	u32 fadeClr;
} C2Di_Context;

enum
{
	C2DiF_Active    = BIT(0),
	C2DiF_DirtyProj = BIT(1),
	C2DiF_DirtyMdlv = BIT(2),
	C2DiF_DirtyTex  = BIT(3),
	C2DiF_DirtySrc  = BIT(4),
	C2DiF_DirtyFade = BIT(5),

	C2DiF_Src_None  = 0,
	C2DiF_Src_Tex   = BIT(31),
	C2DiF_Src_Mask  = BIT(31),

	C2DiF_DirtyAny = C2DiF_DirtyProj | C2DiF_DirtyMdlv | C2DiF_DirtyTex | C2DiF_DirtySrc | C2DiF_DirtyFade,
};

static inline C2Di_Context* C2Di_GetContext(void)
{
	extern C2Di_Context __C2Di_Context;
	return &__C2Di_Context;
}

static inline void C2Di_SetSrc(u32 src)
{
	C2Di_Context* ctx = C2Di_GetContext();
	src &= C2DiF_Src_Mask;
	if ((ctx->flags & C2DiF_Src_Mask) != src)
		ctx->flags = C2DiF_DirtySrc | (ctx->flags &~ C2DiF_Src_Mask) | src;
}

static inline void C2Di_SetTex(C3D_Tex* tex)
{
	C2Di_Context* ctx = C2Di_GetContext();
	C2Di_SetSrc(C2DiF_Src_Tex);
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
void C2Di_AppendVtx(float x, float y, float z, float u, float v, float blend, u32 color);
void C2Di_FlushVtxBuf(void);
void C2Di_Update(void);
