#pragma once
//////////////////////////////////////////////////////////////////////
// Dark Forces
// Landru Palette
//////////////////////////////////////////////////////////////////////
#include <TFE_System/types.h>
#include "lrect.h"

namespace TFE_DarkForces
{
	void ldraw_init(s16 w, s16 h);
	void ldraw_destroy();
	u8*  ldraw_getBitmap();

	void deltaImage(s16_ua* data, s16 x, s16 y);
	void deltaClip(s16_ua* data, s16 x, s16 y);
	void deltaFlip(s16_ua* data, s16 x, s16 y, s16 w);
	void deltaFlipClip(s16_ua* data, s16 x, s16 y, s16 w);
	JBool drawClippedColorRect(LRect* rect, u8 color);

	void drawDeltaIntoBitmap(s16_ua* data, s16 x, s16 y, u8* framebuffer, s32 stride);
}  // namespace TFE_DarkForces