# Dark Forces 64

A port of **Star Wars: Dark Forces** to the Nintendo 64, built on
[The Force Engine](https://github.com/luciusDXL/TheForceEngine) and
[libdragon](https://github.com/DragonMinded/libdragon) (the open-source N64
homebrew SDK), plus a tool that turns your own copy of the game into a
runnable `.z64`.

No copyrighted game data is in this repository. You supply your own Dark
Forces files; the build packs them into the ROM.

## What you need to play

* An **[Expansion Pak](https://en.wikipedia.org/wiki/Nintendo_64_accessories#Expansion_Pak)**
  (4MB RAM upgrade), on real hardware or the equivalent option in your
  emulator. The game needs it and won't boot without one.
* Saves go to the cartridge's SRAM (256 kbit): your flashcart or emulator
  handles that automatically.

## The game files you need

A **legally obtained copy of Dark Forces** (the original CD, GOG or Steam
release). From its install folder you need:

* `DARK.GOB`, `SOUNDS.GOB`, `SPRITES.GOB`, `TEXTURES.GOB`
* `LOCAL.MSG`
* the `LFD` folder (`*.LFD`)

Copy them into `gamedata/`, keeping `LFD/` as a subfolder. Letter case doesn't
matter. You can also drop a single `.zip` of your install folder into
`gamedata/`, or point the packer at the install folder directly with
`--gamedata`.

### Music (optional)

Dark Forces' music is General MIDI. Put a SoundFont 2 file (`.sf2`) in
`soundfont/` and it is converted and packed into the ROM; a Roland SC-55 style
General MIDI SoundFont sounds closest to the original. Without one, the game
runs without music. See [soundfont/README.md](soundfont/README.md).

## How to build the ROM

You only need **[Python 3](https://www.python.org/downloads/)** on your `PATH`
(`python --version`). No Docker or N64 toolchain.

1. Grab the latest [Release](../../releases) and unzip it.
2. Put your Dark Forces files in `gamedata/` (see above), and optionally a
   SoundFont in `soundfont/`.
3. Build it:
   * **Windows:** double-click `build.cmd` (or run it from a terminal).
   * **Any platform:** `python tools/pack_rom.py`
4. Wait for `OK -> output/darkforces64.z64`. That file is your ROM: run it in
   an emulator (e.g. [Ares](https://ares-emu.net/),
   [simple64](https://simple64.github.io/)) with the Expansion Pak enabled, or
   copy it to a flashcart (e.g. EverDrive-64).

Other sources for the game files:

```sh
python tools/pack_rom.py --gamedata "C:/Games/Dark Forces/Game"
python tools/pack_rom.py --gamedata path/to/dark-forces.zip --soundfont path/to/gm.sf2
```

The Release ships the host tools for Windows and Linux x86-64. On other
platforms, build them with `tools/vendor/bin/build-tools.sh` (see
[tools/vendor/bin/README.md](tools/vendor/bin/README.md)).

## Controls

| Button | Action |
|---|---|
| Analog stick | Move forward/back and turn |
| C-Left / C-Right | Strafe |
| C-Up | Center view |
| C-Down | Crouch |
| Z | Fire |
| R + Z | Secondary fire |
| A | Jump |
| B (hold) | Run |
| R (tap) | Use / open doors |
| R + C-Up / R + C-Down | Look up / down |
| D-Left / D-Right | Previous / next weapon |
| D-Down | Head lamp |
| R + D-Up / D-Down / D-Left / D-Right | Goggles / gas mask / ice cleats / center view |
| L | Automap (R + D-pad zooms and changes layer while it is shown) |
| Start | Menu |
| R + Start | PDA |

## Building from source

Working on the engine itself (the C++ code in `src/` and `tfe/`) needs the
full N64 toolchain, which runs in Docker via the `libdragon` CLI:

* **[Docker](https://www.docker.com/)**
* **The `libdragon` CLI** (`npm install -g libdragon`, see
  `.libdragon/config.json`)

Clone with the `libdragon` submodule:

```sh
git clone --recurse-submodules https://github.com/BigChat88/N64DarkForcesPort.git
cd N64DarkForcesPort
libdragon install        # first time only: builds the pinned libdragon into the container
```

With the game files in `gamedata/` (and optionally a SoundFont in
`soundfont/`):

```sh
libdragon make -j8
```

builds `darkforces64.z64` straight from source. Useful options (see the
`Makefile`):

| Option | Effect |
|---|---|
| `N64_START_LEVEL=TALAY` | start directly in a level |
| `N64_DEBUG_OVERLAY=1` | show the development overlay from boot (L + R + C-Down toggles it) |
| `N64_SHOW_FPS=1` | frames per second counter |
| `N64_MEMTRACK=1` | track heap owners for out-of-memory reports |

`libdragon make engine` builds just the data-independent part (compiled
engine + intro sprites + ROM header settings) into `build/engine/`, the same
thing CI publishes in each Release. `tools/pack_rom.py` picks it up from
there automatically.

## Releases and versioning

The project version lives in [`VERSION`](VERSION). Every push to `main`
(including merged pull requests) runs
[`.github/workflows/release.yml`](.github/workflows/release.yml), which builds
the engine and the host tools from source and publishes the Release
`v<VERSION>` with a ready-to-use `N64DarkForcesPort-<VERSION>.zip`. Bump
`VERSION` in the pull request to publish a new Release; pull requests are
built as a check without publishing.

## Acknowledgements

* **[luciusDXL](https://github.com/luciusDXL)** for
  **[The Force Engine](https://github.com/luciusDXL/TheForceEngine)**, the
  reverse-engineered Dark Forces engine this port runs.
* **[BSzili](https://github.com/BSzili)** for the
  [Amiga port of The Force Engine](https://github.com/BSzili/TheForceEngine/tree/amiga),
  which this port starts from (low-spec code paths and big-endian fixes; see
  [tfe/UPSTREAM.md](tfe/UPSTREAM.md)).
* The **[libdragon](https://github.com/DragonMinded/libdragon)** team for the
  open-source N64 SDK.
* The boot intro's dragon logo comes from the N64brew-GameJam2024 repository
  (MIT), see [assets/intro/README.md](assets/intro/README.md).

## License

GPL-2.0, like The Force Engine (see [LICENSE](LICENSE)). Star Wars: Dark
Forces and its data are property of Lucasfilm; they are not included here.

## AI Note

The application was developed using AI. I'm just an enthusiast who wanted to create interesting projects. In this case, how it was achieved is not relevant to me.
