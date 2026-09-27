#include <cstring>

#include "missionBriefing.h"
#include "menu.h"
#include <TFE_DarkForces/Landru/lactorDelt.h>
#include <TFE_DarkForces/Landru/lactorAnim.h>
#include <TFE_DarkForces/Landru/lpalette.h>
#include <TFE_DarkForces/Landru/lcanvas.h>
#include <TFE_DarkForces/Landru/ldraw.h>
#include <TFE_Archive/lfdArchive.h>
#include <TFE_DarkForces/agent.h>
#include <TFE_DarkForces/util.h>
#include <TFE_Archive/archive.h>
#include <TFE_Settings/settings.h>
#include <TFE_Input/input.h>
#include <TFE_RenderBackend/renderBackend.h>
#include <TFE_Jedi/Math/core_math.h>
#include <TFE_Jedi/Level/rtexture.h>
#include <TFE_System/system.h>
#include <TFE_Jedi/Renderer/virtualFramebuffer.h>
#ifdef __N64__
#include "menunav_n64.h"
#endif

using namespace TFE_Jedi;

namespace TFE_DarkForces
{
	///////////////////////////////////////////
	// Constants
	///////////////////////////////////////////
	enum BriefingButton
	{
		BRIEF_BTN_OK = 0,
		BRIEF_BTN_UP,
		BRIEF_BTN_DOWN,
		BRIEF_BTN_CANCEL,
		BRIEF_BTN_EASY,
		BRIEF_BTN_MEDIUM,
		BRIEF_BTN_HARD,
		BRIEF_BTN_COUNT,
	};
	static s32 s_keyPressed = -1;
	static JBool s_briefingOpen = JFALSE;
	static s32 s_skill = 0;
#ifdef __N64__
	static s32 s_n64Focus = BRIEF_BTN_OK;	// see n64_handleInput()
	static JBool s_n64Pressing = JFALSE;	// A is held on the focused button
#endif
	static LRect s_briefRect = { 25, 15, 155, 305 };
	static LRect s_missionTextRect;
	static LRect s_viewBounds;
	static LActor* s_briefActor = nullptr;
	static LActor* s_menuActor = nullptr;
	static LPalette* s_palette = nullptr;
	static u8* s_framebuffer = nullptr;
	static LangHotkeys* s_langKeys;

	s16 s_briefY;
	s32 s_briefingMaxY;
	LRect s_overlayRect;

	enum
	{
		MENU_TYPE_BACK = 200,
		MENU_TYPE_OVERLAY = 201,
	};

	static const s16 c_frameTypes[] =
	{
		MENU_TYPE_BACK,
		MENU_TYPE_OVERLAY,
		-1, //		MENU_TYPE_BUTTON,
		-1, //		MENU_TYPE_BUTTON_UP,
		-1, //		MENU_TYPE_REPEAT_BUTTON,
		-1, //		MENU_TYPE_BUTTON_UP,
		-1, //		MENU_TYPE_REPEAT_BUTTON,
		-1, //		MENU_TYPE_BUTTON_UP,
		-1, //		MENU_TYPE_BUTTON,
		-1, //		MENU_TYPE_BUTTON_UP,
		-1, //		MENU_TYPE_BUTTON,
		-1, //		MENU_TYPE_BUTTON_UP,
		-1, //		MENU_TYPE_BUTTON,
		-1, //		MENU_TYPE_BUTTON_UP,
		-1, //		MENU_TYPE_BUTTON,
		-1, //		MENU_TYPE_BUTTON_UP,
	};

	///////////////////////////////////////////
	// API Implementation
	///////////////////////////////////////////
	void missionBriefing_start(const char* archive, const char* bgAnim, const char* mission, const char* palette, s32 skill, LangHotkeys* langKeys)
	{
		s_langKeys = langKeys;

		menu_init();
		menu_startupDisplay();

		s_briefingOpen = JFALSE;
		s_skill = skill;
#ifdef __N64__
		s_n64Focus = BRIEF_BTN_OK;
		s_n64Pressing = JFALSE;
#endif

		if (!menu_openResourceArchive(archive))
		{
			// Try the dfbrief.lfd file.
			if (!menu_openResourceArchive("dfbrief.lfd"))
			{
				return;
			}
		}

		// Mission specific text and images.
		s_briefActor = lactorDelt_load(mission, &s_briefRect, 0, 0, 0);
		if (!s_briefActor)
		{
			menu_closeResourceArchive();
			return;
		}

		// Menu Items
		LRect bounds;
		lcanvas_getBounds(&bounds);
		s_menuActor = lactorAnim_load(bgAnim, &bounds, 0, 0, 0);
		lactor_setTime(s_menuActor, -1, -1);
		s_palette = lpalette_load(palette);
		lpalette_setScreenPal(s_palette);

		if (!s_menuActor)
		{
			menu_closeResourceArchive();
			s_briefingOpen = JFALSE;
			s_skill = 1;
			vfb_forceToBlack();
			lcanvas_clear();
			return;
		}

		s16 state_btn_index = 600;
		s16 button_index = 0;
		s16 slider_index = 0;
		s16 back_state = -1;
		s16 overlay_state = -1;
			   
		// Buttons
		const s32 count = s_menuActor ? s_menuActor->arraySize : 0;
		for (s32 i = 0; i < count; i++)
		{
			LRect rect;
			lactor_setState(s_menuActor, i, 0);
			lactor_getFrame(s_menuActor, &rect);

			if (c_frameTypes[i] == MENU_TYPE_BACK)
			{
				back_state = i;
			}
			else if (c_frameTypes[i] == MENU_TYPE_OVERLAY)
			{
				overlay_state = i;
				lactorAnim_getFrame(s_menuActor, &s_missionTextRect);
			}
		}
		menu_closeResourceArchive();
		
		LRect rect;
		lactor_setTime(s_briefActor, -1, -1);
		lactorDelt_getFrame(s_briefActor, &rect);

		s_briefingMaxY = (rect.bottom - rect.top) - (s_briefRect.bottom - s_briefRect.top) + BRIEF_VERT_MARGIN;
		// Round to a factor of the line scrolling value.
		s_briefingMaxY = s_briefingMaxY + BRIEF_LINE_SCROLL - (s_briefingMaxY % BRIEF_LINE_SCROLL);

		if (s_briefingMaxY < -BRIEF_VERT_MARGIN)
		{
			s_briefingMaxY = -BRIEF_VERT_MARGIN;
		}
		s_overlayRect = s_briefRect;
		s_briefY = -BRIEF_VERT_MARGIN;

		s_briefingOpen = JTRUE;

		s_framebuffer = ldraw_getBitmap();
		lcanvas_getBounds(&s_viewBounds);

		ltime_setFrameDelay(20);
	}

	void missionBriefing_cleanup()
	{
		lactor_removeActor(s_briefActor);
		lactor_removeActor(s_menuActor);

		lactor_free(s_briefActor);
		lactor_free(s_menuActor);
		lpalette_free(s_palette);

		s_briefActor = nullptr;
		s_menuActor = nullptr;
		s_palette = nullptr;
	}
		
#ifdef __N64__
	// Turns the red label of the button just drawn green, like the lit labels of the
	// escape menu: each red shade of the briefing palette maps to the green of similar
	// brightness (the palette has a pure green ramp at 10-14).
	static void n64_lightLabel()
	{
		static u8 s_remap[256];
		if (!s_remap[1])
		{
			for (s32 i = 0; i < 256; i++) { s_remap[i] = u8(i); }
			s_remap[6] = 10;   s_remap[161] = 10;
			s_remap[7] = 11;   s_remap[162] = 11;
			s_remap[8] = 12;   s_remap[163] = 12;  s_remap[164] = 12;
			s_remap[165] = 13; s_remap[166] = 13;
			s_remap[167] = 14;
		}

		LRect rect;
		lactorAnim_getFrame(s_menuActor, &rect);
		for (s32 y = max(rect.top, (s16)0); y < min(rect.bottom, (s16)200); y++)
		{
			u8* row = &s_framebuffer[y * 320];
			for (s32 x = max(rect.left, (s16)0); x < min(rect.right, (s16)320); x++)
			{
				row[x] = s_remap[row[x]];
			}
		}
	}
#endif

	void drawButton(BriefingButton id)
	{
		s32 pressed = 0;
#ifdef __N64__
		// The chosen difficulty and the scroll arrow in use are drawn pressed; the focused
		// button gets its label lit (see n64_lightLabel()). While choosing the difficulty,
		// the previous choice is not shown: only the focused one is lit.
		if (id == s_keyPressed || (id >= BRIEF_BTN_EASY && s_n64Focus < BRIEF_BTN_EASY && s_skill == id - BRIEF_BTN_EASY) ||
			(s_n64Pressing && id == s_n64Focus))
		{
			pressed = 1;
		}
#else
		if ((s_buttonHover && id == s_buttonPressed) || (id == s_keyPressed))
		{
			pressed = 1;
		}
		else if (id >= BRIEF_BTN_EASY && s_skill == id - BRIEF_BTN_EASY)
		{
			pressed = 1;
		}
#endif

		lactor_setState(s_menuActor, 2*(1+id) + (pressed ? 0 : 1), 0);
		lactorAnim_draw(s_menuActor, &s_viewBounds, &s_viewBounds, 0, 0, JTRUE);
#ifdef __N64__
		if (id == s_n64Focus) { n64_lightLabel(); }
#endif
	}

	void missionBriefing_scroll(s32 amt)
	{
		if (amt < 0 && s_briefY > -BRIEF_VERT_MARGIN)
		{
			s_briefY += amt;
			if (s_briefY < -BRIEF_VERT_MARGIN) { s_briefY = -BRIEF_VERT_MARGIN; }
		}
		else if (amt > 0 && s_briefY != s_briefingMaxY)
		{
			s_briefY += amt;
			if (s_briefY > s_briefingMaxY) { s_briefY = s_briefingMaxY; }
		}
	}
		
#ifdef __N64__
	// __N64__: console style controls instead of the cursor. The focus starts on OK;
	// left moves it to CANCEL and then into the difficulty buttons (starting on EASY), which can only be
	// left by choosing one with A (the focus then returns to OK). Up/down scroll the
	// briefing. The D-pad and the stick both work.

	static JBool n64_handleInput(JBool* abort)
	{
		const u32 pressed = MenuNav_N64::pressed();

		// Like a mouse click, the button is drawn pressed while A is held and activates
		// when A is released. Moving the focus cancels the press.
		JBool select = JFALSE;
		if (pressed & (MenuNav_N64::NAV_LEFT | MenuNav_N64::NAV_RIGHT))
		{
			s_n64Pressing = JFALSE;
		}
		else if (TFE_Input::mousePressed(MBUTTON_LEFT))	// A
		{
			s_n64Pressing = JTRUE;
		}
		else if (s_n64Pressing && !TFE_Input::mouseDown(MBUTTON_LEFT))
		{
			s_n64Pressing = JFALSE;
			select = JTRUE;
		}
		if (s_n64Focus >= BRIEF_BTN_EASY)
		{
			if ((pressed & MenuNav_N64::NAV_LEFT)  && s_n64Focus > BRIEF_BTN_EASY) { s_n64Focus--; }
			if ((pressed & MenuNav_N64::NAV_RIGHT) && s_n64Focus < BRIEF_BTN_HARD) { s_n64Focus++; }
			if (select)
			{
				s_skill = s_n64Focus - BRIEF_BTN_EASY;
				s_n64Focus = BRIEF_BTN_OK;
			}
		}
		else
		{
			if (pressed & MenuNav_N64::NAV_LEFT)
			{
				s_n64Focus = (s_n64Focus == BRIEF_BTN_OK) ? BRIEF_BTN_CANCEL : BRIEF_BTN_EASY;
			}
			else if ((pressed & MenuNav_N64::NAV_RIGHT) && s_n64Focus == BRIEF_BTN_CANCEL)
			{
				s_n64Focus = BRIEF_BTN_OK;
			}
			else if ((select && s_n64Focus == BRIEF_BTN_OK) || TFE_Input::keyPressed(KEY_ESCAPE))	// A on OK, Start
			{
				*abort = JFALSE;
				return JTRUE;
			}
			else if ((select && s_n64Focus == BRIEF_BTN_CANCEL) || TFE_Input::keyPressed(KEY_RETURN))	// A on CANCEL, B
			{
				*abort = JTRUE;
				return JTRUE;
			}
		}

		// Timer limited so that the scrolling works correctly (same as the arrow keys).
		if (ltime_isFrameReady())
		{
			const u32 held = MenuNav_N64::held();
			s_keyPressed = -1;
			if (held & MenuNav_N64::NAV_UP)
			{
				s_keyPressed = BRIEF_BTN_UP;
				missionBriefing_scroll(-BRIEF_LINE_SCROLL);
			}
			else if (held & MenuNav_N64::NAV_DOWN)
			{
				s_keyPressed = BRIEF_BTN_DOWN;
				missionBriefing_scroll(BRIEF_LINE_SCROLL);
			}
		}
		return JFALSE;
	}
#endif

	JBool missionBriefing_handleInput(JBool* abort)
	{
#ifdef __N64__
		return n64_handleInput(abort);
#endif
		JBool exitBriefing = JFALSE;

		// Add support for mouse wheel scrolling.
		s32 wdx, wdy;
		TFE_Input::getMouseWheel(&wdx, &wdy);
		
		// Mouse interactions.
		menu_handleMousePosition();
		if (TFE_Input::mousePressed(MBUTTON_LEFT))
		{
			s_buttonPressed = -1;
			for (s32 i = 0; i < BRIEF_BTN_COUNT; i++)
			{
				lactor_setState(s_menuActor, 2 * (1 + i), 0);
				LRect buttonRect;
				lactorAnim_getFrame(s_menuActor, &buttonRect);

				if (s_cursorPos.x >= buttonRect.left && s_cursorPos.x < buttonRect.right &&
					s_cursorPos.z >= buttonRect.top && s_cursorPos.z < buttonRect.bottom)
				{
					s_buttonPressed = s32(i);
					s_buttonHover = JTRUE;
					break;
				}
			}
		}
		else if (TFE_Input::mouseDown(MBUTTON_LEFT) && s_buttonPressed >= 0)
		{
			lactor_setState(s_menuActor, 2 * (1 + s_buttonPressed), 0);
			LRect buttonRect;
			lactorAnim_getFrame(s_menuActor, &buttonRect);

			// Verify that the mouse is still over the button.
			if (s_cursorPos.x >= buttonRect.left && s_cursorPos.x < buttonRect.right &&
				s_cursorPos.z >= buttonRect.top && s_cursorPos.z < buttonRect.bottom)
			{
				s_buttonHover = JTRUE;
				if (ltime_isFrameReady())
				{
					if (s_buttonPressed == BRIEF_BTN_UP)
					{
						missionBriefing_scroll(-BRIEF_LINE_SCROLL);
					}
					else if (s_buttonPressed == BRIEF_BTN_DOWN)
					{
						missionBriefing_scroll(BRIEF_LINE_SCROLL);
					}
				}
			}
			else
			{
				s_buttonHover = JFALSE;
			}
		}
		else
		{
			// Mouse wheel support.
			if (wdy > 0) // up
			{
				missionBriefing_scroll(-BRIEF_LINE_SCROLL * wdy);
			}
			else if (wdy < 0) // down
			{
				missionBriefing_scroll(-BRIEF_LINE_SCROLL * wdy);
			}

			if (s_buttonPressed >= 0 && s_buttonHover)
			{
				switch (s_buttonPressed)
				{
					case BRIEF_BTN_OK:
					{
						*abort = JFALSE;
						exitBriefing = JTRUE;
					} break;
					case BRIEF_BTN_UP:
					{
						missionBriefing_scroll(-BRIEF_LINE_SCROLL);
					} break;
					case BRIEF_BTN_DOWN:
					{
						missionBriefing_scroll(BRIEF_LINE_SCROLL);
					} break;
					case BRIEF_BTN_CANCEL:
					{
						*abort = JTRUE;
						exitBriefing = JTRUE;
					} break;
					case BRIEF_BTN_EASY:
					{
						s_skill = 0;
					} break;
					case BRIEF_BTN_MEDIUM:
					{
						s_skill = 1;
					} break;
					case BRIEF_BTN_HARD:
					{
						s_skill = 2;
					} break;
				}
			}
			// Reset.
			s_buttonPressed = -1;
			s_buttonHover = JFALSE;
		}

		// Keyboard shortcuts.

		// These need to be timer limited so that the scrolling works correctly.
		if (ltime_isFrameReady())
		{
			s_keyPressed = -1;
			if (TFE_Input::keyDown(KEY_UP))
			{
				s_keyPressed = BRIEF_BTN_UP;
				missionBriefing_scroll(-BRIEF_LINE_SCROLL);
			}
			else if (TFE_Input::keyDown(KEY_DOWN))
			{
				s_keyPressed = BRIEF_BTN_DOWN;
				missionBriefing_scroll(BRIEF_LINE_SCROLL);
			}
			
			if (TFE_Input::keyDown(KEY_PAGEUP))
			{
				missionBriefing_scroll(-BRIEF_PAGE_SCROLL);
			}
			else if (TFE_Input::keyDown(KEY_PAGEDOWN))
			{
				missionBriefing_scroll(BRIEF_PAGE_SCROLL);
			}
		}

		if (TFE_Input::keyPressed(s_langKeys->k_easy))
		{
			s_keyPressed = BRIEF_BTN_EASY;
			s_skill = 0;
		}
		else if (TFE_Input::keyPressed(s_langKeys->k_med))
		{
			s_keyPressed = BRIEF_BTN_MEDIUM;
			s_skill = 1;
		}
		else if (TFE_Input::keyPressed(s_langKeys->k_hard))
		{
			s_keyPressed = BRIEF_BTN_HARD;
			s_skill = 2;
		}

		if (TFE_Input::keyPressed(s_langKeys->k_canc) || TFE_Input::keyPressed(KEY_ESCAPE))
		{
			*abort = JTRUE;
			exitBriefing = JTRUE;
		}
		else if (TFE_Input::keyPressed(KEY_O) || TFE_Input::keyPressed(KEY_RETURN))
		{
			*abort = JFALSE;
			exitBriefing = JTRUE;
		}

		return exitBriefing;
	}

	JBool missionBriefing_update(s32* skill, JBool* abort)
	{
		if (!s_briefingOpen)
		{
			*skill = s_skill;
			*abort = JFALSE;
			return JFALSE;
		}
		tfe_updateLTime();

		// Input
		if (missionBriefing_handleInput(abort))
		{
			s_briefingOpen = JFALSE;
			*skill = s_skill;
			vfb_forceToBlack();
			lcanvas_clear();
			return JFALSE;
		}

		// Background
		lcanvas_eraseRect(&s_viewBounds);
		lactor_setState(s_menuActor, 0, 0);
		lactorAnim_draw(s_menuActor, &s_viewBounds, &s_viewBounds, 0, 0, JTRUE);

		// Buttons
		for (s32 i = 0; i < BRIEF_BTN_COUNT; i++)
		{
			drawButton(BriefingButton(i));
		}

		// Briefing Text.
		LRect rect = s_missionTextRect;
		s16 diff = ((rect.right - rect.left) - s_briefActor->w) >> 1;
		s16 x = diff + rect.left - s_briefActor->x;
		s16 y = (rect.top - s_briefActor->y) - s_briefY;

		lcanvas_setClip(&s_missionTextRect);
		lactorDelt_draw(s_briefActor, &rect, &s_missionTextRect, x, y, JTRUE);
		lcanvas_clearClipRect();

#ifndef __N64__	// __N64__: no cursor, see n64_handleInput().
		menu_blitCursor(s_cursorPos.x, s_cursorPos.z, s_framebuffer);
#endif
		menu_blitToScreen();
		return JTRUE;
	}
	
	///////////////////////////////////////////
	// Internal Implementation
	///////////////////////////////////////////
}