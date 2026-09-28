#pragma once
// Weapon and HUD drawn by the RDP over the 3D view.
//
// While capturing (between begin() and end()), the screen blits used by the
// weapon, the HUD and the HUD messages are queued instead of being drawn by the
// CPU. The render backend draws the queue with the RDP right after it blits the
// 8-bit framebuffer (see render_n64.cpp), with each lighting table folded into
// its own TLUT. Anything the CPU draws on top of them later in the frame (the
// automap, the menus) must call flatten() first, which draws the queue into the
// framebuffer in software so the original draw order is kept.
#include <TFE_System/types.h>

struct TextureData;
struct ScreenRect;
namespace TFE_Jedi { struct DrawRect; }

namespace Overlay_N64
{
	void begin();
	void end();
	// Draws the queued blits into the framebuffer with the CPU and empties the queue.
	void flatten();

	// Queue a TextureData blit (column major, bottom to top). Returns false when it
	// must be drawn in software instead: not capturing, or the queue is full.
	bool addTexture(TextureData* texture, const TFE_Jedi::DrawRect* rect, s32 x0, s32 y0, const u8* atten,
	                u8 transColor, bool forceTransparency, bool forceOpaque);
	// Queue a row major 8-bit image (HUD elements), color 0 is transparent if trans is set.
	bool addImage(const u8* image, s32 width, s32 height, const ScreenRect* rect, s32 x0, s32 y0, bool trans);

	// Render backend: draws the queue with the RDP (the framebuffer blit must be the
	// current render target) and empties it.
	void draw(const u16* tlut);
}
