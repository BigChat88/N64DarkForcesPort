# tools/vendor/engine

The prebuilt engine `tools/pack_rom.py` packs into the ROM:

| file | what |
|---|---|
| `darkforces64.elf.stripped` | the compiled game (compressed ELF) |
| `darkforces64.elf.sym` | symbols for crash backtraces |
| `intro/*.sprite` | the boot intro's dragon logo |
| `libdragon.version` | libdragon version stamp |
| `rom.cfg` | ROM header settings (title, save type, Expansion Pak) |

They contain no game data and are the same for everyone, so they are built
once and reused.

Get them from:

* **A [Release](../../../../releases)**: CI builds them from source on every
  push to `main` and ships them in this folder.
* **`make engine`** at the repo root, inside `libdragon exec` (needs Docker +
  the `libdragon` CLI). The result lands in `build/engine/`, which
  `pack_rom.py` also searches, so there is no need to copy it here.
