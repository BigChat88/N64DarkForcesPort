#include <TFE_FrontEndUI/frontEndUi.h>
#include <TFE_RenderBackend/renderBackend.h>
#include <TFE_Input/inputMapping.h>
#include <TFE_Jedi/Math/core_math.h>

#define MonitorInfo MonitorInfo_Amiga
#define DisplayInfo DisplayInfo_Amiga
#define Image Image_Amiga

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>

#include <SDI_compiler.h>

#undef MonitorInfo
#undef DisplayInfo
#undef Image

namespace TFE_DarkForces
{
	extern void pauseLevelSound();
	extern void resumeLevelSound();
	//extern void clearBufferedSound();
	extern JBool s_palModified;
}

namespace TFE_FrontEndUI
{
	static ULONG pen0Color[3];
	static ULONG penColors[NUMDRIPENS][3];
	static UWORD penNumbers[NUMDRIPENS];
	static UWORD penCount = 0;

	void init()
	{
		struct Screen *screen;
		if ((screen = LockPubScreen(NULL)))
		{
			struct DrawInfo *dri;
			if ((dri = GetScreenDrawInfo(screen)))
			{
				/*
				9b9b9b
				000000
				ffffff
				5078a0
				*/
				GetRGB32(screen->ViewPort.ColorMap, 0, 1, pen0Color);
				penCount = TFE_Jedi::min(NUMDRIPENS, dri->dri_NumPens);
				for (int i = 0; i < penCount; i++)
				{
					UWORD pen = dri->dri_Pens[i];
					GetRGB32(screen->ViewPort.ColorMap, pen, 1, penColors[i]);
					penNumbers[i] = pen;
				}
				FreeScreenDrawInfo(screen, dri);
			}
			UnlockPubScreen(NULL, screen);
		}
	}

	void shutdown()
	{
		// nothing to do here for now
	}

	void enableConfigMenu()
	{
		BOOL fullscreen = FALSE;
		struct Window *window = (struct Window *)TFE_RenderBackend::getVirtualDisplayGpuPtr();
		struct Screen *screen = window->WScreen;
		if ((screen->Flags & SCREENTYPE) == CUSTOMSCREEN)
		{
			SetRGB32(&screen->ViewPort, 0, pen0Color[0], pen0Color[1], pen0Color[2]);
			for (int i = 0; i < penCount; i++)
			{
				SetRGB32(&screen->ViewPort, penNumbers[i], penColors[i][0], penColors[i][1], penColors[i][2]);
			}
		}

		TFE_DarkForces::pauseLevelSound();
		SystemTagList("TFEConfig", NULL);
		TFE_DarkForces::resumeLevelSound();

		TFE_DarkForces::s_palModified = JTRUE;
		TFE_Input::inputMapping_restore();
	}

	//AppState menuReturn();
	//void setMenuReturnState(AppState state);
	//bool isConfigMenuOpen();
}
