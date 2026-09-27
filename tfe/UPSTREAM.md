# Upstream

This directory is a vendored copy of The Force Engine (GPL-2.0), taken from
BSzili's Amiga port, which already strips desktop-only subsystems and fixes
big-endian issues (the N64 is big-endian too).

- Repository: https://github.com/BSzili/TheForceEngine
- Branch: `amiga`
- Commit: `d116b7a9efc29e8c9223db19d5e5ad743c8f8372`
- Original project: https://github.com/luciusDXL/TheForceEngine

Only the `TheForceEngine/` source tree was copied (without prebuilt Windows
libraries, fonts, sound fonts or documentation). N64-specific changes live in
`../src/` whenever possible; edits to files in this directory are kept small
and marked with `__N64__`.
