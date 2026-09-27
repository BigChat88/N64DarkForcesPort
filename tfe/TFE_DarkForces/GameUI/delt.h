#pragma once
//////////////////////////////////////////////////////////////////////
// Note this is still the original TFE code, reverse-engineered game
// UI code will not be available until future releases.
//////////////////////////////////////////////////////////////////////
#include <TFE_System/types.h>
#include <TFE_Jedi/Level/rtexture.h>

struct ScreenRect;

namespace TFE_DarkForces
{
	struct DeltFrame
	{
		TextureData texture;
		s16 offsetX;
		s16 offsetY;
#ifdef __N64__
		// Compressed frames keep the DELT lines instead of texture.image (null).
		const u8* delt;
		u32 deltSize;
#endif
	};

	void delt_resetState();

	u8* getTempBuffer(size_t size);
	JBool loadPaletteFromPltt(const char* name, u8* palette);
	u32 getFramesFromAnim(const char* name, DeltFrame** outFrames);
#ifdef __N64__
	// __N64__: same as getFramesFromAnim() but the frames stay compressed, they are
	// drawn straight from the DELT data by blitDeltaFrame() (roughly 8x less memory).
	u32 getCompressedFramesFromAnim(const char* name, DeltFrame** outFrames);
	// Paints over the label of a button baked into a menu image, by copying the plain rows
	// srcY and srcY + 1 of the button face (alternating, to keep the dithering).
	void eraseButtonLabel(u8* framebuffer, s32 x0, s32 y0, s32 x1, s32 y1, s32 srcY);
#endif
	JBool getFrameFromDelt(const char* name, DeltFrame* outFrame);

	void loadDeltIntoFrame(DeltFrame* frame, const u8* buffer, u32 size);

	void blitDeltaFrame(DeltFrame* frame, s32 x, s32 y, u8* framebuffer);
	void blitDeltaFrameScaled(DeltFrame* frame, s32 x0, s32 y0, fixed16_16 xScale, fixed16_16 yScale, u8* framebuffer);

	void getDeltaFrameRect(DeltFrame* frame, ScreenRect* rect);
}
