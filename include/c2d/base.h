#pragma once
#include <citro3d.h>
#include <tex3ds.h>

#define C2D_DEFAULT_MAX_OBJECTS 4096

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

/** @brief Finishes rendering a 2D scene
 *  @param[in] endFrame Whether this is the last 2D scene to be drawn in this frame
 */
void C2D_SceneDone(bool endFrame);

/** @brief Configures the size of the 2D scene.
 *  @param[in] width The width of the scene, in pixels.
 *  @param[in] height The height of the scene, in pixels.
 *  @param[in] tilt Whether to accommodate for the 3DS's sideways screens.
 */
void C2D_SceneSize(u32 width, u32 height, bool tilt);

/** @brief Configures the size of the 2D scene to match that of the 3DS's top screen (400x240 tilted). */
static inline void C2D_SceneTopScreen(void)
{
	C2D_SceneSize(400, 240, true);
}

/** @brief Configures the size of the 2D scene to match that of the 3DS's bottom screen (320x240 tilted). */
static inline void C2D_SceneBottomScreen(void)
{
	C2D_SceneSize(320, 240, true);
}

/** @brief Helper function to create a render target for a screen
 *  @param[in] screen Screen (GFX_TOP or GFX_BOTTOM)
 *  @param[in] side Side (GFX_LEFT or GFX_RIGHT)
 *  @returns citro3d render target object
 */
C3D_RenderTarget* C2D_CreateScreenTarget(gfxScreen_t screen, gfx3dSide_t side);

/** @brief Helper function to clear a rendertarget using the specified color
 *  @param[in] target Rendertarget to clear
 *  @param[in] color Color to fill the target with
 */
void C2D_TargetClear(C3D_RenderTarget* target, u32 color);

/** @brief Helper function to draw a 2D scene on a render target
 *  @param[in] target Render target to draw the 2D scene to
 */
static inline void C2D_SceneBegin(C3D_RenderTarget* target)
{
	C3D_FrameDrawOn(target);
	C2D_SceneSize(target->frameBuf.width, target->frameBuf.height, target->linked);
}

/** @} */

/** @defgroup Drawing Drawing functions
 *  @{
 */

/** @brief Draws an image using the GPU
 *  @param[in] img Handle of the image to draw
 *  @param[in] params Parameters with which to draw the image
 *  @returns true on success, false on failure
 */
bool C2D_DrawImage(C2D_Image img, C2D_DrawParams params);

static inline bool C2D_DrawImageAt(C2D_Image img, float x, float y)
{
	C2D_DrawParams params = { { x, y, img.subtex->width, img.subtex->height }, { 0.0f, 0.0f }, 0.0f, 0.0f };
	return C2D_DrawImage(img, params);
}

static inline bool C2D_DrawImageAtCentered(C2D_Image img, float x, float y, float centerX, float centerY)
{
	C2D_DrawParams params = { { x, y, img.subtex->width, img.subtex->height }, { centerX, centerY }, 0.0f, 0.0f };
	return C2D_DrawImage(img, params);
}

static inline bool C2D_DrawImageAtRotated(C2D_Image img, float x, float y, float angle)
{
	C2D_DrawParams params = { { x, y, img.subtex->width, img.subtex->height }, { img.subtex->width/2.0f, img.subtex->height/2.0f }, 0.0f, angle };
	return C2D_DrawImage(img, params);
}

static inline bool C2D_DrawImageAtScaled(C2D_Image img, float x, float y, float scaleX, float scaleY)
{
	C2D_DrawParams params = { { x, y, scaleX*img.subtex->width, scaleY*img.subtex->height }, { 0.0f, 0.0f }, 0.0f, 0.0f };
	return C2D_DrawImage(img, params);
}

static inline bool C2D_DrawImageAtCenteredRotated(C2D_Image img, float x, float y, float angle, float centerX, float centerY)
{
	C2D_DrawParams params = { { x, y, img.subtex->width, img.subtex->height }, { centerX, centerY }, 0.0f, angle };
	return C2D_DrawImage(img, params);
}

static inline bool C2D_DrawImageAtCenteredRotatedScaled(C2D_Image img, float x, float y, float angle, float centerX, float centerY, float scaleX, float scaleY)
{
	C2D_DrawParams params = { { x, y, scaleX*img.subtex->width, scaleY*img.subtex->height }, { centerX, centerY }, 0.0f, angle };
	return C2D_DrawImage(img, params);
}

/** @} */
