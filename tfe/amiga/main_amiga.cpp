// main.cpp : Defines the entry point for the application.
//#include "version.h"
#include <TFE_System/types.h>
//#include <TFE_System/profiler.h>
#include <TFE_Memory/memoryRegion.h>
#include <TFE_Archive/gobArchive.h>
#include <TFE_Game/igame.h>
#include <TFE_Game/saveSystem.h>
//#include <TFE_Game/reticle.h>
#include <TFE_Jedi/InfSystem/infSystem.h>
//#include <TFE_Editor/editor.h>
#include <TFE_FileSystem/fileutil.h>
#include <TFE_Audio/audioSystem.h>
#include <TFE_FileSystem/paths.h>
//#include <TFE_Polygon/polygon.h>
#include <TFE_RenderBackend/renderBackend.h>
#include <TFE_Input/inputMapping.h>
#include <TFE_Settings/settings.h>
#include <TFE_System/system.h>
//#include <TFE_System/CrashHandler/crashHandler.h>
//#include <TFE_System/frameLimiter.h>
#include <TFE_System/tfeMessage.h>
#include <TFE_Jedi/Task/task.h>
//#include <TFE_RenderShared/texturePacker.h>
//#include <TFE_Asset/paletteAsset.h>
//#include <TFE_Asset/imageAsset.h>
//#include <TFE_Ui/ui.h>
#include <TFE_FrontEndUI/frontEndUi.h>
//#include <TFE_ForceScript/vm.h>
#include <TFE_Jedi/IMuse/imuse.h>
#include <algorithm>
#include <cinttypes>
#include <time.h>
#include <sys/types.h>
#include <sys/timeb.h>

#define MonitorInfo MonitorInfo_Amiga
#define DisplayInfo DisplayInfo_Amiga
#define Image Image_Amiga

#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/keymap.h>
#include <proto/input.h>
#include <proto/lowlevel.h>
#include <proto/icon.h>
//#include <clib/debug_protos.h>
#include <devices/input.h>
#include <intuition/intuitionbase.h>
#include <workbench/startup.h>
#include <devices/gameport.h>
#include <psxport.h>
#include <newmouse.h>

#include <SDI_compiler.h>
#include <SDI_interrupt.h>

#undef MonitorInfo
#undef DisplayInfo
#undef Image

// Replace with music system
#include <TFE_Audio/midiPlayer.h>

#define PROGRAM_ERROR   1
#define PROGRAM_SUCCESS 0

using namespace TFE_Input;

#ifdef __libnix__
// minimum stack
extern "C" {
void __stkinit(void);
void * __x = (void *)__stkinit;
unsigned long __stack = (100*1024);
}
#endif

static bool s_loop  = true;
static bool s_nullAudioDevice = false;
//static f32  s_refreshRate  = 0;
//static s32  s_displayIndex = 0;
//static u32  s_baseWindowWidth  = 1280;
//static u32  s_baseWindowHeight = 720;
//static u32  s_displayWidth  = s_baseWindowWidth;
//static u32  s_displayHeight = s_baseWindowHeight;
//static u32  s_monitorWidth  = 1280;
//static u32  s_monitorHeight = 720;
//static char s_screenshotTime[TFE_MAX_PATH];
//static s32  s_startupGame = -1;
static IGame* s_curGame = nullptr;
static const char* s_loadRequestFilename = nullptr;
static ULONG fsMonitorID = INVALID_ID;
static ULONG fsModeID = INVALID_ID;
static char wndPubScreen[32] = {"Workbench"};
static ULONG rtg320x240 = FALSE;
static struct Window *window;
static struct MsgPort *inputPort;
static struct IOStdReq *inputReq;


static KeyboardCode keyconv[128] =
{
	KEY_GRAVE,
	KEY_1,
	KEY_2,
	KEY_3,
	KEY_4,
	KEY_5,
	KEY_6,
	KEY_7,
	KEY_8,
	KEY_9,
	KEY_0,
	KEY_MINUS,
	KEY_EQUALS,
	KEY_BACKSLASH,
	KEY_UNKNOWN, // 14
	KEY_KP_0,
	KEY_Q,
	KEY_W,
	KEY_E,
	KEY_R,
	KEY_T,
	KEY_Y,
	KEY_U,
	KEY_I,
	KEY_O,
	KEY_P,
	KEY_LEFTBRACKET,
	KEY_RIGHTBRACKET,
	KEY_UNKNOWN, // 28
	KEY_KP_1,
	KEY_KP_2,
	KEY_KP_3,
	KEY_A,
	KEY_S,
	KEY_D,
	KEY_F,
	KEY_G,
	KEY_H,
	KEY_J,
	KEY_K,
	KEY_L,
	KEY_SEMICOLON ,
	KEY_APOSTROPHE,
	KEY_UNKNOWN, // 43 - key next to Enter on ISO
	KEY_UNKNOWN, // 44
	KEY_KP_4,
	KEY_KP_5,
	KEY_KP_6,
	KEY_UNKNOWN, // 48 - ISO LESS-THAN SIGN and GREATER-THAN SIGN
	KEY_Z,
	KEY_X,
	KEY_C,
	KEY_V,
	KEY_B,
	KEY_N,
	KEY_M,
	KEY_COMMA,
	KEY_PERIOD,
	KEY_SLASH,
	KEY_UNKNOWN, // 59
	KEY_KP_PERIOD,
	KEY_KP_7,
	KEY_KP_8,
	KEY_KP_9,
	KEY_SPACE,
	KEY_BACKSPACE,
	KEY_TAB,
	KEY_KP_ENTER,
	KEY_RETURN,
	KEY_ESCAPE,
	KEY_DELETE,
	KEY_UNKNOWN, // 71
	KEY_UNKNOWN, // 72
	KEY_UNKNOWN, // 73
	KEY_KP_MINUS,
	KEY_UNKNOWN, // 75
	KEY_UP,
	KEY_DOWN,
	KEY_RIGHT,
	KEY_LEFT,
	KEY_F1,
	KEY_F2,
	KEY_F3,
	KEY_F4,
	KEY_F5,
	KEY_F6,
	KEY_F7,
	KEY_F8,
	KEY_F9,
	KEY_F10,
	KEY_NUMLOCKCLEAR, // Numpad ( or [
	KEY_SCROLLLOCK, // Numpad ) or ]
	KEY_KP_DIVIDE,
	KEY_KP_MULTIPLY,
	KEY_KP_PLUS,
	KEY_PAUSE, // Help key
	KEY_LSHIFT,
	KEY_RSHIFT,
	KEY_UNKNOWN, //KEY_CAPSLOCK,
	KEY_LCTRL,
	KEY_LALT,
	KEY_RALT,
	KEY_LGUI,
	KEY_RGUI,
};
#define MAX_KEYCONV (sizeof keyconv / sizeof keyconv[0])
static char keyascii[MAX_KEYCONV];
static char keyascii_shift[MAX_KEYCONV];

#define buildprintf(...) TFE_System::logWrite(LOG_MSG, "AppMain", __VA_ARGS__)
#define buildputs(x) TFE_System::logWrite(LOG_MSG, "AppMain", (x))

bool validatePath()
{
	if (!TFE_Paths::hasPath(PATH_SOURCE_DATA)) { return false; }

	char testFile[TFE_MAX_PATH];
	// if (game->id == Game_Dark_Forces)
	{
		// Does DARK.GOB exist?
		sprintf(testFile, "%s%s", TFE_Paths::getPath(PATH_SOURCE_DATA), "DARK.GOB");
		if (!FileUtil::exists(testFile))
		{
			TFE_System::logWrite(LOG_ERROR, "Main", "Invalid game source path: '%s' - '%s' does not exist.", TFE_Paths::getPath(PATH_SOURCE_DATA), testFile);
			TFE_Paths::setPath(PATH_SOURCE_DATA, "");
		}
		else if (!GobArchive::validate(testFile, 130))
		{
			TFE_System::logWrite(LOG_ERROR, "Main", "Invalid game source path: '%s' - '%s' GOB is invalid, too few files.", TFE_Paths::getPath(PATH_SOURCE_DATA), testFile);
			TFE_Paths::setPath(PATH_SOURCE_DATA, "");
		}
	}
	return TFE_Paths::hasPath(PATH_SOURCE_DATA);
}

static void parseTooltypes(int argc, char *argv[])
{
	char *exename;
	struct DiskObject *appicon;

	if (argc == 0) {
		struct WBStartup *startup = (struct WBStartup *)argv;
		exename = (char *)startup->sm_ArgList->wa_Name;
	} else {
		exename = argv[0];
	}

	if ((appicon = GetDiskObject((STRPTR)exename))) {
		char *value;

		if ((value = (char *)FindToolType((CONST STRPTR *)appicon->do_ToolTypes, (CONST STRPTR)"FORCEMODE"))) {
			if (!strcmp(value, "NTSC"))
				fsMonitorID = NTSC_MONITOR_ID;
			else if (!strcmp(value, "PAL"))
				fsMonitorID = PAL_MONITOR_ID;
			else if (!strcmp(value, "MULTISCAN"))
				fsMonitorID = VGA_MONITOR_ID;
			else if (!strcmp(value, "EURO72"))
				fsMonitorID = EURO72_MONITOR_ID;
			else if (!strcmp(value, "EURO36"))
				fsMonitorID = EURO36_MONITOR_ID;
			else if (!strcmp(value, "SUPER72"))
				fsMonitorID = SUPER72_MONITOR_ID;
			else if (!strcmp(value, "DBLNTSC"))
				fsMonitorID = DBLNTSC_MONITOR_ID;
			else if (!strcmp(value, "DBLPAL"))
				fsMonitorID = DBLPAL_MONITOR_ID;
		}

/*
		if ((value = (char *)FindToolType((CONST STRPTR *)appicon->do_ToolTypes, (CONST STRPTR)"FORCEID"))) {
			int id;
			if (sscanf(value, "%x", &id) == 1) {
				fsModeID = id;
				fsMonitorID = INVALID_ID;
			}
		}
*/

		if ((value = (char *)FindToolType((CONST STRPTR *)appicon->do_ToolTypes, (CONST STRPTR)"PUBSCREEN"))) {
			strncpy(wndPubScreen, value, sizeof(wndPubScreen));
		}

		if ((value = (char *)FindToolType((CONST STRPTR *)appicon->do_ToolTypes, (CONST STRPTR)"RTG320X240"))) {
			rtg320x240 = TRUE;
		}

		FreeDiskObject(appicon);
	}
}

static void fillAsciiTable(void)
{
	for (int i = 0; i < (int)MAX_KEYCONV; i++)
	{
		struct InputEvent ie;
		UBYTE bufascii;

		ie.ie_Class = IECLASS_RAWKEY;
		ie.ie_SubClass = 0;
		ie.ie_Code = i;
		ie.ie_Qualifier = 0;
		ie.ie_EventAddress = NULL;

		if (MapRawKey(&ie, (STRPTR)&bufascii, sizeof(bufascii), NULL) > 0 && bufascii >= 32 && bufascii < 127)
		{
			keyascii[i] = bufascii;
			//printf("%s rawkey %x name %c\n", __FUNCTION__, i, bufascii);
		}
		else
		{
			keyascii[i] = 0;
		}

		ie.ie_Qualifier = IEQUALIFIER_LSHIFT;
		if (MapRawKey(&ie, (STRPTR)&bufascii, sizeof(bufascii), NULL) > 0 && bufascii >= 32 && bufascii < 127)
		{
			keyascii_shift[i] = bufascii;
			//printf("%s rawkey %x shifted name %c\n", __FUNCTION__, i, bufascii);
		}
		else
		{
			keyascii_shift[i] = 0;
		}
	}
}

// lowlevel.library gamepads

static void ReadJoystick(ULONG port)
{
	ULONG portstate;

	portstate = ReadJoyPort(port);

	if (((portstate & JP_TYPE_MASK) == JP_TYPE_GAMECTLR) || ((portstate & JP_TYPE_MASK) == JP_TYPE_JOYSTK))
	{
		Button directionMap[] =
		{
			CONTROLLER_BUTTON_DPAD_RIGHT,		// JPB_JOY_RIGHT
			CONTROLLER_BUTTON_DPAD_LEFT,		// JPB_JOY_LEFT
			CONTROLLER_BUTTON_DPAD_DOWN,		// JPB_JOY_DOWN
			CONTROLLER_BUTTON_DPAD_UP,			// JPB_JOY_UP
		};

		for (int i = 0; i < 4; i++)
		{
			if (portstate & 1)
				TFE_Input::setButtonDown(directionMap[i]);
			else
				TFE_Input::setButtonUp(directionMap[i]);

			portstate >>= 1;
		}
		Button buttonMap[] =
		{
			CONTROLLER_BUTTON_START,			// JPB_BUTTON_PLAY
			CONTROLLER_BUTTON_LEFTSHOULDER,		// JPB_BUTTON_REVERSE
			CONTROLLER_BUTTON_RIGHTSHOULDER,	// JPB_BUTTON_FORWARD
			CONTROLLER_BUTTON_X,				// JPB_BUTTON_GREEN
			CONTROLLER_BUTTON_Y,				// JPB_BUTTON_YELLOW
			CONTROLLER_BUTTON_A,				// JPB_BUTTON_RED
			CONTROLLER_BUTTON_B,				// JPB_BUTTON_BLUE
		};
		portstate >>= (JPB_BUTTON_PLAY - JPB_JOY_UP - 1);
		for (int i = 0; i < 7; i++)
		{
			if (portstate & 1)
				TFE_Input::setButtonDown(buttonMap[i]);
			else
				TFE_Input::setButtonUp(buttonMap[i]);

			portstate >>= 1;
		}
	}
}

// psxport.device
static struct MsgPort *gameport_mp = NULL;
static struct IOStdReq *gameport_io = NULL;
static BOOL gameport_is_open = FALSE;
static struct InputEvent gameport_ie;
static BOOL analog_centered = FALSE;
static int analog_clx;
static int analog_cly;
static int analog_crx;
static int analog_cry;
struct GamePortTrigger gameport_gpt = {
	GPTF_UPKEYS | GPTF_DOWNKEYS,	/* gpt_Keys */
	0,				/* gpt_Timeout */
	1,				/* gpt_XDelta */
	1				/* gpt_YDelta */
};

void uninitPsxPort(void)
{
	if (gameport_is_open) {
		AbortIO((struct IORequest *)gameport_io);
		WaitIO((struct IORequest *)gameport_io);
		BYTE gameport_ct = GPCT_NOCONTROLLER;
		gameport_io->io_Command = GPD_SETCTYPE;
		gameport_io->io_Length = 1;
		gameport_io->io_Data = &gameport_ct;
		DoIO((struct IORequest *)gameport_io);
		CloseDevice((struct IORequest *)gameport_io);
		gameport_is_open = FALSE;
	}
	if (gameport_io != NULL) {
		DeleteIORequest((struct IORequest *)gameport_io);
		gameport_io = NULL;
	}
	if (gameport_mp != NULL) {
		DeleteMsgPort(gameport_mp);
		gameport_mp = NULL;
	}
}

static bool initPsxPort(void)
{
	if ((gameport_mp = CreateMsgPort())) {
		if ((gameport_io = (struct IOStdReq *)CreateIORequest(gameport_mp, sizeof(struct IOStdReq)))) {
			int ix;
			BYTE gameport_ct;
			for (ix=0; ix<4; ix++) {
				if (!OpenDevice((STRPTR)"psxport.device", ix, (struct IORequest *)gameport_io, 0)) {
					buildprintf("psxport.device unit %d opened.\n", ix);
					Forbid();
					gameport_io->io_Command = GPD_ASKCTYPE;
					gameport_io->io_Length = 1;
					gameport_io->io_Data = &gameport_ct;
					DoIO((struct IORequest *)gameport_io);
					if (gameport_ct == GPCT_NOCONTROLLER) {
						gameport_ct = GPCT_ALLOCATED;
						gameport_io->io_Command = GPD_SETCTYPE;
						gameport_io->io_Length = 1;
						gameport_io->io_Data = &gameport_ct;
						DoIO((struct IORequest *)gameport_io);

						Permit();

						gameport_io->io_Command = GPD_SETTRIGGER;
						gameport_io->io_Length = sizeof(struct GamePortTrigger);
						gameport_io->io_Data = &gameport_gpt;
						DoIO((struct IORequest *)gameport_io);

						gameport_io->io_Command = GPD_READEVENT;
						gameport_io->io_Length = sizeof(struct InputEvent);
						gameport_io->io_Data = &gameport_ie;
						SendIO((struct IORequest *)gameport_io);
						gameport_is_open = TRUE;

						return true;
					} else {
						Permit();
						buildprintf("psxport.device unit %d in use.\n", ix);
						CloseDevice((struct IORequest *)gameport_io);
					}
				} else {
					//buildprintf("psxport.device unit %d won't open.\n", ix);
				}
			}
		}
	}
	uninitPsxPort();

	return false;
}

static void updatePsxPort(void)
{
	if (gameport_is_open) {
		// PSX joypad
		if (GetMsg(gameport_mp) != NULL) {
			if ((PSX_CLASS(gameport_ie) == PSX_CLASS_JOYPAD) || (PSX_CLASS(gameport_ie) == PSX_CLASS_WHEEL))
				analog_centered = FALSE;

			if (PSX_CLASS(gameport_ie) != PSX_CLASS_MOUSE) {
				ULONG gameport_curr = ~PSX_BUTTONS(gameport_ie);

				// triggers
				TFE_Input::setAxis(AXIS_LEFT_TRIGGER, (gameport_curr & PSX_L2) ? 1.0f : 0.0f);
				TFE_Input::setAxis(AXIS_RIGHT_TRIGGER, (gameport_curr & PSX_R2) ? 1.0f : 0.0f);

				// buttons
				Button buttonMap[] =
				{
					CONTROLLER_BUTTON_LEFTSHOULDER,		// PSX_L1
					CONTROLLER_BUTTON_RIGHTSHOULDER,	// PSX_R1
					CONTROLLER_BUTTON_Y,				// PSX_TRIANGLE
					CONTROLLER_BUTTON_B,				// PSX_CIRCLE
					CONTROLLER_BUTTON_A,				// PSX_CROSS
					CONTROLLER_BUTTON_X,				// PSX_SQUARE
					CONTROLLER_BUTTON_BACK,				// PSX_SELECT
					CONTROLLER_BUTTON_LEFTSTICK,		// PSX_L3
					CONTROLLER_BUTTON_RIGHTSTICK,		// PSX_R3
					//CONTROLLER_BUTTON_GUIDE,			// PSX_START
					CONTROLLER_BUTTON_START,			// PSX_START
					CONTROLLER_BUTTON_DPAD_UP,			// PSX_UP
					CONTROLLER_BUTTON_DPAD_RIGHT,		// PSX_RIGHT
					CONTROLLER_BUTTON_DPAD_DOWN,		// PSX_DOWN
					CONTROLLER_BUTTON_DPAD_LEFT,		// PSX_LEFT
				};
				gameport_curr >>= 2;
				for (int i = 0; i < 14; i++)
				{
					if (gameport_curr & 1)
						TFE_Input::setButtonDown(buttonMap[i]);
					else
						TFE_Input::setButtonUp(buttonMap[i]);

					gameport_curr >>= 1;
				}
			}

			if ((PSX_CLASS(gameport_ie) == PSX_CLASS_ANALOG) || (PSX_CLASS(gameport_ie) == PSX_CLASS_ANALOG2) || (PSX_CLASS(gameport_ie) == PSX_CLASS_ANALOG_MODE2)) {
				int analog_lx = PSX_LEFTX(gameport_ie);
				int analog_ly = PSX_LEFTY(gameport_ie);
				int analog_rx = PSX_RIGHTX(gameport_ie);
				int analog_ry = PSX_RIGHTY(gameport_ie);

				if (!analog_centered) {
					analog_clx = analog_lx;
					analog_cly = analog_ly;
					analog_crx = analog_rx;
					analog_cry = analog_ry;
					analog_centered = TRUE;
				}

				// left analog stick
				TFE_Input::setAxis(AXIS_LEFT_X, (float)(analog_lx - analog_clx) / 128.0f);
				TFE_Input::setAxis(AXIS_LEFT_Y, (float)(analog_ly - analog_cly) / -128.0f);

				// right analog stick
				TFE_Input::setAxis(AXIS_RIGHT_X, (float)(analog_rx - analog_crx) / 128.0f);
				TFE_Input::setAxis(AXIS_RIGHT_Y, (float)(analog_ry - analog_cry) / -128.0f);
			}

			gameport_io->io_Command = GPD_READEVENT;
			gameport_io->io_Length = sizeof(struct InputEvent);
			gameport_io->io_Data = &gameport_ie;
			SendIO((struct IORequest *)gameport_io);
		}
	}
}

static s32 s_mx = 0;
static s32 s_my = 0;

HANDLERPROTO(InputHandlerFunc, struct InputEvent *, struct InputEvent *input_event, APTR data)
{
	struct InputEvent *ie;
	int code, press;

	if (!window || !(window->Flags & WFLG_WINDOWACTIVE) || (window->WScreen && window->WScreen != IntuitionBase->FirstScreen))
		return input_event;

	if (!TFE_Input::relativeModeEnabled())
		return input_event;

	for (ie = input_event; ie; ie = ie->ie_NextEvent)
	{
		if (ie->ie_Class == IECLASS_RAWMOUSE)
		{
			s_mx += ie->ie_position.ie_xy.ie_x;
			s_my += ie->ie_position.ie_xy.ie_y;
			ie->ie_position.ie_xy.ie_x = 0;
			ie->ie_position.ie_xy.ie_y = 0;
		}
	}

	return input_event;
}
MakeInterruptPri(inputHandler, InputHandlerFunc, "TFE relative mouse", NULL, 100);

static void RemoveInputHandler(void)
{
	if (inputReq) {
		inputReq->io_Data = &inputHandler;
		inputReq->io_Command = IND_REMHANDLER;
		DoIO((struct IORequest *)inputReq);
		CloseDevice((struct IORequest *)inputReq);
		DeleteIORequest((struct IORequest *)inputReq);
		inputReq = NULL;
		InputBase = NULL;
	}

	if (inputPort) {
		DeleteMsgPort(inputPort);
		inputPort = NULL;
	}
}

static int AddInputHandler(void)
{
	if ((inputPort = CreateMsgPort())) {
		if ((inputReq = (struct IOStdReq *)CreateIORequest(inputPort, sizeof(*inputReq)))) {
			if (!OpenDevice((STRPTR)"input.device", 0, (struct IORequest *)inputReq, 0)) {
				InputBase = inputReq->io_Device;
				inputReq->io_Data = &inputHandler;
				inputReq->io_Command = IND_ADDHANDLER;
				if (!DoIO((struct IORequest *)inputReq))
				{
					return 0;
				}
			}
		}
	}

	RemoveInputHandler();

	return 1;
}

static InputBinding s_defaultCD32Binds[] =
{
	{ IADF_JUMP,   ITYPE_CONTROLLER, CONTROLLER_BUTTON_Y },
	{ IADF_RUN,    ITYPE_CONTROLLER, CONTROLLER_BUTTON_LEFTSHOULDER },
	{ IADF_CROUCH, ITYPE_CONTROLLER, CONTROLLER_BUTTON_X },
	{ IADF_USE,    ITYPE_CONTROLLER, CONTROLLER_BUTTON_RIGHTSHOULDER },

	{ IADF_PRIMARY_FIRE,   ITYPE_CONTROLLER, CONTROLLER_BUTTON_A },
	{ IADF_SECONDARY_FIRE, ITYPE_CONTROLLER, CONTROLLER_BUTTON_B },

	{ IADF_CYCLEWPN_NEXT, ITYPE_CONTROLLER, CONTROLLER_BUTTON_START },

	{ IADF_TURN_RT,  ITYPE_CONTROLLER, CONTROLLER_BUTTON_DPAD_RIGHT },
	{ IADF_TURN_LT,  ITYPE_CONTROLLER, CONTROLLER_BUTTON_DPAD_LEFT },
	{ IADF_FORWARD,  ITYPE_CONTROLLER, CONTROLLER_BUTTON_DPAD_UP },
	{ IADF_BACKWARD, ITYPE_CONTROLLER, CONTROLLER_BUTTON_DPAD_DOWN },

	{ IAS_SYSTEM_MENU,   ITYPE_CONTROLLER, CONTROLLER_BUTTON_RIGHTSHOULDER }, // strafe modifier
};

static InputBinding s_defaultPSXBinds[] =
{
	{ IAS_SYSTEM_MENU, ITYPE_CONTROLLER, CONTROLLER_BUTTON_RIGHTSTICK },

	{ IADF_JUMP,   ITYPE_CONTROLLER, CONTROLLER_BUTTON_A },
	{ IADF_RUN,    ITYPE_CONTROLLER, CONTROLLER_BUTTON_B },
	{ IADF_CROUCH, ITYPE_CONTROLLER, CONTROLLER_BUTTON_X },
	{ IADF_USE,    ITYPE_CONTROLLER, CONTROLLER_BUTTON_Y },

	{ IADF_MENU_TOGGLE, ITYPE_CONTROLLER, CONTROLLER_BUTTON_START },

	{ IADF_PRIMARY_FIRE,   ITYPE_CONTROLLER_AXIS, AXIS_RIGHT_TRIGGER },
	{ IADF_SECONDARY_FIRE, ITYPE_CONTROLLER_AXIS, AXIS_LEFT_TRIGGER },
	
	{ IADF_HEAD_LAMP_TOGGLE, ITYPE_CONTROLLER, CONTROLLER_BUTTON_DPAD_RIGHT },
	{ IADF_NIGHT_VISION_TOG, ITYPE_CONTROLLER, CONTROLLER_BUTTON_DPAD_LEFT },
	{ IADF_AUTOMAP,          ITYPE_CONTROLLER, CONTROLLER_BUTTON_DPAD_UP },
	{ IADF_GAS_MASK_TOGGLE,  ITYPE_CONTROLLER, CONTROLLER_BUTTON_DPAD_DOWN },

	{ IADF_CYCLEWPN_PREV, ITYPE_CONTROLLER, CONTROLLER_BUTTON_LEFTSHOULDER },
	{ IADF_CYCLEWPN_NEXT, ITYPE_CONTROLLER, CONTROLLER_BUTTON_RIGHTSHOULDER },
};

static void SetDefaultControllerBinds(void)
{
	if (gameport_is_open)
	{
		for (s32 i = 0; i < TFE_ARRAYSIZE(s_defaultPSXBinds); i++)
		{
			inputMapping_addBinding(&s_defaultPSXBinds[i]);
		}
	}
	else
	{
		for (s32 i = 0; i < TFE_ARRAYSIZE(s_defaultCD32Binds); i++)
		{
			inputMapping_addBinding(&s_defaultCD32Binds[i]);
		}
	}
}

// main function

int main(int argc, char *argv[])
{
	//setbuf(stdout, NULL); // TODO remove

	// Paths
	bool pathsSet = true;
	pathsSet &= TFE_Paths::setProgramPath();
	//pathsSet &= TFE_Paths::setProgramDataPath("TheForceEngine");
	//pathsSet &= TFE_Paths::setUserDocumentsPath("TheForceEngine");
	TFE_System::logOpen("the_force_engine_log.txt");
	//TFE_System::logWrite(LOG_MSG, "Main", "The Force Engine %s", c_gitVersion);
	TFE_System::logWrite(LOG_MSG, "Main", "The Force Engine");
	if (!pathsSet)
	{
		TFE_System::logWrite(LOG_ERROR, "Main", "Cannot set paths.");
		return PROGRAM_ERROR;
	}
	
	// Before loading settings, read in the Input key lists.
	/*
	if (!TFE_Input::loadKeyNames("UI_Text/KeyText.txt"))
	{
		TFE_System::logWrite(LOG_ERROR, "Main", "Cannot load key names.");
		return PROGRAM_ERROR;
	}

	if (!TFE_System::loadMessages("UI_Text/TfeMessages.txt"))
	{
		TFE_System::logWrite(LOG_ERROR, "Main", "Cannot load TFE messages.");
		return PROGRAM_ERROR;
	}
	*/

	// Initialize settings so that the paths can be read.
	bool firstRun;
	if (!TFE_Settings::init(firstRun))
	{
		TFE_System::logWrite(LOG_ERROR, "Main", "Cannot load settings.");
		return PROGRAM_ERROR;
	}

	// Override settings with command line options.
	//parseCommandLine(argc, argv);
	parseTooltypes(argc, argv);

	// Setup game paths.
	// Get the current game.
	const TFE_Game* game = TFE_Settings::getGame();
	const TFE_GameHeader* gameHeader = TFE_Settings::getGameHeader(game->game);
	TFE_Paths::setPath(PATH_SOURCE_DATA, gameHeader->sourcePath);
	//TFE_Paths::setPath(PATH_EMULATOR, gameHeader->emulatorPath);

	// Validate the current game path.
	//validatePath();

	TFE_System::logWrite(LOG_MSG, "Paths", "Program Path: \"%s\"",   TFE_Paths::getPath(PATH_PROGRAM));
	TFE_System::logWrite(LOG_MSG, "Paths", "Program Data: \"%s\"",   TFE_Paths::getPath(PATH_PROGRAM_DATA));
	TFE_System::logWrite(LOG_MSG, "Paths", "User Documents: \"%s\"", TFE_Paths::getPath(PATH_USER_DOCUMENTS));
	TFE_System::logWrite(LOG_MSG, "Paths", "Source Data: \"%s\"",    TFE_Paths::getPath(PATH_SOURCE_DATA));

	// this has to be done early
	initPsxPort();

	// Initialize the window
	TFE_Settings_Window* windowSettings = TFE_Settings::getWindowSettings();
	TFE_Settings_Graphics* graphics = TFE_Settings::getGraphicsSettings();
	TFE_System::init(/*s_refreshRate*/0.0f, /*graphics->vsync*/false, /*c_gitVersion*/"");

	// force the correct resolution
	graphics->gameResolution.x = 320;
	graphics->gameResolution.z = 200;
	graphics->widescreen = false;
	graphics->rendererIndex = 0;

	// Setup the GPU Device and Window.
	u32 windowFlags = 0;
	if (windowSettings->fullscreen) { TFE_System::logWrite(LOG_MSG, "Display", "Fullscreen enabled."); windowFlags |= WINFLAG_FULLSCREEN; }
	if (graphics->vsync) { TFE_System::logWrite(LOG_MSG, "Display", "Vertical Sync enabled."); windowFlags |= WINFLAG_VSYNC; }

	WindowState windowState;
	memset(&windowState, 0, sizeof(windowState));
	windowState.width = graphics->gameResolution.x;
	windowState.height = graphics->gameResolution.z;
	windowState.baseWindowWidth = fsMonitorID;
	windowState.baseWindowHeight = fsModeID;
	windowState.monitorWidth = 0; // TODO wndPubScreen
	windowState.monitorHeight = rtg320x240;
	windowState.flags = windowFlags;
	//sprintf(windowState.name, "The Force Engine  %s", TFE_System::getVersionString());
	strcpy(windowState.name, "The Force Engine");

	if (!TFE_RenderBackend::init(windowState))
	{
		TFE_System::logWrite(LOG_CRITICAL, "GPU", "Cannot initialize GPU/Window.");
		TFE_System::logClose();
		return PROGRAM_ERROR;
	}
	window = (struct Window *)TFE_RenderBackend::getVirtualDisplayGpuPtr();

	TFE_FrontEndUI::initConsole();
	TFE_MidiPlayer::init(TFE_Settings::getSoundSettings()->midiOutput, (MidiDeviceType)TFE_Settings::getSoundSettings()->midiType);
	TFE_Audio::init(s_nullAudioDevice, TFE_Settings::getSoundSettings()->audioDevice);
	//TFE_Polygon::init();
	//TFE_Image::init();
	//TFE_Palette::createDefault256();
	TFE_FrontEndUI::init();
	game_init();
	//inputMapping_startup();
	// custom replacement
	if (!inputMapping_restore())
	{
		inputMapping_resetToDefaults();
		SetDefaultControllerBinds();
		inputMapping_serialize();
	}
	TFE_SaveSystem::init();
	fillAsciiTable();
	//initPsxPort();
	AddInputHandler();

	// Color correction.
	const ColorCorrection colorCorrection = { graphics->brightness, graphics->contrast, graphics->saturation, graphics->gamma };
	TFE_RenderBackend::setColorCorrection(graphics->colorCorrection, &colorCorrection); 

	// Try to set the game right away, so the load menu works.
	TFE_Game* gameInfo = TFE_Settings::getGame();
	//TFE_SaveSystem::setCurrentGame(gameInfo->id);

	s_curGame = createGame(gameInfo->id);
	TFE_SaveSystem::setCurrentGame(s_curGame);
	if (!s_curGame)
	{
		TFE_System::logWrite(LOG_ERROR, "AppMain", "Cannot create game '%s'.", gameInfo->game);
		//newState = APP_STATE_CANNOT_RUN;
		s_loop = false;
	}
	else if (!s_curGame->runGame(argc, (const char**)argv, nullptr))
	{
		TFE_System::logWrite(LOG_ERROR, "AppMain", "Cannot run game '%s'.", gameInfo->game);
		freeGame(s_curGame);
		s_curGame = nullptr;
		//newState = APP_STATE_CANNOT_RUN;
		s_loop = false;
	}
	/*
	else
	{
		TFE_Input::enableRelativeMode(true);
	}
	*/

	// Game loop
	bool showPerf = false;
	bool relativeMode = false;
	TFE_System::logWrite(LOG_MSG, "Progam Flow", "The Force Engine Game Loop Started");
	while (s_loop && !TFE_System::quitMessagePosted())
	{
		relativeMode = s_curGame->canSave() && !s_curGame->isPaused();
		TFE_Input::enableRelativeMode(relativeMode);

		// System events
		ReadJoystick(1);
		updatePsxPort();
		struct IntuiMessage *imsg;
		//struct Window *window = (struct Window *)TFE_RenderBackend::getVirtualDisplayGpuPtr();
		//if (window)
		{
			/*
				if (screen && screen == IntuitionBase->FirstScreen && !(window->Flags & WFLG_WINDOWACTIVE)) {
					ActivateWindow(window);
				}
			*/

			// caps lock handling
			if (InputBase)
			{
				UWORD qualifier = PeekQualifier();
				if ((qualifier & IEQUALIFIER_CAPSLOCK) != 0)
					TFE_Input::setKeyDown(KEY_CAPSLOCK);
				else
					TFE_Input::setKeyUp(KEY_CAPSLOCK);
			}
			while ((imsg = (struct IntuiMessage *)GetMsg(window->UserPort)))
			{
				switch (imsg->Class)
				{
				case IDCMP_MOUSEBUTTONS:
					{
						UWORD press = (imsg->Code & IECODE_UP_PREFIX) == 0;
						UWORD code = imsg->Code & ~IECODE_UP_PREFIX;
						//MouseButton button = (MouseButton)(code - IECODE_LBUTTON);
						WORD j = code - IECODE_LBUTTON;
						MouseButton buttons[] = {MBUTTON_LEFT, MBUTTON_RIGHT, MBUTTON_MIDDLE};
						if (press)
							TFE_Input::setMouseButtonDown(buttons[j]);
						else
							TFE_Input::setMouseButtonUp(buttons[j]);
					}
					break;
				case IDCMP_RAWKEY:
					{
						UWORD press = (imsg->Code & IECODE_UP_PREFIX) == 0;
						UWORD code = imsg->Code & ~IECODE_UP_PREFIX;
						UWORD qualifier;
						KeyboardCode scan;

						if (code > (int)MAX_KEYCONV)
						{
							// newmouse wheeel?
							switch (code)
							{
							case NM_WHEEL_UP:
								TFE_Input::setMouseWheel(0, 1);
								break;
							case NM_WHEEL_DOWN:
								TFE_Input::setMouseWheel(0, -1);
								break;
							}
							break;
						}

						qualifier = imsg->Qualifier;
						scan = keyconv[code];

						if (scan != KEY_UNKNOWN) {
							if (press)
							{
								if (!(qualifier & IEQUALIFIER_REPEAT))
								{
									TFE_Input::setKeyDown(scan);
								}
								TFE_Input::setBufferedKey(scan);
							}
							else
							{
								TFE_Input::setKeyUp(scan);
							}
						}

						if (press && !(qualifier & IEQUALIFIER_REPEAT)) {
							int ch = 0;
							if ((qualifier & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) != 0) {
								ch = keyascii_shift[code];
							} else {
								ch = keyascii[code];
							}
							if ((qualifier & IEQUALIFIER_CAPSLOCK) != 0)
							{
								if (ch >= 'A' && ch <= 'Z')
								{
									// to lowercase
									ch += ('a'-'A');
								}
								else if (ch >= 'a' && ch <= 'z')
								{
									// to uppercase
									ch -= ('a'-'A');
								}
							}
							if (ch)
							{
								TFE_Input::setBufferedChar(ch);
							}
						}
					}
					break;
					/*
				case IDCMP_ACTIVEWINDOW:
					appactive = 1;
					rv=-1;
					break;
				case IDCMP_INACTIVEWINDOW:
					appactive = 0;
					rv=-1;
					break;
					*/
				case IDCMP_CLOSEWINDOW:
					s_loop = false;
					break;
				}

				ReplyMsg((struct Message *)imsg);
			}
		}
		// sleep when the window isn't active
		if (!(window->Flags & WFLG_WINDOWACTIVE))
		{
			TFE_System::sleep(1000/25);
		}

		// Handle mouse state.
		s32 mouseX, mouseY;
		mouseX = s_mx;
		mouseY = s_my;
		s_mx = s_my = 0;
		TFE_Input::setRelativeMousePos(mouseX, mouseY);
		TFE_Input::setMousePos(window->MouseX - window->BorderLeft, window->MouseY - window->BorderTop);
		inputMapping_updateInput();

		// Can we save?
		//TFE_FrontEndUI::setCanSave(s_curGame ? s_curGame->canSave() : false);

		// load saves
		s_loadRequestFilename = TFE_SaveSystem::loadRequestFilename();
		if (s_loadRequestFilename /*&& s_curGame->canSave()*/)
		{
#if 0
			TFE_Game* gameInfo = TFE_Settings::getGame();
			if (s_curGame)
			{
				freeGame(s_curGame);
				s_curGame = nullptr;
			}
			s_curGame = createGame(gameInfo->id);
			TFE_SaveSystem::setCurrentGame(s_curGame);
			if (!s_curGame)
			{
				TFE_System::logWrite(LOG_ERROR, "AppMain", "Cannot create game '%s'.", gameInfo->game);
				//newState = APP_STATE_CANNOT_RUN;
				s_loop = false;
				continue;
			}
			else if (!TFE_SaveSystem::loadGame(s_loadRequestFilename))
			{
				TFE_System::logWrite(LOG_ERROR, "AppMain", "Cannot run game '%s'.", gameInfo->game);
				freeGame(s_curGame);
				s_curGame = nullptr;
				s_loop = false;
				continue;
			}
#else

			ImStopAllSounds();
			//ImUnloadAll();
			//TFE_Audio::MidiDevice* getMidiDevice();
			TFE_MidiPlayer::destroy();
			if (!TFE_SaveSystem::loadGame(s_loadRequestFilename))
			{
				TFE_System::logWrite(LOG_ERROR, "AppMain", "Cannot run game '%s'.", gameInfo->game);
				freeGame(s_curGame);
				s_curGame = nullptr;
				//newState = APP_STATE_CANNOT_RUN;
				s_loop = false;
			}
			TFE_MidiPlayer::init(TFE_Settings::getSoundSettings()->midiOutput, (MidiDeviceType)TFE_Settings::getSoundSettings()->midiType);
#endif
		}

		TFE_System::update();

		bool toggleSystemMenu = TFE_System::systemUiRequestPosted();
		if (toggleSystemMenu)
		{
#ifdef MUIPREFS
			showConfigMenu(window);
#endif
			TFE_FrontEndUI::enableConfigMenu();
		}

		// Update
		TFE_SaveSystem::update();
		s_curGame->loopGame();
		bool endInputFrame = TFE_Jedi::task_run() != 0;

		// Blit the frame to the window and draw UI.
		TFE_RenderBackend::swap(true);

		// Clear transitory input state.
		if (endInputFrame)
		{
			TFE_Input::endFrame();
			inputMapping_endFrame();
		}
	}

	if (s_curGame)
	{
		freeGame(s_curGame);
		s_curGame = nullptr;
	}
	//s_soundPaused = false;
	game_destroy();
	//reticle_destroy();
	inputMapping_shutdown();
	uninitPsxPort();
	RemoveInputHandler();

	// Cleanup
	TFE_FrontEndUI::shutdown();
	TFE_Audio::shutdown();
	TFE_MidiPlayer::destroy();
	//TFE_Polygon::shutdown();
	//TFE_Image::shutdown();
	//TFE_Palette::freeAll();
	TFE_RenderBackend::updateSettings();
	TFE_Settings::shutdown();
	//TFE_Jedi::texturepacker_freeGlobal();
	TFE_RenderBackend::destroy();
	TFE_SaveSystem::destroy();

	TFE_System::logWrite(LOG_MSG, "Progam Flow", "The Force Engine Game Loop Ended.");
	TFE_System::logClose();
	//TFE_System::freeMessages();
	return PROGRAM_SUCCESS;
}
