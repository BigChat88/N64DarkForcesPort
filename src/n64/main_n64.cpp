// Entry point for the N64 port of The Force Engine.
// Based on tfe/amiga/main_amiga.cpp.
#include <TFE_System/types.h>
#include <TFE_Memory/memoryRegion.h>
#include <TFE_Game/igame.h>
#include <TFE_Game/saveSystem.h>
#include <TFE_FileSystem/paths.h>
#include <TFE_Audio/audioSystem.h>
#include <TFE_Audio/midiPlayer.h>
#include <TFE_RenderBackend/renderBackend.h>
#include <TFE_Input/inputMapping.h>
#include <TFE_Settings/settings.h>
#include <TFE_System/system.h>
#include <TFE_Jedi/Task/task.h>
#include <TFE_Jedi/IMuse/imuse.h>
#include <TFE_FrontEndUI/frontEndUi.h>
#include "audio_n64.h"
#include "debug_n64.h"
#include "savefs_n64.h"
#include "menunav_n64.h"
#include "intro_n64.h"
#include <TFE_DarkForces/automap.h>
#include <TFE_DarkForces/GameUI/pda.h>

#include <libdragon.h>

using namespace TFE_Input;

// The ROM filesystem holds the original game files (see gamedata/); the agent
// progress and settings are written to the cartridge SRAM through "save:/".
static const char* c_romPath = "rom:/";
static const char* c_savePath = "save:/";

// Virtual display size used by the classic renderer.
enum
{
	GAME_WIDTH  = 320,
	GAME_HEIGHT = 200,
};

// Stick values reported by a standard controller rarely exceed this.
static const f32 c_stickRange = 80.0f;
// Worn sticks do not return exactly to the center; smaller values are ignored.
static const f32 c_stickDeadzone = 8.0f;

static IGame* s_curGame = nullptr;
static f32 s_cursorX = GAME_WIDTH / 2;
static f32 s_cursorY = GAME_HEIGHT / 2;

// Controls: Z fire, B run, A jump, L automap, Start menu, C-up center view,
// C-down crouch, C-left/C-right strafe, D-pad left/right weapons, D-pad down headlamp.
// R is both "use" (when tapped on its own, see updateInput()) and a modifier:
// holding R, Z is secondary fire, Start the PDA, C-up/C-down look up/down, and the
// D-pad toggles goggles/gas mask/cleats or centers the view. While the automap
// is shown, R + D-pad up/down zooms it in/out and left/right changes layer.
//
// Buttons that do not depend on R are bound as controller buttons. The rest are
// resolved in updateInput() and sent as virtual keys, which never exist on a
// real keyboard, so TFE can bind each combination to its own action.
enum VirtualKey
{
	VK_JUMP        = KEY_F13,
	VK_CROUCH      = KEY_F14,
	VK_LOOK_UP     = KEY_F15,
	VK_LOOK_DOWN   = KEY_F16,
	VK_STRAFE_LEFT = KEY_F17,
	VK_STRAFE_RIGHT= KEY_F18,
	VK_MAP_ZOOM_OUT= KEY_F19,
	VK_MAP_ZOOM_IN = KEY_F20,
	VK_WEAPON_PREV = KEY_F21,
	VK_WEAPON_NEXT = KEY_F22,
	VK_PDA         = KEY_F23,
	VK_HEADLAMP    = KEY_F24,
	VK_GOGGLES     = KEY_KP_1,
	VK_GAS_MASK    = KEY_KP_2,
	VK_CLEATS      = KEY_KP_3,
	VK_CENTER_VIEW = KEY_KP_4,
	VK_MENU        = KEY_KP_5,
	VK_FIRE        = KEY_KP_6,
	VK_FIRE_SECOND = KEY_KP_7,
	VK_MAP_LAYER_DOWN = KEY_KP_8,
	VK_MAP_LAYER_UP   = KEY_KP_9,
	VK_USE         = KEY_KP_0,
};

static InputBinding s_n64Binds[] =
{
	{ IADF_RUN,            ITYPE_CONTROLLER, CONTROLLER_BUTTON_B },
	{ IADF_AUTOMAP,        ITYPE_CONTROLLER, CONTROLLER_BUTTON_LEFTSHOULDER },	// L

	{ IADF_USE,              ITYPE_KEYBOARD, VK_USE },
	{ IADF_JUMP,             ITYPE_KEYBOARD, VK_JUMP },
	{ IADF_CROUCH,           ITYPE_KEYBOARD, VK_CROUCH },
	{ IADF_LOOK_UP,          ITYPE_KEYBOARD, VK_LOOK_UP },
	{ IADF_LOOK_DN,          ITYPE_KEYBOARD, VK_LOOK_DOWN },
	{ IADF_STRAFE_LT,        ITYPE_KEYBOARD, VK_STRAFE_LEFT },
	{ IADF_STRAFE_RT,        ITYPE_KEYBOARD, VK_STRAFE_RIGHT },
	{ IADF_MAP_ZOOM_OUT,     ITYPE_KEYBOARD, VK_MAP_ZOOM_OUT },
	{ IADF_MAP_ZOOM_IN,      ITYPE_KEYBOARD, VK_MAP_ZOOM_IN },
	{ IADF_CYCLEWPN_PREV,    ITYPE_KEYBOARD, VK_WEAPON_PREV },
	{ IADF_CYCLEWPN_NEXT,    ITYPE_KEYBOARD, VK_WEAPON_NEXT },
	{ IADF_PDA_TOGGLE,       ITYPE_KEYBOARD, VK_PDA },
	{ IADF_HEAD_LAMP_TOGGLE, ITYPE_KEYBOARD, VK_HEADLAMP },
	{ IADF_NIGHT_VISION_TOG, ITYPE_KEYBOARD, VK_GOGGLES },
	{ IADF_GAS_MASK_TOGGLE,  ITYPE_KEYBOARD, VK_GAS_MASK },
	{ IADF_CLEATS_TOGGLE,    ITYPE_KEYBOARD, VK_CLEATS },
	{ IADF_CENTER_VIEW,      ITYPE_KEYBOARD, VK_CENTER_VIEW },
	{ IADF_MENU_TOGGLE,      ITYPE_KEYBOARD, VK_MENU },
	{ IADF_PRIMARY_FIRE,     ITYPE_KEYBOARD, VK_FIRE },
	{ IADF_SECONDARY_FIRE,   ITYPE_KEYBOARD, VK_FIRE_SECOND },
	{ IADF_MAP_LAYER_DN,     ITYPE_KEYBOARD, VK_MAP_LAYER_DOWN },
	{ IADF_MAP_LAYER_UP,     ITYPE_KEYBOARD, VK_MAP_LAYER_UP },
};

static void setN64Binds()
{
	// Replace the desktop keyboard/controller defaults with the N64 layout.
	InputConfig* config = inputMapping_get();
	while (config->bindCount)
	{
		inputMapping_removeBinding(config->bindCount - 1);
	}
	for (s32 i = 0; i < TFE_ARRAYSIZE(s_n64Binds); i++)
	{
		inputMapping_addBinding(&s_n64Binds[i]);
	}

	// Stick X turns and stick Y moves (GoldenEye style); strafing is on the C buttons.
	config->axis[AA_LOOK_HORZ] = AXIS_RIGHT_X;
	config->axis[AA_LOOK_VERT] = AXIS_RIGHT_Y;
	config->axis[AA_MOVE] = AXIS_LEFT_Y;
	config->axis[AA_STRAFE] = AXIS_LEFT_X;
	// The deadzone is applied in stickToGameAxis(), together with the response curve.
	config->ctrlDeadzone[0] = 0.0f;
	config->ctrlDeadzone[1] = 0.0f;
}

static void setButton(Button button, bool down)
{
	if (down) { TFE_Input::setButtonDown(button); }
	else { TFE_Input::setButtonUp(button); }
}

static void setKey(KeyboardCode key, bool down)
{
	if (down) { TFE_Input::setKeyDown(key); }
	else { TFE_Input::setKeyUp(key); }
}

static void setVirtualKey(VirtualKey key, bool down)
{
	setKey((KeyboardCode)key, down);
}

static f32 stickToAxis(s8 value)
{
	f32 axis = (f32)value / c_stickRange;
	if (axis > 1.0f) { axis = 1.0f; }
	if (axis < -1.0f) { axis = -1.0f; }
	return axis;
}

// Gameplay response of the stick, like modern analog controls: past the deadzone
// the remaining travel is rescaled to 0..1, so the smallest push already responds.
// Moving and strafing are proportional (a light push walks slowly), turning uses a
// squared curve: slow and precise near the center, full speed at the edge.
static f32 stickToGameAxis(s8 value, bool squared)
{
	const f32 mag = fabsf((f32)value);
	if (mag <= c_stickDeadzone) { return 0.0f; }

	f32 axis = (mag - c_stickDeadzone) / (c_stickRange - c_stickDeadzone);
	if (axis > 1.0f) { axis = 1.0f; }
	if (squared) { axis *= axis; }
	return value < 0 ? -axis : axis;
}

static void updateInput(bool inGame)
{
	joypad_poll();
	joypad_inputs_t in = joypad_get_inputs(JOYPAD_PORT_1);
	joypad_buttons_t pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);

	// Buttons held while switching between gameplay and menus are ignored until
	// released, otherwise holding Start opens the escape menu and then closes it
	// again (in menus Start is also mapped to Escape).
	static bool s_prevInGame = false;
	static u16 s_latchedButtons = 0;
	if (inGame != s_prevInGame)
	{
		s_latchedButtons = in.btn.raw;
		s_prevInGame = inGame;
	}
	s_latchedButtons &= in.btn.raw;
	in.btn.raw &= ~s_latchedButtons;
	pressed.raw &= ~s_latchedButtons;

	// L + R + C-down toggles the development overlay.
	if (in.btn.l && in.btn.r && pressed.c_down)
	{
		Debug_N64::toggleOverlay();
	}

	setButton(CONTROLLER_BUTTON_A, in.btn.a);
	setButton(CONTROLLER_BUTTON_B, in.btn.b);
	setButton(CONTROLLER_BUTTON_LEFTSHOULDER, in.btn.l);
	setButton(CONTROLLER_BUTTON_START, in.btn.start);
	// The D-pad also edits text in menus (see editBox.cpp).
	setButton(CONTROLLER_BUTTON_DPAD_UP, in.btn.d_up);
	setButton(CONTROLLER_BUTTON_DPAD_DOWN, in.btn.d_down);
	setButton(CONTROLLER_BUTTON_DPAD_LEFT, in.btn.d_left);
	setButton(CONTROLLER_BUTTON_DPAD_RIGHT, in.btn.d_right);

	// Actions that depend on the R modifier (released outside of gameplay).
	const bool r = inGame && in.btn.r;
	const bool n = inGame && !in.btn.r;
	const bool mapShown = TFE_DarkForces::s_drawAutomap;
	const bool mapR = r && mapShown;	// R + D-pad controls the automap while it is shown
	const bool itemR = r && !mapShown;
	// C-down pressed before R keeps crouching and leaves R free for "use";
	// R pressed before C-down looks down instead.
	static bool s_crouchHeld = false;
	s_crouchHeld = inGame && in.btn.c_down && (s_crouchHeld || !in.btn.r);
	setVirtualKey(VK_FIRE,         n && in.btn.z);
	setVirtualKey(VK_FIRE_SECOND,  r && in.btn.z);
	setVirtualKey(VK_MENU,         n && in.btn.start);
	// The PDA pauses the game, so it must also close from "menu" mode.
	setVirtualKey(VK_PDA,          in.btn.r && in.btn.start);
	setVirtualKey(VK_JUMP,         inGame && in.btn.a);
	setVirtualKey(VK_CROUCH,       s_crouchHeld);
	setVirtualKey(VK_STRAFE_LEFT,  inGame && in.btn.c_left);
	setVirtualKey(VK_STRAFE_RIGHT, inGame && in.btn.c_right);
	setVirtualKey(VK_WEAPON_PREV,  n && in.btn.d_left);
	setVirtualKey(VK_WEAPON_NEXT,  n && in.btn.d_right);
	setVirtualKey(VK_HEADLAMP,     n && in.btn.d_down);
	setVirtualKey(VK_LOOK_UP,      r && in.btn.c_up);
	setVirtualKey(VK_LOOK_DOWN,    r && in.btn.c_down && !s_crouchHeld);
	setVirtualKey(VK_MAP_ZOOM_IN,  mapR && in.btn.d_up);
	setVirtualKey(VK_MAP_ZOOM_OUT, mapR && in.btn.d_down);
	setVirtualKey(VK_MAP_LAYER_DOWN, mapR && in.btn.d_left);
	setVirtualKey(VK_MAP_LAYER_UP,   mapR && in.btn.d_right);
	setVirtualKey(VK_GOGGLES,      itemR && in.btn.d_up);
	setVirtualKey(VK_GAS_MASK,     itemR && in.btn.d_down);
	setVirtualKey(VK_CLEATS,       itemR && in.btn.d_left);
	setVirtualKey(VK_CENTER_VIEW,  (n && in.btn.c_up) || (itemR && in.btn.d_right));

	// R alone is "use": it fires when R is released without having been combined
	// with another button, so the R combinations never trigger it by accident.
	// The key is held for two input updates so the game always sees the press.
	static bool s_prevR = false;
	static bool s_rCombined = false;
	static s32 s_usePulse = 0;
	if (in.btn.r && !s_prevR) { s_rCombined = false; }
	if (in.btn.r && (in.btn.z || in.btn.start || in.btn.c_up || (in.btn.c_down && !s_crouchHeld) ||
		in.btn.d_up || in.btn.d_down || in.btn.d_left || in.btn.d_right))
	{
		s_rCombined = true;
	}
	if (!in.btn.r && s_prevR && inGame && !s_rCombined) { s_usePulse = 2; }
	s_prevR = in.btn.r;
	setVirtualKey(VK_USE, s_usePulse > 0);
	if (s_usePulse > 0) { s_usePulse--; }

	// Console style navigation for the menus that support it (escape menu, briefing).
	if (inGame) { MenuNav_N64::update(false, false, false, false, 0, 0); }
	else { MenuNav_N64::update(in.btn.d_up, in.btn.d_down, in.btn.d_left, in.btn.d_right, in.stick_x, in.stick_y); }

	if (inGame)
	{
		TFE_Input::setAxis(AXIS_RIGHT_X, stickToGameAxis(in.stick_x, true));	// turn
		TFE_Input::setAxis(AXIS_LEFT_Y, stickToGameAxis(in.stick_y, false));	// move
		TFE_Input::setAxis(AXIS_LEFT_X, 0.0f);
		TFE_Input::setAxis(AXIS_RIGHT_Y, 0.0f);
		setKey(KEY_ESCAPE, false);
		setKey(KEY_RETURN, false);
		TFE_Input::setMouseButtonUp(MBUTTON_LEFT);
		TFE_Input::setRelativeMousePos(0, 0);
		return;
	}

	TFE_Input::setAxis(AXIS_RIGHT_X, 0.0f);
	TFE_Input::setAxis(AXIS_LEFT_Y, 0.0f);

	// The PDA is driven with the controller, without the cursor, through the keys it
	// handles on the PC: the stick or D-pad pans the map (scrolls the briefing),
	// C-up/C-down zoom, C-left/C-right change the map layer and L/R change the page.
	// Start or B closes it.
	const bool pda = TFE_DarkForces::pda_isOpen();
	const s32 c_pdaStick = 30;
	setKey(KEY_UP,           pda && (in.btn.d_up    || in.stick_y >  c_pdaStick));
	setKey(KEY_DOWN,         pda && (in.btn.d_down  || in.stick_y < -c_pdaStick));
	setKey(KEY_LEFT,         pda && (in.btn.d_left  || in.stick_x < -c_pdaStick));
	setKey(KEY_RIGHT,        pda && (in.btn.d_right || in.stick_x >  c_pdaStick));
	setKey(KEY_EQUALS,       pda && in.btn.c_up);
	setKey(KEY_MINUS,        pda && in.btn.c_down);
	setKey(KEY_LEFTBRACKET,  pda && in.btn.c_left);
	setKey(KEY_RIGHTBRACKET, pda && in.btn.c_right);
	// Space is the next page, Shift + Space the previous one.
	setKey(KEY_LSHIFT,       pda && in.btn.l);
	setKey(KEY_SPACE,        pda && (in.btn.l || in.btn.r));
	if (pda)
	{
		TFE_Input::setMouseButtonUp(MBUTTON_LEFT);
		setKey(KEY_ESCAPE, in.btn.start || in.btn.b);
		setKey(KEY_RETURN, false);
		return;
	}

	// The other menus (agent menu, briefings, escape menu) are mouse driven:
	// the stick moves a virtual cursor and A clicks.
	s_cursorX += stickToAxis(in.stick_x) * 4.0f;
	s_cursorY -= stickToAxis(in.stick_y) * 4.0f;
	if (s_cursorX < 0.0f) { s_cursorX = 0.0f; }
	if (s_cursorY < 0.0f) { s_cursorY = 0.0f; }
	if (s_cursorX > GAME_WIDTH - 1) { s_cursorX = GAME_WIDTH - 1; }
	if (s_cursorY > GAME_HEIGHT - 1) { s_cursorY = GAME_HEIGHT - 1; }
	TFE_Input::setMousePos((s32)s_cursorX, (s32)s_cursorY);

	if (in.btn.a) { TFE_Input::setMouseButtonDown(MBUTTON_LEFT); }
	else { TFE_Input::setMouseButtonUp(MBUTTON_LEFT); }

	// Start backs out of menus, B confirms.
	setKey(KEY_ESCAPE, in.btn.start);
	setKey(KEY_RETURN, in.btn.b);
	if (pressed.start) { TFE_Input::setBufferedKey(KEY_ESCAPE); }
	if (pressed.b) { TFE_Input::setBufferedKey(KEY_RETURN); }
}

static void setupPaths()
{
	TFE_Paths::setPath(PATH_PROGRAM, c_romPath);
	TFE_Paths::setPath(PATH_PROGRAM_DATA, c_savePath);
	TFE_Paths::setPath(PATH_USER_DOCUMENTS, c_savePath);
	TFE_Paths::setPath(PATH_SOURCE_DATA, c_romPath);
}

namespace MemReserve_N64
{
	void acquire();
}

int main(void)
{
	// Held back for the out-of-memory report (see malloc_n64.cpp).
	MemReserve_N64::acquire();
	debug_init_isviewer();
	debug_init_usblog();
	dfs_init(DFS_DEFAULT_LOCATION);
	SaveFS_N64::init();
	joypad_init();
	timer_init();

	if (!is_memory_expanded())
	{
		console_init();
		printf("Dark Forces 64 requires the\nExpansion Pak (8 MB).\n");
		console_render();
		while (1) {}
	}

#ifndef N64_START_LEVEL	// level test builds skip it
	Intro_N64::play();
#endif

	setupPaths();
	TFE_System::logOpen("the_force_engine_log.txt");
	TFE_System::logWrite(LOG_MSG, "Main", "The Force Engine (N64)");

	// A failure to save the default settings (e.g. SRAM full) is not fatal.
	bool firstRun;
	TFE_Settings::init(firstRun);
	setupPaths();

	TFE_Settings_Window* windowSettings = TFE_Settings::getWindowSettings();
	TFE_Settings_Graphics* graphics = TFE_Settings::getGraphicsSettings();
	TFE_System::init(60.0f, true, "");

	graphics->gameResolution.x = GAME_WIDTH;
	graphics->gameResolution.z = GAME_HEIGHT;
	graphics->widescreen = false;
	graphics->rendererIndex = 0;

	WindowState windowState = {};
	windowState.width = GAME_WIDTH;
	windowState.height = GAME_HEIGHT;
	windowState.flags = WINFLAG_FULLSCREEN | WINFLAG_VSYNC;
	strcpy(windowState.name, "The Force Engine");
	if (!TFE_RenderBackend::init(windowState))
	{
		TFE_System::logWrite(LOG_CRITICAL, "GPU", "Cannot initialize the display.");
		return 1;
	}

	TFE_FrontEndUI::initConsole();
	TFE_MidiPlayer::init(TFE_Settings::getSoundSettings()->midiOutput, (MidiDeviceType)TFE_Settings::getSoundSettings()->midiType);
	TFE_Audio::init(false, TFE_Settings::getSoundSettings()->audioDevice);
	TFE_FrontEndUI::init();
	game_init();

	inputMapping_resetToDefaults();
	setN64Binds();
	TFE_SaveSystem::init();

	TFE_Game* gameInfo = TFE_Settings::getGame();
	s_curGame = createGame(gameInfo->id);
	TFE_SaveSystem::setCurrentGame(s_curGame);
#ifdef N64_START_LEVEL
	// Testing: skip the menus and cutscenes and start in the given level.
	const char* args[] = { "darkforces64", "-l" N64_START_LEVEL };
	const s32 argCount = 2;
#else
	const char** args = nullptr;
	const s32 argCount = 0;
#endif
	if (!s_curGame || !s_curGame->runGame(argCount, args, nullptr))
	{
		TFE_System::logWrite(LOG_ERROR, "AppMain", "Cannot run game '%s'.", gameInfo->game);
		console_init();
		printf("Cannot start Dark Forces.\nAre the game files in gamedata/?\n");
		console_render();
		while (1) {}
	}

	TFE_System::logWrite(LOG_MSG, "Progam Flow", "The Force Engine Game Loop Started");
	while (!TFE_System::quitMessagePosted())
	{
		const bool inGame = s_curGame->canSave() && !s_curGame->isPaused();
		TFE_Input::enableRelativeMode(inGame);
		updateInput(inGame);
		inputMapping_updateInput();

		TFE_System::update();
		TFE_SaveSystem::update();
		s_curGame->loopGame();
		const bool endInputFrame = TFE_Jedi::task_run() != 0;
		TFE_Audio::n64_update();

		TFE_RenderBackend::swap(true);

		if (endInputFrame)
		{
			TFE_Input::endFrame();
			inputMapping_endFrame();
			MenuNav_N64::endFrame();
		}
	}

	freeGame(s_curGame);
	game_destroy();
	TFE_System::logWrite(LOG_MSG, "Progam Flow", "The Force Engine Game Loop Ended.");
	TFE_System::logClose();
	return 0;
}
