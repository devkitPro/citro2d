#pragma once
#include <citro3d.h>
#include <tex3ds.h>

#define C2D_DEFAULT_MAX_OBJECTS 4096

#ifdef __cplusplus
#define C2D_CONSTEXPR constexpr
#else
#define C2D_CONSTEXPR static inline
#endif

typedef struct
{
	struct
	{
		float x, y, w, h;
	} pos;

	struct
	{
		float x, y;
	} center;

	float depth;
	float angle;
} C2D_DrawParams;

typedef struct ALIGN(8)
{
	C3D_Tex* tex;
	const Tex3DS_SubTexture* subtex;
} C2D_Image;

/** @defgroup Helper Helper functions
 *  @{
 */

/** @brief Clamps a value between bounds
 *  @param[in] x The value to clamp
 *  @param[in] min The lower bound
 *  @param[in] max The upper bound
 *  @returns The clamped value
 */
C2D_CONSTEXPR float C2D_Clamp(float x, float min, float max)
{
	return x <= min ? min : x >= max ? max : x;
}

/** @brief Converts a float to u8
 *  @param[in] x Input value (0.0~1.0)
 *  @returns Output value (0~255)
 */
C2D_CONSTEXPR u8 C2D_FloatToU8(float x)
{
	return (u8)(255.0f*C2D_Clamp(x, 0.0f, 1.0f)+0.5f);
}

/** @brief Builds a 32-bit RGBA color value
 *  @param[in] r Red component (0~255)
 *  @param[in] g Green component (0~255)
 *  @param[in] b Blue component (0~255)
 *  @param[in] a Alpha component (0~255)
 *  @returns The 32-bit RGBA color value
 */
C2D_CONSTEXPR u32 C2D_Color32(u8 r, u8 g, u8 b, u8 a)
{
	return r | (g << (u32)8) | (b << (u32)16) | (a << (u32)24);
}

/** @brief Builds a 32-bit RGBA color value from float values
 *  @param[in] r Red component (0.0~1.0)
 *  @param[in] g Green component (0.0~1.0)
 *  @param[in] b Blue component (0.0~1.0)
 *  @param[in] a Alpha component (0.0~1.0)
 *  @returns The 32-bit RGBA color value
 */
C2D_CONSTEXPR u32 C2D_Color32f(float r, float g, float b, float a)
{
	return C2D_Color32(C2D_FloatToU8(r),C2D_FloatToU8(g),C2D_FloatToU8(b),C2D_FloatToU8(a));
}

/** @} */

/** @defgroup Base Basic functions
 *  @{
 */

/** @brief Initialize citro2d
 *  @param[in] maxObjects Maximum number of 2D objects that can be drawn per frame.
 *  @remarks Pass C2D_DEFAULT_MAX_OBJECTS as a starting point.
 *  @returns true on success, false on failure
 */
bool C2D_Init(size_t maxObjects);

/** @brief Deinitialize citro2d */
void C2D_Fini(void);

/** @brief Prepares the GPU for rendering 2D content
 *  @remarks This needs to be done only once in the program if citro2d is the sole user of the GPU.
 */
void C2D_Prepare(void);

/** @brief Ensures all 2D objects so far have been drawn */
void C2D_Flush(void);

/** @brief Configures the size of the 2D scene.
 *  @param[in] width The width of the scene, in pixels.
 *  @param[in] height The height of the scene, in pixels.
 *  @param[in] tilt Whether the scene is tilted like the 3DS's sideways screens.
 */
void C2D_SceneSize(u32 width, u32 height, bool tilt);

/** @brief Configures the size of the 2D scene to match that of the specified render target.
 *  @param[in] target Render target
 */
static inline void C2D_SceneTarget(C3D_RenderTarget* target)
{
	C2D_SceneSize(target->frameBuf.width, target->frameBuf.height, target->linked);
}

/** @brief Helper function to create a render target for a screen
 *  @param[in] screen Screen (GFX_TOP or GFX_BOTTOM)
 *  @param[in] side Side (GFX_LEFT or GFX_RIGHT)
 *  @returns citro3d render target object
 */
C3D_RenderTarget* C2D_CreateScreenTarget(gfxScreen_t screen, gfx3dSide_t side);

/** @brief Helper function to clear a rendertarget using the specified color
 *  @param[in] target Render target to clear
 *  @param[in] color 32-bit RGBA color value to fill the target with
 */
void C2D_TargetClear(C3D_RenderTarget* target, u32 color);

/** @brief Helper function to begin drawing a 2D scene on a render target
 *  @param[in] target Render target to draw the 2D scene to
 */
static inline void C2D_SceneBegin(C3D_RenderTarget* target)
{
	C2D_Flush();
	C3D_FrameDrawOn(target);
	C2D_SceneTarget(target);
}

/** @} */

/** @defgroup Env Drawing environment functions
 *  @{
 */

/** @brief Configures the fading color
 *  @param[in] color 32-bit RGBA color value to be used as the fading color (0 by default)
 *  @remark The alpha component of the color is used as the strength of the fading color.
 *          If alpha is zero, the fading color has no effect. If it is the highest value,
 *          the rendered pixels will all have the fading color. Everything inbetween is
 *          rendered as a blend of the original pixel color and the fading color.
 */
void C2D_Fade(u32 color);

/** @defgroup Drawing Drawing functions
 *  @{
 */

/** @brief Draws an image using the GPU
 *  @param[in] img Handle of the image to draw
 *  @param[in] params Parameters with which to draw the image
 *  @returns true on success, false on failure
 */
bool C2D_DrawImage(C2D_Image img, const C2D_DrawParams* params);

static inline bool C2D_DrawImageAt(C2D_Image img, float x, float y)
{
	C2D_DrawParams params = { { x, y, img.subtex->width, img.subtex->height }, { 0.0f, 0.0f }, 0.0f, 0.0f };
	return C2D_DrawImage(img, &params);
}

static inline bool C2D_DrawImageAtCentered(C2D_Image img, float x, float y, float centerX, float centerY)
{
	C2D_DrawParams params = { { x, y, img.subtex->width, img.subtex->height }, { centerX, centerY }, 0.0f, 0.0f };
	return C2D_DrawImage(img, &params);
}

static inline bool C2D_DrawImageAtRotated(C2D_Image img, float x, float y, float angle)
{
	C2D_DrawParams params = { { x, y, img.subtex->width, img.subtex->height }, { img.subtex->width/2.0f, img.subtex->height/2.0f }, 0.0f, angle };
	return C2D_DrawImage(img, &params);
}

static inline bool C2D_DrawImageAtScaled(C2D_Image img, float x, float y, float scaleX, float scaleY)
{
	C2D_DrawParams params = { { x, y, scaleX*img.subtex->width, scaleY*img.subtex->height }, { 0.0f, 0.0f }, 0.0f, 0.0f };
	return C2D_DrawImage(img, &params);
}

static inline bool C2D_DrawImageAtCenteredRotated(C2D_Image img, float x, float y, float angle, float centerX, float centerY)
{
	C2D_DrawParams params = { { x, y, img.subtex->width, img.subtex->height }, { centerX, centerY }, 0.0f, angle };
	return C2D_DrawImage(img, &params);
}

static inline bool C2D_DrawImageAtCenteredRotatedScaled(C2D_Image img, float x, float y, float angle, float centerX, float centerY, float scaleX, float scaleY)
{
	C2D_DrawParams params = { { x, y, scaleX*img.subtex->width, scaleY*img.subtex->height }, { centerX, centerY }, 0.0f, angle };
	return C2D_DrawImage(img, &params);
}

bool C2D_DrawTriangle(float x0, float y0, u32 clr0, float x1, float y1, u32 clr1, float x2, float y2, u32 clr2, float depth);

/** @} */
