#include <cstring>

#include "agentMenu.h"
#include "editBox.h"
#include "delt.h"
#include <TFE_Game/igame.h>
#include "menu.h"
#include "uiDraw.h"
#include <TFE_FrontEndUI/frontEndUi.h>
#include <TFE_DarkForces/agent.h>
#include <TFE_DarkForces/util.h>
#include <TFE_DarkForces/Landru/lcanvas.h>
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
	enum AgentMenuButtons
	{
		AGENT_NEW,
		AGENT_REMOVE,
		AGENT_EXIT,
		AGENT_BEGIN,
		AGENT_COUNT
	};
	static const Vec2i c_agentButtons[AGENT_COUNT] =
	{
		{21, 165},	// AGENT_NEW
		{90, 165},	// AGENT_REMOVE
		{164, 165},	// AGENT_EXIT
		{239, 165},	// AGENT_BEGIN
	};
	static const Vec2i c_agentButtonDim = { 60, 25 };

	enum NewAgentDlg
	{
		NEW_AGENT_NO,
		NEW_AGENT_YES,
		NEW_AGENT_COUNT
	};
	static const Vec2i c_newAgentButtons[NEW_AGENT_COUNT] =
	{
		{167, 107},	// NEW_AGENT_NO
		{206, 107},	// NEW_AGENT_YES
	};
	static const Vec2i c_newAgentButtonDim = { 28, 15 };

	///////////////////////////////////////////
	// Internal State
	///////////////////////////////////////////
	static JBool s_loaded = JFALSE;
	static JBool s_displayInit = JFALSE;
	static u8*   s_framebuffer = nullptr;

	static s32 s_selectedMission = 0;
	static s32 s_editCursorFlicker = 0;
	static s32 s_agentCount = 0;
	static JBool s_lastSelectedAgent = JTRUE;
	static JBool s_newAgentDlg = JFALSE;
	static JBool s_removeAgentDlg = JFALSE;
	static JBool s_quitConfirmDlg = JFALSE;
	static JBool s_missionBegin = JFALSE;
	static EditBox s_editBox = {};
	
	static u8 s_menuPalette[768];
	static DeltFrame* s_agentMenuFrames;
	static DeltFrame* s_agentDlgFrames;
	static u32 s_agentMenuCount;
	static u32 s_agentDlgCount;
	
	static char s_newAgentName[32];
	static LangHotkeys* s_langKeys;

	///////////////////////////////////////////
	// Forward Declarations
	///////////////////////////////////////////
	void agentMenu_startup();
	void agentMenu_startupDisplay();
	void agentMenu_blit();
	void agentMenu_update();
	void agentMenu_draw();

	void drawYesNoDlg(s32 baseFrame);
	void drawNewAgentDlg();

	void updateNewAgentDlg();
	void updateRemoveAgentDlg();
	JBool updateQuitConfirmDlg();

	///////////////////////////////////////////
	// API Implementation
	///////////////////////////////////////////
	void agentMenu_resetState()
	{
		s_loaded = JFALSE;
		s_displayInit = JFALSE;
		s_agentMenuFrames = nullptr;
		s_agentDlgFrames = nullptr;
		s_framebuffer = nullptr;
		delt_resetState();
	}

#ifdef __N64__
	// __N64__: the menu animations take ~1.3MB, which a level needs. They are
	// loaded when the agent menu is shown and freed when a mission begins.
	static JBool s_framesLoaded = JFALSE;
	static void n64_loadFrames();
	static void n64_resetFocus();

	static void freeDeltFrames(DeltFrame*& frames, u32& count)
	{
		if (!frames) { return; }
		for (u32 i = 0; i < count; i++)
		{
			game_free(frames[i].texture.image);
		}
		game_free(frames);
		frames = nullptr;
		count = 0;
	}

	static void n64_freeFrames()
	{
		freeDeltFrames(s_agentMenuFrames, s_agentMenuCount);
		freeDeltFrames(s_agentDlgFrames, s_agentDlgCount);
		s_framesLoaded = JFALSE;
	}
#endif

	JBool agentMenu_update(s32* levelIndex)
	{
#ifdef __N64__
		n64_loadFrames();
#endif
		if (!s_loaded)
		{
			menu_init();
			agentMenu_startup();
			s_loaded = JTRUE;
		}
		if (!s_displayInit)
		{
			agentMenu_startupDisplay();
			s_missionBegin = JFALSE;
			s_displayInit = JTRUE;
#ifdef __N64__
			n64_resetFocus();
#endif
		}

		// Update the mouse position.
		menu_handleMousePosition();
		agentMenu_update();
		agentMenu_draw();
		
		agentMenu_blit();

		// Reset for next time.
		if (s_missionBegin)
		{
			s_displayInit = JFALSE;
			vfb_forceToBlack();
			lcanvas_clear();

			assert(s_selectedMission >= 0 && s_selectedMission <= MAX_LEVEL_COUNT - 1);
			s_selectedMission = clamp(s_selectedMission, 0, MAX_LEVEL_COUNT - 1);
#ifdef __N64__
			n64_freeFrames();
#endif
		}

		*levelIndex = s_selectedMission + 1;
		return ~s_missionBegin;
	}
	
	///////////////////////////////////////////
	// Internal Implementation
	///////////////////////////////////////////
#ifdef __N64__
	///////////////////////////////////////////
	// __N64__: console style controls instead of the cursor (D-pad or stick).
	// The focus starts on BEGIN MISSION; left/right move it between NEW AGENT,
	// REMOVE AGENT and BEGIN MISSION (DOS is disabled). Up enters a list (missions
	// from BEGIN, agents otherwise): up/down change the selection, left/right switch
	// lists, and A returns to BEGIN MISSION. Buttons activate when A is released.
	///////////////////////////////////////////
	enum N64Zone
	{
		N64_ZONE_BUTTONS = 0,
		N64_ZONE_AGENTS,
		N64_ZONE_MISSIONS,
	};
	static s32 s_n64Zone = N64_ZONE_BUTTONS;
	static s32 s_n64Button = AGENT_BEGIN;
	static s32 s_n64DlgFocus = NEW_AGENT_NO;
	static JBool s_n64Pressing = JFALSE;

	void agentMenu_createNewAgent();
	void agentMenu_removeAgent(s32 index);

	static void n64_resetFocus()
	{
		s_n64Zone = N64_ZONE_BUTTONS;
		s_n64Button = s_agentCount > 0 ? AGENT_BEGIN : AGENT_NEW;
		s_n64Pressing = JFALSE;
	}

	// While A is held, the lit (green) button is drawn one shade darker.
	static void n64_darken(s32 x0, s32 y0, s32 w, s32 h)
	{
		for (s32 y = y0; y < y0 + h; y++)
		{
			u8* row = &s_framebuffer[y * 320];
			for (s32 x = x0; x < x0 + w; x++)
			{
				if (row[x] >= 10 && row[x] <= 13) { row[x]++; }
			}
		}
	}

	static void n64_openNewAgentDlg()
	{
		s_newAgentDlg = JTRUE;
		memset(s_newAgentName, 0, 32);
		// Suggest a name, since typing one with the D-pad is slow.
		strcpy(s_newAgentName, "KYLE");
		s_editBox.cursor = 4;
		s_editBox.inputField = s_newAgentName;
		s_editBox.maxLen = 16;
	}

	static void n64_moveSelection(s32 dir)
	{
		if (s_n64Zone == N64_ZONE_AGENTS && s_agentCount > 0)
		{
			s_agentId += dir;
			if (s_agentId >= (s32)s_agentCount) { s_agentId = 0; }
			if (s_agentId < 0) { s_agentId = s_agentCount - 1; }
			s_selectedMission = max(0, s_agentData[s_agentId].selectedMission - 1);
		}
		else if (s_n64Zone == N64_ZONE_MISSIONS && s_agentId >= 0 && s_agentData[s_agentId].nextMission > 1)
		{
			const s32 lastMission = min(s_agentData[s_agentId].nextMission, s_maxLevelIndex) - 1;
			s_selectedMission += dir;
			if (s_selectedMission > lastMission) { s_selectedMission = 0; }
			if (s_selectedMission < 0) { s_selectedMission = lastMission; }
			s_agentData[s_agentId].selectedMission = s_selectedMission + 1;
		}
	}

	static void n64_update()
	{
		const u32 nav = MenuNav_N64::pressed();
		const JBool back = TFE_Input::keyPressed(KEY_RETURN);	// B

		// Like a mouse click: pressed while A is held, activated when A is released.
		JBool activate = JFALSE;
		if (TFE_Input::mousePressed(MBUTTON_LEFT))
		{
			s_n64Pressing = JTRUE;
		}
		else if (s_n64Pressing && !TFE_Input::mouseDown(MBUTTON_LEFT))
		{
			s_n64Pressing = JFALSE;
			activate = JTRUE;
		}

		if (s_newAgentDlg)
		{
			// The D-pad and stick edit the name (see editBox.cpp), A creates the agent.
			updateEditBox(&s_editBox);
			s_buttonPressed = NEW_AGENT_YES;
			s_buttonHover = JTRUE;
			if (back)
			{
				s_newAgentDlg = JFALSE;
				s_n64Pressing = JFALSE;
			}
			else if (activate)
			{
				agentMenu_createNewAgent();
				s_newAgentDlg = JFALSE;
				n64_resetFocus();
			}
			return;
		}
		if (s_removeAgentDlg)
		{
			if (nav & MenuNav_N64::NAV_LEFT)  { s_n64DlgFocus = NEW_AGENT_NO;  s_n64Pressing = JFALSE; }
			if (nav & MenuNav_N64::NAV_RIGHT) { s_n64DlgFocus = NEW_AGENT_YES; s_n64Pressing = JFALSE; }
			s_buttonPressed = s_n64DlgFocus;
			s_buttonHover = JTRUE;
			if (back)
			{
				s_removeAgentDlg = JFALSE;
				s_n64Pressing = JFALSE;
			}
			else if (activate)
			{
				if (s_n64DlgFocus == NEW_AGENT_YES) { agentMenu_removeAgent(s_agentId); }
				s_removeAgentDlg = JFALSE;
				n64_resetFocus();
			}
			return;
		}

		if (s_selectedMission >= s_maxLevelIndex)
		{
			s_selectedMission = max(0, s_maxLevelIndex - 1);
		}
		if (s_selectedMission < 0) { s_selectedMission = 0; }

		if (s_n64Zone == N64_ZONE_BUTTONS)
		{
			if (nav & (MenuNav_N64::NAV_LEFT | MenuNav_N64::NAV_RIGHT | MenuNav_N64::NAV_UP))
			{
				s_n64Pressing = JFALSE;
			}
			if (nav & MenuNav_N64::NAV_LEFT)
			{
				if (s_n64Button == AGENT_BEGIN) { s_n64Button = AGENT_REMOVE; }
				else if (s_n64Button == AGENT_REMOVE) { s_n64Button = AGENT_NEW; }
			}
			else if (nav & MenuNav_N64::NAV_RIGHT)
			{
				if (s_n64Button == AGENT_NEW) { s_n64Button = AGENT_REMOVE; }
				else if (s_n64Button == AGENT_REMOVE) { s_n64Button = AGENT_BEGIN; }
			}
			else if ((nav & MenuNav_N64::NAV_UP) && s_agentCount > 0)
			{
				s_n64Zone = (s_n64Button == AGENT_BEGIN) ? N64_ZONE_MISSIONS : N64_ZONE_AGENTS;
			}
			else if (activate)
			{
				switch (s_n64Button)
				{
					case AGENT_NEW:
						n64_openNewAgentDlg();
						break;
					case AGENT_REMOVE:
						if (s_agentCount > 0)
						{
							s_removeAgentDlg = JTRUE;
							s_n64DlgFocus = NEW_AGENT_NO;
						}
						break;
					case AGENT_BEGIN:
						if (s_agentCount > 0 && s_selectedMission >= 0)
						{
							s_missionBegin = JTRUE;
						}
						break;
				}
			}
		}
		else
		{
			// Inside a list: only A leaves it.
			if (nav & (MenuNav_N64::NAV_LEFT | MenuNav_N64::NAV_RIGHT))
			{
				s_n64Zone = (s_n64Zone == N64_ZONE_AGENTS) ? N64_ZONE_MISSIONS : N64_ZONE_AGENTS;
			}
			if (nav & MenuNav_N64::NAV_DOWN) { n64_moveSelection(1); }
			if (nav & MenuNav_N64::NAV_UP)   { n64_moveSelection(-1); }
			if (activate)
			{
				s_n64Zone = N64_ZONE_BUTTONS;
				s_n64Button = AGENT_BEGIN;
			}
		}
		s_lastSelectedAgent = (s_n64Zone == N64_ZONE_AGENTS);
	}
#endif

	void agentMenu_update()
	{
#ifdef __N64__
		n64_update();
		return;
#endif
		if (s_newAgentDlg)
		{
			updateNewAgentDlg();
			return;
		}
		else if (s_removeAgentDlg)
		{
			updateRemoveAgentDlg();
			return;
		}
		else if (s_quitConfirmDlg)
		{
			if (updateQuitConfirmDlg())
			{
				if (TFE_Settings::getSystemSettings()->gameQuitExitsToMenu)
				{
					TFE_FrontEndUI::exitToMenu();
				}
				else
				{
					TFE_System::postQuitMessage();
				}
			}
			return;
		}

		if (s_selectedMission < 0)
		{
			assert(0);	// This shouldn't happen.
			s_selectedMission = 0;
		}
		else if (s_selectedMission >= s_maxLevelIndex)
		{
			s_selectedMission = max(0, s_maxLevelIndex - 1);
		}

		const s32 x = s_cursorPos.x;
		const s32 z = s_cursorPos.z;
		if (TFE_Input::mousePressed(MBUTTON_LEFT))
		{
			s_buttonPressed = -1;
			for (u32 i = 0; i < AGENT_COUNT; i++)
			{
#ifdef __N64__
				// __N64__: there is no DOS to exit to, the "DOS" button is disabled.
				if (i == AGENT_EXIT) { continue; }
#endif
				if (x >= c_agentButtons[i].x && x < c_agentButtons[i].x + c_agentButtonDim.x &&
					z >= c_agentButtons[i].z && z < c_agentButtons[i].z + c_agentButtonDim.z)
				{
					s_buttonPressed = s32(i);
					s_buttonHover = JTRUE;
					break;
				}
			}

			if (s_buttonPressed < 0)
			{
				if (x >= 25 && x < 122 && z >= 35 && z < 147 && s_agentCount > 0)
				{
					s_agentId = clamp((z - 35) / 8, 0, (s32)s_agentCount - 1);
					s_selectedMission = max(0, s_agentData[s_agentId].selectedMission - 1);
					s_lastSelectedAgent = true;
				}
				else if (x >= 171 && x < 290 && z >= 35 && z < 147 && s_agentCount > 0)
				{
					s_selectedMission = clamp((z - 35) / 8, 0, (s32)s_agentData[s_agentId].nextMission - 1);
					s_agentData[s_agentId].selectedMission = s_selectedMission + 1;
					s_lastSelectedAgent = false;
				}
			}
		}
		else if (TFE_Input::mouseDown(MBUTTON_LEFT) && s_buttonPressed >= 0)
		{
			// Verify that the mouse is still over the button.
			if (x >= c_agentButtons[s_buttonPressed].x && x < c_agentButtons[s_buttonPressed].x + c_agentButtonDim.x &&
				z >= c_agentButtons[s_buttonPressed].z && z < c_agentButtons[s_buttonPressed].z + c_agentButtonDim.z)
			{
				s_buttonHover = true;
			}
			else
			{
				s_buttonHover = false;
			}
		}
		else
		{
			if (TFE_Input::keyPressed(KEY_N))
			{
				s_buttonPressed = AGENT_NEW;
				s_buttonHover = JTRUE;
			}
			else if (TFE_Input::keyPressed(s_langKeys->k_agdel))
			{
				s_buttonPressed = AGENT_REMOVE;
				s_buttonHover = JTRUE;
			}
#ifndef __N64__
			else if (TFE_Input::keyPressed(KEY_D))	// DOS
			{
				s_buttonPressed = AGENT_EXIT;
				s_buttonHover = JTRUE;
			}
#endif
			else if (TFE_Input::keyPressed(s_langKeys->k_begin) || TFE_Input::keyPressed(KEY_RETURN) || TFE_Input::keyPressed(KEY_KP_ENTER))
			{
				s_buttonPressed = AGENT_BEGIN;
				s_buttonHover = JTRUE;
			}

			// Arrow keys to change the selected agent or mission.
			if (TFE_Input::bufferedKeyDown(KEY_DOWN))
			{
				if (s_lastSelectedAgent && s_agentCount > 0)
				{
					s_agentId++;
					if (s_agentId >= (s32)s_agentCount) { s_agentId = 0; }
					if (s_agentId >= 0) { s_selectedMission = s_agentData[s_agentId].selectedMission - 1; }
				}
				else if (!s_lastSelectedAgent && s_agentId >= 0 && s_agentData[s_agentId].nextMission > 1)
				{
					s_selectedMission++;
					if (s_selectedMission > s_agentData[s_agentId].nextMission - 1 || s_selectedMission == 14) { s_selectedMission = 0; }
				}
			}
			else if (TFE_Input::bufferedKeyDown(KEY_UP))
			{
				if (s_lastSelectedAgent && s_agentCount > 0)
				{
					s_agentId--;
					if (s_agentId < 0) { s_agentId = s_agentCount - 1; }
					if (s_agentId >= 0) { s_selectedMission = s_agentData[s_agentId].selectedMission - 1; }
				}
				else if (!s_lastSelectedAgent && s_agentId >= 0 && s_agentData[s_agentId].nextMission > 1)
				{
					s_selectedMission--;
					if (s_selectedMission < 0) { s_selectedMission = s_agentData[s_agentId].nextMission - 1; }
				}
			}
			else if (TFE_Input::bufferedKeyDown(KEY_LEFT) || TFE_Input::bufferedKeyDown(KEY_RIGHT))
			{
				s_lastSelectedAgent = !s_lastSelectedAgent;
			}

			if (s_buttonPressed >= AGENT_NEW && s_buttonHover)
			{
				switch (s_buttonPressed)
				{
					case AGENT_NEW:
					{
						s_newAgentDlg = JTRUE;
						memset(s_newAgentName, 0, 32);
						s_editBox.cursor = 0;
#ifdef __N64__
						// __N64__: suggest a name, since typing one with the D-pad is slow.
						strcpy(s_newAgentName, "KYLE");
						s_editBox.cursor = 4;
#endif
						s_editBox.inputField = s_newAgentName;
						s_editBox.maxLen = 16;
					} break;
					case AGENT_REMOVE:
						s_removeAgentDlg = JTRUE;
						break;
					case AGENT_EXIT:
						s_quitConfirmDlg = JTRUE;
						break;
					case AGENT_BEGIN:
						if (s_agentCount > 0 && s_selectedMission >= 0)
						{
							s_missionBegin = JTRUE;
						}
						break;
				};
			}

			// Reset.
			s_buttonPressed = -1;
			s_buttonHover = JFALSE;
		}
	}

	void agentMenu_draw()
	{
		memset(s_framebuffer, 0, 320 * 200);
		blitDeltaFrame(&s_agentMenuFrames[0], 0, 0, s_framebuffer);
		
		// Draw the buttons
		for (s32 i = 0; i < 4; i++)
		{
			s32 index;
			if (s_newAgentDlg)
			{
				if (i == 0) { index = i * 2 + 1; }
				else { index = i * 2 + 2; }
			}
			else if (s_removeAgentDlg)
			{
				if (i == 1) { index = i * 2 + 1; }
				else { index = i * 2 + 2; }
			}
			else if (s_quitConfirmDlg)
			{
				if (i == 2) { index = i * 2 + 1; }
				else { index = i * 2 + 2; }
			}
			else
			{
#ifdef __N64__
				// The focused button is lit (its "pressed" frame has a green label).
				if (s_n64Zone == N64_ZONE_BUTTONS && s_n64Button == i) { index = i * 2 + 1; }
#else
				if (s_buttonPressed == i && s_buttonHover) { index = i * 2 + 1; }
#endif
				else { index = i * 2 + 2; }
			}
			blitDeltaFrame(&s_agentMenuFrames[index], 0, 0, s_framebuffer);
		}
#ifdef __N64__
		if (s_n64Pressing && s_n64Zone == N64_ZONE_BUTTONS && !s_newAgentDlg && !s_removeAgentDlg)
		{
			n64_darken(c_agentButtons[s_n64Button].x, c_agentButtons[s_n64Button].z, c_agentButtonDim.x, c_agentButtonDim.z);
		}
#endif
#ifdef __N64__
		// __N64__: hide the "DOS" label, the button is disabled.
		{
			const Vec2i dos = c_agentButtons[AGENT_EXIT];
			eraseButtonLabel(s_framebuffer, dos.x + 20, dos.z + 7, dos.x + 44, dos.z + 18, dos.z + 5);
		}
#endif

#ifdef __N64__
		// The bar of the list with the focus is bright, both are dark while on the buttons.
		const u8 agentBar   = (s_n64Zone == N64_ZONE_AGENTS) ? 12 : 13;
		const u8 missionBar = (s_n64Zone == N64_ZONE_MISSIONS) ? 12 : 13;
#else
		const u8 agentBar   = s_lastSelectedAgent ? 12 : 13;
		const u8 missionBar = s_lastSelectedAgent ? 13 : 12;
#endif
		// 11 = easy, 12 = medium, 13 = hard
		s32 yOffset = -20;
		s32 textX = 174;
		s32 textY = 36;
		u8 color = 47;
		if (s_agentId >= 0 && s_agentCount > 0)
		{
			s32 nextMission = min(s_agentData[s_agentId].nextMission, s_maxLevelIndex);

			for (s32 i = 0; i < nextMission - 1 && i < s_maxLevelIndex; i++)
			{
				color = 47;
				if (s_selectedMission == i)
				{
					color = 32;
					drawColoredQuad(textX - 3, textY - 1, 118, 9, missionBar, s_framebuffer);
				}

				print(s_levelDisplayNames[i], textX, textY, color, s_framebuffer);
				s32 diff = s_agentData[s_agentId].completed[i] + 11;
				blitDeltaFrame(&s_agentMenuFrames[diff], 0, yOffset, s_framebuffer);
				yOffset += 8;
				textY += 8;
			}
			color = 47;
			if (s_selectedMission == nextMission - 1)
			{
				color = 32;
				drawColoredQuad(textX - 3, textY - 1, 118, 9, missionBar, s_framebuffer);
			}
			print(s_levelDisplayNames[nextMission - 1], textX, textY, color, s_framebuffer);

			// Show final level complete.
			if (s_agentData[s_agentId].nextMission > s_maxLevelIndex)
			{
				yOffset = -20 + 8 * (s_maxLevelIndex - 1);
				s32 diff = s_agentData[s_agentId].completed[s_maxLevelIndex-1] + 11;
				blitDeltaFrame(&s_agentMenuFrames[diff], 0, yOffset, s_framebuffer);
			}
		}

		// Draw agents.
		textX = 28;
		textY = 36;
		for (s32 i = 0; i < s_agentCount; i++)
		{
			color = 47;
			if (s_agentId == i)
			{
				color = 32;
				drawColoredQuad(textX - 3, textY - 1, 118, 9, agentBar, s_framebuffer);
			}
			print(s_agentData[i].name, textX, textY, color, s_framebuffer);
			textY += 8;
		}

		if (s_newAgentDlg)
		{
			drawNewAgentDlg();
		}
		else if (s_removeAgentDlg)
		{
			drawYesNoDlg(0);
		}
		else if (s_quitConfirmDlg)
		{
			drawYesNoDlg(1);
		}
	}

	void setPalette()
	{
		// Update the palette.
		u32 palette[256];
		u32* outColor = palette;
		u8* srcColor = s_menuPalette;
		for (s32 i = 0; i < 256; i++, outColor++, srcColor += 3)
		{
			*outColor = u32(srcColor[0]) | (u32(srcColor[1]) << 8u) | (u32(srcColor[2]) << 16u) | (0xffu << 24u);
		}
		vfb_setPalette(palette);
	}

	// TFE Specific.
	void agentMenu_startupDisplay()
	{
		s_framebuffer = menu_startupDisplay();
		menu_resetCursor();

		setPalette();
	}

	void agentMenu_load(LangHotkeys* langKeys)
	{
		FilePath filePath;
		if (!TFE_Paths::getFilePath("AGENTMNU.LFD", &filePath)) { return; }
		Archive* archive = Archive::getArchive(ARCHIVE_LFD, "AGENTMNU", filePath.path);
		TFE_Paths::addLocalArchive(archive);

		loadPaletteFromPltt("agentmnu.pltt", s_menuPalette);
#ifndef __N64__	// __N64__: loaded on demand, see n64_loadFrames().
		s_agentMenuCount = getFramesFromAnim("agentmnu.anim", &s_agentMenuFrames);
		s_agentDlgCount = getFramesFromAnim("agentdlg.anim", &s_agentDlgFrames);
#endif
		getFrameFromDelt("cursor.delt", &s_cursor);
		TFE_Paths::removeLastArchive();
		
		s_langKeys = langKeys;
	}

#ifdef __N64__
	static void n64_loadFrames()
	{
		if (s_framesLoaded) { return; }

		FilePath filePath;
		if (!TFE_Paths::getFilePath("AGENTMNU.LFD", &filePath)) { return; }
		Archive* archive = Archive::getArchive(ARCHIVE_LFD, "AGENTMNU", filePath.path);
		TFE_Paths::addLocalArchive(archive);
		s_agentMenuCount = getFramesFromAnim("agentmnu.anim", &s_agentMenuFrames);
		s_agentDlgCount = getFramesFromAnim("agentdlg.anim", &s_agentDlgFrames);
		TFE_Paths::removeLastArchive();
		s_framesLoaded = JTRUE;
	}
#endif
		
	void agentMenu_startup()
	{
		s_agentCount = 0;
		for (s32 i = 0; i < MAX_AGENT_COUNT; i++)
		{
			if (!s_agentData[i].name[0])
			{
				break;
			}
			s_agentCount++;
		}
		// Clear out extra data.
		for (s32 i = s_agentCount; i < MAX_AGENT_COUNT; i++)
		{
			memset(&s_agentData[i], 0, sizeof(AgentData));
		}

		if (s_agentCount > 0)
		{
			s_agentId = 0;
			s_selectedMission = max(0, s_agentData[s_agentId].selectedMission - 1);
		}
		else
		{
			s_agentId = -1;
			s_selectedMission = 0;
		}

		s_missionBegin = JFALSE;
	}
		
	void agentMenu_blit()
	{
		setPalette();
#ifndef __N64__	// __N64__: no cursor, see n64_update().
		menu_blitCursor(s_cursorPos.x, s_cursorPos.z, s_framebuffer);
#endif
		menu_blitToScreen(s_framebuffer);
	}

	void agentMenu_createNewAgent()
	{
		if (s_agentCount < MAX_AGENT_COUNT)
		{
			s_agentId = s_agentCount;
			s_agentCount++;
			agent_createNewAgent(s_agentId, &s_agentData[s_agentId], s_newAgentName);

			s_selectedMission = 0;
		}
	}

	void agentMenu_removeAgent(s32 index)
	{
		if (s_agentCount < 1)
		{
			return;
		}

		for (s32 i = index; i < (s32)s_agentCount; i++)
		{
			s_agentData[i] = s_agentData[i + 1];
		}
		s_agentCount--;
		memset(&s_agentData[s_agentCount], 0, sizeof(AgentData));
		
		s_agentId = s_agentCount > 0 ? 0 : -1;
		s_selectedMission = s_agentCount > 0 ? s_agentData[0].selectedMission : 0;
	}

	// UI routines.
	void drawYesNoButtons(u32 indexNo, u32 indexYes)
	{
#ifdef __N64__
		if (s_n64Pressing && s_buttonHover && s_buttonPressed >= 0)
		{
			blitDeltaFrame(&s_agentDlgFrames[s_buttonPressed == 0 ? indexNo : indexYes], 0, 0, s_framebuffer);
			n64_darken(c_newAgentButtons[s_buttonPressed].x, c_newAgentButtons[s_buttonPressed].z, c_newAgentButtonDim.x, c_newAgentButtonDim.z);
			return;
		}
#endif
		if (s_buttonPressed == 0 && s_buttonHover)
		{
			blitDeltaFrame(&s_agentDlgFrames[indexNo], 0, 0, s_framebuffer);
		}
		else if (s_buttonPressed == 1 && s_buttonHover)
		{
			blitDeltaFrame(&s_agentDlgFrames[indexYes], 0, 0, s_framebuffer);
		}
	}

	void drawYesNoDlg(s32 baseFrame)
	{
		blitDeltaFrame(&s_agentDlgFrames[baseFrame], 0, 0, s_framebuffer);
		drawYesNoButtons(4, 6);
	}

	void drawNewAgentDlg()
	{
		blitDeltaFrame(&s_agentDlgFrames[2], 0, 0, s_framebuffer);
		drawEditBox(&s_editBox, 106, 87, 216, 100, s_framebuffer);
		drawYesNoButtons(8, 10);
	}

	void updateNewAgentDlg()
	{
		updateEditBox(&s_editBox);

		if (TFE_Input::keyPressed(KEY_RETURN) || TFE_Input::keyPressed(KEY_KP_ENTER))
		{
			agentMenu_createNewAgent();
			s_newAgentDlg = JFALSE;
			return;
		}
		else if (TFE_Input::keyPressed(KEY_ESCAPE))
		{
			s_newAgentDlg = JFALSE;
			return;
		}

		if (TFE_Input::mousePressed(MBUTTON_LEFT))
		{
			s_buttonPressed = -1;
			for (u32 i = 0; i < NEW_AGENT_COUNT; i++)
			{
				if (s_cursorPos.x >= c_newAgentButtons[i].x && s_cursorPos.x < c_newAgentButtons[i].x + c_newAgentButtonDim.x &&
					s_cursorPos.z >= c_newAgentButtons[i].z && s_cursorPos.z < c_newAgentButtons[i].z + c_newAgentButtonDim.z)
				{
					s_buttonPressed = s32(i);
					s_buttonHover = true;
					break;
				}
			}
		}
		else if (TFE_Input::mouseDown(MBUTTON_LEFT) && s_buttonPressed >= 0)
		{
			// Verify that the mouse is still over the button.
			if (s_cursorPos.x >= c_newAgentButtons[s_buttonPressed].x && s_cursorPos.x < c_newAgentButtons[s_buttonPressed].x + c_newAgentButtonDim.x &&
				s_cursorPos.z >= c_newAgentButtons[s_buttonPressed].z && s_cursorPos.z < c_newAgentButtons[s_buttonPressed].z + c_newAgentButtonDim.z)
			{
				s_buttonHover = true;
			}
			else
			{
				s_buttonHover = false;
			}
		}
		else
		{
			// Activate button 's_escButtonPressed'
			if (s_buttonPressed >= NEW_AGENT_NO && s_buttonHover)
			{
				if (s_buttonPressed == NEW_AGENT_YES)
				{
					agentMenu_createNewAgent();
				}
				s_newAgentDlg = JFALSE;
			}
			// Reset.
			s_buttonPressed = -1;
			s_buttonHover = false;
		}
	}

	void updateRemoveAgentDlg()
	{
		if (TFE_Input::keyPressed(KEY_RETURN) || TFE_Input::keyPressed(KEY_KP_ENTER) || TFE_Input::keyPressed(KEY_ESCAPE))
		{
			s_removeAgentDlg = false;
			return;
		}

		if (TFE_Input::mousePressed(MBUTTON_LEFT))
		{
			s_buttonPressed = -1;
			for (u32 i = 0; i < NEW_AGENT_COUNT; i++)
			{
				if (s_cursorPos.x >= c_newAgentButtons[i].x && s_cursorPos.x < c_newAgentButtons[i].x + c_newAgentButtonDim.x &&
					s_cursorPos.z >= c_newAgentButtons[i].z && s_cursorPos.z < c_newAgentButtons[i].z + c_newAgentButtonDim.z)
				{
					s_buttonPressed = s32(i);
					s_buttonHover = true;
					break;
				}
			}
		}
		else if (TFE_Input::mouseDown(MBUTTON_LEFT) && s_buttonPressed >= 0)
		{
			// Verify that the mouse is still over the button.
			if (s_cursorPos.x >= c_newAgentButtons[s_buttonPressed].x && s_cursorPos.x < c_newAgentButtons[s_buttonPressed].x + c_newAgentButtonDim.x &&
				s_cursorPos.z >= c_newAgentButtons[s_buttonPressed].z && s_cursorPos.z < c_newAgentButtons[s_buttonPressed].z + c_newAgentButtonDim.z)
			{
				s_buttonHover = true;
			}
			else
			{
				s_buttonHover = false;
			}
		}
		else
		{
			if (TFE_Input::keyPressed(s_langKeys->k_yes))
			{
				agentMenu_removeAgent(s_agentId);
				s_removeAgentDlg = false;
			}
			else if (TFE_Input::keyPressed(KEY_N))
			{
				s_removeAgentDlg = false;
			}
			else if (s_buttonPressed >= NEW_AGENT_NO && s_buttonHover)
			{
				if (s_buttonPressed == NEW_AGENT_YES)
				{
					agentMenu_removeAgent(s_agentId);
				}
				s_removeAgentDlg = false;
			}
			// Reset.
			s_buttonPressed = -1;
			s_buttonHover = false;
		}
	}

	JBool updateQuitConfirmDlg()
	{
		JBool quit = JFALSE;
		if (TFE_Input::keyPressed(KEY_ESCAPE))
		{
			s_quitConfirmDlg = JFALSE;
			return JFALSE;
		}
		else if (TFE_Input::keyPressed(KEY_RETURN) || TFE_Input::keyPressed(KEY_KP_ENTER))
		{
			s_quitConfirmDlg = JFALSE;
			return JTRUE;
		}

		if (TFE_Input::mousePressed(MBUTTON_LEFT))
		{
			s_buttonPressed = -1;
			for (u32 i = 0; i < NEW_AGENT_COUNT; i++)
			{
				if (s_cursorPos.x >= c_newAgentButtons[i].x && s_cursorPos.x < c_newAgentButtons[i].x + c_newAgentButtonDim.x &&
					s_cursorPos.z >= c_newAgentButtons[i].z && s_cursorPos.z < c_newAgentButtons[i].z + c_newAgentButtonDim.z)
				{
					s_buttonPressed = s32(i);
					s_buttonHover = JTRUE;
					break;
				}
			}
		}
		else if (TFE_Input::mouseDown(MBUTTON_LEFT) && s_buttonPressed >= 0)
		{
			// Verify that the mouse is still over the button.
			if (s_cursorPos.x >= c_newAgentButtons[s_buttonPressed].x && s_cursorPos.x < c_newAgentButtons[s_buttonPressed].x + c_newAgentButtonDim.x &&
				s_cursorPos.z >= c_newAgentButtons[s_buttonPressed].z && s_cursorPos.z < c_newAgentButtons[s_buttonPressed].z + c_newAgentButtonDim.z)
			{
				s_buttonHover = JTRUE;
			}
			else
			{
				s_buttonHover = JFALSE;
			}
		}
		else
		{
			// Activate button 's_escButtonPressed'
			if (TFE_Input::keyPressed(s_langKeys->k_yes))
			{
				quit = JTRUE;
				s_quitConfirmDlg = JFALSE;
			}
			else if (TFE_Input::keyPressed(KEY_N))
			{
				s_quitConfirmDlg = JFALSE;
			}
			else if (s_buttonPressed >= NEW_AGENT_NO && s_buttonHover)
			{
				if (s_buttonPressed == NEW_AGENT_YES)
				{
					quit = JTRUE;
				}
				s_quitConfirmDlg = JFALSE;
			}
			// Reset.
			s_buttonPressed = -1;
			s_buttonHover = false;
		}

		return quit;
	}
}