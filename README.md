# 007: Nightfire — native PC port

**Version 0.1 (October 2026) — work in progress.**

A native Windows rebuild of *007: Nightfire* for the PlayStation 2 (NTSC-U, SLUS-20579). This is not an emulator.

The game is rebuilt from its own executables:
- the data formats are reverse-engineered;
- the game logic is decompiled function by function into C++;
- the PS2 hardware layer (GS, VU0/VU1, IOP, SPU2, CD) is replaced by a PC implementation (Direct3D 11, XAudio2, XInput).

The aim is to play the original game the way it was made, plus the PC features it lacks: any resolution and aspect ratio, mouse and keyboard, high frame rates.

> **No game data is included.** You need your own copy of the US PS2 disc. Nothing extracted from the disc (models,
> textures, sounds, movies, executables) is or will be in this repository.

## Status

| Area | State |
|------|-------|
| On-foot campaign (7 missions, 12 with the driving ones) | Every level loads and runs on the game's own code, and every objective has its mechanism. A complete playthrough of every mission has not been verified yet. |
| Driving missions (Paris, Alpine Escape, Enemies Vanquished, Deep Descent, Island Infiltration) | Skipped for now: they count as done without a medal. They run in a separate EA engine (`DRIVING.ELF`), which is being ported. The prototype already draws the cars and runs the physics, both checked against the original code. |
| Multiplayer | Not yet; planned last. |
| macOS | Planned. The engine is being made 64-bit clean first, Windows-only, with the 32-bit build kept identical. |
| Decompilation | 2,103 of 2,550 functions of `ACTION.ELF` (79% of its code). |

### What works

- **Rendering.** Levels, characters, skinned animation, faces that move with speech, and the sky.
  - Lighting and fog reproduce the PS2's VU1 microcode, including the environment maps.
  - Also: particles, glass that shatters, weather (rain, snow, lightning), screen fades and the load screen.
- **Display options.** Any resolution and aspect ratio (Hor+ field of view, 4K, ultrawide), and smooth frame rates above 60 fps: the logic runs at the PS2's 60 Hz and the frames are interpolated between ticks.
  - Image quality: MSAA with alpha-to-coverage, supersampling, anisotropic filtering.
  - Optional HD texture packs in the PCSX2 format.
- **Gameplay.** The player, weapons and gadgets, AI enemies and allies with their vision, combat, cover and alarms, and pickups.
  - Also: doors, ladders, the grapple, ziplines, locks, security cameras, turrets, helicopters, explosions and destruction.
  - Scripted cutscenes, the HUD, mission objectives, statistics, score and medals.
- **Menus.** The game's own menus, front end, codenames and saves, dossier and credits, all run by the game's code.
  - Two styles: **Original**, exactly as on the PS2, and **Modern**, the default: a redesigned look at native resolution that still drives the game's own menu logic.
- **Audio.** The game's sound driver (`SFX.IRX`) and the SPU2 are reproduced in software: effects, 3D positioning, reverb, speech with subtitles, and the interactive music.
- **Movies.** The PSS movies play at the points where the game plays them. They need an MPEG-2 decoder in Windows; without one they are skipped.
- **PC controls.**
  - Raw mouse and keyboard with PC-standard defaults, all rebindable.
  - XInput controllers through the game's own pad layer (its 8 button layouts, dead zone, curves and rumble).
  - Mouse support in every menu.
  - Optional modern aim-down-sights and a weapon wheel.
- **Performance.** The target is 60 fps on integrated GPUs with the default settings. Measured at 1080p, a Radeon integrated GPU runs 144 fps or more.

### Known gaps

- Driving missions and multiplayer (see above).
- A few cosmetic rendering details still have to be checked against real hardware: fog edge cases and some tint values.
- Only scripted controller input has been tested so far, not a physical pad. DualShock/DualSense pads work only through XInput wrappers.
- The RTX Remix path (`--renderer d3d9ff`) is an early prototype and is parked.

## Requirements

- Windows 10 or 11.
- A GPU with Direct3D 11. Integrated GPUs are fine.
- Your own copy of the game: *007: Nightfire*, PS2, US version (SLUS-20579).
- To build:
  - Visual Studio Build Tools with the C++ workload (MSVC, x86 target), CMake and Ninja;
  - Python 3.12 for the tools (standard library only).
- Optional: Microsoft's *MPEG-2 Video Extension* (Microsoft Store), for the movies.

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

## Game data

Extract the contents of your disc into the repository root: `ACTION.ELF`, `BASE.ELF`, `FILES.BIN`, `DRIVING\`,
`MODULES\`, `MOVIES\`, `PS2\` and the rest. These files are ignored by git.

## Running

```bat
build\Release\nightfire.exe                      :: the game's front end (Press START, codenames, mission map)
build\Release\nightfire.exe --level 07000001     :: start a level directly
```

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

## Legal

This is an unofficial fan project. It is not affiliated with or endorsed by Electronic Arts, Eurocom, MGM, Danjaq or EON
Productions. *007*, *James Bond* and *Nightfire* are trademarks of their owners. No copyrighted game content is
distributed here; to run anything, you must own the game and use your own copy.
