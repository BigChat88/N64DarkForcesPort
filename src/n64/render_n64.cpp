// Render backend for the N64.
//
// TFE's classic renderer draws into an 8-bit paletted framebuffer (320x200).
// Instead of converting it on the CPU, the RDP draws it as a CI8 texture with
// the game palette loaded as a TLUT: palette lookup and conversion to 16-bit
// happen for free. The video output is 320x200 like the original game, and the
// VI stretches it to the full 4:3 screen with interpolation: stretching it with
// the RDP to 320x240 doubled every fifth line, which made scrolling text (the
// opening crawl) wobble.
#include <TFE_RenderBackend/renderBackend.h>
#include <TFE_RenderBackend/dynamicTexture.h>
#include <TFE_RenderBackend/textureGpu.h>
#include <TFE_System/system.h>
#include "debug_n64.h"
#include "overlay_n64.h"
#include "profile_n64.h"
#include <cstdio>

#include <libdragon.h>

namespace TFE_RenderBackend
{
	enum
	{
		SCREEN_WIDTH  = 320,
		SCREEN_HEIGHT = 200,
		DEBUG_FONT_ID = 1,
	};

	static WindowState s_windowState;
	static u32 s_virtualWidth;
	static u32 s_virtualHeight;
	static u32 s_virtualWidthUi;
	static u32 s_virtualWidth3d;
	static u8* s_curFrameBuffer = nullptr;

	static u32 s_palette[256];
	// The RDP reads the TLUT from RDRAM asynchronously; the palette is only
	// rewritten after the previous frame has been fully drawn (see swap()).
	static u16 s_tlut[256] __attribute__((aligned(16)));

	static u16 toRgba16(u32 color)
	{
		const u8 r = color & 0xff;
		const u8 g = (color >> 8) & 0xff;
		const u8 b = (color >> 16) & 0xff;
		return color_to_packed16(RGBA32(r, g, b, 0xff));
	}

	bool init(const WindowState& state)
	{
		s_windowState = state;
		const resolution_t resolution = { SCREEN_WIDTH, SCREEN_HEIGHT, INTERLACE_OFF };
		display_init(resolution, DEPTH_16_BPP, 2, GAMMA_NONE, FILTERS_RESAMPLE);
		rdpq_init();
		rdpq_text_register_font(DEBUG_FONT_ID, rdpq_font_load_builtin(FONT_BUILTIN_DEBUG_MONO));
		return true;
	}

	void destroy()
	{
		rdpq_close();
		display_close();
	}

	void swap(bool blitVirtualDisplay)
	{
		if (!s_curFrameBuffer) { return; }

		const u32 width = s_virtualWidth;
		const u32 height = s_virtualHeight;
		data_cache_hit_writeback(s_curFrameBuffer, width * height);
		data_cache_hit_writeback(s_tlut, sizeof(s_tlut));

		surface_t* disp = display_get();
		rdpq_attach(disp, NULL);

		rdpq_set_mode_standard();
		rdpq_mode_tlut(TLUT_RGBA16);
		rdpq_tex_upload_tlut(s_tlut, 0, 256);

		surface_t src = surface_make_linear(s_curFrameBuffer, FMT_CI8, width, height);
		rdpq_blitparms_t parms = {};
		parms.scale_x = (f32)SCREEN_WIDTH / (f32)width;
		parms.scale_y = (f32)SCREEN_HEIGHT / (f32)height;
		rdpq_tex_blit(&src, 0, 0, &parms);
		// The weapon and the HUD, see overlay_n64.cpp.
		Overlay_N64::draw(s_tlut);

		const char* overlay = Debug_N64::getOverlayText();
		if (overlay[0])
		{
			rdpq_text_print(nullptr, DEBUG_FONT_ID, 8, 16, overlay);
		}
#if N64_SHOW_FPS
		// Frames per second, averaged over one second (make N64_SHOW_FPS=1).
		{
			static u32 s_fpsStart = 0;
			static u32 s_fpsFrames = 0;
			static char s_fpsText[16] = "";
			const u32 now = (u32)get_ticks_ms();
			s_fpsFrames++;
			if (now - s_fpsStart >= 1000)
			{
				const u32 tenths = s_fpsFrames * 10000 / (now - s_fpsStart);
				snprintf(s_fpsText, sizeof(s_fpsText), "%lu.%lu fps", (unsigned long)(tenths / 10), (unsigned long)(tenths % 10));
				s_fpsStart = now;
				s_fpsFrames = 0;
			}
			rdpq_text_print(nullptr, DEBUG_FONT_ID, 256, SCREEN_HEIGHT - 8, s_fpsText);
		}
#endif

#if N64_PROFILE
		rdpq_text_print(nullptr, DEBUG_FONT_ID, 216, 16, Profile_N64::getText());
#endif

		rdpq_detach_show();
#if N64_PROFILE
		Profile_N64::frameShown();
#endif
		// TFE starts drawing the next frame into the same buffer right away.
		rspq_wait();

		s_curFrameBuffer = nullptr;
	}

	void updateVirtualDisplay(const void* buffer, size_t size)
	{
		s_curFrameBuffer = (u8*)buffer;
	}

	bool createVirtualDisplay(const VirtualDisplayInfo& vdispInfo)
	{
		s_virtualWidth = vdispInfo.width;
		s_virtualHeight = vdispInfo.height;
		s_virtualWidthUi = vdispInfo.widthUi;
		s_virtualWidth3d = vdispInfo.width3d;
		return false;
	}

	void setPalette(const u32* palette)
	{
		for (s32 i = 0; i < 256; i++)
		{
			s_palette[i] = palette[i];
			s_tlut[i] = toRgba16(palette[i]);
		}
	}

	const u32* getPalette()
	{
		return s_palette;
	}

	// Everything below is only meaningful for the desktop GPU renderer.
	bool getVsyncEnabled() { return true; }
	void enableVsync(bool enable) {}
	void setClearColor(const f32* color) {}
	void captureScreenToMemory(u32* mem) {}
	void queueScreenshot(const char* screenshotPath) {}
	void startGifRecording(const char* path) {}
	void stopGifRecording() {}
	void updateSettings() {}
	void resize(s32 width, s32 height) {}
	void enumerateDisplays() {}
	s32 getDisplayCount() { return 1; }
	s32 getDisplayIndex(s32 x, s32 y) { return 0; }

	bool getDisplayMonitorInfo(s32 displayIndex, MonitorInfo* monitorInfo)
	{
		monitorInfo->x = 0;
		monitorInfo->y = 0;
		monitorInfo->w = SCREEN_WIDTH;
		monitorInfo->h = SCREEN_HEIGHT;
		return true;
	}

	f32 getDisplayRefreshRate() { return 60.0f; }
	void getCurrentMonitorInfo(MonitorInfo* monitorInfo) {}
	void enableFullscreen(bool enable) {}
	void clearWindow() {}

	void getDisplayInfo(DisplayInfo* displayInfo)
	{
		displayInfo->width = s_windowState.width;
		displayInfo->height = s_windowState.height;
		displayInfo->refreshRate = 60.0f;
	}

	u32 getVirtualDisplayWidth2D() { return s_virtualWidthUi; }
	u32 getVirtualDisplayWidth3D() { return s_virtualWidth3d; }
	u32 getVirtualDisplayHeight() { return s_virtualHeight; }
	u32 getVirtualDisplayOffset2D() { return 0; }
	u32 getVirtualDisplayOffset3D() { return 0; }
	void* getVirtualDisplayGpuPtr() { return nullptr; }
	bool getWidescreen() { return false; }
	bool getFrameBufferAsync() { return false; }
	bool getGPUColorConvert() { return false; }
	void bindVirtualDisplay() {}
	void clearVirtualDisplay(f32* color, bool clearColor) {}
	void copyToVirtualDisplay(RenderTargetHandle src) {}
	void copyBackbufferToRenderTarget(RenderTargetHandle dst) {}
	const TextureGpu* getPaletteTexture() { return nullptr; }
	void setColorCorrection(bool enabled, const ColorCorrection* color/* = nullptr*/, bool bloomChanged/* = false*/) {}
	void drawVirtualDisplay() {}

	RenderTargetHandle createRenderTarget(u32 width, u32 height, bool hasDepthBuffer) { return RenderTargetHandle(nullptr); }
	void freeRenderTarget(RenderTargetHandle handle) {}
	void bindRenderTarget(RenderTargetHandle handle) {}
	void clearRenderTarget(RenderTargetHandle handle, const f32* clearColor, f32 clearDepth) {}
	void clearRenderTargetDepth(RenderTargetHandle handle, f32 clearDepth) {}
	void copyRenderTarget(RenderTargetHandle dst, RenderTargetHandle src) {}
	void unbindRenderTarget() {}
	const TextureGpu* getRenderTargetTexture(RenderTargetHandle rtHandle) { return nullptr; }
	void getRenderTargetDim(RenderTargetHandle rtHandle, u32* width, u32* height) { *width = 0; *height = 0; }

	TextureGpu* createTexture(u32 width, u32 height, u32 channels) { return nullptr; }
	TextureGpu* createTextureArray(u32 width, u32 height, u32 layers, u32 channels) { return nullptr; }
	TextureGpu* createTexture(u32 width, u32 height, const u32* data, MagFilter magFilter) { return nullptr; }
	void freeTexture(TextureGpu* texture) {}
	void getTextureDim(TextureGpu* texture, u32* width, u32* height) { *width = 0; *height = 0; }
	void* getGpuPtr(const TextureGpu* texture) { return nullptr; }
	void drawIndexedTriangles(u32 triCount, u32 indexStride, u32 indexStart) {}
	void drawLines(u32 lineCount) {}
	void bloomPostEnable(bool enable) {}
	void setupPostEffectChain(bool useDynamicTexture) {}
}
