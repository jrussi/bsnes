# Sprite pack (replacing sprites with high-resolution images)

This fork of [bsnes](https://github.com/bsnes-emu/bsnes) (v115) adds a *texture-pack*-style feature to the `ppu-fast` renderer:
the game's sprites are grouped into characters, and each recognized group is replaced by a high-resolution PNG
drawn in an enlarged frame (4x by default: 1024x960), respecting the game's priority/layers. The bundled pack
(`sprite-pack/lontra`, "lontra" is Portuguese for otter) is an original character, the **Otter**, which mimics the walking and standing poses of the
player in **Goof Troop (USA)**.

> **Warning: you need a Goof Troop (USA) ROM.** It is **not** included in this repository or in the release,
> and it will not be provided. Use the ROM from your own cartridge. The pack only recognizes the USA version of the game.

## How to run (Windows)

### Option A: ready-made release (no compiling)

1. Download `bsnes-sprite-pack-v1.zip` from the [Releases](https://github.com/jrussi/bsnes/releases) page and extract it to a folder.
2. Have your **Goof Troop (USA)** ROM at hand (`.sfc` or `.smc`). Without it there is nothing to run.
3. Double-click `rodar_com_pack.bat` ("run with pack"). **Do not open `bsnes.exe` directly**: without the `.bat` the pack is not loaded.
4. In bsnes, use the menu *System > Load Game...* and pick the ROM.
   Or drag the ROM onto `rodar_com_pack.bat`.
5. In game, when the player character (side/front/back, walking or standing) is recognized, it appears as the Otter in high resolution.
   The default renderer (`ppu-fast`) is the one used; you don't need to change any settings.

### Option B: build it yourself

1. Build bsnes (MSYS2 MINGW64):
   ```
   pacman -S --needed mingw-w64-x86_64-gcc make
   cd bsnes
   make -j8 openmp=false
   ```
   The executable ends up at `bsnes/out/bsnes.exe`.
2. Have the **Goof Troop (USA)** ROM. Double-click `sprite-pack/rodar_com_pack.bat` and load the ROM from the menu, or run it from a terminal passing the ROM:
   ```
   sprite-pack\rodar_com_pack.bat "C:\path\Goof Troop (USA).sfc"
   ```
   The batch file only sets the `BSNES_SPRITE_PACK` variable to `sprite-pack\lontra` and starts `bsnes.exe`.

Without the variable, bsnes behaves like the original.

## Limitations

- Only works with the `ppu-fast` renderer (the default) and in normal mode (no hires, interlace or HD Mode 7).
- With the pack active the frame is output enlarged (the pack's scale) and bsnes' software filters are disabled.
- Group ids are hashes of the game's sprite pieces, so the bundled pack only recognizes Goof Troop (USA).
  Poses that are not in the pack show the original sprite.
- This is a proof of concept: the character replaces only the mapped poses; enemies and items are not replaced.

## Environment variables

- `BSNES_SPRITE_PACK`: folder containing `pack.txt` and the PNGs. Each `pack.txt` line: `id png scale width height margin`
  (`width`/`height` are the PNG dimensions in pixels; `margin` is the extra border in native pixels, 8 in the pack's images; all
  lines must use the same scale, from 2 to 8). With the pack active the emulator writes the logs `recognized-*.csv` and
  `unrecognized-*.csv` into that folder (groups seen that are or are not in the pack), which help find which poses are missing.
- `BSNES_SPRITE_DUMP`: an existing folder; writes each sprite piece as a PNG, a `manifest.csv` and `frames-*.csv` (the pieces of each frame).
  Useful for building new packs.

## Creating or regenerating a pack

In `sprite-pack/tools` (Python 3, `librsvg` with `rsvg-convert`, and Pillow):

- `listar_grupos.py frames.csv grupos.json` lists the sprite groups seen in a dump.
- `render_pack.py tabela.json grupos.json output_folder [scale]` renders the SVGs (in `svg/`) to PNG with the right margin and writes `pack.txt`.
  `tabela.json` links each group id to the SVG that replaces it. The SVG viewBox must match the group size in native pixels.
- `lontra.py` and `lontra2.py` generate the otter SVGs (side, front and back poses).

The bundled pack was generated with `python3 render_pack.py tabela.json grupos.json ../lontra 4`.

## Emulator files changed

- `bsnes/sfc/ppu-fast/spritedump.cpp` (new): dump, grouping, pack loading and composition.
- `bsnes/sfc/ppu-fast/ppu.cpp`, `ppu.hpp`, `line.cpp`: hooks for the dump and a second pass without sprites (`skipObjects`/`renderRedirect`).
- `nall/arithmetic.hpp`: `#include <stdexcept>` (needed to build with recent GCC).

License: GPLv3 or later, same as bsnes. The otter images are original to this fork.
