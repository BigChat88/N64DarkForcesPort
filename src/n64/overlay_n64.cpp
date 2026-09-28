// Weapon and HUD drawn by the RDP over the 3D view (see overlay_n64.h).
//
// The CPU used to draw them one column at a time into the 320 byte wide
// framebuffer, which misses the data cache on almost every pixel (the same
// problem as the walls, see rwallFixed.cpp). The RDP reads the images straight
// from RDRAM instead:
// * TextureData images (weapon frames, font glyphs) are stored column major and
//   bottom to top. They are drawn with TEXTURE_RECTANGLE_FLIP, which steps T over
//   the X axis (T = column) and S over the Y axis, with a negative S step.
// * The lighting table (atten) and the transparent color are folded into a TLUT
//   per draw, built from the final palette of the frame: the result is the same
//   color the CPU would have written.
// * The RDP can crash loading from addresses misaligned within the lower half of
//   a 16 byte line, so the texture image starts at the previous 8-byte boundary
//   and the misalignment is added to S (the surface is wider than its pitch).
#include "overlay_n64.h"
#include <TFE_Jedi/Level/rtexture.h>
#include <TFE_Jedi/Renderer/screenDraw.h>
#include <TFE_Jedi/Renderer/virtualFramebuffer.h>
#include <cstring>

#include <libdragon.h>

using namespace TFE_Jedi;

namespace Overlay_N64
{
	enum
	{
		MAX_COMMANDS = 128,	// a full HUD message is ~80 glyphs
		MAX_TLUTS    = 6,	// distinct (atten, transparent color) pairs in a frame
		SCREEN_W     = 320,
		SCREEN_H     = 200,
	};

	enum CommandType : u8
	{
		CMD_TEXTURE,
		CMD_IMAGE,
	};

	struct Command
	{
		CommandType type;
		u8 tlut;
		bool forceTransparency;	// software replay of CMD_TEXTURE
		bool forceOpaque;
		TextureData* texture;	// CMD_TEXTURE
		const u8* image;
		const u8* atten;
		s32 width;
		s32 height;
		s32 x0, y0;
		DrawRect rect;			// inclusive clip rect
	};

	struct TlutKey
	{
		const u8* atten;
		s32 transColor;		// -1 = opaque
	};

	static Command s_commands[MAX_COMMANDS];
	static s32 s_commandCount = 0;
	static TlutKey s_tlutKeys[MAX_TLUTS];
	static s32 s_tlutCount = 0;
	// Read by the RDP: only rebuilt after the previous frame has been drawn (see swap()).
	static u16 s_tluts[MAX_TLUTS][256] __attribute__((aligned(16)));
	static bool s_capturing = false;
	static bool s_flattening = false;

	void begin()
	{
		s_capturing = true;
	}

	void end()
	{
		s_capturing = false;
	}

	static void clear()
	{
		s_commandCount = 0;
		s_tlutCount = 0;
	}

	static void drawImageSoftware(const Command& cmd, u8* framebuffer)
	{
		s32 x0 = cmd.x0, y0 = cmd.y0;
		s32 x1 = x0 + cmd.width - 1;
		s32 y1 = y0 + cmd.height - 1;
		const DrawRect& rect = cmd.rect;
		if (x0 > rect.x1 || x1 < rect.x0 || y0 > rect.y1 || y1 < rect.y0) { return; }

		const u8* image = cmd.image;
		if (y0 < rect.y0) { image += (rect.y0 - y0) * cmd.width; y0 = rect.y0; }
		if (y1 > rect.y1) { y1 = rect.y1; }
		if (x0 < rect.x0) { image += rect.x0 - x0; x0 = rect.x0; }
		if (x1 > rect.x1) { x1 = rect.x1; }

		const s32 count = x1 - x0 + 1;
		const bool trans = s_tlutKeys[cmd.tlut].transColor >= 0;
		u8* output = framebuffer + y0 * SCREEN_W + x0;
		for (s32 y = y0; y <= y1; y++, output += SCREEN_W, image += cmd.width)
		{
			if (trans)
			{
				for (s32 x = 0; x < count; x++)
				{
					if (image[x]) { output[x] = image[x]; }
				}
			}
			else
			{
				memcpy(output, image, count);
			}
		}
	}

	void flatten()
	{
		if (!s_commandCount) { return; }

		u8* framebuffer = vfb_getCpuBuffer();
		s_flattening = true;
		for (s32 i = 0; i < s_commandCount; i++)
		{
			const Command& cmd = s_commands[i];
			if (cmd.type == CMD_IMAGE)
			{
				drawImageSoftware(cmd, framebuffer);
			}
			else if (cmd.atten)
			{
				blitTextureToScreenLit(cmd.texture, (DrawRect*)&cmd.rect, cmd.x0, cmd.y0, cmd.atten, framebuffer, cmd.forceTransparency ? JTRUE : JFALSE);
			}
			else
			{
				blitTextureToScreen(cmd.texture, (DrawRect*)&cmd.rect, cmd.x0, cmd.y0, framebuffer, cmd.forceTransparency ? JTRUE : JFALSE, cmd.forceOpaque ? JTRUE : JFALSE);
			}
		}
		s_flattening = false;
		clear();
	}

	// Returns the command to fill, or null if the blit must be drawn in software.
	static Command* allocCommand(const u8* atten, s32 transColor)
	{
		if (!s_capturing || s_flattening) { return nullptr; }

		u32 width, height;
		vfb_getResolution(&width, &height);
		if (width != SCREEN_W || height != SCREEN_H) { return nullptr; }

		s32 tlut = 0;
		for (; tlut < s_tlutCount; tlut++)
		{
			if (s_tlutKeys[tlut].atten == atten && s_tlutKeys[tlut].transColor == transColor) { break; }
		}
		if (s_commandCount == MAX_COMMANDS || (tlut == s_tlutCount && s_tlutCount == MAX_TLUTS))
		{
			// Out of space: what is queued goes into the framebuffer first, so the
			// caller can draw on top of it in software.
			flatten();
			return nullptr;
		}
		if (tlut == s_tlutCount)
		{
			s_tlutKeys[tlut] = { atten, transColor };
			s_tlutCount++;
		}

		Command* cmd = &s_commands[s_commandCount++];
		cmd->tlut = (u8)tlut;
		cmd->atten = atten;
		return cmd;
	}

	bool addTexture(TextureData* texture, const DrawRect* rect, s32 x0, s32 y0, const u8* atten,
	                u8 transColor, bool forceTransparency, bool forceOpaque)
	{
		if (!texture->image || texture->compressed) { return false; }
		const bool trans = !forceOpaque && ((texture->flags & OPACITY_TRANS) || forceTransparency);
		Command* cmd = allocCommand(atten, trans ? transColor : -1);
		if (!cmd) { return false; }

		cmd->type = CMD_TEXTURE;
		cmd->forceTransparency = forceTransparency;
		cmd->forceOpaque = forceOpaque;
		cmd->texture = texture;
		cmd->image = texture->image;
		cmd->width = texture->width;
		cmd->height = texture->height;
		cmd->x0 = x0;
		cmd->y0 = y0;
		cmd->rect = *rect;
		return true;
	}

	bool addImage(const u8* image, s32 width, s32 height, const ScreenRect* rect, s32 x0, s32 y0, bool trans)
	{
		Command* cmd = allocCommand(nullptr, trans ? 0 : -1);
		if (!cmd) { return false; }

		cmd->type = CMD_IMAGE;
		cmd->forceTransparency = false;
		cmd->forceOpaque = false;
		cmd->texture = nullptr;
		cmd->image = image;
		cmd->width = width;
		cmd->height = height;
		cmd->x0 = x0;
		cmd->y0 = y0;
		cmd->rect = { rect->left, rect->top, rect->right, rect->bot };
		return true;
	}

	// Visible screen area of a command (inclusive); false if nothing is visible.
	static bool clipCommand(const Command& cmd, s32* sx0, s32* sy0, s32* sx1, s32* sy1)
	{
		*sx0 = max(max(cmd.x0, cmd.rect.x0), 0);
		*sy0 = max(max(cmd.y0, cmd.rect.y0), 0);
		*sx1 = min(min(cmd.x0 + cmd.width - 1, cmd.rect.x1), SCREEN_W - 1);
		*sy1 = min(min(cmd.y0 + cmd.height - 1, cmd.rect.y1), SCREEN_H - 1);
		return *sx0 <= *sx1 && *sy0 <= *sy1;
	}

	static tex_loader_t initLoader(const surface_t* surf, u32 off)
	{
		tex_loader_t tload = tex_loader_init(TILE0, surf);
		// LOAD_BLOCK addresses rows with the surface width, which is wider than the
		// real pitch when the image start was moved back to an 8-byte boundary.
		if (off) { tload.load_block = tload.load_tile; }
		return tload;
	}

	// Screen pixel (x, y) shows texel (s, t) = (off + height-1 - (y-y0), x - x0).
	static void drawTexture(const Command& cmd)
	{
		s32 sx0, sy0, sx1, sy1;
		if (!clipCommand(cmd, &sx0, &sy0, &sx1, &sy1)) { return; }

		const u32 off = (uintptr_t)cmd.image & 7;
		const u8* base = cmd.image - off;
		data_cache_hit_writeback(base, off + cmd.width * cmd.height);
		surface_t surf = surface_make((void*)base, FMT_CI8, off + cmd.height, cmd.width, cmd.height);

		const s32 s0 = off + cmd.height - 1 - (sy1 - cmd.y0);
		const s32 s1 = off + cmd.height - (sy0 - cmd.y0);	// exclusive
		const s32 t1 = sx1 - cmd.x0 + 1;					// exclusive

		tex_loader_t tload = initLoader(&surf, off);
		const s32 stripColumns = tex_loader_calc_max_height(&tload, s0, s1);
		for (s32 t0 = sx0 - cmd.x0; t0 < t1; t0 += stripColumns)
		{
			const s32 tn = min(t0 + stripColumns, t1);
			tex_loader_load(&tload, s0, t0, s1, tn);
			rdpq_texture_rectangle_flip_raw(TILE0, cmd.x0 + t0, sy0, cmd.x0 + tn, sy1 + 1, s1 - 1, t0, -1, 1);
		}
	}

	static void drawImage(const Command& cmd)
	{
		s32 sx0, sy0, sx1, sy1;
		if (!clipCommand(cmd, &sx0, &sy0, &sx1, &sy1)) { return; }

		const u32 off = (uintptr_t)cmd.image & 7;
		const u8* base = cmd.image - off;
		data_cache_hit_writeback(base, off + cmd.width * cmd.height);
		surface_t surf = surface_make((void*)base, FMT_CI8, off + cmd.width, cmd.height, cmd.width);

		const s32 s0 = off + sx0 - cmd.x0;
		const s32 s1 = off + sx1 - cmd.x0 + 1;	// exclusive
		const s32 t1 = sy1 - cmd.y0 + 1;		// exclusive

		tex_loader_t tload = initLoader(&surf, off);
		const s32 stripRows = tex_loader_calc_max_height(&tload, s0, s1);
		for (s32 t0 = sy0 - cmd.y0; t0 < t1; t0 += stripRows)
		{
			const s32 tn = min(t0 + stripRows, t1);
			tex_loader_load(&tload, s0, t0, s1, tn);
			rdpq_texture_rectangle(TILE0, sx0, cmd.y0 + t0, sx1 + 1, cmd.y0 + tn, s0, t0);
		}
	}

	void draw(const u16* tlut)
	{
		if (!s_commandCount) { return; }

		// Alpha is the lowest bit of an RGBA16 color: clear it for the transparent color.
		for (s32 i = 0; i < s_tlutCount; i++)
		{
			const TlutKey& key = s_tlutKeys[i];
			u16* out = s_tluts[i];
			for (s32 c = 0; c < 256; c++)
			{
				out[c] = tlut[key.atten ? key.atten[c] : c] | 1;
			}
			if (key.transColor >= 0) { out[key.transColor] &= ~1; }
		}
		data_cache_hit_writeback(s_tluts, sizeof(s_tluts[0]) * s_tlutCount);

		rdpq_set_mode_standard();
		rdpq_mode_tlut(TLUT_RGBA16);
		rdpq_mode_alphacompare(128);

		s32 curTlut = -1;
		for (s32 i = 0; i < s_commandCount; i++)
		{
			const Command& cmd = s_commands[i];
			if (cmd.tlut != curTlut)
			{
				curTlut = cmd.tlut;
				rdpq_tex_upload_tlut(s_tluts[curTlut], 0, 256);
			}
			if (cmd.type == CMD_TEXTURE) { drawTexture(cmd); }
			else { drawImage(cmd); }
		}
		clear();
	}
}
