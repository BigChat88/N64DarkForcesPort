@echo off
rem  build.cmd -- one-click Dark Forces 64 ROM build.
rem
rem  1. copy your Dark Forces files (DARK.GOB, SOUNDS.GOB, SPRITES.GOB,
rem     TEXTURES.GOB, LOCAL.MSG and the LFD folder) into  gamedata\
rem     (or a .zip of your install folder)
rem  2. optional: put a General MIDI SoundFont (.sf2) in  soundfont\
rem  3. run this file
rem  4. the ROM appears in  output\darkforces64.z64
rem
rem  Extra options are passed straight through, e.g.:
rem     build.cmd --gamedata "C:\Games\Dark Forces\Game"
setlocal
cd /d "%~dp0"

set "PY=python"
where python >nul 2>&1 || set "PY=py -3"

%PY% tools\pack_rom.py %*
set "RC=%ERRORLEVEL%"

echo.
if not "%RC%"=="0" (
    echo Build FAILED ^(exit %RC%^).
) else (
    echo Done. Check output\ for your ROM.
)
pause
exit /b %RC%
