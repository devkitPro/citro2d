#include "internal.h"
#include "render2d_shbin.h"

C2Di_Context __C2Di_Context;
static C3D_Mtx s_projTop, s_projBot;
static int uLoc_mdlvMtx, uLoc_projMtx;

bool C2D_Init(size_t maxObjects)
{
	C2Di_Context* ctx = C2Di_GetContext();
	if (ctx->flags & C2DiF_Active)
		return false;

	ctx->vtxBufSize = 6*maxObjects;
	ctx->vtxBuf = (C2Di_Vertex*)linearAlloc(ctx->vtxBufSize*sizeof(C2Di_Vertex));
	if (!ctx->vtxBuf)
		return false;

	ctx->shader = DVLB_ParseFile((u32*)render2d_shbin, render2d_shbin_size);
	if (!ctx->shader)
	{
		linearFree(ctx->vtxBuf);
		return false;
	}

	shaderProgramInit(&ctx->program);
	shaderProgramSetVsh(&ctx->program, &ctx->shader->DVLE[0]);

	AttrInfo_Init(&ctx->attrInfo);
	AttrInfo_AddLoader(&ctx->attrInfo, 0, GPU_FLOAT, 3); // v0=position
	AttrInfo_AddLoader(&ctx->attrInfo, 1, GPU_FLOAT, 2); // v1=texcoord

	BufInfo_Init(&ctx->bufInfo);
	BufInfo_Add(&ctx->bufInfo, ctx->vtxBuf, sizeof(C2Di_Vertex), 2, 0x10);

	// Cache these common projection matrices
	Mtx_OrthoTilt(&s_projTop, 0.0f, 400.0f, 240.0f, 0.0f, 1.0f, -1.0f, true);
	Mtx_OrthoTilt(&s_projBot, 0.0f, 320.0f, 240.0f, 0.0f, 1.0f, -1.0f, true);

	// Get uniform locations
	uLoc_mdlvMtx = shaderInstanceGetUniformLocation(ctx->program.vertexShader, "mdlvMtx");
	uLoc_projMtx = shaderInstanceGetUniformLocation(ctx->program.vertexShader, "projMtx");

	ctx->flags = C2DiF_Active;
	ctx->vtxBufPos = 0;
	ctx->vtxBufLastPos = 0;
	Mtx_Identity(&ctx->projMtx);
	Mtx_Identity(&ctx->mdlvMtx);
	return true;
}

void C2D_Fini(void)
{
	C2Di_Context* ctx = C2Di_GetContext();
	if (!(ctx->flags & C2DiF_Active))
		return;

	ctx->flags = 0;
	shaderProgramFree(&ctx->program);
	DVLB_Free(ctx->shader);
	linearFree(ctx->vtxBuf);
}

void C2D_Prepare(void)
{
	C2Di_Context* ctx = C2Di_GetContext();
	if (!(ctx->flags & C2DiF_Active))
		return;

	ctx->flags |= C2DiF_DirtyAny;
	C3D_BindProgram(&ctx->program);
	C3D_SetAttrInfo(&ctx->attrInfo);
	C3D_SetBufInfo(&ctx->bufInfo);

	// Set texenv0 to output the texture color blended with the vertex color
	C3D_TexEnv* env = C3D_GetTexEnv(0);
	C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, 0);
	C3D_TexEnvOp(env, C3D_Both, 0, 0, 0);
	C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);

	// Configure depth test to overwrite pixels with the same depth (needed to draw overlapping sprites)
	C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_ALL);
}

void C2D_SceneDone(bool endFrame)
{
	C2Di_Context* ctx = C2Di_GetContext();
	if (!(ctx->flags & C2DiF_Active))
		return;

	C2Di_FlushVtxBuf();
	if (endFrame)
	{
		ctx->vtxBufPos = 0;
		ctx->vtxBufLastPos = 0;
	}
}

void C2D_SceneSize(u32 width, u32 height, bool tilt)
{
	C2Di_Context* ctx = C2Di_GetContext();
	if (!(ctx->flags & C2DiF_Active))
		return;

	if (tilt)
	{
		u32 temp = width;
		width = height;
		height = temp;
	}

	ctx->flags |= C2DiF_DirtyProj;
	ctx->sceneW = width;
	ctx->sceneH = height;

	// Check for cached projection matrices
	if (height == 240 && tilt)
	{
		if (width == 400)
		{
			Mtx_Copy(&ctx->projMtx, &s_projTop);
			return;
		}
		else if (width == 320)
		{
			Mtx_Copy(&ctx->projMtx, &s_projBot);
			return;
		}
	}

	// Construct the projection matrix
	(tilt ? Mtx_OrthoTilt : Mtx_Ortho)(&ctx->projMtx, 0.0f, width, height, 0.0f, 1.0f, -1.0f, true);
}

C3D_RenderTarget* C2D_CreateScreenTarget(gfxScreen_t screen, gfx3dSide_t side)
{
	C3D_RenderTarget* target = C3D_RenderTargetCreate(240, screen==GFX_TOP ? 400 : 320, GPU_RB_RGBA8, GPU_RB_DEPTH16);
	if (target)
		C3D_RenderTargetSetOutput(target, screen, side,
			GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |
			GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) |
			GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
	return target;
}

void C2D_TargetClear(C3D_RenderTarget* target, u32 color)
{
	C2Di_FlushVtxBuf();
	C3D_FrameSplit(0);
	C3D_FrameBufClear(&target->frameBuf, C3D_CLEAR_ALL, color, 0);
}

static inline void C2Di_RotatePoint(float* point, float rsin, float rcos)
{
	float x = point[0] * rcos - point[1] * rsin;
	float y = point[1] * rcos + point[0] * rsin;
	point[0] = x;
	point[1] = y;
}

bool C2D_DrawImage(C2D_Image img, C2D_DrawParams params)
{
	C2Di_Context* ctx = C2Di_GetContext();
	if (!(ctx->flags & C2DiF_Active))
		return false;
	if (6 > (ctx->vtxBufSize - ctx->vtxBufPos))
		return false;

	if (img.tex != ctx->curTex)
	{
		ctx->flags |= C2DiF_DirtyTex;
		ctx->curTex = img.tex;
	}

	C2Di_Update();

	// Calculate positions
	float topLeft[2], topRight[2], botLeft[2], botRight[2];

	topLeft[0]  = -params.center.x;
	topLeft[1]  = -params.center.y;
	topRight[0] = -params.center.x+params.pos.w;
	topRight[1] = -params.center.y;
	botLeft[0]  = -params.center.x;
	botLeft[1]  = -params.center.y+params.pos.h;
	botRight[0] = -params.center.x+params.pos.w;
	botRight[1] = -params.center.y+params.pos.h;

	if (params.angle != 0.0f)
	{
		float rsin = sinf(params.angle);
		float rcos = cosf(params.angle);
		C2Di_RotatePoint(topLeft,  rsin, rcos);
		C2Di_RotatePoint(topRight, rsin, rcos);
		C2Di_RotatePoint(botLeft,  rsin, rcos);
		C2Di_RotatePoint(botRight, rsin, rcos);
	}

	topLeft[0]  += params.pos.x;
	topLeft[1]  += params.pos.y;
	topRight[0] += params.pos.x;
	topRight[1] += params.pos.y;
	botLeft[0]  += params.pos.x;
	botLeft[1]  += params.pos.y;
	botRight[0] += params.pos.x;
	botRight[1] += params.pos.y;

	// Calculate texcoords
	float tcTopLeft[2], tcTopRight[2], tcBotLeft[2], tcBotRight[2];
	Tex3DS_SubTextureTopLeft    (img.subtex, &tcTopLeft[0],  &tcTopLeft[1]);
	Tex3DS_SubTextureTopRight   (img.subtex, &tcTopRight[0], &tcTopRight[1]);
	Tex3DS_SubTextureBottomLeft (img.subtex, &tcBotLeft[0],  &tcBotLeft[1]);
	Tex3DS_SubTextureBottomRight(img.subtex, &tcBotRight[0], &tcBotRight[1]);

	// Draw triangles
	C2Di_AppendVtx(topLeft[0],  topLeft[1],  params.depth, tcTopLeft[0],  tcTopLeft[1]);
	C2Di_AppendVtx(botLeft[0],  botLeft[1],  params.depth, tcBotLeft[0],  tcBotLeft[1]);
	C2Di_AppendVtx(botRight[0], botRight[1], params.depth, tcBotRight[0], tcBotRight[1]);

	C2Di_AppendVtx(topLeft[0],  topLeft[1],  params.depth, tcTopLeft[0],  tcTopLeft[1]);
	C2Di_AppendVtx(botRight[0], botRight[1], params.depth, tcBotRight[0], tcBotRight[1]);
	C2Di_AppendVtx(topRight[0], topRight[1], params.depth, tcTopRight[0], tcTopRight[1]);

	return true;
}

void C2Di_AppendVtx(float x, float y, float z, float u, float v)
{
	C2Di_Context* ctx = C2Di_GetContext();
	C2Di_Vertex* vtx = &ctx->vtxBuf[ctx->vtxBufPos++];
	vtx->pos[0] = x;
	vtx->pos[1] = y;
	vtx->pos[2] = z;
	vtx->texcoord[0] = u;
	vtx->texcoord[1] = v;
}

void C2Di_FlushVtxBuf(void)
{
	C2Di_Context* ctx = C2Di_GetContext();
	size_t len = ctx->vtxBufPos - ctx->vtxBufLastPos;
	if (!len) return;
	C3D_DrawArrays(GPU_TRIANGLES, ctx->vtxBufLastPos, len);
	ctx->vtxBufLastPos = ctx->vtxBufPos;
}

void C2Di_Update(void)
{
	C2Di_Context* ctx = C2Di_GetContext();
	u32 flags = ctx->flags & C2DiF_DirtyAny;
	if (!flags) return;

	C2Di_FlushVtxBuf();

	if (flags & C2DiF_DirtyProj)
		C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_projMtx, &ctx->projMtx);
	if (flags & C2DiF_DirtyMdlv)
		C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_mdlvMtx, &ctx->mdlvMtx);
	if (flags & C2DiF_DirtyTex)
		C3D_TexBind(0, ctx->curTex);

	ctx->flags &= ~C2DiF_DirtyAny;
}
