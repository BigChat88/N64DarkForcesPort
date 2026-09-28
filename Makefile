BUILD_DIR = build
SOURCE_DIR = .
include $(N64_INST)/include/n64.mk

ROM_NAME = darkforces64
TFE = tfe

# The ROM is built next to the Makefile (n64.mk's %.z64 rule) and copied to output/,
# where build.cmd / tools/pack_rom.py also write it. The copy is not a %.z64 target:
# n64.mk's pattern specific flags would be applied twice to the link.
all: $(ROM_NAME).z64
	@mkdir -p output
	@echo "    [COPY] output/$(ROM_NAME).z64"
	@cp $< output/$(ROM_NAME).z64
.PHONY: all

# The Force Engine is compiled with the Amiga low-spec code paths enabled
# (__AMIGA__), plus __N64__ to replace the few spots that call AmigaOS APIs.
TFE_DEFINES = -D__AMIGA__ -D__N64__ -DNDEBUG

# Development: track heap owners for out-of-memory reports (costs ~96KB of RAM).
N64_MEMTRACK ?= 0
TFE_DEFINES += -DN64_MEMTRACK=$(N64_MEMTRACK)

# Testing: start directly in a level (e.g. make N64_START_LEVEL=TALAY) and/or
# show the development overlay from boot.
N64_DEBUG_OVERLAY ?= 0
TFE_DEFINES += -DN64_DEBUG_OVERLAY=$(N64_DEBUG_OVERLAY)

# Performance: draw wall columns in strips of 16 (see rwallFixed.cpp), and/or show
# a frames per second counter in the corner of the screen.
N64_WALL_STRIPS ?= 1
N64_SHOW_FPS ?= 0
TFE_DEFINES += -DN64_WALL_STRIPS=$(N64_WALL_STRIPS) -DN64_SHOW_FPS=$(N64_SHOW_FPS)

# Performance: show the average milliseconds per frame spent in each part of the
# frame (renderer, game logic, audio, ...) in the top right corner, see profile_n64.h.
N64_PROFILE ?= 0
TFE_DEFINES += -DN64_PROFILE=$(N64_PROFILE)

ifneq ($(N64_START_LEVEL),)
TFE_DEFINES += -DN64_START_LEVEL=\"$(N64_START_LEVEL)\"
endif
TFE_WARNINGS = -Wno-error -Wno-unused-variable -Wno-switch -Wno-sign-compare \
	-Wno-class-memaccess -Wno-format-truncation -Wno-stringop-truncation \
	-Wno-misleading-indentation -Wno-parentheses -Wno-unused-value

N64_CXXFLAGS += $(TFE_DEFINES) $(TFE_WARNINGS) -I$(TFE) -Isrc/n64 -Isrc/n64/compat \
	-fno-exceptions -fno-rtti -fno-strict-aliasing
N64_CFLAGS += $(TFE_DEFINES) -Wno-error -I$(TFE) -Isrc/n64 -Isrc/n64/compat

# Code is loaded in RAM, so everything outside the per-frame hot paths (renderer,
# math, collision) is optimized for size: it leaves more memory for the levels.
TFE_SIZE_OPT_DIRS = TFE_DarkForces TFE_Asset TFE_Archive TFE_FileSystem TFE_Game \
	TFE_Settings TFE_System TFE_Input TFE_Audio TFE_Outlaws TFE_Memory TFE_Polygon \
	TFE_RenderShared TFE_Jedi/InfSystem TFE_Jedi/Level TFE_Jedi/Serialization TFE_Jedi/IMuse
$(foreach d,$(TFE_SIZE_OPT_DIRS),$(BUILD_DIR)/$(TFE)/$(d)/%.o): CXXFLAGS += -Os

# The sound cache is purgeable memory, see src/n64/malloc_n64.cpp.
N64_LDFLAGS += --wrap=malloc --wrap=calloc --wrap=realloc --wrap=memalign --wrap=free

# ---------------------------------------------------------------------------
# N64 platform layer (replaces tfe/amiga)
# ---------------------------------------------------------------------------
N64_SRC = $(wildcard src/n64/*.cpp)

# ---------------------------------------------------------------------------
# The Force Engine sources (same selection as tfe/Makefile.amiga)
# ---------------------------------------------------------------------------
ARCHIVE_SRC = \
	TFE_Archive/archive.cpp \
	TFE_Archive/gobArchive.cpp \
	TFE_Archive/labArchive.cpp \
	TFE_Archive/lfdArchive.cpp

ASSET_SRC = \
	TFE_Asset/assetSystem.cpp \
	TFE_Asset/dfKeywords.cpp \
	TFE_Asset/modelAsset_jedi.cpp \
	TFE_Asset/spriteAsset_Jedi.cpp \
	TFE_Asset/vueAsset.cpp

AUDIO_SRC = \
	TFE_Audio/midiPlayer.cpp

DARKFORCES_SRC = \
	$(addprefix TFE_DarkForces/, \
		agent.cpp animLogic.cpp automap.cpp briefingList.cpp cheats.cpp config.cpp \
		darkForcesMain.cpp gameMessage.cpp gameMusic.cpp generator.cpp hitEffect.cpp \
		hud.cpp item.cpp logic.cpp mission.cpp pickup.cpp player.cpp playerCollision.cpp \
		projectile.cpp random.cpp sound.cpp time.cpp updateLogic.cpp util.cpp \
		vueLogic.cpp weapon.cpp weaponFireFunc.cpp) \
	$(addprefix TFE_DarkForces/Actor/, \
		actor.cpp actorSerialization.cpp animTables.cpp bobaFett.cpp dragon.cpp \
		enemies.cpp exploders.cpp flyers.cpp mousebot.cpp phaseOne.cpp phaseThree.cpp \
		phaseTwo.cpp scenery.cpp sewer.cpp troopers.cpp turret.cpp welder.cpp) \
	$(addprefix TFE_DarkForces/GameUI/, \
		agentMenu.cpp delt.cpp editBox.cpp escapeMenu.cpp menu.cpp missionBriefing.cpp \
		pda.cpp uiDraw.cpp) \
	$(addprefix TFE_DarkForces/Landru/, \
		cutscene.cpp cutsceneList.cpp cutscene_film.cpp cutscene_player.cpp lactor.cpp \
		lactorAnim.cpp lactorCust.cpp lactorDelt.cpp lcanvas.cpp ldraw.cpp lfade.cpp \
		lfont.cpp lmusic.cpp lpalette.cpp lrect.cpp lsound.cpp lsystem.cpp ltimer.cpp \
		lview.cpp textCrawl.cpp)

FILESYSTEM_SRC = \
	TFE_FileSystem/filestream-posix.cpp \
	TFE_FileSystem/paths-posix.cpp \
	TFE_FileSystem/memorystream.cpp

GAME_SRC = \
	TFE_Game/igame.cpp \
	TFE_Game/saveSystem.cpp

INPUT_SRC = \
	TFE_Input/input.cpp \
	TFE_Input/inputMapping.cpp

JEDI_SRC = \
	TFE_Jedi/Collision/collision.cpp \
	$(addprefix TFE_Jedi/IMuse/, \
		imConst.cpp imDigitalSound.cpp imList.cpp imMidiCmd.cpp imMidiPlayer.cpp \
		imSoundFader.cpp imTrigger.cpp imuse.cpp midiData.cpp) \
	TFE_Jedi/InfSystem/infState.cpp \
	TFE_Jedi/InfSystem/infSystem.cpp \
	TFE_Jedi/InfSystem/message.cpp \
	$(addprefix TFE_Jedi/Level/, \
		level.cpp levelData.cpp levelTextures.cpp rfont.cpp robjData.cpp robject.cpp \
		roffscreenBuffer.cpp rsector.cpp rtexture.cpp rwall.cpp) \
	TFE_Jedi/Math/core_math.cpp \
	TFE_Jedi/Math/cosTable.cpp \
	TFE_Jedi/Memory/allocator.cpp \
	TFE_Jedi/Memory/list.cpp \
	TFE_Jedi/Serialization/serialization.cpp \
	TFE_Jedi/Task/task.cpp

OUTLAWS_SRC = \
	TFE_Outlaws/outlawsMain.cpp

RENDFIXED_SRC = \
	$(addprefix TFE_Jedi/Renderer/RClassic_Fixed/, \
		rclassicFixed.cpp rclassicFixedSharedState.cpp redgePairFixed.cpp rflatFixed.cpp \
		rlightingFixed.cpp rsectorFixed.cpp rwallFixed.cpp) \
	$(addprefix TFE_Jedi/Renderer/RClassic_Fixed/robj3d_fixed/, \
		robj3dFixed.cpp robj3dFixed_Clipping.cpp robj3dFixed_Culling.cpp \
		robj3dFixed_PolygonDraw.cpp robj3dFixed_PolygonSetup.cpp \
		robj3dFixed_TransformAndLighting.cpp)

REND_SRC = \
	TFE_Jedi/Renderer/jediRenderer.cpp \
	TFE_Jedi/Renderer/rcommon.cpp \
	TFE_Jedi/Renderer/rscanline.cpp \
	TFE_Jedi/Renderer/rsectorRender.cpp \
	TFE_Jedi/Renderer/screenDraw.cpp \
	TFE_Jedi/Renderer/virtualFramebuffer.cpp \
	$(RENDFIXED_SRC)

MEMORY_SRC = TFE_Memory/chunkedArray.cpp
POLYGON_SRC = TFE_Polygon/clipper.cpp TFE_Polygon/polygon.cpp
RENDERSHARED_SRC = TFE_RenderShared/lineDraw2d.cpp TFE_RenderShared/quadDraw2d.cpp
SETTINGS_SRC = TFE_Settings/settings.cpp

SYSTEM_SRC = \
	TFE_System/log.cpp \
	TFE_System/math.cpp \
	TFE_System/parser.cpp \
	TFE_System/system.cpp

TFE_SRC = $(AUDIO_SRC) $(INPUT_SRC) $(REND_SRC) $(DARKFORCES_SRC) $(OUTLAWS_SRC) \
	$(ASSET_SRC) $(MEMORY_SRC) $(GAME_SRC) $(JEDI_SRC) $(SETTINGS_SRC) $(ARCHIVE_SRC) \
	$(FILESYSTEM_SRC) $(SYSTEM_SRC)

OBJS = $(addprefix $(BUILD_DIR)/,$(N64_SRC:.cpp=.o)) \
	$(addprefix $(BUILD_DIR)/$(TFE)/,$(TFE_SRC:.cpp=.o))

# ---------------------------------------------------------------------------
# ROM
# ---------------------------------------------------------------------------
# The original game data is NOT part of this repository. Copy your own
# Dark Forces files (DARK.GOB, SOUNDS.GOB, SPRITES.GOB, TEXTURES.GOB, *.LFD,
# LOCAL.MSG, ...) into gamedata/ before building; they are packed into the
# ROM filesystem as rom:/<NAME>.
N64_MKDFS_ROOT = gamedata
GAMEDATA = $(wildcard gamedata/*)

N64_ROM_TITLE = "Dark Forces 64"
N64_ROM_SAVETYPE = sram256k
N64_ROM_EXPANSIONPAK = required

$(BUILD_DIR)/$(ROM_NAME).dfs: $(GAMEDATA)

# Music: the project's General MIDI SoundFont (soundfont/SC55.sf2, see its
# README) is converted to rom:/MUSIC.SF64.
SOUNDFONT = soundfont/SC55.sf2
MUSIC_SF64 = gamedata/MUSIC.SF64
$(MUSIC_SF64): $(SOUNDFONT)
	@mkdir -p $(BUILD_DIR)/sf64
	@echo "    [SF64] $<"
	$(N64_AUDIOCONV) -o $(BUILD_DIR)/sf64 "$<"
	mv $(BUILD_DIR)/sf64/*.sf64 $@
$(BUILD_DIR)/$(ROM_NAME).dfs: $(MUSIC_SF64)

# Boot intro: the libdragon dragon logo (assets/intro, see its README) is converted
# to rom:/intro/*.sprite.
INTRO_SPRITES = $(patsubst assets/intro/%.png,gamedata/intro/%.sprite,$(wildcard assets/intro/*.png))
gamedata/intro/%.sprite: assets/intro/%.png
	@mkdir -p gamedata/intro
	@echo "    [SPRITE] $@"
	$(N64_MKSPRITE) -f I8 -o gamedata/intro "$<"
$(BUILD_DIR)/$(ROM_NAME).dfs: $(INTRO_SPRITES)

$(BUILD_DIR)/$(ROM_NAME).elf: $(OBJS)
$(ROM_NAME).z64: $(BUILD_DIR)/$(ROM_NAME).dfs

# ---------------------------------------------------------------------------
# Prebuilt engine (no game data needed)
# ---------------------------------------------------------------------------
# The compiled engine is the same for everyone: only the ROM filesystem holds
# the user's Dark Forces files. `make engine` builds just the code, the same
# way n64.mk's %.z64 rule does, plus the intro sprites, so CI can publish them
# in a Release (see .github/workflows/release.yml) and tools/pack_rom.py can
# pack a ROM without the N64 toolchain.
ENGINE_DIR = $(BUILD_DIR)/engine
ENGINE_ELF = $(BUILD_DIR)/$(ROM_NAME).elf

# n64.mk only switches to the N64 toolchain for %.z64 targets.
engine: CC=$(N64_CC)
engine: CXX=$(N64_CXX)
engine: AS=$(N64_AS)
engine: LD=$(N64_LD)
engine: CFLAGS+=$(N64_CFLAGS)
engine: CXXFLAGS+=$(N64_CXXFLAGS)
engine: ASFLAGS+=$(N64_ASFLAGS)
engine: RSPASFLAGS+=$(N64_RSPASFLAGS)
engine: LDFLAGS+=$(N64_LDFLAGS)
engine: $(ENGINE_ELF) $(INTRO_SPRITES)
	@echo "    [ENGINE] $(ENGINE_DIR)"
	@rm -rf $(ENGINE_DIR)
	@mkdir -p $(ENGINE_DIR)/intro
	$(N64_SYM) --all $< $<.sym
	cp $< $<.stripped
	$(N64_STRIP) -s $<.stripped
	$(N64_ELFCOMPRESS) -o $(dir $<) -c $(N64_ROM_ELFCOMPRESS) $<.stripped
	cp $<.stripped $(ENGINE_DIR)/$(ROM_NAME).elf.stripped
	cp $<.sym $(ENGINE_DIR)/$(ROM_NAME).elf.sym
	cp $(INTRO_SPRITES) $(ENGINE_DIR)/intro/
	$(if $(N64_TOOLFILES),cp $(N64_TOOLFILES) $(ENGINE_DIR)/)
	@# ROM header settings, so pack_rom.py stays in sync with this Makefile.
	@printf 'title=%s\nsavetype=%s\nexpansionpak=%s\nregionfree=%s\n' \
		$(N64_ROM_TITLE) $(strip $(N64_ROM_SAVETYPE)) $(strip $(N64_ROM_EXPANSIONPAK)) \
		$(if $(strip $(N64_ROM_REGIONFREE)),1,0) > $(ENGINE_DIR)/rom.cfg
.PHONY: engine

clean:
	rm -rf $(BUILD_DIR) *.z64 output/$(ROM_NAME).z64
.PHONY: clean

-include $(shell find $(BUILD_DIR) -name '*.d' 2>/dev/null)
