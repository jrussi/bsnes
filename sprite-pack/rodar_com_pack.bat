@echo off
rem Roda o bsnes compilado com o pack da Lontra (pasta "lontra" ao lado deste arquivo).
rem Uso: rodar_com_pack.bat   ou   rodar_com_pack.bat "C:\caminho\Goof Troop (USA).sfc"
set "BSNES_SPRITE_PACK=%~dp0lontra"
if not exist "%~dp0..\bsnes\out\bsnes.exe" (
  echo bsnes.exe nao encontrado em bsnes\out. Compile antes: cd bsnes ^&^& make -j8 openmp=false
  pause
  exit /b 1
)
"%~dp0..\bsnes\out\bsnes.exe" %*
