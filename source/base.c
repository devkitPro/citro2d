#include "internal.h"
#include "render2d_shbin.h"

C2Di_Context __C2Di_Context;
static C3D_Mtx s_projTop, s_projBot;
static int uLoc_mdlvMtx, uLoc_projMtx;

static void C2Di_FrameEndHook(void* unused)
{
	C2Di_Context* ctx = C2Di_GetContext();
	C2Di_FlushVtxBuf();
	ctx->vtxBufPos = 0;
	ctx->vtxBufLastPos = 0;
}

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

	// Prepare proctex lut
	float data[129];
	int i;
	for (i = 0; i <= 128; i ++)
		data[i] = i/128.0f;
	ProcTexLut_FromArray(&ctx->ptBlendLut, data);

	ctx->flags = C2DiF_Active;
	ctx->vtxBufPos = 0;
	ctx->vtxBufLastPos = 0;
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
	linearFree(ctx->vtxBuf);
}

void C2D_Prepare(void)
{
	C2Di_Context* ctx = C2Di_GetContext();
	if (!(ctx->flags & C2DiF_Active))
		return;

	ctx->flags  = (ctx->flags &~ C2DiF_Src_Mask) | C2DiF_DirtyAny;
	ctx->curTex = NULL;

	C3D_BindProgram(&ctx->program);
	C3D_SetAttrInfo(&ctx->attrInfo);
	C3D_SetBufInfo(&ctx->bufInfo);
	C3D_ProcTexBind(1, &ctx->ptBlend);
	C3D_ProcTexLutBind(GPU_LUT_ALPHAMAP, &ctx->ptBlendLut);

	C3D_TexEnv* env;

	// Set texenv0 to retrieve the texture color (or white if disabled)
	// texenv0.rgba = texture2D(texunit0, vtx.texcoord0);
	env = C3D_GetTexEnv(0);
	C3D_TexEnvInit(env);
	//C3D_TexEnvSrc set afterwards by C2Di_Update()
	C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
	C3D_TexEnvColor(env, 0xFFFFFFFF);

	// Set texenv1 to blend the output of texenv0 with the primary color
	// texenv1.rgb = mix(texenv0.rgb, vtx.color.rgb, vtx.blend.y);
	// texenv1.a   = texenv0.a * vtx.color.a;
	env = C3D_GetTexEnv(1);
	C3D_TexEnvInit(env);
	C3D_TexEnvSrc(env, C3D_RGB, GPU_PREVIOUS, GPU_PRIMARY_COLOR, GPU_TEXTURE3);
	C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_ONE_MINUS_SRC_ALPHA);
	C3D_TexEnvSrc(env, C3D_Alpha, GPU_PREVIOUS, GPU_PRIMARY_COLOR, 0);
	C3D_TexEnvFunc(env, C3D_RGB, GPU_INTERPOLATE);
	C3D_TexEnvFunc(env, C3D_Alpha, GPU_MODULATE);

	/*
	// Set texenv2 to tint the output of texenv1 with the specified tint
	// texenv2.rgb = texenv1.rgb * colorwheel(vtx.blend.x);
	// texenv2.a   = texenv1.a;
	env = C3D_GetTexEnv(2);
	C3D_TexEnvInit(env);
	C3D_TexEnvSrc(env, C3D_RGB, GPU_PREVIOUS, GPU_TEXTURE3, 0);
	C3D_TexEnvFunc(env, C3D_RGB, GPU_MODULATE);
	*/

	// Set texenv5 to apply the fade color
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
	C3D_FrameBufClear(&target->frameBuf, C3D_CLEAR_ALL, __builtin_bswap32(color), 0);
}

static inline void C2Di_RotatePoint(float* point, float rsin, float rcos)
{
	float x = point[0] * rcos - point[1] * rsin;
	float y = point[1] * rcos + point[0] * rsin;
	point[0] = x;
	point[1] = y;
}

void C2Di_CalcQuad(C2Di_Quad* quad, const C2D_DrawParams* params)
{
	quad->topLeft[0]  = -params->center.x;
	quad->topLeft[1]  = -params->center.y;
	quad->topRight[0] = -params->center.x+params->pos.w;
	quad->topRight[1] = -params->center.y;
	quad->botLeft[0]  = -params->center.x;
	quad->botLeft[1]  = -params->center.y+params->pos.h;
	quad->botRight[0] = -params->center.x+params->pos.w;
	quad->botRight[1] = -params->center.y+params->pos.h;

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

bool C2D_DrawImage(C2D_Image img, const C2D_DrawParams* params)
{
	C2Di_Context* ctx = C2Di_GetContext();
	if (!(ctx->flags & C2DiF_Active))
		return false;
	if (6 > (ctx->vtxBufSize - ctx->vtxBufPos))
		return false;

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

	// Draw triangles
	C2Di_AppendVtx(quad.topLeft[0],  quad.topLeft[1],  params->depth, tcTopLeft[0],  tcTopLeft[1],  0.0f, 0xFF<<24);
	C2Di_AppendVtx(quad.botLeft[0],  quad.botLeft[1],  params->depth, tcBotLeft[0],  tcBotLeft[1],  0.0f, 0xFF<<24);
	C2Di_AppendVtx(quad.botRight[0], quad.botRight[1], params->depth, tcBotRight[0], tcBotRight[1], 0.0f, 0xFF<<24);

	C2Di_AppendVtx(quad.topLeft[0],  quad.topLeft[1],  params->depth, tcTopLeft[0],  tcTopLeft[1],  0.0f, 0xFF<<24);
	C2Di_AppendVtx(quad.botRight[0], quad.botRight[1], params->depth, tcBotRight[0], tcBotRight[1], 0.0f, 0xFF<<24);
	C2Di_AppendVtx(quad.topRight[0], quad.topRight[1], params->depth, tcTopRight[0], tcTopRight[1], 0.0f, 0xFF<<24);

	return true;
}

bool C2D_DrawTriangle(float x0, float y0, u32 clr0, float x1, float y1, u32 clr1, float x2, float y2, u32 clr2, float depth)
{
	C2Di_Context* ctx = C2Di_GetContext();
	if (!(ctx->flags & C2DiF_Active))
		return false;
	if (3 > (ctx->vtxBufSize - ctx->vtxBufPos))
		return false;

	// Not necessary:
	//C2Di_SetSrc(C2DiF_Src_None);
	C2Di_Update();

	C2Di_AppendVtx(x0, y0, depth, 1.0f, 1.0f, 1.0f, clr0);
	C2Di_AppendVtx(x1, y1, depth, 1.0f, 1.0f, 1.0f, clr1);
	C2Di_AppendVtx(x2, y2, depth, 1.0f, 1.0f, 1.0f, clr2);
	return true;
}

void C2Di_AppendVtx(float x, float y, float z, float u, float v, float blend, u32 color)
{
	C2Di_Context* ctx = C2Di_GetContext();
	C2Di_Vertex* vtx = &ctx->vtxBuf[ctx->vtxBufPos++];
	vtx->pos[0]      = x;
	vtx->pos[1]      = y;
	vtx->pos[2]      = z;
	vtx->texcoord[0] = u;
	vtx->texcoord[1] = v;
	vtx->blend[0]    = 0.0f; // reserved for future expansion
	vtx->blend[1]    = blend;
	vtx->color       = color;
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
	if (flags & C2DiF_DirtySrc)
		C3D_TexEnvSrc(C3D_GetTexEnv(0), C3D_Both, (ctx->flags & C2DiF_Src_Tex) ? GPU_TEXTURE0 : GPU_CONSTANT, 0, 0);
	if (flags & C2DiF_DirtyFade)
		C3D_TexEnvColor(C3D_GetTexEnv(5), ctx->fadeClr);

	ctx->flags &= ~C2DiF_DirtyAny;
}
