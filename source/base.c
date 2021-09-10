#include "internal.h"
#include "render2d_shbin.h"

C2Di_Context __C2Di_Context;
static C3D_Mtx s_projTop, s_projBot;
static int uLoc_mdlvMtx, uLoc_projMtx;

static inline bool C2Di_CheckBufSpace(C2Di_Context* ctx, unsigned idx, unsigned vtx)
{
	size_t free_idx = ctx->idxBufSize - ctx->idxBufPos;
	size_t free_vtx = ctx->vtxBufSize - ctx->vtxBufPos;
	return free_idx >= idx && free_vtx >= vtx;
}

static void C2Di_FrameEndHook(void* unused)
{
	C2Di_Context* ctx = C2Di_GetContext();
	C2Di_FlushVtxBuf();
	ctx->vtxBufPos = 0;
	ctx->idxBufPos = 0;
	ctx->idxBufLastPos = 0;
}

bool C2D_Init(size_t maxObjects)
{
	C2Di_Context* ctx = C2Di_GetContext();
	if (ctx->flags & C2DiF_Active)
		return false;

	ctx->vtxBufSize = 4*maxObjects;
	ctx->vtxBuf = (C2Di_Vertex*)linearAlloc(ctx->vtxBufSize*sizeof(C2Di_Vertex));
	if (!ctx->vtxBuf)
		return false;

	ctx->idxBufSize = 6*maxObjects;
	ctx->idxBuf = (u16*)linearAlloc(ctx->idxBufSize*sizeof(u16));
	if (!ctx->idxBuf)
	{
		linearFree(ctx->vtxBuf);
		return false;
	}

	ctx->shader = DVLB_ParseFile((u32*)render2d_shbin, render2d_shbin_size);
	if (!ctx->shader)
	{
		linearFree(ctx->idxBuf);
		linearFree(ctx->vtxBuf);
		return false;
	}

	shaderProgramInit(&ctx->program);
	shaderProgramSetVsh(&ctx->program, &ctx->shader->DVLE[0]);

	AttrInfo_Init(&ctx->attrInfo);
	AttrInfo_AddLoader(&ctx->attrInfo, 0, GPU_FLOAT,         3); // v0=position
	AttrInfo_AddLoader(&ctx->attrInfo, 1, GPU_FLOAT,         2); // v1=texcoord
	AttrInfo_AddLoader(&ctx->attrInfo, 2, GPU_FLOAT,         2); // v2=blend
	AttrInfo_AddLoader(&ctx->attrInfo, 3, GPU_UNSIGNED_BYTE, 4); // v3=color

	BufInfo_Init(&ctx->bufInfo);
	BufInfo_Add(&ctx->bufInfo, ctx->vtxBuf, sizeof(C2Di_Vertex), 4, 0x3210);

	// Cache these common projection matrices
	Mtx_OrthoTilt(&s_projTop, 0.0f, 400.0f, 240.0f, 0.0f, 1.0f, -1.0f, true);
	Mtx_OrthoTilt(&s_projBot, 0.0f, 320.0f, 240.0f, 0.0f, 1.0f, -1.0f, true);

	// Get uniform locations
	uLoc_mdlvMtx = shaderInstanceGetUniformLocation(ctx->program.vertexShader, "mdlvMtx");
	uLoc_projMtx = shaderInstanceGetUniformLocation(ctx->program.vertexShader, "projMtx");

	// Prepare proctex
	C3D_ProcTexInit(&ctx->ptBlend, 0, 1);
	C3D_ProcTexClamp(&ctx->ptBlend, GPU_PT_CLAMP_TO_EDGE, GPU_PT_CLAMP_TO_EDGE);
	C3D_ProcTexCombiner(&ctx->ptBlend, true, GPU_PT_U, GPU_PT_V);
	C3D_ProcTexFilter(&ctx->ptBlend, GPU_PT_LINEAR);

	C3D_ProcTexInit(&ctx->ptCircle, 0, 1);
	C3D_ProcTexClamp(&ctx->ptCircle, GPU_PT_MIRRORED_REPEAT, GPU_PT_MIRRORED_REPEAT);
	C3D_ProcTexCombiner(&ctx->ptCircle, true, GPU_PT_SQRT2, GPU_PT_SQRT2);
	C3D_ProcTexFilter(&ctx->ptCircle, GPU_PT_LINEAR);

	// Prepare proctex lut
	float data[129];
	int i;
	for (i = 0; i <= 128; i ++)
		data[i] = i/128.0f;
	ProcTexLut_FromArray(&ctx->ptBlendLut, data);

	for (i = 0; i <= 128; i ++)
		data[i] = (i >= 127) ? 0 : 1;
	ProcTexLut_FromArray(&ctx->ptCircleLut, data);

	ctx->flags = C2DiF_Active | (C2DiF_Mode_ImageSolid << (C2DiF_TintMode_Shift-C2DiF_Mode_Shift));
	ctx->vtxBufPos = 0;
	ctx->idxBufPos = 0;
	ctx->idxBufLastPos = 0;
	Mtx_Identity(&ctx->projMtx);
	Mtx_Identity(&ctx->mdlvMtx);
	ctx->fadeClr = 0;

	C3D_FrameEndHook(C2Di_FrameEndHook, NULL);
	return true;
}

void C2D_Fini(void)
{
	C2Di_Context* ctx = C2Di_GetContext();
	if (!(ctx->flags & C2DiF_Active))
		return;

	ctx->flags = 0;
	C3D_FrameEndHook(NULL, NULL);
	shaderProgramFree(&ctx->program);
	DVLB_Free(ctx->shader);
	linearFree(ctx->idxBuf);
	linearFree(ctx->vtxBuf);
}

void C2D_Prepare(void)
{
	C2Di_Context* ctx = C2Di_GetContext();
	if (!(ctx->flags & C2DiF_Active))
		return;

	ctx->flags  = (ctx->flags &~ (C2DiF_Mode_Mask|C2DiF_ProcTex_Mask)) | C2DiF_DirtyAny;
	ctx->curTex = NULL;

	C3D_BindProgram(&ctx->program);
	C3D_SetAttrInfo(&ctx->attrInfo);
	C3D_SetBufInfo(&ctx->bufInfo);

	// texenv usage:
	// 0..4: used by switchable mode
	// 5..6: used by post processing
	C3D_TexEnv* env;

	// Configure texenv4 as a no-op (reserved)
	env = C3D_GetTexEnv(4);
	C3D_TexEnvInit(env);

	// Configure texenv5 to apply the fade color
	// texenv5.rgb = mix(texenv4.rgb, fadeclr.rgb, fadeclr.a);
	// texenv5.a   = texenv4.a;
	env = C3D_GetTexEnv(5);
	C3D_TexEnvInit(env);
	C3D_TexEnvSrc(env, C3D_RGB, GPU_PREVIOUS, GPU_CONSTANT, GPU_CONSTANT);
	C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_ONE_MINUS_SRC_ALPHA);
	C3D_TexEnvFunc(env, C3D_RGB, GPU_INTERPOLATE);
	C3D_TexEnvColor(env, ctx->fadeClr);

	// Configure depth test to overwrite pixels with the same depth (needed to draw overlapping sprites)
	C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_ALL);

	// Don't cull anything
	C3D_CullFace(GPU_CULL_NONE);
}

void C2D_Flush(void)
{
	C2Di_Context* ctx = C2Di_GetContext();
	if (!(ctx->flags & C2DiF_Active))
		return;

	C2Di_FlushVtxBuf();
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
	if (height == GSP_SCREEN_WIDTH && tilt)
	{
		if (width == GSP_SCREEN_HEIGHT_TOP || width == GSP_SCREEN_HEIGHT_TOP_2X)
		{
			Mtx_Copy(&ctx->projMtx, &s_projTop);
			return;
		}
		else if (width == GSP_SCREEN_HEIGHT_BOTTOM)
		{
			Mtx_Copy(&ctx->projMtx, &s_projBot);
			return;
		}
	}

	// Construct the projection matrix
	(tilt ? Mtx_OrthoTilt : Mtx_Ortho)(&ctx->projMtx, 0.0f, width, height, 0.0f, 1.0f, -1.0f, true);
}

void C2D_ViewReset(void)
{
	C2Di_Context* ctx = C2Di_GetContext();
	if (!(ctx->flags & C2DiF_Active))
		return;

	Mtx_Identity(&ctx->mdlvMtx);
	ctx->flags |= C2DiF_DirtyMdlv;
}

void C2D_ViewSave(C3D_Mtx* matrix)
{
	C2Di_Context* ctx = C2Di_GetContext();
	Mtx_Copy(matrix, &ctx->mdlvMtx);
}

void C2D_ViewRestore(const C3D_Mtx* matrix)
{
	C2Di_Context* ctx = C2Di_GetContext();
	if (!(ctx->flags & C2DiF_Active))
		return;

	Mtx_Copy(&ctx->mdlvMtx, matrix);
	ctx->flags |= C2DiF_DirtyMdlv;
}

void C2D_ViewTranslate(float x, float y)
{
	C2Di_Context* ctx = C2Di_GetContext();
	if (!(ctx->flags & C2DiF_Active))
		return;

	Mtx_Translate(&ctx->mdlvMtx, x, y, 0.0f, true);
	ctx->flags |= C2DiF_DirtyMdlv;
}

void C2D_ViewRotate(float radians)
{
	C2Di_Context* ctx = C2Di_GetContext();
	if (!(ctx->flags & C2DiF_Active))
		return;

	Mtx_RotateZ(&ctx->mdlvMtx, radians, true);
	ctx->flags |= C2DiF_DirtyMdlv;
}

void C2D_ViewShear(float x, float y)
{
	C2Di_Context* ctx = C2Di_GetContext();
	if (!(ctx->flags & C2DiF_Active))
		return;

	C3D_Mtx mult;
	Mtx_Identity(&mult);
	mult.r[0].y = x;
	mult.r[1].x = y;
	Mtx_Multiply(&ctx->mdlvMtx, &ctx->mdlvMtx, &mult);
	ctx->flags |= C2DiF_DirtyMdlv;
}

void C2D_ViewScale(float x, float y)
{
	C2Di_Context* ctx = C2Di_GetContext();
	if (!(ctx->flags & C2DiF_Active))
		return;

	Mtx_Scale(&ctx->mdlvMtx, x, y, 1.0f);
	ctx->flags |= C2DiF_DirtyMdlv;
}

C3D_RenderTarget* C2D_CreateScreenTarget(gfxScreen_t screen, gfx3dSide_t side)
{
	int height;
	switch (screen)
	{
		default:
		case GFX_BOTTOM:
			height = GSP_SCREEN_HEIGHT_BOTTOM;
			break;
		case GFX_TOP:
			height = !gfxIsWide() ? GSP_SCREEN_HEIGHT_TOP : GSP_SCREEN_HEIGHT_TOP_2X;
			break;
	}
	C3D_RenderTarget* target = C3D_RenderTargetCreate(GSP_SCREEN_WIDTH, height, GPU_RB_RGBA8, GPU_RB_DEPTH16);
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
	C3D_RenderTargetClear(target, C3D_CLEAR_ALL, __builtin_bswap32(color), 0);
}

void C2D_Fade(u32 color)
{
	C2Di_Context* ctx = C2Di_GetContext();
	if (!(ctx->flags & C2DiF_Active))
		return false;

	ctx->flags |= C2DiF_DirtyFade;
	ctx->fadeClr = color;
}

void C2D_SetTintMode(C2D_TintMode mode)
{
	C2Di_Context* ctx = C2Di_GetContext();
	if (!(ctx->flags & C2DiF_Active))
		return false;

	u32 new_mode;
	switch (mode)
	{
		default:
		case C2D_TintSolid:
			new_mode = C2DiF_Mode_ImageSolid;
			break;
		case C2D_TintMult:
			new_mode = C2DiF_Mode_ImageMult;
			break;
		case C2D_TintLuma:
			new_mode = C2DiF_Mode_ImageLuma;
			break;
	}

	ctx->flags = (ctx->flags &~ C2DiF_TintMode_Mask) | (new_mode << (C2DiF_TintMode_Shift - C2DiF_Mode_Shift));
}

static inline void C2Di_RotatePoint(float* point, float rsin, float rcos)
{
	float x = point[0] * rcos - point[1] * rsin;
	float y = point[1] * rcos + point[0] * rsin;
	point[0] = x;
	point[1] = y;
}

static inline void C2Di_SwapUV(float* a, float* b)
{
	float temp[2] = { a[0], a[1] };
	a[0] = b[0];
	a[1] = b[1];
	b[0] = temp[0];
	b[1] = temp[1];
}

void C2Di_CalcQuad(C2Di_Quad* quad, const C2D_DrawParams* params)
{
	const float w = fabs(params->pos.w);
	const float h = fabs(params->pos.h);

	quad->topLeft[0]  = -params->center.x;
	quad->topLeft[1]  = -params->center.y;
	quad->topRight[0] = -params->center.x+w;
	quad->topRight[1] = -params->center.y;
	quad->botLeft[0]  = -params->center.x;
	quad->botLeft[1]  = -params->center.y+h;
	quad->botRight[0] = -params->center.x+w;
	quad->botRight[1] = -params->center.y+h;

	if (params->angle != 0.0f)
	{
		float rsin = sinf(params->angle);
		float rcos = cosf(params->angle);
		C2Di_RotatePoint(quad->topLeft,  rsin, rcos);
		C2Di_RotatePoint(quad->topRight, rsin, rcos);
		C2Di_RotatePoint(quad->botLeft,  rsin, rcos);
		C2Di_RotatePoint(quad->botRight, rsin, rcos);
	}

	quad->topLeft[0]  += params->pos.x;
	quad->topLeft[1]  += params->pos.y;
	quad->topRight[0] += params->pos.x;
	quad->topRight[1] += params->pos.y;
	quad->botLeft[0]  += params->pos.x;
	quad->botLeft[1]  += params->pos.y;
	quad->botRight[0] += params->pos.x;
	quad->botRight[1] += params->pos.y;
}

bool C2D_DrawImage(C2D_Image img, const C2D_DrawParams* params, const C2D_ImageTint* tint)
{
	C2Di_Context* ctx = C2Di_GetContext();
	if (!(ctx->flags & C2DiF_Active))
		return false;
	if (!C2Di_CheckBufSpace(ctx, 6, 4))
		return false;

	C2Di_SetMode((ctx->flags & C2DiF_TintMode_Mask) >> (C2DiF_TintMode_Shift - C2DiF_Mode_Shift));
	C2Di_SetTex(img.tex);
	C2Di_Update();

	// Calculate positions
	C2Di_Quad quad;
	C2Di_CalcQuad(&quad, params);

	// Calculate texcoords
	float tcTopLeft[2], tcTopRight[2], tcBotLeft[2], tcBotRight[2];
	Tex3DS_SubTextureTopLeft    (img.subtex, &tcTopLeft[0],  &tcTopLeft[1]);
	Tex3DS_SubTextureTopRight   (img.subtex, &tcTopRight[0], &tcTopRight[1]);
	Tex3DS_SubTextureBottomLeft (img.subtex, &tcBotLeft[0],  &tcBotLeft[1]);
	Tex3DS_SubTextureBottomRight(img.subtex, &tcBotRight[0], &tcBotRight[1]);

	// Perform flip if needed
	if (params->pos.w < 0)
	{
		C2Di_SwapUV(tcTopLeft, tcTopRight);
		C2Di_SwapUV(tcBotLeft, tcBotRight);
	}
	if (params->pos.h < 0)
	{
		C2Di_SwapUV(tcTopLeft, tcBotLeft);
		C2Di_SwapUV(tcTopRight, tcBotRight);
	}

	// Calculate colors
	static const C2D_Tint s_defaultTint = { 0xFF<<24, 0.0f };
	const C2D_Tint* tintTopLeft  = tint ? &tint->corners[C2D_TopLeft]  : &s_defaultTint;
	const C2D_Tint* tintTopRight = tint ? &tint->corners[C2D_TopRight] : &s_defaultTint;
	const C2D_Tint* tintBotLeft  = tint ? &tint->corners[C2D_BotLeft]  : &s_defaultTint;
	const C2D_Tint* tintBotRight = tint ? &tint->corners[C2D_BotRight] : &s_defaultTint;

	C2Di_AppendQuad();
	C2Di_AppendVtx(quad.topLeft[0],  quad.topLeft[1],  params->depth, tcTopLeft[0],  tcTopLeft[1],  0, tintTopLeft->blend,  tintTopLeft->color);
	C2Di_AppendVtx(quad.topRight[0], quad.topRight[1], params->depth, tcTopRight[0], tcTopRight[1], 0, tintTopRight->blend, tintTopRight->color);
	C2Di_AppendVtx(quad.botLeft[0],  quad.botLeft[1],  params->depth, tcBotLeft[0],  tcBotLeft[1],  0, tintBotLeft->blend,  tintBotLeft->color);
	C2Di_AppendVtx(quad.botRight[0], quad.botRight[1], params->depth, tcBotRight[0], tcBotRight[1], 0, tintBotRight->blend, tintBotRight->color);
	return true;
}

bool C2D_DrawTriangle(float x0, float y0, u32 clr0, float x1, float y1, u32 clr1, float x2, float y2, u32 clr2, float depth)
{
	C2Di_Context* ctx = C2Di_GetContext();
	if (!(ctx->flags & C2DiF_Active))
		return false;
	if (!C2Di_CheckBufSpace(ctx, 3, 3))
		return false;

	C2Di_SetMode(C2DiF_Mode_Solid);
	C2Di_Update();

	C2Di_AppendTri();
	C2Di_AppendVtx(x0, y0, depth, 0.0f, 0.0f, 0.0f, 0.0f, clr0);
	C2Di_AppendVtx(x1, y1, depth, 0.0f, 0.0f, 0.0f, 0.0f, clr1);
	C2Di_AppendVtx(x2, y2, depth, 0.0f, 0.0f, 0.0f, 0.0f, clr2);
	return true;
}

bool C2D_DrawLine(float x0, float y0, u32 clr0, float x1, float y1, u32 clr1, float thickness, float depth)
{
	C2Di_Context* ctx = C2Di_GetContext();
	if (!(ctx->flags & C2DiF_Active))
		return false;
	if (!C2Di_CheckBufSpace(ctx, 6, 4))
		return false;

	float dx = x1-x0, dy = y1-y0, len = sqrtf(dx*dx+dy*dy), th = thickness/2;
	float ux = (-dy/len)*th, uy = (dx/len)*th;
	float px0 = x0-ux, py0 = y0-uy, px1 = x0+ux, py1 = y0+uy, px2 = x1+ux, py2 = y1+uy, px3 = x1-ux, py3 = y1-uy;

	C2Di_SetMode(C2DiF_Mode_Solid);
	C2Di_Update();

	C2Di_AppendQuad();
	C2Di_AppendVtx(px0, py0, depth, 0.0f, 0.0f, 0.0f, 0.0f, clr0);
	C2Di_AppendVtx(px3, py3, depth, 0.0f, 0.0f, 0.0f, 0.0f, clr1);
	C2Di_AppendVtx(px1, py1, depth, 0.0f, 0.0f, 0.0f, 0.0f, clr0);
	C2Di_AppendVtx(px2, py2, depth, 0.0f, 0.0f, 0.0f, 0.0f, clr1);
	return true;
}

bool C2D_DrawRectangle(float x, float y, float z, float w, float h, u32 clr0, u32 clr1, u32 clr2, u32 clr3)
{
	C2Di_Context* ctx = C2Di_GetContext();
	if (!(ctx->flags & C2DiF_Active))
		return false;
	if (!C2Di_CheckBufSpace(ctx, 6, 4))
		return false;

	C2Di_SetMode(C2DiF_Mode_Solid);
	C2Di_Update();

	C2Di_AppendQuad();
	C2Di_AppendVtx(x,   y,   z, 0.0f, 0.0f, 0.0f, 0.0f, clr0);
	C2Di_AppendVtx(x+w, y,   z, 0.0f, 0.0f, 0.0f, 0.0f, clr1);
	C2Di_AppendVtx(x,   y+h, z, 0.0f, 0.0f, 0.0f, 0.0f, clr2);
	C2Di_AppendVtx(x+w, y+h, z, 0.0f, 0.0f, 0.0f, 0.0f, clr3);
	return true;
}

bool C2D_DrawEllipse(float x, float y, float z, float w, float h, u32 clr0, u32 clr1, u32 clr2, u32 clr3)
{
	C2Di_Context* ctx = C2Di_GetContext();
	if (!(ctx->flags & C2DiF_Active))
		return false;
	if (!C2Di_CheckBufSpace(ctx, 6, 4))
		return false;

	C2Di_SetMode(C2DiF_Mode_Circle);
	C2Di_Update();

	C2Di_AppendQuad();
	C2Di_AppendVtx(x,   y,   z, 0.0f, 0.0f, -1.0f, -1.0f, clr0);
	C2Di_AppendVtx(x+w, y,   z, 0.0f, 0.0f,  1.0f, -1.0f, clr1);
	C2Di_AppendVtx(x,   y+h, z, 0.0f, 0.0f, -1.0f,  1.0f, clr2);
	C2Di_AppendVtx(x+w, y+h, z, 0.0f, 0.0f,  1.0f,  1.0f, clr3);
	return true;
}

void C2Di_AppendTri(void)
{
	C2Di_Context* ctx = C2Di_GetContext();
	u16* idx = &ctx->idxBuf[ctx->idxBufPos];
	ctx->idxBufPos += 3;

	*idx++ = ctx->vtxBufPos+0;
	*idx++ = ctx->vtxBufPos+1;
	*idx++ = ctx->vtxBufPos+2;
}

void C2Di_AppendQuad(void)
{
	C2Di_Context* ctx = C2Di_GetContext();
	u16* idx = &ctx->idxBuf[ctx->idxBufPos];
	ctx->idxBufPos += 6;

	*idx++ = ctx->vtxBufPos+0;
	*idx++ = ctx->vtxBufPos+2;
	*idx++ = ctx->vtxBufPos+1;
	*idx++ = ctx->vtxBufPos+1;
	*idx++ = ctx->vtxBufPos+2;
	*idx++ = ctx->vtxBufPos+3;
}

void C2Di_AppendVtx(float x, float y, float z, float u, float v, float ptx, float pty, u32 color)
{
	C2Di_Context* ctx = C2Di_GetContext();
	C2Di_Vertex* vtx = &ctx->vtxBuf[ctx->vtxBufPos++];
	vtx->pos[0]      = x;
	vtx->pos[1]      = y;
	vtx->pos[2]      = z;
	vtx->texcoord[0] = u;
	vtx->texcoord[1] = v;
	vtx->ptcoord[0]  = ptx;
	vtx->ptcoord[1]  = pty;
	vtx->color       = color;
}

void C2Di_FlushVtxBuf(void)
{
	C2Di_Context* ctx = C2Di_GetContext();
	size_t len = ctx->idxBufPos - ctx->idxBufLastPos;
	if (!len) return;
	C3D_DrawElements(GPU_TRIANGLES, len, C3D_UNSIGNED_SHORT, &ctx->idxBuf[ctx->idxBufLastPos]);
	ctx->idxBufLastPos = ctx->idxBufPos;
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
	if (flags & C2DiF_DirtyFade)
		C3D_TexEnvColor(C3D_GetTexEnv(5), ctx->fadeClr);

	u32 mode = ctx->flags & C2DiF_Mode_Mask;
	u32 proctex = C2DiF_ProcTex_None;
	C3D_TexEnv* env;

	if (flags & C2DiF_DirtyMode) switch (mode)
	{
		case C2DiF_Mode_Solid:
		{
			// Plain ol' passthrough of vertex color.
			env = C3D_GetTexEnv(0);
			C3D_TexEnvInit(env);
			C3D_TexEnvSrc(env, C3D_Both, GPU_PRIMARY_COLOR, 0, 0);
			C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);

			// Reset unused texenv stages
			C3D_TexEnvInit(C3D_GetTexEnv(1));
			C3D_TexEnvInit(C3D_GetTexEnv(2));
			C3D_TexEnvInit(C3D_GetTexEnv(3));
			break;
		}

		case C2DiF_Mode_Circle:
		case C2DiF_Mode_Text:
		{
			GPU_TEVSRC alphamask = GPU_TEXTURE0;
			if (mode == C2DiF_Mode_Circle)
			{
				// Generate a circle shaped alpha mask (u^2 + v^2 < 1) using proctex.
				proctex = C2DiF_ProcTex_Circle;
				alphamask = GPU_TEXTURE3;
			}

			// texenv0.rgb = vtxcolor.rgb
			// texenv0.a = vtxcolor.a * alphamask
			env = C3D_GetTexEnv(0);
			C3D_TexEnvInit(env);
			C3D_TexEnvSrc(env, C3D_RGB, GPU_PRIMARY_COLOR, 0, 0);
			C3D_TexEnvFunc(env, C3D_RGB, GPU_REPLACE);
			C3D_TexEnvSrc(env, C3D_Alpha, GPU_PRIMARY_COLOR, alphamask, 0);
			C3D_TexEnvFunc(env, C3D_Alpha, GPU_MODULATE);

			// Reset unused texenv stages
			C3D_TexEnvInit(C3D_GetTexEnv(1));
			C3D_TexEnvInit(C3D_GetTexEnv(2));
			C3D_TexEnvInit(C3D_GetTexEnv(3));
			break;
		}

		case C2DiF_Mode_ImageSolid:
		{
			// Use texenv to blend the color source with the solid tint color,
			// according to a parameter that is passed through with the help of proctex.
			proctex = C2DiF_ProcTex_Blend;

			// texenv0.rgb = mix(texclr.rgb, vtxcolor.rgb, vtx.blend.y);
			// texenv0.a = texclr.a * vtxcolor.a
			env = C3D_GetTexEnv(0);
			C3D_TexEnvInit(env);
			C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_TEXTURE3);
			C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_ONE_MINUS_SRC_ALPHA);
			C3D_TexEnvFunc(env, C3D_RGB, GPU_INTERPOLATE);
			C3D_TexEnvSrc(env, C3D_Alpha, GPU_TEXTURE0, GPU_PRIMARY_COLOR, 0);
			C3D_TexEnvFunc(env, C3D_Alpha, GPU_MODULATE);

			// Reset unused texenv stages
			C3D_TexEnvInit(C3D_GetTexEnv(1));
			C3D_TexEnvInit(C3D_GetTexEnv(2));
			C3D_TexEnvInit(C3D_GetTexEnv(3));
			break;
		}

		case C2DiF_Mode_ImageMult:
		{
			// Use texenv to blend the color source with the multiply-tinted version of it,
			// according to a parameter that is passed through with the help of proctex.
			proctex = C2DiF_ProcTex_Blend;

			// texenv0 = texclr * vtxcolor
			env = C3D_GetTexEnv(0);
			C3D_TexEnvInit(env);
			C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, 0);
			C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);

			// texenv1.rgb = mix(texclr.rgb, texenv0.rgb, vtx.blend.y);
			env = C3D_GetTexEnv(1);
			C3D_TexEnvInit(env);
			C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, GPU_PREVIOUS, GPU_TEXTURE3);
			C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_ONE_MINUS_SRC_ALPHA);
			C3D_TexEnvFunc(env, C3D_RGB, GPU_INTERPOLATE);

			// Reset unused texenv stages
			C3D_TexEnvInit(C3D_GetTexEnv(2));
			C3D_TexEnvInit(C3D_GetTexEnv(3));
			break;
		}

		case C2DiF_Mode_ImageLuma:
		{
			// Use texenv to blend the color source with the grayscale/tinted version of it,
			// according to a parameter that is passed through with the help of proctex.
			proctex = C2DiF_ProcTex_Blend;

			// texenv0.rgb = 0.5*texclr.rgb + 0.5
			// texenv0.a = texclr.a * vtxcolor.a
			env = C3D_GetTexEnv(0);
			C3D_TexEnvInit(env);
			C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, GPU_CONSTANT, GPU_CONSTANT);
			C3D_TexEnvFunc(env, C3D_RGB, GPU_MULTIPLY_ADD);
			C3D_TexEnvSrc(env, C3D_Alpha, GPU_TEXTURE0, GPU_PRIMARY_COLOR, 0);
			C3D_TexEnvFunc(env, C3D_Alpha, GPU_MODULATE);
			C3D_TexEnvColor(env, 0x808080);

			// K_grayscale = 0.5*vec3(0.299, 0.587, 0.114) + 0.5
			// texenv1.rgb = 4*dot(texenv0.rgb - 0.5, K_grayscale - 0.5)
			env = C3D_GetTexEnv(1);
			C3D_TexEnvInit(env);
			C3D_TexEnvSrc(env, C3D_RGB, GPU_PREVIOUS, GPU_CONSTANT, 0);
			C3D_TexEnvFunc(env, C3D_RGB, GPU_DOT3_RGB);
			C3D_TexEnvColor(env, 0x8ecaa6);

			// texenv2.rgb = texenv1.rgb * vtxcolor.rgb
			env = C3D_GetTexEnv(2);
			C3D_TexEnvInit(env);
			C3D_TexEnvSrc(env, C3D_RGB, GPU_PREVIOUS, GPU_PRIMARY_COLOR, 0);
			C3D_TexEnvFunc(env, C3D_RGB, GPU_MODULATE);

			// texenv3.rgb = mix(texclr.rgb, texenv2.rgb, vtx.blend.y);
			env = C3D_GetTexEnv(3);
			C3D_TexEnvInit(env);
			C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, GPU_PREVIOUS, GPU_TEXTURE3);
			C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_ONE_MINUS_SRC_ALPHA);
			C3D_TexEnvFunc(env, C3D_RGB, GPU_INTERPOLATE);
			break;
		}
	}

	if (proctex && proctex != (ctx->flags & C2DiF_ProcTex_Mask))
	{
		ctx->flags = (ctx->flags &~ C2DiF_ProcTex_Mask) | proctex;
		switch (proctex)
		{
			case C2DiF_ProcTex_Blend:
				C3D_ProcTexBind(1, &ctx->ptBlend);
				C3D_ProcTexLutBind(GPU_LUT_ALPHAMAP, &ctx->ptBlendLut);
				break;
			case C2DiF_ProcTex_Circle:
				C3D_ProcTexBind(1, &ctx->ptCircle);
				C3D_ProcTexLutBind(GPU_LUT_ALPHAMAP, &ctx->ptCircleLut);
				break;
		}
	}

	ctx->flags &= ~C2DiF_DirtyAny;
}
