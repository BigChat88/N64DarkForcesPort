#include <TFE_System/tfeMessage.h>

namespace TFE_System
{
	static char* s_tfeMessage[TFE_MSG_COUNT];
	static bool s_messagesLoaded = false;

	const char* getMessage(TFE_Message msg)
	{
		switch (msg)
		{
			case TFE_MSG_SAVE:
				return "Game Saved.";
			case TFE_MSG_SECRET:
				return "You found a secret!";
			case TFE_MSG_FLYMODE:
				return "Fly Mode Toggle.";
			case TFE_MSG_NOCLIP:
				return "No Clip Toggle.";
			case TFE_MSG_TESTER:
				return "Testing Mode Toggle.";
		}
		return nullptr;
	}
	

}