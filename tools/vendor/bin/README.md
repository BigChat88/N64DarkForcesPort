# tools/vendor/bin

The libdragon host tools `tools/pack_rom.py` runs:

| tool | job |
|---|---|
| `mkdfs` | pack the game files into the DragonFS filesystem image |
| `n64tool` | assemble engine + symbols + filesystem into the `.z64` |
| `ed64romconfig` | write the save type / Expansion Pak flags into the ROM header |
| `audioconv64` | convert your SoundFont (`.sf2`) to `MUSIC.SF64` |

`pack_rom.py` looks for them in `--tools-dir`, `$N64_INST/bin`, this folder
and then `PATH`.

## Getting them

* **Download a [Release](../../../../releases)**: it ships Windows `.exe`s and
  Linux x86-64 binaries here, plus the matching prebuilt engine (see
  `../engine/README.md`).
* **Build them yourself** from the `libdragon` submodule with a native C and
  C++ compiler (no Docker needed):

  ```sh
  ./build-tools.sh                                                        # gcc/g++ on PATH
  CC=x86_64-w64-mingw32-gcc CXX=x86_64-w64-mingw32-g++ ./build-tools.sh   # cross to Windows
  ```
