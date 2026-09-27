// N64 stubs for TFE subsystems that are not used on this platform.
// Adapted from tfe/amiga/stubs_amiga.cpp.
#include <TFE_Game/saveSystem.h>
//#include <TFE_PostProcess/postprocess.h>
#include <TFE_Archive/zipArchive.h>
#include <TFE_Archive/gobMemoryArchive.h>
#include <TFE_Jedi/Level/level.h>
#include <TFE_Jedi/Math/core_math.h>
#include <TFE_Jedi/Renderer/screenDraw.h>
#include <TFE_Jedi/Renderer/RClassic_GPU/rsectorGPU.h>
#include <TFE_Jedi/Renderer/RClassic_Float/rsectorFloat.h>
#include <TFE_Jedi/Renderer/RClassic_Float/rclassicFloatSharedState.h>
#include <TFE_Audio/MidiSynth/soundFontDevice.h>
#include <TFE_Audio/MidiSynth/fm4Opl3Device.h>
#include <TFE_System/system.h>
#include <TFE_A11y/accessibility.h>
#include <TFE_FrontEndUI/console.h>

#include <debug.h>

namespace TFE_FrontEndUI
{
	void logToConsole(const char* str)
	{
#ifdef _DEBUG
		debugf("%s\n", str);
#endif
	}

	void initConsole() {}
	bool toggleConsole() { return false; }
	bool isConsoleOpen() { return false; }
	void exitToMenu()
	{
		TFE_System::postQuitMessage();
	}
}

namespace TFE_Image
{
	Image* get(const char* imagePath)
	{
		return nullptr;
	}

	size_t writeImageToMemory(u8*& output, u32 width, u32 height, const u32* pixelData)
	{
		output = (u8*)pixelData;
		return width * height;
	}

	void readImageFromMemory(Image* output, size_t size, const u32* pixelData)
	{
		output->width = TFE_SaveSystem::SAVE_IMAGE_WIDTH;
		output->height = TFE_SaveSystem::SAVE_IMAGE_HEIGHT;
		output->data = (u32*)pixelData;
	}
	
	void writeImage(const char* path, u32 width, u32 height, u32* pixelData)
	{
	}
}

/*
namespace TFE_PostProcess
{
	OverlayID addOverlayTexture(OverlayImage* overlay, f32* tint, f32 screenX, f32 screenY, f32 scale)
	{
		return (OverlayID)0;
	}
	
	void enableOverlay(OverlayID id, bool enable)
	{
	}
	
	void removeOverlay(OverlayID id)
	{
	}
	
	void setOverlayImage(OverlayID id, const OverlayImage* image)
	{
	}
	
	void modifyOverlay(OverlayID id, f32* tint, f32 screenX, f32 screenY, f32 scale)
	{
	}
}
*/

// TFE_Archive

ZipArchive::~ZipArchive() {}
bool ZipArchive::create(const char *archivePath) { return false; }
bool ZipArchive::open(const char *archivePath) { return false; }
void ZipArchive::close() {}
bool ZipArchive::openFile(const char *file) { return false; }
bool ZipArchive::openFile(u32 index) { return false; }
void ZipArchive::closeFile() {}
bool ZipArchive::fileExists(const char *file) { return false; }
bool ZipArchive::fileExists(u32 index) { return false; }
u32 ZipArchive::getFileIndex(const char* file) { return INVALID_FILE; }
size_t ZipArchive::getFileLength() { return 0;}
size_t ZipArchive::readFile(void* data, size_t size) { return 0; }
bool ZipArchive::seekFile(s32 offset, s32 origin) { return false; }
size_t ZipArchive::getLocInFile() { return 0; }
u32 ZipArchive::getFileCount() { return 0; }
const char* ZipArchive::getFileName(u32 index) { return nullptr; }
size_t ZipArchive::getFileLength(u32 index) { return 0; }
void ZipArchive::addFile(const char* fileName, const char* filePath) {}

GobMemoryArchive::~GobMemoryArchive() {}
bool GobMemoryArchive::create(const char *archivePath) { return false; }
bool GobMemoryArchive::open(const char *archivePath) { return false; }
bool GobMemoryArchive::open(const u8* buffer, size_t size) { return false; }
void GobMemoryArchive::close() {}
bool GobMemoryArchive::openFile(const char *file) { return false; }
bool GobMemoryArchive::openFile(u32 index) { return false; }
void GobMemoryArchive::closeFile() {}
u32 GobMemoryArchive::getFileIndex(const char* file) { return INVALID_FILE; }
bool GobMemoryArchive::fileExists(const char *file) { return false; }
bool GobMemoryArchive::fileExists(u32 index) { return false; }
size_t GobMemoryArchive::getFileLength() { return 0; }
size_t GobMemoryArchive::readFile(void *data, size_t size) { return 0; }
bool GobMemoryArchive::seekFile(s32 offset, s32 origin) { return false; }
size_t GobMemoryArchive::getLocInFile() { return 0; }
u32 GobMemoryArchive::getFileCount() { return 0; }
const char* GobMemoryArchive::getFileName(u32 index) { return nullptr; }
size_t GobMemoryArchive::getFileLength(u32 index) { return 0; }
void GobMemoryArchive::addFile(const char* fileName, const char* filePath) {}

namespace TFE_Jedi
{
	// RClassic_GPU
	Vec3f s_cameraPos;
	Vec3f s_cameraDir;
	namespace RClassic_GPU
	{
		void resetState() {}
		void setupInitCameraAndLights(s32 width, s32 height) {}
		void changeResolution(s32 width, s32 height) {}
		void computeCameraTransform(RSector* sector, f32 pitch, f32 yaw, f32 camX, f32 camY, f32 camZ) {}
		void transformPointByCamera(vec3_float* worldPoint, vec3_float* viewPoint) {}
		void computeSkyOffsets() {}
	}

	namespace RClassic_Float
	{
		void resetState() {}
		void setupInitCameraAndLights(s32 width, s32 height) {}
		void changeResolution(s32 width, s32 height) {}
		void computeCameraTransform(RSector* sector, f32 pitch, f32 yaw, f32 camX, f32 camY, f32 camZ) {}
		//void transformPointByCamera(vec3_float* worldPoint, vec3_float* viewPoint) {}
		void computeSkyOffsets() {}
	}
	// s_rcfltState (float renderer state, ~300KB) is intentionally not defined on the N64.

	void TFE_Sectors_Float::destroy() {}
	void TFE_Sectors_Float::reset() {}
	void TFE_Sectors_Float::prepare() {}
	void TFE_Sectors_Float::draw(RSector* sector) {}
	void TFE_Sectors_Float::subrendererChanged() {}

	void TFE_Sectors_GPU::destroy() {}
	void TFE_Sectors_GPU::reset() {}
	void TFE_Sectors_GPU::prepare() {}
	void TFE_Sectors_GPU::draw(RSector* sector) {}
	void TFE_Sectors_GPU::subrendererChanged() {}
	void TFE_Sectors_GPU::flushCache() {}
	TextureGpu* getColormap() { return nullptr; }

	void screenGPU_init() {}
	void screenGPU_setHudTextureCallbacks(s32 count, TextureListCallback* callbacks) {}
	void screenGPU_beginQuads(u32 width, u32 height) {}
	void screenGPU_endQuads() {}
	void screenGPU_beginLines(u32 width, u32 height) {}
	void screenGPU_endLines() {}
	void screenGPU_beginImageQuads(u32 width, u32 height) {}
	void screenGPU_endImageQuads() {}
	void screenGPU_addImageQuad(s32 x0, s32 z0, s32 x1, s32 z1, TextureGpu* texture) {}
	void screenGPU_addImageQuad(s32 x0, s32 z0, s32 x1, s32 z1, f32 u0, f32 u1, TextureGpu* texture) {}
	void screenGPU_drawPoint(ScreenRect* rect, s32 x, s32 z, u8 color) {}
	void screenGPU_drawLine(ScreenRect* rect, s32 x0, s32 z0, s32 x1, s32 z1, u8 color) {}
	void screenGPU_blitTextureLit(TextureData* texture, DrawRect* rect, s32 x0, s32 y0, u8 lightLevel, JBool forceTransparency) {}
	void screenGPU_drawColoredQuad(fixed16_16 x0, fixed16_16 y0, fixed16_16 x1, fixed16_16 y1, u8 color) {}
	void screenGPU_blitTextureScaled(TextureData* texture, DrawRect* rect, fixed16_16 x0, fixed16_16 y0, fixed16_16 xScale, fixed16_16 yScale, u8 lightLevel, JBool forceTransparency) {}
	
	void texturepacker_reset() {}
	
}

namespace TFE_Audio
{
	SoundFontDevice::~SoundFontDevice() {}
	u32 SoundFontDevice::getOutputCount() { return 0; }
	void SoundFontDevice::getOutputName(s32 index, char* buffer, u32 maxLength) {}
	bool SoundFontDevice::selectOutput(s32 index) { return false; }
	s32 SoundFontDevice::getActiveOutput(void) { return 0; }
	//void SoundFontDevice::beginStream(s32 sampleRate) {}
	void SoundFontDevice::exit() {}
	const char* SoundFontDevice::getName() { return nullptr; }
	bool SoundFontDevice::render(f32* buffer, u32 sampleCount) { return false; }
	bool SoundFontDevice::canRender() { return false; }
	void SoundFontDevice::setVolume(f32 volume) {}
	void SoundFontDevice::message(u8 type, u8 arg1, u8 arg2) {}
	void SoundFontDevice::message(const u8* msg, u32 len) {}
	void SoundFontDevice::noteAllOff() {}

/*
	Fm4Opl3Device::~Fm4Opl3Device() {}
	u32 Fm4Opl3Device::getOutputCount() { return 0; }
	void Fm4Opl3Device::getOutputName(s32 index, char* buffer, u32 maxLength) {}
	bool Fm4Opl3Device::selectOutput(s32 index) { return false; }
	s32 Fm4Opl3Device::getActiveOutput(void) { return 0; }
	void Fm4Opl3Device::beginStream(s32 sampleRate) {}
	void Fm4Opl3Device::exit() {}
	const char* Fm4Opl3Device::getName() { return nullptr; }
	bool Fm4Opl3Device::render(f32* buffer, u32 sampleCount) { return false; }
	bool Fm4Opl3Device::canRender() { return false; }
	void Fm4Opl3Device::setVolume(f32 volume) {}
	void Fm4Opl3Device::message(u8 type, u8 arg1, u8 arg2) {}
	void Fm4Opl3Device::message(const u8* msg, u32 len) {}
	void Fm4Opl3Device::noteAllOff() {}
*/
};

// TFE_Game

void reticle_enable(bool enable) {}

namespace TFE_A11Y
{
	void drawCaptions(/*std::vector<Caption>* captions*/) {}
	void clearCaptions() {}
	bool cutsceneCaptionsEnabled() { return false; }
	bool gameplayCaptionsEnabled() { return false; }
	void onSoundPlay(char* name, CaptionEnv env) {}
}

namespace TFE_Console
{
	void registerCVarInt(const char* name, u32 flags, s32* var, const char* helpString) {}
	void registerCVarFloat(const char* name, u32 flags, f32* var, const char* helpString) {}
	void registerCVarBool(const char* name, u32 flags, bool* var, const char* helpString) {}
	void registerCVarString(const char* name, u32 flags, char* var, u32 maxLen, const char* helpString) {}
	void registerCommand(const char* name, ConsoleFunc func, u32 argCount, const char* helpString, bool repeat) {}

	void addSerializedCVarInt(const char* name, s32 value) {}
	void addSerializedCVarFloat(const char* name, f32 value) {}
	void addSerializedCVarBool(const char* name, bool value) {}
	void addSerializedCVarString(const char* name, const char* value) {}

	void addToHistory(const char* str) {}
	u32 getCVarCount() { return 0; }
	const CVar* getCVarByIndex(u32 index) { return nullptr; }
}