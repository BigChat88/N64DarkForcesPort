#!/usr/bin/env bash
# build-tools.sh -- build the libdragon host tools tools/pack_rom.py needs
# (n64tool, mkdfs, ed64romconfig, audioconv64), natively, straight from the
# libdragon submodule's own tools/Makefile.
#
# That Makefile always links through $CXX (even for the C-only tools), so a
# C++ compiler is needed too, not just a C one.
#
#   ./build-tools.sh                                                        # native gcc/g++
#   CC=x86_64-w64-mingw32-gcc CXX=x86_64-w64-mingw32-g++ ./build-tools.sh   # Windows cross-build
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
TOOLS="$HERE/../../../libdragon/tools"
CC="${CC:-gcc}"
CXX="${CXX:-g++}"

[ -d "$TOOLS" ] || { echo "libdragon not checked out ($TOOLS) - run: git submodule update --init"; exit 1; }

# Same check as libdragon/tools/Makefile: a mingw compiler means *.exe output.
EXE=""
case "$("$CC" -dumpmachine 2>/dev/null)" in
    *mingw*)
        EXE=".exe"
        # Link fully static: cross-compiling from Linux, libdragon's Makefile
        # doesn't add -static (it only does when running *on* Windows), and
        # audioconv64.exe would then need libwinpthread-1.dll/libstdc++-6.dll,
        # which a normal Windows PC doesn't have. Passed via the environment
        # so the Makefile's own "LDFLAGS += -pthread" still appends to it.
        export LDFLAGS="${LDFLAGS:-} -static"
        ;;
esac

set -x
# Always rebuild from clean: tools share objects under common/, and make can't
# tell they were built by a different compiler/target before.
make -C "$TOOLS" clean
make -C "$TOOLS" CC="$CC" CXX="$CXX" n64tool mkdfs ed64romconfig audioconv64
set +x

cp "$TOOLS/n64tool$EXE"                 "$HERE/n64tool$EXE"
cp "$TOOLS/mkdfs/mkdfs$EXE"             "$HERE/mkdfs$EXE"
cp "$TOOLS/ed64romconfig$EXE"           "$HERE/ed64romconfig$EXE"
cp "$TOOLS/audioconv64/audioconv64$EXE" "$HERE/audioconv64$EXE"

# Refuse to ship a Windows binary that needs a DLL Windows doesn't provide.
if [ -n "$EXE" ]; then
    OBJDUMP="${CC%gcc}objdump"
    command -v "$OBJDUMP" >/dev/null || OBJDUMP=objdump
    bad=0
    for tool in n64tool mkdfs ed64romconfig audioconv64; do
        dlls="$("$OBJDUMP" -p "$HERE/$tool.exe" | sed -n 's/^\s*DLL Name: //p')"
        echo "$tool.exe imports: $(echo $dlls)"
        for dll in $dlls; do
            case "$(echo "$dll" | tr 'A-Z' 'a-z')" in
                kernel32.dll|msvcrt.dll|ntdll.dll|user32.dll|advapi32.dll|shell32.dll|ws2_32.dll|ucrtbase.dll|api-ms-win-*) ;;
                *) echo "error: $tool.exe depends on non-system DLL $dll"; bad=1 ;;
            esac
        done
    done
    [ "$bad" -eq 0 ] || exit 1
fi

echo "built: n64tool mkdfs ed64romconfig audioconv64 -> $HERE"
