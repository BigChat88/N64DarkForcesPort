#include <TFE_RenderBackend/renderBackend.h>
#include <TFE_RenderBackend/dynamicTexture.h>
#include <TFE_RenderBackend/textureGpu.h>

#include <TFE_System/system.h>
#include <TFE_Input/input.h>
/*
#include <TFE_Settings/settings.h>
#include <TFE_Asset/imageAsset.h>	// For image saving, this should be refactored...
#include <TFE_System/profiler.h>
#include <TFE_PostProcess/blit.h>
#include <TFE_PostProcess/postprocess.h>
*/
#include <TFE_FileSystem/filestream.h>

#define MonitorInfo MonitorInfo_Amiga
#define DisplayInfo DisplayInfo_Amiga

//#include <devices/input.h>
#include <libraries/lowlevel.h>
#include <intuition/intuition.h>
//#include <intuition/intuitionbase.h>
#include <graphics/videocontrol.h>
//#include <workbench/startup.h>
#include <clib/alib_protos.h>
#include <clib/debug_protos.h>
#include <proto/intuition.h>
#include <exec/execbase.h>
#include <proto/exec.h>
//#include <proto/keymap.h>
//#include <proto/lowlevel.h>
#include <proto/dos.h>
//#include <proto/timer.h>
#include <proto/graphics.h>
//#include <proto/icon.h>
//#include <proto/input.h>

#include <cybergraphx/cybergraphics.h>
#include <proto/cybergraphics.h>

//#include <newmouse.h>

#undef MonitorInfo
#undef DisplayInfo

#include <SDI_compiler.h>
//#include <SDI_interrupt.h>

#include <stdio.h>
#include <math.h>

#define buildprintf(...) TFE_System::logWrite(LOG_MSG, "RenderBackend", __VA_ARGS__)
#define buildputs(x) TFE_System::logWrite(LOG_MSG, "RenderBackend", (x))

//#define BENCHMARK

extern "C" {
	void ASM c2p1x1_8_c5_bm_040(REG(d0, WORD chunkyx), REG(d1, WORD chunkyy), REG(d2, WORD offsx), REG(d3, WORD offsy), REG(a0, APTR chunkyscreen), REG(a1, struct BitMap *bitmap));
	#define c2p_write_bm c2p1x1_8_c5_bm_040

	//struct Library *CyberGfxBase = NULL;
	struct Device *InputBase;
}

#ifdef BENCHMARK
int t1, t2, t3, t4, t5, t6, t7, t8, t9;
#endif

namespace TFE_RenderBackend
{
	static struct Window *window = NULL;
	static struct Screen *screen = NULL;
	static unsigned char ppal[256 * 4];
	static ULONG spal[1 + (256 * 3) + 1];
	static int updatePalette = FALSE;
	static int use_c2p = FALSE;
	//static int use_wcp = FALSE;
	static int currentBitMap;
	static struct ScreenBuffer *sbuf[2];
	static struct RastPort temprp;
	//static struct BitMap *tempbm;
	static struct MsgPort *dispport;
	static struct MsgPort *safeport;
	static int safetochange = FALSE;
	static int safetowrite = FALSE;
	static ULONG fsMonitorID = INVALID_ID;
	static ULONG fsModeID = INVALID_ID;
	struct Library *CyberGfxBase = NULL;
	static char wndPubScreen[32] = {"Workbench"};
	static ULONG rtg320x240 = FALSE;

	static UWORD *pointermem;

	static WindowState m_windowState;
	static u32 s_virtualWidth, s_virtualHeight;
	static u32 s_virtualWidthUi;
	static u32 s_virtualWidth3d;
	static u8* s_curFrameBuffer = nullptr;
	static bool s_colorCorrection = false;
	static u8 s_gammaTable[256];

	static const char *getmonitorname(ULONG modeID)
	{
		ULONG monitorid = (modeID & MONITOR_ID_MASK);

		switch (monitorid) {
			case PAL_MONITOR_ID:
				return "PAL";
			case NTSC_MONITOR_ID:
				return "NTSC";
			case DBLPAL_MONITOR_ID:
				return "DBLPAL";
			case DBLNTSC_MONITOR_ID:
				return "DBLNTSC";
			case EURO36_MONITOR_ID:
				return "EURO36";
			case EURO72_MONITOR_ID:
				return "EURO72";
			case SUPER72_MONITOR_ID:
				return "SUPER72";
			case VGA_MONITOR_ID:
				return "MULTISCAN";
		}

		if (CyberGfxBase && IsCyberModeID(modeID))
			return "RTG";

		return "UNKNOWN";
	}

	#include <stdarg.h>
	static void BE_ST_DebugText(struct RastPort *rp, int x, int y, const char *fmt, ...)
	{
		char buffer[256];
		va_list ap;

		va_start(ap, fmt); 
		vsnprintf(buffer, sizeof(buffer), fmt, ap);
		va_end(ap);
		//SetAPen(rp, 247);
		Move(rp, x + window->BorderLeft, y + rp->Font->tf_Baseline + window->BorderTop);
		Text(rp, buffer, strlen(buffer));
	}

	static void showframe(void)
	{
		if (screen) {
			currentBitMap ^= 1;
			if (use_c2p) {
				c2p_write_bm(s_virtualWidth, s_virtualHeight, 0, 0, s_curFrameBuffer, sbuf[currentBitMap]->sb_BitMap);
			} else if (CyberGfxBase) {
				//WritePixelArray(s_curFrameBuffer, 0, 0, s_virtualWidth, window->RPort, 0, 0, s_virtualWidth, s_virtualHeight, RECTFMT_LUT8);
				temprp.BitMap = sbuf[currentBitMap]->sb_BitMap;
				WritePixelArray(s_curFrameBuffer, 0, 0, s_virtualWidth, &temprp, 0, 0, s_virtualWidth, s_virtualHeight, RECTFMT_LUT8);
			}
#ifdef BENCHMARK
			{
				struct RastPort *rp = &temprp;
				temprp.BitMap = sbuf[currentBitMap]->sb_BitMap;
				BE_ST_DebugText(rp, 0,16,"t1 %2d t2 %2d t3 %2d t4 %2d t5 %2d t6 %2d", t1,t2,t3,t4,t5,t6);
			}
#endif
			if (dispport) {
				if (!safetochange) {
					while (!GetMsg(dispport)) WaitPort(dispport);
					safetochange = TRUE;
				}
			} /*else {
				WaitTOF();
			}*/
			if (ChangeScreenBuffer(screen, sbuf[currentBitMap])) {
				safetochange = FALSE;
			}
			/*
			{
				FileStream file;
				if (file.open("chunky.bin", Stream::MODE_WRITE))
				{
					file.writeBuffer(s_curFrameBuffer, s_virtualWidth*s_virtualHeight);
					file.close();
				}
			}
			*/
			if (updatePalette) {
				LoadRGB32(&screen->ViewPort, spal);
				/*
				if (!TFE_Input::relativeModeEnabled())
				{
					// update the colormap too if we are in the menu
					ULONG *sp = &spal[1];
					for (int i = 0; i < 256; i++)
					{
						//SetRGB32(&screen->ViewPort, i, *sp++, *sp++, *sp++);
						SetRGB32CM(screen->ViewPort.ColorMap, i, *sp++, *sp++, *sp++);
					}
				}
				*/
				updatePalette = FALSE;
			}
		} else {
			WriteLUTPixelArray(s_curFrameBuffer, 0, 0, s_virtualWidth, window->RPort, ppal, window->BorderLeft, window->BorderTop, s_virtualWidth, s_virtualHeight, CTABFMT_XRGB8);
		}
	}

	static void shutdownvideo(void)
	{
		//RemoveInputHandler();

		if (window) {
			CloseWindow(window);
			window = NULL;
		}
		if (sbuf[0]) {
			FreeScreenBuffer(screen, sbuf[0]);
			sbuf[0] = NULL;
		}
		if (sbuf[1]) {
			FreeScreenBuffer(screen, sbuf[1]);
			sbuf[1] = NULL;
		}
		if (dispport) {
			//while (GetMsg(dispport));
			DeleteMsgPort(dispport);
			dispport = NULL;
		}
		if (screen) {
			CloseScreen(screen);
			screen = NULL;
		}
		use_c2p = 0;
	}

	static int setvideomode(int x, int y, int c, int fs)
	{
		ULONG flags, idcmp;

		shutdownvideo();

		//buildprintf("Setting video mode %dx%d (%d-bpp %s)\n", x,y,c,(fs & 1) ? "fullscreen" : "windowed");

		if (fs) {
			ULONG modeID = INVALID_ID;

			if (fsModeID != (ULONG)INVALID_ID) {
				modeID = fsModeID;
				buildprintf("Using forced mode id: %08x\n", (int)fsModeID);
			} else if (fsMonitorID != (ULONG)INVALID_ID) {
				buildprintf("Using forced monitor: %s\n", getmonitorname(fsMonitorID));
			} else if (CyberGfxBase /*&& fsMonitorID == (ULONG)INVALID_ID*/ /*&& !(x / y >= 2)*/) {
				if (!rtg320x240) {
					modeID = BestCModeIDTags(
						CYBRBIDTG_Depth, 8,
						CYBRBIDTG_NominalWidth, x,
						CYBRBIDTG_NominalHeight, y,
						TAG_DONE);
				}
				if (modeID == (ULONG)INVALID_ID && x == 320 && y == 200) {
					// some cards like the Voodoo 3 lack a 320x200 mode
					modeID = BestCModeIDTags(
						CYBRBIDTG_Depth, 8,
						CYBRBIDTG_NominalWidth, 320,
						CYBRBIDTG_NominalHeight, 240,
						TAG_DONE);
				}
			}

			if (modeID == (ULONG)INVALID_ID) {
				modeID = BestModeID(
					BIDTAG_NominalWidth, x,
					BIDTAG_NominalHeight, y,
					BIDTAG_Depth, 8,
					//BIDTAG_DIPFMustNotHave, SPECIAL_FLAGS|DIPF_IS_LACE,
					(fsMonitorID == (ULONG)INVALID_ID) ? TAG_IGNORE : BIDTAG_MonitorID, fsMonitorID,
					TAG_DONE);
			}

			struct TagItem vctl[] =
			{
				//{VTAG_SPEVEN_BASE_SET, 10*16},
				//{VTAG_SPEVEN_BASE_SET, 0},
				{VTAG_BORDERBLANK_SET, TRUE},
				{VC_IntermediateCLUpdate, FALSE},
				{VTAG_END_CM, 0}
			};

			UWORD dummypens[] = {(UWORD)~0};

			screen = OpenScreenTags(0,
				modeID != (ULONG)INVALID_ID ? SA_DisplayID : TAG_IGNORE, modeID,
				SA_Width, x,
				SA_Height, y,
				SA_Depth, 8,
				SA_ShowTitle, FALSE,
				SA_Quiet, TRUE,
				SA_Draggable, FALSE,
				SA_Type, CUSTOMSCREEN,
				SA_VideoControl, (IPTR)vctl,
				SA_Pens, (IPTR)dummypens,
				//SA_Exclusive, TRUE,
				TAG_DONE);

			memset(spal, 0, sizeof(spal));
			spal[0] = 256 << 16;

			//SetRast(&screen->RastPort, blackcol);
			//SetRast(&screen->RastPort, 0);

			modeID = GetVPModeID(&screen->ViewPort);
			struct NameInfo nameinfo;
			if (GetDisplayInfoData(NULL, (UBYTE *)&nameinfo, sizeof(nameinfo), DTAG_NAME, modeID))
				buildprintf("Opened screen: 0x%08x %s\n", (int)modeID, nameinfo.Name);
			else
				buildprintf("Opened screen: 0x%08x %s\n", (int)modeID, getmonitorname(modeID));

			currentBitMap = 0;
			use_c2p = FALSE;
			InitRastPort(&temprp);

			if ((sbuf[0] = AllocScreenBuffer(screen, 0, SB_SCREEN_BITMAP)) && (sbuf[1] = AllocScreenBuffer(screen, 0, SB_COPY_BITMAP))) {
				if ((GetBitMapAttr(screen->RastPort.BitMap, BMA_FLAGS) & BMF_STANDARD) != 0 && (x % 32) == 0) {
					use_c2p = TRUE;
				}
				safetochange = TRUE;
				if ((m_windowState.flags & WINFLAG_VSYNC) != 0)
				{
					dispport = CreateMsgPort();
					sbuf[0]->sb_DBufInfo->dbi_DispMessage.mn_ReplyPort = dispport;
					sbuf[1]->sb_DBufInfo->dbi_DispMessage.mn_ReplyPort = dispport;
				}
			} else {
				// BIG FAIL
				return -1;
			}
		}

		flags = WFLG_ACTIVATE | WFLG_RMBTRAP;
		idcmp = IDCMP_CLOSEWINDOW /*| IDCMP_ACTIVEWINDOW | IDCMP_INACTIVEWINDOW*/ | IDCMP_RAWKEY | IDCMP_MOUSEBUTTONS;
		if (screen) {
			flags |= WFLG_BACKDROP | WFLG_BORDERLESS;
		} else {
			flags |= WFLG_DRAGBAR | WFLG_DEPTHGADGET | WFLG_CLOSEGADGET;
		}

		if (!screen && strcasecmp(wndPubScreen, "Workbench")) {
			buildprintf("Using forced public screen: %s\n", wndPubScreen);
		}

		window = OpenWindowTags(0,
			WA_InnerWidth, x,
			WA_InnerHeight, y,
			screen ? TAG_IGNORE : WA_Title, (IPTR)m_windowState.name,
			WA_Flags, flags,
			screen ? WA_CustomScreen : TAG_IGNORE, (IPTR)screen,
			!screen ? WA_PubScreenName : TAG_IGNORE, (IPTR)wndPubScreen,
			WA_IDCMP, idcmp,
			TAG_DONE);

		if (window == NULL /*|| AddInputHandler()*/) {
			shutdownvideo();
			buildputs("Could not open the window");
			return -1;
		}

		char loadText[] = "Loading...";
		//SetAPen(window->RPort, 2);
		Move(window->RPort, 8 + window->BorderLeft, 8 + window->RPort->Font->tf_Baseline + window->BorderTop);
		Text(window->RPort, loadText, sizeof(loadText)-1);

		if (!screen) {
			buildprintf("Opened window on public screen: %s\n", window->WScreen->Title);
		}
		
		//AddInputHandler();
		if (pointermem && window->Pointer != pointermem) {
			SetPointer(window, pointermem, 1, 1, 0, 0);
		}

		return 0;
	}


	bool init(const WindowState& state)
	{
		CyberGfxBase = OpenLibrary((STRPTR)"cybergraphics.library", 41);
		pointermem = (UWORD *)AllocVec(2 * 6, MEMF_CHIP | MEMF_CLEAR);
		/*
		buildprintf("%s\n name '%s'\n width %u\n height %u\n baseWindowWidth %u\n baseWindowHeight %u\n monitorWidth %u\n monitorHeight %u\n flags %u\n refreshRate %f\n",
			__FUNCTION__,
			state.name,
			state.width,
			state.height,
			state.baseWindowWidth,
			state.baseWindowHeight,
			state.monitorWidth,
			state.monitorHeight,
			state.flags,
			state.refreshRate
		);
		*/
		m_windowState = state;
		fsMonitorID = m_windowState.baseWindowWidth;
		fsModeID = m_windowState.baseWindowHeight;
		rtg320x240 = m_windowState.monitorHeight;
		int error = setvideomode(state.width, state.height, 8, !!(state.flags & WINFLAG_FULLSCREEN));
		return error == 0;
	}

	void destroy()
	{
		shutdownvideo();
		if (CyberGfxBase) {
			CloseLibrary(CyberGfxBase);
			CyberGfxBase = NULL;
		}
		if (pointermem) {
			FreeVec(pointermem);
			pointermem = NULL;
		}
	}

	bool getVsyncEnabled()
	{
		return false;
	}

	void enableVsync(bool enable)
	{
	}

	void setClearColor(const f32* color)
	{
	}

	void swap(bool blitVirtualDisplay)
	{
		//TFE_RenderState::clear();
		if (s_curFrameBuffer)
		{
			showframe();
			s_curFrameBuffer = nullptr;
		}
	}

	void captureScreenToMemory(u32* mem)
	{
	}

	void queueScreenshot(const char* screenshotPath)
	{
	}

	void startGifRecording(const char* path)
	{
	}

	void stopGifRecording()
	{
	}

	void updateSettings()
	{
		// TODO save window position
	}

	void resize(s32 width, s32 height)
	{
	}

	void enumerateDisplays()
	{
		
	}

	s32 getDisplayCount()
	{
		return 1;
	}

	s32 getDisplayIndex(s32 x, s32 y)
	{
		return -1;
	}

	bool getDisplayMonitorInfo(s32 displayIndex, MonitorInfo* monitorInfo)
	{
		monitorInfo->x = 0;
		monitorInfo->y = 0;
		monitorInfo->w = 320;
		monitorInfo->h = 200;
		//return false;
		return true;
	}

	f32 getDisplayRefreshRate()
	{
		return 0.0f;
	}

	void getCurrentMonitorInfo(MonitorInfo* monitorInfo)
	{
	}

	void enableFullscreen(bool enable)
	{
	}

	void clearWindow()
	{
	}

	void getDisplayInfo(DisplayInfo* displayInfo)
	{
		displayInfo->width = m_windowState.width;
		displayInfo->height = m_windowState.height;
		displayInfo->refreshRate = (m_windowState.flags & WINFLAG_VSYNC) != 0 ? m_windowState.refreshRate : 0.0f;
	}

	// New version of the function.
	bool createVirtualDisplay(const VirtualDisplayInfo& vdispInfo)
	{
		/*
		buildprintf("%s\n width %u\n height %u widthUi %u\n width3d %u\n",
			__FUNCTION__,
			vdispInfo.width,
			vdispInfo.height,
			vdispInfo.widthUi,
			vdispInfo.width3d
		);
		*/
		s_virtualWidth = vdispInfo.width;
		s_virtualHeight = vdispInfo.height;
		s_virtualWidthUi = vdispInfo.widthUi;
		s_virtualWidth3d = vdispInfo.width3d;
		return false;
	}

	u32 getVirtualDisplayWidth2D()
	{
		return 0;
	}

	u32 getVirtualDisplayWidth3D()
	{
		return 0;
	}

	u32 getVirtualDisplayHeight()
	{
		return 0;
	}

	u32 getVirtualDisplayOffset2D()
	{
		return 0;
	}

	u32 getVirtualDisplayOffset3D()
	{
		return 0;
	}

	void* getVirtualDisplayGpuPtr()
	{
		// HACK re-purposed for the palette pointer
		return window;
		//return nullptr;
	}

	bool getWidescreen()
	{
		return false;
	}

	bool getFrameBufferAsync()
	{
		return false;
	}

	bool getGPUColorConvert()
	{
		return false;
	}

	void updateVirtualDisplay(const void* buffer, size_t size)
	{
		s_curFrameBuffer = (u8*)buffer;
	}

	void bindVirtualDisplay()
	{
		
	}

	void clearVirtualDisplay(f32* color, bool clearColor)
	{
		
	}

	void copyToVirtualDisplay(RenderTargetHandle src)
	{
		
	}

	void copyBackbufferToRenderTarget(RenderTargetHandle dst)
	{
		
	}

	void setPalette(const u32* palette)
	{
		u8* palrgba = (u8*)palette;
		if (screen) {
			ULONG *sp = &spal[1];

			//*sp++ = 256 << 16;
			if (s_colorCorrection) {
				u8* gammaTable = s_gammaTable;
				for (int i = 0; i < 256; i++) {
					*sp++ = gammaTable[palrgba[3]] << 24;
					*sp++ = gammaTable[palrgba[2]] << 24;
					*sp++ = gammaTable[palrgba[1]] << 24;
					palrgba += 4;
				}
			} else {
				for (int i = 0; i < 256; i++) {
					*sp++ = *((ULONG *)&palrgba[3]);
					*sp++ = *((ULONG *)&palrgba[2]);
					*sp++ = *((ULONG *)&palrgba[1]);
					palrgba += 4;
				}
			}
			//*sp = 0;
//if (!TFE_Input::relativeModeEnabled()) buildprintf("%s relativeModeEnabled\n", __FUNCTION__);
			updatePalette = TRUE;
		} else {
			unsigned char *pp = ppal;

			for (int i = 0; i < 256; i++)
			{
				*pp++ = 0;
				*pp++ = palrgba[3];
				*pp++ = palrgba[2];
				*pp++ = palrgba[1];
				palrgba += 4;
			}
		}
	}

	const u32* getPalette()
	{
		//return s_paletteCpu;
		return nullptr; // TODO
	}

	const TextureGpu* getPaletteTexture()
	{
		return nullptr;
	}

	void setColorCorrection(bool enabled, const ColorCorrection* color/* = nullptr*/, bool bloomChanged/* = false*/)
	{
		s_colorCorrection = enabled && color->gamma != 1.0f;
		if (s_colorCorrection)
		{
			for (int i = 0; i < 256; i++)
			{
				s_gammaTable[i] = pow((float)i / 256.0f, 2.0f - color->gamma) * 256.0f;
			}
		}
	}

	void drawVirtualDisplay()
	{
	}

	// GPU commands
	// core gpu functionality for UI and editor.
	// Render target.
	RenderTargetHandle createRenderTarget(u32 width, u32 height, bool hasDepthBuffer)
	{
		return RenderTargetHandle(nullptr);
	}

	void freeRenderTarget(RenderTargetHandle handle)
	{
	}

	void bindRenderTarget(RenderTargetHandle handle)
	{
	}

	void clearRenderTarget(RenderTargetHandle handle, const f32* clearColor, f32 clearDepth)
	{
	}

	void clearRenderTargetDepth(RenderTargetHandle handle, f32 clearDepth)
	{
	}

	void copyRenderTarget(RenderTargetHandle dst, RenderTargetHandle src)
	{
	}

	void unbindRenderTarget()
	{
	}

	const TextureGpu* getRenderTargetTexture(RenderTargetHandle rtHandle)
	{
		return nullptr;
	}

	void getRenderTargetDim(RenderTargetHandle rtHandle, u32* width, u32* height)
	{
	}

	TextureGpu* createTexture(u32 width, u32 height, u32 channels)
	{
		return nullptr;
	}

	TextureGpu* createTextureArray(u32 width, u32 height, u32 layers, u32 channels)
	{
		return nullptr;
	}

	// Create a GPU version of a texture, assumes RGBA8 and returns a GPU handle.
	TextureGpu* createTexture(u32 width, u32 height, const u32* data, MagFilter magFilter)
	{
		return nullptr;
	}

	void freeTexture(TextureGpu* texture)
	{
	}

	void getTextureDim(TextureGpu* texture, u32* width, u32* height)
	{
		*width = 0;
		*height = 0;
	}

	void* getGpuPtr(const TextureGpu* texture)
	{
		return nullptr;
	}

	void drawIndexedTriangles(u32 triCount, u32 indexStride, u32 indexStart)
	{
	}

	void drawLines(u32 lineCount)
	{
	}

	// A quick way of toggling the bloom, but just for the final blit.
	void bloomPostEnable(bool enable)
	{
	}

	// Setup the Post effect chain based on current settings.
	// TODO: Move out of render backend since this should be independent of the backend.
	void setupPostEffectChain(bool useDynamicTexture)
	{
	}
}  // namespace
