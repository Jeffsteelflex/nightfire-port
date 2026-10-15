# 007: Nightfire — native PC port

**Version 0.5 (October 2026) — work in progress.**

A native Windows rebuild of *007: Nightfire* for the PlayStation 2 (NTSC-U, SLUS-20579), with a first macOS version
for testing. This is not an emulator.

The game is rebuilt from its own executables:
- the data formats are reverse-engineered;
- the game logic is decompiled function by function into C++;
- the PS2 hardware layer (GS, VU0/VU1, IOP, SPU2, CD) is replaced by a PC implementation (Direct3D 11, XAudio2, XInput;
  on macOS Metal, Core Audio and GameController).

The aim is to play the original game the way it was made, plus the PC features it lacks: any resolution and aspect ratio, mouse and keyboard, high frame rates.

> **No game data is included.** You need your own copy of the US PS2 disc. Nothing extracted from the disc (models,
> textures, sounds, movies, executables) is or will be in this repository.

## Status

### Works

- **On-foot campaign.** Every level loads and runs on the game's own code: the player, weapons and gadgets, AI with
  vision, combat and cover, objectives, cutscenes, HUD, score and medals. A full playthrough of every mission has not
  been verified yet.
- **Rendering.** The PS2's lighting, fog and environment maps, skinned characters with talking faces, particles,
  shattering glass, weather; any resolution and aspect ratio, above 60 fps (frames interpolated between the 60 Hz
  ticks), MSAA, supersampling, PCSX2-format HD texture packs.
- **Menus.** The game's own front end, codenames, saves, dossier and credits, in two styles: Original (as on the PS2)
  and Modern (redesigned, native resolution). A launcher before the game picks the disc and the display settings.
- **Audio and movies.** The game's sound driver and the SPU2 in software: effects, 3D sound, reverb, speech with
  subtitles, interactive music. The PSS movies play where the game plays them.
- **PC controls.** Raw mouse and keyboard, all rebindable; XInput pads through the game's own pad layer; mouse in every
  menu; optional aim-down-sights and a weapon wheel. New in 0.5: the guided missile is steered by the mouse.
- **Disc.** An `.iso`, a DVD drive or the extracted files; only the US version.
- **Performance.** 60 fps on integrated GPUs is the target; a Radeon iGPU runs 144 fps or more at 1080p.

### In progress

- **Driving missions** (Paris, Alpine Escape, Enemies Vanquished, Deep Descent, Island Infiltration). They run in a
  separate EA engine (`DRIVING.ELF`), being ported function by function (about 1,700 so far, each checked against the
  original code in an emulated CPU). The game still skips them: they count as done without a medal. In the port's test
  tool (`nf_driving`, build only) **Paris, the first mission, plays from start to end**: the helicopter section, the car
  chase, the enemy cars and their gunmen, weapons, explosions, the damage, speech and music, won or lost by play
  (tested with scripted input). Left for Paris: the mission's movies and real HUD, some effects and sounds, a few
  camera modes; then the other four missions, and running them inside the game.
- **macOS.** A universal app (`Nightfire PC.app`, Apple Silicon and Intel, macOS 11+) built and tested by CI on GitHub's
  Mac machines. **Not yet played on a real Mac**: reports are welcome (see "On a Mac" below).
- **Decompilation of `ACTION.ELF`** (the on-foot game): 2,117 of 2,550 functions (80% of its code).
- **To check on real hardware:** a physical controller (only scripted pad input is tested), and a few fog and tint
  details against the PS2.

### Not started or parked

- Multiplayer (planned last).
- The RTX Remix path (`--renderer d3d9ff`) is an early prototype and is parked.

## Requirements

- Windows 10 or 11.
- A GPU with Direct3D 11. Integrated GPUs are fine.
- Or, for the Mac build (first version, not yet tested on a real Mac): macOS 11 or newer, Apple Silicon or Intel.
- Your own copy of the game: *007: Nightfire*, PS2, US version (SLUS-20579).
- To build (not needed to play with the release zip):
  - Visual Studio Build Tools with the C++ workload (MSVC, x86 target), CMake and Ninja;
  - Python 3.12 for the tools (standard library only).

## Building

The port builds as a 32-bit (x86) program on purpose: game structures keep their PS2 layouts, with 4-byte pointers.

```bat
build.bat Release
```

The executables land in `build\Release\`:

| Executable | What it is |
|------------|------------|
| `nightfire.exe` | the game |
| `nf_tests.exe` | the unit tests (no game data needed) |
| `nf_level_check.exe` | a regression check on your data: every level |
| `nf_loader_check.exe` | a regression check on your data: the original loader |
| `nf_disc_check.exe` | compares two copies of the disc file by file (the `.iso` against the extracted files, the drive) |

`build.bat Release x64` builds the 64-bit version into `build\x64\Release\` (experimental: it passes the same checks).
`python tools/make_release.py` packs both builds into the release zip (`out\release\`), with nothing from the game.
The Mac build (`Nightfire PC.app`, universal) and its zip are made by the CI on GitHub's macOS runners
(`tools/make_release_mac.py`; see `docs/port.md`, "macOS: o pacote e a instalação").

## Game data

The game reads your disc wherever it is: a disc image (`.iso`), the disc in a DVD drive, or the files extracted into a
folder. For development, extract the contents of your disc into the repository root (`ACTION.ELF`, `BASE.ELF`,
`FILES.BIN`, `DRIVING\`, `MODULES\`, `MOVIES\`, `PS2\` and the rest; ignored by git): `build\Release\nightfire.exe` finds
them there. Only the US version (SLUS-20579) is supported; the program checks `SYSTEM.CNF` and tells you if a disc is
another version.

## Running

**From the release zip** (no build tools needed):

1. Unzip it anywhere and run `nightfire.exe` (the 32-bit build, the reference; `nightfire64.exe` is the experimental
   64-bit build).
2. A launcher opens before the game, with **Play** selected: press Enter (or A/Start on a controller, or click Play)
   to start. The first time it asks for your disc: the **Disc** row opens a file picker for the `.iso` of your
   *007: Nightfire* PS2 disc (USA), or **Use the disc in drive X:** reads the disc in your DVD drive (the choice is
   saved in `nightfire.ini`; an `.iso` placed next to `nightfire.exe` is found on its own). The launcher also sets the
   resolution, the display mode (windowed, borderless or exclusive fullscreen) and VSync, the menu style (Modern or
   Original) and the HD 2D layer; **Play** saves your changes in `nightfire.ini`, **Quit** leaves without saving.
3. Turn **Show at startup** off to start the game directly next time; hold **Shift** while the game starts to see the
   launcher again.
4. Optional: **Single-file copy** writes one `.exe` with your game inside (about 1.8 GB) to play without the disc
   image. It contains your copy of the game: it is for you only, do not share it.

**On a Mac** (experimental, from 0.4: the zip is `Nightfire-PC-mac-<version>.zip`, next to the Windows one): unzip it, drag `Nightfire PC.app` to Applications and follow the `README.txt` inside. The app is not
signed by Apple, so the first start needs "Open Anyway" in System Settings > Privacy & Security (macOS 15 and newer) or
Control-click > Open (macOS 11 to 14). The disc can be a Disk Utility image (`.cdr`, "DVD/CD master"), an `.iso` or the
disc in an external DVD drive. Settings, saves and a log for bug reports (`nightfire.log`) are in
`~/Library/Application Support/Nightfire PC/`.

**From a build:**

```bat
build\Release\nightfire.exe                                   :: the launcher, then the game's front end (Press START, codenames...)
build\Release\nightfire.exe --level 07000001                  :: start a level directly
build\Release\nightfire.exe --disc "D:\007 - Nightfire.iso"   :: use this disc: an .iso, a folder, a drive ("E:")
build\Release\nightfire.exe --launcher                        :: show the launcher (also --setup); other arguments skip it
build\Release\nightfire.exe --pack-disc <disc> --out <file.exe>   :: make the single-file copy (never inside a git repository)
```

Without `--disc`, the program looks for the disc in this order: a single-file copy (itself), the folder of the
executable (the extracted files or an `.iso`), the current folder and the folders above the executable (the repository
root, for developers), `[game] disc` in `nightfire.ini`, and the DVD drives.

Settings are kept in `nightfire.ini` next to the executable. Saves go to `saves\`. Both can be changed in the in-game
OPTIONS.

Default controls:

| Action | Keys |
|--------|------|
| Move | WASD |
| Look | Mouse |
| Fire / aim | Left / right mouse button |
| Use | E |
| Reload | R |
| Jump / crouch | Space / C or Ctrl |
| Weapons | Mouse wheel, Q/Z |
| Gadgets | 1-5, G |
| Weapon wheel | Hold Tab |
| Pause | Esc |

## Repository layout

| Path | Contents |
|------|----------|
| `src/engine` | the game logic decompiled from `ACTION.ELF` (each function marked with its original address) |
| `src/platform` | the PC replacement of the PS2 platform layer: files, input, pad, memory card, sound driver, SPU2, movies, settings |
| `src/render` | the Direct3D 11 renderer, tick interpolation and the HD 2D layer |
| `src/ui` | the Modern menu style |
| `src/driving` | the port of the driving missions' engine (`DRIVING.ELF`), in progress |
| `tests/` | the unit tests and the regression checks on real data |
| `tools/` | Python tools for the formats, plus Ghidra scripts |
| `docs/` | the format, engine and port documentation (in Portuguese) |

## Credits

Thanks to [NightfireResearch](https://github.com/NightfireResearch) for
[nightfire-cxbx](https://github.com/NightfireResearch/nightfire-cxbx), their decompilation of the Xbox version, and for
letting us build on it: their function and structure names for the driving engine and their reverse-engineering notes
are a reading reference for this port (see `docs/cxbx.md`). This port remains a port of the PS2 version.

## Legal

This is an unofficial fan project. It is not affiliated with or endorsed by Electronic Arts, Eurocom, MGM, Danjaq or EON
Productions. *007*, *James Bond* and *Nightfire* are trademarks of their owners. No copyrighted game content is
distributed here; to run anything, you must own the game and use your own copy.
