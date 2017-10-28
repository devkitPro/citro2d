#pragma once
#include <c2d/base.h>

typedef struct
{
	float pos[3];
	float texcoord[2];
} C2Di_Vertex;

typedef struct
{
	DVLB_s* shader;
	shaderProgram_s program;
	C3D_AttrInfo attrInfo;
	C3D_BufInfo bufInfo;
	u32 sceneW, sceneH;

	C2Di_Vertex* vtxBuf;
	size_t vtxBufSize;
	size_t vtxBufPos;
	size_t vtxBufLastPos;

	u32 flags;
	C3D_Mtx projMtx;
	C3D_Mtx mdlvMtx;
	C3D_Tex* curTex;
} C2Di_Context;

enum
{
	C2DiF_Active    = BIT(0),
	C2DiF_DirtyProj = BIT(1),
	C2DiF_DirtyMdlv = BIT(2),
	C2DiF_DirtyTex  = BIT(3),

	C2DiF_DirtyAny = C2DiF_DirtyProj | C2DiF_DirtyMdlv | C2DiF_DirtyTex,
};

static inline C2Di_Context* C2Di_GetContext(void)
{
	extern C2Di_Context __C2Di_Context;
	return &__C2Di_Context;
}

void C2Di_AppendVtx(float x, float y, float z, float u, float v);
void C2Di_FlushVtxBuf(void);
void C2Di_Update(void);
