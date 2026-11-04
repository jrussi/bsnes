@echo off
rem Runs the built bsnes with the Otter pack (the "lontra" folder next to this file).
rem Usage: rodar_com_pack.bat   or   rodar_com_pack.bat "C:\path\Goof Troop (USA).sfc"
set "BSNES_SPRITE_PACK=%~dp0lontra"
if not exist "%~dp0..\bsnes\out\bsnes.exe" (
  echo bsnes.exe not found in bsnes\out. Build it first: cd bsnes ^&^& make -j8 openmp=false
  pause
  exit /b 1
)
"%~dp0..\bsnes\out\bsnes.exe" %*
