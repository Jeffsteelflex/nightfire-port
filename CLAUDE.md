# CLAUDE.md — Nightfire PC port

Guide for working on this repository. Keep it current: update the **Status** section and any
changed command or convention in the same commit as the change.

## Goal

Native Windows port of *007: Nightfire* (PS2, NTSC-U, SLUS-20579), with PC features the original
lacks: any resolution and aspect ratio (4K, ultrawide), raw mouse + keyboard, high frame rates.
Performance target (user, 2026-10-10): 60 fps on integrated GPUs with the default settings, no upscaler needed.
Measured with `--bench` (see Status): met on this machine's Radeon iGPU, 144 fps or more at 1080p.
Long-term (user, 2026-10-11): a "007 Anthology" of 007 games never ported to PC, built the same way. Keep the PC
platform layer (`src/platform`), the Modern UI kit (`src/ui`) and the PS2-library replacements (libmc card, libpad,
PSS movies, SPU2) free of Nightfire specifics where it costs little, so they can become a shared library later.
Strategy: rebuild the game from its own executables. Reverse-engineer the data formats, decompile
the game logic function by function into C++, and replace the PS2 platform layer (GS, VU0/VU1,
IOP, SPU2, CD) with a PC implementation.

## Hard rules

- **Never commit game data.** `.gitignore` excludes the ISO, `*.ELF`, `FILES.BIN`, `DRIVING/`,
  `MODULES/`, `MOVIES/`, `PS2/`, `extracted/` and `out/`. Before committing, `git status` must show
  only source, tools and docs. Anything derived from the disc (models, textures, decompiled C dumps)
  stays in `extracted/` or `out/`. Small facts (names, offsets, a 45-entry cutscene name list in
  `tools/data/`) are fine.
- **Preserve original behavior** when porting a function, including bugs, unless it would corrupt
  memory or crash. Document every intentional divergence in a comment at the function and in the
  file header. Examples already in the code: `Mat_Inverse`, `Quat_Slerp`, hashtable type-7 range.
- User-facing text, docs (`docs/*.md`), code comments and commit messages are in **Portuguese**
  (the user's language). This file is in English by request. Exception (user, 2026-10-08): text shown
  **inside the game** (menus, port-only option pages, labels) is **English**, like the game's own text;
  the port-only labels keep a Portuguese variant (`[ui] language_pc = pt`), off by default.
- Commit message trailer: `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`
  (or whatever attribution the current system reminder specifies).

## Repository layout

| Path | Contents |
|------|----------|
| `src/core` | binary reading helpers, `xxh3` (XXH3-64, used by PCSX2 texture names) |
| `src/data` | ELF reader (sections, segments, symbols), `FILES.BIN` index, level packs, map items (blocks, textures, geometry with per-batch GS state, placement, `gs_blend_equation`, `gs_alpha_coverage_candidate`), `pack_images` (2D images of a pack decoded by hash without loading it: the weapon wheel's icons from 07000048), `texture_pack` (PCSX2 replacement pack index and hashes), `mips_interp` (minimal R5900 interpreter for the ELF's static constructors; with `attach_vu0` it also runs COP2 (lqc2/sqc2, qmfc2/ctc2, macro-mode VU0 ops, vcallms through `ps2::VuMachine`) and the quadword MMI, which `nf_driving_check` uses to run DRIVING.ELF's physics) |
| `src/game` | `boot` (installs `FileList` and the static-constructor tables), `static_init` (runs `__do_global_ctors`, copies `weapon_data`/`ammo_data`), `hud_tables` (the HUD's and the mission's static tables from the ELF), `menu_tables` (the credits and the debug
page's lists, run from the ELF's own code; the check of the "Secret Unlocks" codes against the ELF), `LevelScene` (independent reader, now only used by `nf_level_check`), object-type classification, initial camera (`suggest_view`) |
| `src/engine` | **code ported from ACTION.ELF**: `vecmath`, `hashtable`, `gameflow`, `memory`, `loader` + `parsemap` (original level loader), `build`, objects (`control`, `objects`), `collide`, `bullet` (projectiles: `Bullet_init/update/CollisionHandler`, trails, fuses, homing, ejected casings), `effect` (impact effects by material: bullet holes and footprints as attachments, smoke `Gas_Init`/`gases_smoke`, debris, `Swoosh` trails, `DList`), `emitter` (particle emitters: block 0x30 definitions, `Emitter_Create/Update/Draw/CreatePlist`, `ColourGetValue`), `view` (cameras, portal visibility, draw lists, `Game_Draw`), `player` (the Bond object: start points, `Player_Init/Update`, walking, capsule, use, first-person camera), `hud` (the HUD: panes, crosshair, `HUDINFO_tag`), `mission` (objectives, mission success and failure), `player_weapon` (the player's weapons: inventory, ammo, the first-person arms and gun, aiming, switching, firing, auto-aim `Check_AutoAim`, the laser pointer, beams and the watch), `player_ads` (port-only: modern aim-down-sights, the aim style, the measured sight table `player_ads_table.inc`), `weapon_wheel` (port-only: the weapon wheel's logic), `pickup` (items: `Pickup_*`, `Player_EquipKey`, `Check_Target`), `player_stats` (the player statistics and the mission score: `PlrStat_*`/`PlrStats_*`, `ScoringTable`), `break` (breakables, glass shards,
destroyables, hurt zones), `explode` (explosions and the damage they spread), `script` + `script_player` (scripted scenes: `Script_*`, the script player `SP_*`, switches `Switch_*` and fuse boxes `FuseBox_*`; helpers from other modules that only the scripts needed in `script_support`, AI state 3 in `drone_script`), the 2D layer: `txt` (game text, `Txt_*`), `font` (`Font_*`), `text` (messages and subtitles `Text_*`, `Sound_DoSubtitle`, `CirBuf_*`, the menu text box), `sprite` (`Sprite_*`, `View_DrawSprites/AddSprite`, `hashtable_set_sprite`), `weapons` (`weapon_data`/`ammo_data` and the code-pointer resolver), `input` (the 40 game actions), `anim` (skeletal animation: loading, scripts, sequences, poses, bone matrices and skinning palette; the faces: `AnimFacial*`, head tracking), `door`, `spline` (path interpolation), `player_traverse` (ladders, grapple, ropes, ziplines, creep walls, touch icons, third-person cameras), `ssys` (locks, keypad codes, the Q-Worm's targets), `turret` (gun turrets `GT_*`), `copter` (helicopters), `simple_script` (`SS_*`: models moving along their path), `rigid_body` (rigid bodies `RB_*`: the hanging pendulums), `sensor` (security cameras `Sensor_*`, monitors `Monitor_*`, the SSys countdowns), `searchlight` (`Searchlight_*`), `hazard` (mines `Mine_*`, shooters `Shooter_*`, the quake `Apocalypse_*`), `gunimp` (the mounted gun `GunImp_*`), `space_station` (0700001b's laser and missiles), `game` (`Game_Run`, `bootup_bootup`), `resetmap` (`ResetMap_Load`, the load screen `ResetMap_GenLoadScreen`), `weather` (rain, snow and bubbles: `DrawDrops`, the rain boxes `RainBox_*`, the room wind `Env_Update`; lightning beams `Lightning_*`; clouds), `scenery` (ripples, falling leaves, animated trees, rotors, one-sided walls, the breath `Env_Breath`, `Particles_*`), `lights` (ambient light), `trigger` (`Trigger_Calc`; the hints `Hint_*`), `drone*` (AI characters: creation, init, modes, animation layer, state machine, states, vision, body, weapons, spawners), `ainetwork` + `aipath` (AI navigation: block 0x05 paths and bounds, cover nodes, AI points, volumes, A* routes, emitters, link creeping; `npc.h` has `NPCGlobals`), the menus (`menu_manager`: managers, pages, windows, colour
processes, linked lists; `menu`: components, control lookup and navigation, messages, pad input, sprites;
`menu_script`: menu keyframe scripts; `menu_controls`: label, button, checkbox, scroll bar, list, radio,
memo; `menu_handlers`: the handler table, the pause, end-mission and results pages, medals and rewards;
`menu_pc_options` + `pc_text`: the port-only PC options page (the pause row OPTIONS, the tabbed page
0x40000060 with its row framework and the VIDEO/GRAPHICS/INTERFACE/AUDIO tabs) and their pt/en string table;
`menu_pc_controls` the CONTROLS and KEYS tabs; `menu_mouse` the mouse in every menu (port-only);
`menu_controller_style` the pause CONTROLS tab's button map; `menu_modern` the bridge between the Modern menus and
the game's menus (snapshot of the open menu, the front end's pages included, requests through the manager tick); `menu_frontend` the front end's
single-player path (language, intro, Press START, main menu, mission select, difficulty, mission map, end of game, the
wheels and the iris; `menu_frontend.h` has `M_ITEM`, `MENU_LS`) and `menu_codename` the codenames, the on-screen
keyboard and the memory-card dialog (`PS2_DoCardFlow`, `Menu_UpdateMessageBox`, `Menu_EnumSaves`); `menu_dossier`
the dossier (records, rewards, gadgets, weapons; `ds_*` tables, `Menu_GetObjectUpgradeLevel`); `menu_cheats` the
in-game debug page's cheat checkboxes (`C_CHCH*`) and the port's `--debug-menu`/`--unlock-all`; `menu_options` the
codename's option pages (Controller Setup, Advanced, Multiplayer and AV Options, the screen adjust) and the credits;
`menu_secret` the "Secret Unlocks" codes (`Menu_SpecialCodenameCheck`, `Menu_UpgradeCheat`); `menu_tweaks` the debug
page's subpages (damage and AI-firing tweaks, "Previews", the level list); `loadsave` the
saved profile (`LS_*`, IFF blocks, `BIN_PushBits/PullBits`); `sound` the EE sound module (`Sound_*`, the map's sound
triggers) and `music` (`Music_Event`, `UpdateMusicalEvents`, the per-level `UpdateMFX_*`, the music triggers);
structs in `menu.h`); runtime structs in `map_types.h`, `gamestate.h`, `object.h`, `view.h`, `player.h` (`BLData`, `PLRWEAPON_tag`), `player_weapon.h` (`weapon_definition_tag`), `anim.h` (`ANIMOBJ_tag`), `bullet.h` (`BU_tag`, `CASING_tag`), `script.h` (`SCRIPTINFO`, `SSTREAM`, `SCRIPTPLAYER_tag`, `SWITCH_tag`, `FUSEBOX_tag`); not-yet-ported callees declared in `modules.h`/`view.h` and stubbed in `modules_stubs.cpp` |
| `src/platform` | Win32 window, raw input, `psi*` (PC implementation of the engine's platform interface: clock, heap, `psi_file` over `FILES.BIN`, `psi_gfx` load-time resources, `psi_draw` per-frame draw record incl. `psiDrawParticleList` sprites, loose world-space triangles
(`psi_draw_triangles`), the lights, the fog pass and the screen fades (`psiFadeView`), `psi_loading` the load screen's blobs and hooks, `psi_shard` glass breakable data and shards, `psi_2d` the 2D draw record (`psiDrawSprites`, `psi_2d_viewport`), `psi_input` keyboard, mouse and pad merged into player 0's actions, `psi_pad` the virtual PS2 pad (libpad slots, styles, rumble), `gamepad` XInput behind `nf::PadSource`, `input_bindings` the rebindable commands and `[controls]`/`[bindings]`); `vu1` (CPU reference of the VU1 routines and the fog: vertex lighting, env-map ST, GS Z, fog factor); `pending.*` counts stub calls; `psi_card` the memory card (the game's `MemCard_*` state machine and `psi*`
save/load/enumerate/delete over `nf::PcCard`: a folder, `saves\` or `--save-dir`, or in memory); `psi_sound` the
EE side of the sound driver (`SFX*`: command batches, `SFXUpdate`, volumes and mode), `sfx_iop` the IOP sound driver
`SFX.IRX` ported (banks, items, voices, 3D, streams, music markers), `spu2` the SPU2 in software (PS-ADPCM, 48 voices,
reverb, the core-0 data input of the movies), `audio_out` the output (XAudio2, a WAV or nothing, paced by VBlanks); `psi_movie` the movies (the game's `psi*Movie*` functions and the PC player under them), `pss` (GPU-free: the PSS demuxer, the "SS" PCM audio, the video units, the movie clock) and `movie_decoder` (the Windows MPEG-2 MFT through Media Foundation, NV12 to BGRA); `pc_gameplay` the port-only `[game] aim_style` and `wheel_slowmo`; `game_options` the game's own options (accessors by
`nf::GameOption`) and their rule with `nightfire.ini`; `crash_report` prints the call stack of an unhandled exception (dbghelp + the Release PDB, looked for next to the executable). `disc_source` (the user's disc behind one interface: a folder of extracted files, an ISO 9660 image, a DVD drive, the personal copy; `DiscFile`, the version from `SYSTEM.CNF` (`check_disc`), the search `find_disc`) and `disc_pack` (the single-file personal copy: the files after the PE image, the index and footer, `inside_git_tree`). `ptr32` (`P32<T>`/`F32<Sig>`, the 4-byte pointer fields of game structures in both builds, and the `nf::p32_*` helpers), `arena` (x64 only: the 4 GB window from `__ImageBase`, the game heap, the replaced `operator new` in `arena_new.cpp`, the game stack with `arena_stack_x64.asm`) and `fmath` (`nf::fm::`, float math with the same results in x86 and x64). `settings` (`nightfire.ini`) and `video_settings` (PC video/UI options: ini keys, value lists, the
apply-and-confirm controller); `gpu_adapter` (the graphics adapter preference `[video] adapter` and the rule that picks
one, GPU-free) and `bench_stats` (the `--bench` frame-time statistics); game-agnostic PS2 pieces: `ps2_float` (the EE FPU and VU float arithmetic bit for bit: no denormals, no inf/NaN, round toward zero), `vu_interp` (`ps2::VuMachine`, a VU0/VU1 interpreter: micro mode with Q/P/flag latencies, delay slots, the E bit and XGKICK capture, and macro mode) and `vu0_macro` (the VU vector ops with dest masks for ported VU0 macro code and hand-ported microcode). Built as `nf_platform`, which `nf_engine` links |
| `src/driving` | the DRIVING.ELF port (namespace `drv`, library `nf_drv`, conventions in `drv.h`): `physics/` (`rigid_body` the RigidBody/RigidBodyInfo layouts and functions, `vu0_physics` the VU0_physics microprogram hand-ported plus the EE side that loads and collects it, `collision` GenerateImpulse and ResolveCollision with ModifyLevers/ScaleObjObjForces, `simulation` SimulateGame and ESimFrameUpdate's physics, `world` the ground interface and `FlatGround`), `real/vu0_math` (EA REAL matrix and quaternion routines); docs/driving.md section 11. The render (docs/driving.md section 10): `bigf` (BIGF + RefPack), `shps` (SHPS to RGBA), `carp` (the CARP UGroup tree, the `in` instances and the `vd` per-view draw lists), `eagl_loader` (`EAGL::DynamicLoader`), `eagl_model` (`EAGL::Model`, `GeoPrim::Construct/Draw`, the DMA buffer), `eagl_state` (`GeoPrimState`, `RStateManager`, `EAGL::TAR`), `vif` (DMA chain + VIF1 into a 16 KB VU memory), `mat_car` (the MatCar render-method family read from the VU1 microprogram: layouts, CPU reference), `mat_car_verify` (the original microprogram in `ps2::VuMachine` vs the reference), `render_method` (the census of the 115 methods), `car_model` (a car loaded, its batches, parts and instances); `src/driving_app` is `nf_driving` (D3D11 viewer, captures, `--check`, `--census`, `--verify`) |
| `src/render` | Direct3D 11 renderer (resources by `Tex` slot / mesh id, draws a `DrawItem` list of rigid and GPU-skinned meshes, particle sprites, loose triangles (shards) and screen-space 2D quads, with the GS state, tweak, VU1 lighting and env map of each draw, plus the fog pass); `runtime_gfx` is the psi_gfx backend and turns the `psi_draw` frame record (camera 0, or 4 during a scripted scene) + provisional objects into the draw list (`frame_markers`, GPU-free in `nf_frame_interp`: where the fog pass and the list-2 depth clear go); `ui_hd*` the optional HD 2D layer (`ui_hd` GPU-free logic in `nf_ui_hd`, `ui_hd_draw` Direct2D/DirectWrite/WIC, `ui_hd_game` what it reads from the game, `ui_hd_app` the nightfire.exe glue); the renderer's PC options: the 3D scene in its own target at a render
scale and/or with MSAA, then the 2D at window size; texture filter, VSync, DXGI exclusive fullscreen; `frame_interp` (`nf_frame_interp`, GPU-free and tested) the tick interpolation: the last two tick records, draw matching, cuts, the matrix/palette/camera maths, the instant mouse look, the frame-cap timing and `plan_tick` (the tick scheduler with the weapon wheel's slow motion); `movie_layer` the movie frame drawn first in the frame (black target, the image in the PS2 screen, Catmull-Rom); `gpu_adapters` the DXGI adapter enumeration (`--list-adapters`,
`[video] adapter`) and the GPU timestamp queries of `--bench` live in `renderer`; `remix_d3d9` the optional Direct3D 9 fixed-function path for RTX Remix (`--renderer d3d9ff`, a prototype; docs/remix.md); `setup_app` the setup screen's loop in nightfire.exe (the Windows file picker, the DVD drives, the personal copy's thread) |
| `src/driving` | the driving missions' port (`DRIVING.ELF`, EA's engine; docs/driving.md): library `nf_drv` (its `.cpp` files are globbed), namespace `drv`, no dependency on `src/engine`; ported functions marked `// [driving] 0x<address> <Name>` (DRIVING.SYM / `out/ghidra_driving` names); the PS2 is the truth, the Xbox build only explains; tests in `tests/test_driving_*.cpp` (globbed into `nf_tests`); conventions in `src/driving/drv.h` |
| `src/ui` | the Modern menu style (port-only, `nf_ui_modern`, GPU-free and tested): `modern_ui` (tokens, the 1920x1080 virtual canvas, motion curves and tweens, the draw list), `modern_kit` (the components), `modern_input` (the navigation input of the game-menu screens), `modern_options`
(the Modern PC options screen), `modern_pause` (the Modern pause), `modern_end` (mission failed, results and the controller-disconnected warning),
`modern_front` (the Modern front end), `modern_dossier` (the Modern dossier, a view of the front end), `modern_wheel` (the weapon wheel's drawing over the game), `modern_setup` (the first-run setup screen, before any disc); drawn by `src/render/ui_modern_draw` (Direct2D/DirectWrite) and wired to `nightfire.exe` by `src/render/ui_modern_app` |
| `hd_ui/` | the HD 2D layer's `map.json` (font mapping, image replacements) and its SVG assets (`svg/`, generated by `tools/ui_hd_assets.py`); `hd_ui/fonts/` is the user's font folder and is git-ignored; `hd_ui/fonts_ofl/rajdhani` (OFL) and `hd_ui/design/tokens.json` belong to the Modern menu style |
| `src/main.cpp` | `nightfire.exe`: main loop modeled on the PS2 `main` + `GameFlow_Main` |
| `tests/` | `nf_tests` (unit tests, no game data; `test_ptr32.cpp` the 64-bit pointer scheme; `test_disc.cpp` the disc sources, a hand-built ISO, the personal copy and the setup screen), `nf_disc_check` (two disc sources compared file by file, `disc_check.cpp`), `nf_level_check` and `nf_loader_check` (regressions on real data; `nf_loader_check <data> --miras` measures the guns' sight lines, `tests/sights_measure.cpp`), `nf_driving_check` (DRIVING.ELF's physics: the original VU0 microcode in the interpreter vs the hand port, the whole step both ways, the original EE code in the MIPS interpreter vs the C++ port; `driving_check.cpp`, `driving_check_ee.cpp`) |
| `tools/` | Python tools (`make_release.py` the release zip, with `tools/release/README.txt` and `LICENSES.txt`; formats, exporters, analysis; `vudis.py` disassembles the VU0/VU1 microcode, `scripts.py` decodes the scene scripts, `morph.py` the faces' morph targets and face sequences, `movies.py` lists and demuxes the PSS movies; `driving_sym.py` reads `DRIVING/DRIVING.SYM` and matches it to `DRIVING.ELF`, `driving_data.py` lists and extracts the `DRIVING/` BIGF archives, `xbox.py` reads the Xbox disc image (XDVDFS: `.xiso` or a full image) and XBE headers, writes an equivalent PE32 for Ghidra (`pe`: flat layout, kernel thunk and XDK section labels) and disassembles NV2A vertex programs of Xbox EAGL render methods (`vsh`): the user's NTSC-U Xbox copy in `xbox/`, git-ignored, a reading reference for the driving code only; `xbox_match.py` matches `Driving.xbe` functions to `DRIVING.ELF` ones (strings, constants, call graph, vtables, globals); `vudis.py DRIVING.ELF --prog=N` and `--eagl <obj> <method>` read the driving VU0 programs and the EAGL VU1 render methods) and Ghidra scripts (`ghidra_driving.sh`: the separate `out/ghidra_driving` project for `DRIVING.ELF` and `BASE.ELF`; `ghidra_xbox.sh`: the `out/ghidra_xbox` project for the Xbox PE; `ApplySymbolTable.java` applies a name table; `ExportMatchFeatures.java` exports the matcher's per-function facts; `XboxVtables.java` finds MSVC vtables and creates their functions) |
| `tools/viewer/` | three.js web viewer for exported `.glb` files |
| `docs/formats.md` | every file format decoded so far |
| `docs/engine.md` | ACTION.ELF architecture: boot, main loop, GameFlow, frame pipeline, code layers |
| `docs/port.md` | `nightfire.exe` usage and renderer architecture |
| `docs/design.md` | the Modern menu style's design system (tokens, type, grid, components, motion, prompts) |
| `docs/remix.md` | the RTX Remix study (what Remix hooks and needs, with dated links) and the `d3d9ff` prototype |
| `docs/driving.md` | the `DRIVING.ELF` study (the driving missions): `DRIVING.SYM`, architecture, the hand-off through `BASE.ELF`, the data formats, size, porting plan and risks; section 9: the Xbox version as a reading reference (function match, physics and render side by side); section 10: the render prototype; section 11: the physics prototype |
| `docs/setup.md` | environment setup and how to run each tool |

## Environment (this machine)

- Windows 11, Git Bash for shell commands. Game files extracted from the ISO sit in the repo root, and so does the ISO
  itself (`007 - Nightfire (USA).iso`, git-ignored; `Mount-DiskImage` on it gives a drive letter like a real DVD).
- Remotes (user, 2026-10-11/12): `origin` is a private GitHub repository with the full history, only `main` pushed
  (to work from other devices; push main after each merge). `public` is a public repository (user decision, 2026-10-12)
  that only gets versions: one commit per version, the tree of `main` at that point, authored by that account (its
  GitHub noreply address), parent = the previous version, tag `vX.Y`; the private history is never pushed there. Never
  push game data to either. No account name or e-mail goes into the tree (both are in the local git config): grep the
  tree before each version. This checkout picks the account per remote through a repo-local `credential.helper` (the
  URL's user name selects the `gh auth token --user`; the GitHub CLI's active account here is a work account). Another
  device needs its own copy of the game files at the repo root and the Windows toolchain to build or run anything; a
  cloud session can only read and edit code and docs.
- Compiler: **Visual Studio Build Tools 2026** (MSVC 14.51, CMake, Ninja; x86 target) at
  `C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools`. VS 2022 Community has no C++ workload.
- Ghidra 12.1.3 at `C:\Tools\ghidra_12.1.3_PUBLIC` with ghidra-emotionengine-reloaded v2.1.37
  (must match the Ghidra version exactly). GUI project: `ghidra/`; headless project used by scripts:
  `out/ghidra_headless/` (separate because the GUI locks its project).
- Python 3.12, standard library only (no numpy/Pillow). Set `PYTHONIOENCODING=utf-8` when printing.

## Build and test

```bash
# Release build into build/Release (CMake + Ninja). From Git Bash, go through PowerShell:
powershell.exe -NoProfile -Command "& cmd.exe /c '\"C:\Projects\Nightifire Port\build.bat\" Release'"
# The 64-bit build (see "Two builds" in the conventions) into build/x64/Release; the same tools and checks run there:
powershell.exe -NoProfile -Command "& cmd.exe /c '\"C:\Projects\Nightifire Port\build.bat\" Release x64'"
./build/x64/Release/nf_tests.exe          # x86's tests plus arena_* (window, allocator, game stack)
./build/x64/Release/nf_loader_check.exe . # must match x86's output (addresses masked); see docs/port.md "64 bits"
./build/Release/nf_tests.exe              # unit tests; optional name filter as first argument
./build/Release/nf_level_check.exe .      # loads all 35 levels from the real data; must report 0 unresolved
./build/Release/nf_loader_check.exe .     # loads all 36 packs with the ported original loader; must report 0 differences
./build/Release/nf_disc_check.exe "007 - Nightfire (USA).iso" .   # every file of the ISO against the extracted copy: 177 equal
./build/Release/nightfire.exe --disc "007 - Nightfire (USA).iso" --level 07000012 --screenshot out/shots/iso.png  # the disc from the ISO (also a drive "H:", a folder, a personal copy); report line `disco`
./build/Release/nightfire.exe --setup --width 1920 --height 1080 --wait 1.5 --screenshot out/shots/setup.png  # the first-run setup screen (report line `configuracao`; without --setup it shows only when no disc is found)
./build/Release/nightfire.exe --config out/s.ini --setup --setup-pick "007 - Nightfire (USA).iso" --setup-input "none:0.5 up:0.1 none:0.3 enter:0.1 none:0.5 enter:0.1" --input "none:4.5 enter:0.1" --wait 7 --screenshot out/shots/s.png  # setup: pick the ISO, store it in out/s.ini, Play, the front end
./build/Release/nightfire.exe --pack-disc "007 - Nightfire (USA).iso" --out C:/somewhere/nightfire-personal.exe  # the single-file personal copy (refused inside a git tree)
python tools/make_release.py --version 0.2       # the release zip from build/Release and build/x64/Release into the main checkout's out/release/
./build/Release/nightfire.exe --level 07000001
./build/Release/nightfire.exe                    # no --level: the game's front end (Press START, main menu, codenames, mission map)
./build/Release/nightfire.exe --save-dir out/saves   # the memory card in another folder (default: saves\ next to the exe)
./build/Release/nightfire.exe --input "none:4.5 enter:0.1 none:2 enter:0.1 none:2 enter:0.1 none:1.5 enter:0.1 none:1.5 down:0.1 none:0.6 enter:0.1 none:1.5 enter:0.1" --shot-level 07000005 --wait 3 --screenshot out/shots/fe.png  # front end: NightFire > Play without saving > OK > Agent > The Exchange
./build/Release/nightfire.exe --level 07000005 --win 1 --input "none:6.5 enter:0.1 none:2 enter:0.1" --shot-level 07000048 --wait 11.5 --screenshot out/shots/map.png  # results > bonus > the mission map with the next mission unlocked
./build/Release/nightfire.exe --unlock-all --input "none:4.5 enter:0.1 none:2 enter:0.1 none:2 enter:0.1 none:1.5 enter:0.1 none:1.5 down:0.1 none:0.6 enter:0.1 none:1.5 pad_x:0.1 none:2 down:0.1 none:0.6 enter:0.1 none:1.5" --wait 19 --screenshot out/shots/rewards.png  # front end > mission map > square: the dossier's rewards (every medal: --unlock-all)
./build/Release/nightfire.exe --level 07000012 --open-menu-page 40000007 --input "none:1 mouse=320,121:0.3 mouse=320,121+lmb:0.1 none:0.5 enter:0.1 none:2" --wait 3.5 --screenshot out/shots/nohud.png  # the debug page: HUD cheat off, back to the game
./build/Release/nightfire.exe --level 07000012 --debug-menu   # F9 in a level opens the pause on the game's debug page (the cheats)
./build/Release/nightfire.exe --level 07000012 --open-menu-page 40000007 --input "none:1 mouse=300,401:0.3 mouse=300,401+lmb:0.1" --wait 3.5 --screenshot out/shots/tweaks.png  # debug page > "Total Score:" > damage tweaks
./build/Release/nightfire.exe --input "none:4.5 enter:0.1 none:2 enter:0.1 none:2 up:0.1 none:0.6 enter:0.1 none:2 enter:0.1 none:5" --wait 17 --screenshot out/shots/ctl.png  # front end: NightFire > New Codename > End (BOND) > Controller Setup
./build/Release/nightfire.exe --input "none:4.5 enter:0.1 none:2 enter:0.1 none:2 up:0.1 none:0.6 enter:0.1 none:2 enter:0.1 none:5 backspace:0.1 none:1.5 down:0.1 none:0.6 down:0.1 none:0.6 down:0.1 none:0.6 down:0.1 none:0.6 enter:0.1" --wait 24 --screenshot out/shots/av.png  # ... > back > codename menu > AV Options (down x2 = Advanced, x3 = Multiplayer)
./build/Release/nightfire.exe --level 0700000a --width 2560 --height 1080 --screenshot out/shots/x.png
./build/Release/nightfire.exe --level 07000001 --at 9.18,6.59,1.94,-1.571 --input "none:0.3 E:0.1" --wait 2 --screenshot out/shots/door.png
./build/Release/nightfire.exe --level 07000005 --at 59.43,27.10,89.21,1.571 --input "none:0.3 W:7" --wait 8 --fixed-video-hz 60 --screenshot out/shots/ladder.png  # climbs the ladder (y 27.10 -> 34.63)
./build/Release/nightfire.exe --level 0700000c --at 5.31,7.38,-112.12,0.002 --input "none:0.5 4:0.1 none:1.5 rmb:1.5 rmb+lmb:0.3 rmb:1" --wait 5.5 --fixed-video-hz 60 --screenshot out/shots/photo.png  # micro-camera photo (4 = the 4th gadget): objective channel 28
./build/Release/nightfire.exe --level 07000012 --fixed-video-hz 60 --input "none:1 escape:0.2" --wait 2 --screenshot out/shots/esc.png  # Esc opens the pause (a 2nd Esc closes it, both styles: "none:1 escape:0.2 none:1 escape:0.2" --wait 3.5 gives `menus status 0`)
./build/Release/nightfire.exe --level 07000005 --fixed-video-hz 60 --input "none:1.5 lmb:0.1 none:0.5 lmb:0.1 none:0.5 lmb:0.1 none:0.7 R:0.1 none:3" --wait 3.75 --screenshot out/shots/reload.png  # R reloads: `arma 2 estado 6 pente 4 municao 41` (--wait 6: pente 7 municao 38)
./build/Release/nightfire.exe --level 0700000c --fixed-video-hz 60 --input "none:1.5 rmb:1 rmb+wheel=3:1.5" --wait 3.8 --screenshot out/shots/zoom.png  # the wheel zooms the scoped rifle: `arma 30 ... zoom 6.16` (3.16 without the wheel); not aiming, wheel=-1 is the next weapon
./build/Release/nightfire.exe --level 07000005 --fixed-video-hz 60 --input "none:1.5 3:0.1 none:2.5" --wait 4 --screenshot out/shots/gadget.png  # 1-5 pick the n-th gadget: 3 = the grapple (`arma 80`; 1 taser 74, 2 laser 78, 4 camera 84, 5 nothing)
./build/Release/nightfire.exe --level 07000012 --open-menu-page 40000060 --menu-tab controls --input "none:1 down:0.1 none:0.4" --wait 2 --screenshot out/shots/ctlmouse.png  # Modern CONTROLS: the mouse rows, the cm per turn in the description's meter
./build/Release/nightfire.exe --level 07000012 --at 22.49,1.09,19.155,-1.68 --input "none:0.5 C:0.1" --wait 5 --fixed-video-hz 60 --screenshot out/shots/torso.png  # torso aiming: report `mira_ia ... erro_cano 0.5` (6.3 with --no-torso-aim)
./build/Release/nightfire.exe --config out/aa.ini --level 07000012 --at 22.49,1.09,19.155,-1.68 --input "none:4.5 pad_rt:0.1 none:0.3 pad_rt:0.1 none:0.3 pad_rt:0.1 none:0.3 pad_rt:0.1 none:0.3 pad_rt:0.1" --wait 7 --fixed-video-hz 60 --screenshot out/shots/aa.png  # auto-aim: out/aa.ini "[game] auto_aim = on|off": `estatisticas tiros 5 acertos 2|0`
./build/Release/nightfire.exe --level 07000008 --at 160.24,43.10,-117.14,1.571 --input "none:0.5 2:0.1 none:1.9 lmb:3" --wait 4 --fixed-video-hz 60 --screenshot out/shots/laser.png  # the watch laser's beam on the laser lock, the watch hands (report `laser ... relogio 3`)
./build/Release/nightfire.exe --level 07000003 --at 1.95,6.43,-20.5,0.0 --input "none:0.3 mlook=0,-700:0.25 none:0.4 lmb:0.1 none:0.3 lmb:0.1 none:0.3 lmb:0.1 none:0.1 D:0.9 mlook=-1300,0:0.3" --wait 3.2 --fixed-video-hz 60 --screenshot out/shots/lantern.png --frames-dump out/shots/lantern --frames 40  # a rigid-body lantern shot and swinging (`corpos_rigidos ... tiros 3`; NF_LIST_RB=1 lists them)
./build/Release/nightfire.exe --level 07000016 --at 2.27,-2.0,-94.70,0.0 --input "none:1.0 mlook=0,500:0.4 W:2.5" --wait 4 --fixed-video-hz 60 --screenshot out/shots/water.png  # underwater: `agua ... submerso 1 ar 88.6 ondulacao ...`
./build/Release/nightfire.exe --level 07000012 --load-screen out/shots/carga.png --load-screen-blob 3 --wait 1 --screenshot out/shots/x.png  # the load screen (picture, mission, tip, blobs) as a PNG; report line `tela_carga`
./build/Release/nightfire.exe --level 07000012 --fixed-video-hz 60 --wait 0.3 --screenshot out/shots/fadein.png  # the level-start fade-in (camera 0, `esmaecimento 1 | camera 0 ... alfa 153`)
./build/Release/nightfire.exe --level 07000001 --fixed-video-hz 60 --input "none:12 enter:0.1" --wait 12.35 --screenshot out/shots/skip.png  # skipping the intro NIS (Enter/Space, pad Start/A): camera 4 fades out
./build/Release/nightfire.exe --level 07000016 --channel f5 --input "none:5 pad_rx=0.8:5" --fixed-video-hz 60 --wait 7.5 --screenshot out/shots/quake.png  # the quake's white flash (Player_SetFlashBang) and screen blur: `clarao ... rastro`
./build/Release/nightfire.exe --level 0700000b --fixed-video-hz 60 --wait 4 --screenshot out/shots/rain.png  # rain (07000005: snow); report lines `tempo` and `enfeites`
./build/Release/nightfire.exe --level 07000001 --force-lightning --cam -12,10,-25,5,18,-6 --input "none:1 enter:0.1" --fixed-video-hz 60 --wait 4 --screenshot out/shots/bolt.png  # the lightning beams (debug: the sky's flash forced on)
./build/Release/nightfire.exe --level 0700000a --at 10.67,1.28,17.79,-1.686 --wait 9 --fixed-video-hz 60 --screenshot out/shots/camera.png  # a security camera spots the player: alarm 0x1e -> 0x12, the ceiling turret deploys and fires
./build/Release/nightfire.exe --level 07000014 --at 47.45,1.1,35.6,0 --input "none:0.5 E:0.1" --wait 1.5 --fixed-video-hz 60 --screenshot out/shots/monitor.png  # E on a monitor: camera 0 at the security camera, HUD pane 16 (E again: the next camera)
./build/Release/nightfire.exe --level 07000006 --at 6.40,41.01,-43.95,-1.23 --channel 3d --channel 3e --channel 3f --channel 40 --wait 6 --fixed-video-hz 60 --screenshot out/shots/searchlight.png  # searchlights lit by their story channels (--channel), one follows the player: alarm 0x37
./build/Release/nightfire.exe --level 07000015 --at 13.49,41.4,73.68,-2.338 --input "none:0.5 mlook=0,277:1 none:0.3 lmb:0.1 none:2" --wait 2.25 --fixed-video-hz 60 --screenshot out/shots/mine.png  # a crossbow bolt sets off a laser mine (health 100 -> 32.9)
./build/Release/nf_loader_check.exe . 0700000c --campanha   # the campaign line with every provisional create and the stubs called
./build/Release/nf_driving_check.exe . --casos 4000 --quadros 600   # driving physics vs DRIVING.ELF: must report 0 diferentes everywhere
./build/Release/nf_driving.exe --data . --viv MIS01.VIV --model vanquish --yaw 0.6 --width 1920 --height 1080 --screenshot out/shots/drv/vanquish.png  # a driving car drawn offscreen (+ .txt report); without --screenshot: a window, the car turning
./build/Release/nf_driving.exe --data . --check --verify   # every car .crp of every archive (63) loads; each MatCar batch also runs the original VU1 microprogram in the interpreter: cor_dif/xyz_dif near 0, adc_a_mais 0
./build/Release/nf_driving.exe --data . --census          # the 115 render methods by microprogram and family
python tools/pcsx2_pine.py status                # PCSX2's PINE (port 28011) during a driving mission; record <tsv> --frames N; test
./build/Release/nightfire.exe --level 07000012 --at 20.5,1.09,19.155,3.1416 --input "none:0.5 W:2" --wait 3 --screenshot out/shots/wall.png  # the gun against a wall (list 2 after the depth clear)
./build/Release/nightfire.exe --level 0700000c --debug-shading --no-fog --screenshot out/shots/y.png  # old flat shading, no lights/fog
./build/Release/nightfire.exe --level 07000012 --input "none:1 enter:0.2" --wait 2 --screenshot out/shots/pause.png      # pause menu
./build/Release/nightfire.exe --level 07000012 --input "none:10 space:0.3" --wait 12.5 --screenshot out/shots/fim.png   # death -> end-mission menu
./build/Release/nightfire.exe --level 07000012 --win 1 --shot-level 07000048 --wait 2 --screenshot out/shots/res.png    # results screen
./build/Release/nightfire.exe --level 07000001 --hd-ui --width 3840 --height 2160 --screenshot out/shots/hd.png  # HD 2D layer (F4)
./build/Release/nightfire.exe --level 07000012 --open-menu-page 40000060 --menu-tab video --hd-ui --prompts keyboard --width 3840 --height 2160 --wait 1.5 --screenshot out/shots/k.png  # HD menus, key caps
./build/Release/nightfire.exe --level 0700000c --render-scale 200 --msaa 4 --filter aniso16 --screenshot out/shots/aa.png  # PC options
./build/Release/nightfire.exe --level 07000012 --config out/x.ini --input "none:1 enter:0.2 none:0.5 down:0.1 none:0.1 down:0.1 none:0.1 down:0.1 none:0.1 space:0.1" --wait 3 --screenshot out/shots/video.png  # pause > OPTIONS (VIDEO tab)
./build/Release/nightfire.exe --level 07000012 --input "none:0.3 pad_ly=1+pad_rx=0.6:1.5 pad_rt:0.6" --wait 2.6 --screenshot out/shots/pad.png  # simulated pad
./build/Release/nightfire.exe --level 07000012 --renderer d3d9ff --fixed-video-hz 60 --wait 2 --d3d9-remix-conf out/remix/rtx.conf --screenshot out/remix/d9.png  # the Direct3D 9 fixed-function path for RTX Remix (compare with --no-hd --fixed-video-hz 60 in D3D11)
./build/Release/nightfire.exe --level 07000012 --fixed-video-hz 144 --input "none:1 pad_rx=0.6:3" --wait 2 --screenshot out/shots/t.png --frames-dump out/shots/t144 --frames 30  # tick interpolation: 30 consecutive 144 Hz frames + frames.tsv (camera yaw per frame)
./build/Release/nightfire.exe --level 07000012 --fixed-video-hz 144 --input "none:1 mlook=800,0:3" --wait 1.5 --screenshot out/shots/m.png --frames-dump out/shots/m144 --frames 16 --frames-no-png  # instant mouse look (--no-instant-mouse to compare)
./build/Release/nightfire.exe --level 0700001b --fixed-video-hz 144 --wait 5 --screenshot out/shots/sky.png  # report "ritmo ... ceu_gira 600": the same at 60/144/240 Hz (1438 with --no-interp at 144)
./build/Release/nightfire.exe --level 07000012 --open-menu-page 40000060 --menu-tab keys --wait 1.5 --screenshot out/shots/keys.png  # an options tab
./build/Release/nightfire.exe --level 07000012 --open-menu-page 40000060 --menu-tab controls --input "none:1 mouse=300,167:0.2 mouse=300,167+lmb:0.2 mouse=395,167+lmb:0.6" --wait 2 --screenshot out/shots/slider.png  # mouse drags a slider
./build/Release/nightfire.exe --level 07000012 --input "none:1 enter:0.2 none:0.5 mouse=212,119:0.3 mouse=212,119+lmb:0.1 none:0.5" --wait 3 --screenshot out/shots/tab.png  # mouse clicks the pause's OBJECTIVES tab
./build/Release/nightfire.exe --level 07000001 --ui-inventory out/ui_crops   # inventory.tsv + PNG crops of every 2D image drawn
./build/Release/nightfire.exe --level 07000012 --open-menu-page 40000060 --menu-tab graphics --wait 1.5 --screenshot out/shots/m.png  # Modern options (default style)
./build/Release/nightfire.exe --level 07000012 --open-menu-page 40000060 --menu-tab video --input "none:0.8 cmouse=1100,311+lmb:0.1" --wait 2 --screenshot out/shots/m2.png  # Modern, mouse in canvas units
./build/Release/nightfire.exe --level 07000012 --menu-style original --open-menu-page 40000060 --menu-tab interface --wait 1.5 --screenshot out/shots/o.png  # Original page
./build/Release/nightfire.exe --level 07000012 --open-menu-page 4000004b --menu-tab objectives --wait 1.5 --screenshot out/shots/mo.png  # Modern pause, a tab (pause|objectives|controls|score)
./build/Release/nightfire.exe --level 07000012 --input "none:1 enter:0.2 none:0.6 down:0.1 none:0.3 space:0.1" --wait 2.8 --screenshot out/shots/mr.png  # Modern pause: RESTART asks YES/NO
./build/Release/nightfire.exe --input "none:4.5 enter:0.1 none:2 enter:0.1" --wait 9.5 --screenshot out/shots/fc.png  # Modern front end: the campaign's codenames (Press START > NIGHTFIRE)
./build/Release/nightfire.exe --input "none:4.5 enter:0.1 none:2 enter:0.1 none:2 up:0.1 none:0.6 enter:0.1 none:1.5 J:0.1 none:0.1 A:0.1 none:0.1 M:0.1 none:0.1 E:0.1 none:0.1 S:0.1 none:0.5" --wait 12.5 --screenshot out/shots/fn.png  # Modern front end: JAMES typed on the keyboard
./build/Release/nightfire.exe --level 07000012 --input "none:1 pad_a:0.2 pad_off:5" --wait 3.5 --screenshot out/shots/fw.png  # controller disconnected (pad_off), the Modern warning
./build/Release/nightfire.exe --unlock-all --input "none:4.5 enter:0.1 none:2 enter:0.1 none:2 enter:0.1 none:1.5 enter:0.1 none:1.5 down:0.1 none:0.6 enter:0.1 none:1.5 E:0.1 none:2 E:0.1 none:1 E:0.1 none:2" --wait 21.5 --screenshot out/shots/md.png  # Modern map > E: the dossier (RECORDS), E REWARDS, E GADGETS (each tab through the game's pages)
./build/Release/nightfire.exe --input "none:4.5 enter:0.1 none:2.5 down:0.1 none:0.3 down:0.1 none:0.3 down:0.1 none:0.3 down:0.1 none:0.5 enter:0.1 none:30" --wait 25 --screenshot out/shots/mc.png  # Modern main menu > CREDITS: the game's credits page, restyled
./build/Release/nightfire.exe --level 07000012 --at 22.49,1.09,19.155,-1.68 --input "none:4.5 pad_ry=-0.6:0.25 none:0.25 lmb:0.1 none:0.25 lmb:0.1 none:0.25 lmb:0.1 none:1 enter:0.1 none:0.8 E:0.1 none:0.3 E:0.1 none:0.3 E:0.1 none:1.5" --wait 9.5 --screenshot out/shots/ms.png  # Modern pause > SCORE after shooting (the mission's stats)
python tools/ui_hd_assets.py                     # regenerates hd_ui/svg; --check measures the health blades
python tools/vudis.py ACTION.ELF ROTATE_LIGHT   # VU1 microcode by label or address range; --test checks the decoder
./build/Release/nightfire.exe --level 07000001 --fixed-video-hz 60 --wait 11.7 --width 1920 --height 1080 --screenshot out/shots/face.png --frames-dump out/shots/face --frames 40  # Mayhew talking in the intro NIS (report line `rosto`; --no-morphs draws the rest pose)
NF_FACE_SKIN=0500004f ./build/Release/nightfire.exe --level 07000001 --config out/nomusic.ini --fixed-video-hz 60 --wait 9.5 --screenshot out/shots/n.png --frames-dump out/shots/nis --frames 480 --frames-no-png --audio-dump out/shots/nis/a.wav  # frames.tsv rosto_* columns of that skin; out/nomusic.ini: [game] music_volume = 0
python tools/morph.py mouth extracted/files/07000001.bin 01000145 out/shots/nis/frames.tsv --wav out/shots/nis/a.wav --skin 0500004f   # mouth opening vs the voice (list|targets|faceseq|gltf: the heads, the 22 targets, a face sequence, a .glb with glTF morph targets)
./build/Release/nightfire.exe --level 07000012 --wait 14 --screenshot out/shots/a.png --audio-dump out/audio/a.wav  # the sound of a capture in a WAV (captures are silent otherwise; --no-audio silences a normal run)
NF_SFX_LOG=out/sfx.log ./build/Release/nightfire.exe --level 07000001 --wait 15 --screenshot out/shots/n.png --audio-dump out/audio/n.wav  # logs every sound request, start, rejection and music jump
./build/Release/nightfire.exe --movie-frame 48 --movie 07370048 --screenshot out/shots/ea.png  # a movie frame (the EA logo at boot; captures start without movies: --movies)
./build/Release/nightfire.exe --level 07000001 --intro-movie --movie-frame 330 --screenshot out/shots/m1.png  # the mission's intro movie with the game's subtitles (--intro-movie: --level with the movie code)
./build/Release/nightfire.exe --input "none:19 enter:0.1 none:8 enter:0.1" --movie 07330048 --movie-frame 90 --screenshot out/shots/bg.png  # the main menu over its background loop (Modern: behind the blur)
./build/Release/nightfire.exe --level 07000001 --intro-movie --movie-frame 900 --audio-dump out/audio/m1.wav --screenshot out/shots/m1b.png  # a movie's sound in a WAV (BVOL 0x2710: the file x 0.305)
python tools/movies.py list       # the 22 movies (info <hash>, demux <hash> <dir>: the .m2v and the .wav into out/, test)
python tools/sound.py analyse out/audio/a.wav --window 1   # peak/RMS/zero crossings per window; banks|sfx|wavbank|streams|streamwav|music|musicwav|sfxtable|irxsyms decode the disc's audio (into out/)
./build/Release/nightfire.exe --list-adapters        # the graphics adapters (index, name, vendor, VRAM, integrated/discrete, monitor) and what --adapter picks
./build/Release/nightfire.exe --level 07000012 --adapter integrated --screenshot out/shots/igpu.png  # a capture on the iGPU (report line video: adaptador 1 "AMD Radeon(TM) Graphics" integrated)
./build/Release/nightfire.exe --level 0700000c --adapter integrated --width 1920 --height 1080 --input "mlook=700,0:120" --bench 10 --offscreen --bench-out out/bench/b.txt  # benchmark on the iGPU (offscreen: the GPU's own cost; without --offscreen: in a window, with Present)
./build/Release/nightfire.exe --level 07000012 --open-menu-page 4000004b --menu-tab pause --wait 2.5 --bench 10 --bench-frames out/bench/f.tsv  # the Modern pause (blur + Direct2D), every frame in a TSV
python tools/bench_matrix.py --exe build/Release/nightfire.exe --data . --out out/bench/m.md --seconds 8  # scenes x adapters (integrated,auto) x 1080p/720p x offscreen/window, a markdown table + the raw output
./build/Release/nightfire.exe --level 07000012 --fixed-video-hz 60 --input "none:1 rmb:3" --wait 2.5 --screenshot out/shots/ads.png  # modern aiming (ADS): report "ads ... peso 1.000 zoom 1.350 ... alca 0 0 d massa 0 0 d'"
./build/Release/nightfire.exe --level 07000005 --weapon 14 --fixed-video-hz 60 --input "none:2.5 rmb:3" --wait 4 --screenshot out/shots/w14.png  # --weapon <hex> (debug) gives and draws a weapon after the load
./build/Release/nightfire.exe --level 0700000c --fixed-video-hz 60 --input "none:1 rmb:3" --wait 2.5 --screenshot out/shots/scope.png  # the rifle scope by RMB: zoom 3.16 drawn, black side bars
./build/Release/nightfire.exe --level 07000012 --fixed-video-hz 60 --input "none:1 tab+mlook=400,0:0.4 tab:2" --wait 1.6 --screenshot out/shots/wheel.png  # the weapon wheel open in slow motion (report "roda ... espacos 8/6 imagens 3 ... lento 0.25"; pointing at an empty slot)
./build/Release/nightfire.exe --level 07000012 --fixed-video-hz 60 --input "none:1 tab+mlook=-400,-150:0.4 tab:2" --wait 1.6 --weapon all --width 1920 --height 1080 --screenshot out/shots/wall.png  # the wheel with every weapon (--weapon all, as the cheat): 24 + 16 slots, icon and ammo only
./build/Release/nightfire.exe --level 07000001 --fixed-video-hz 60 --wait 1 --width 1920 --height 1080 --msaa 4 --screenshot out/shots/bamboo.png  # alpha-to-coverage: the intro's bamboo (report "video ... msaa 4 cobertura 1/3"; --no-alpha-coverage to compare)
./build/Release/nightfire.exe --level 07000012 --fixed-video-hz 60 --input "none:2.5 tab:3" --wait 3 --screenshot out/shots/slow.png --frames-dump out/shots/slow --frames 9  # one tick every 4 frames, alpha 0/.25/.5/.75
./build/Release/nf_loader_check.exe . --miras   # the sight lines of every gun skin (the rows of src/engine/player_ads_table.inc); NF_SIGHTS_DUMP=<dir> writes the vertices
```

`--screenshot` renders a few frames, writes a PNG and exits: use it to verify rendering changes
(read the PNG to look at it); it also writes `<png>.txt` (camera room, visible rooms, player pose,
draws with 0/1/2 VU1 lights, where the fog pass falls and the depth clears: `luzes ... nevoa N limpa_z i,j lista2 N`;
the 2D line `2d` and one
`texto2d x y fonte "text"` per text drawn, in PS2 screen coordinates; the first line, `ui_hd`, is the HD
2D layer's state). It renders offscreen into a `--width` x `--height` texture (port-only), so captures can
be larger than the desktop (2560x1440, 3840x2160); `--width 1024 --height 576` (16:9) and `--width 768
--height 576` (4:3) are the usual quick sizes.
In player mode it ignores real input; `--input "keys:seconds ..."` scripts the player and the menus (also
the menu mouse: `mouse=x,y` puts the cursor at that point of the PS2 screen, 512x448 like the report's
`texto2d`, from that step on; `wheel=n` turns n notches when the step starts; `lmb`, `rmb`, `mouse4` are the
buttons) and `--at x,y,z,yaw` places it (`nf_loader_check` prints usable spots as `porta_usada=`). The `--input`/`--wait`
clock is `GameState.ticks` since the level loaded, so it keeps running while a menu pauses the game.
`--win <s>` (debug) turns channel 100 on (every objective done) `s` seconds after the load of the first level that is
not the front end, and `--shot-level <hash>` only captures once that pack is loaded (`--wait` counts from its load;
`<hash>:<n>` waits for its n-th load, e.g. `07000048:2` is the return to the front end after the mission when booting
without `--level`): together they reach the results screen of the front end. The report's last video line `roteiro t`
is the `--input` clock at the capture (to place steps after a load whose length varies with the machine). Without `--level` the capture boots the front end (Press START
accepts input after 4 s, the legal text's delay): the `--input` clock then counts from the first load and goes on into
the mission the script picks; the memory card is in memory unless `--save-dir` is given (captures never touch the
player's saves). Keep menu presses short (0.1 s): a key still held when a mission loads reaches the player (Enter opens
the pause). The report adds `frontend pagina X perfil N codinome "..." op O ativo A fim D nomes N missoes bits
dificuldade D` and `cartao memoria|pasta pilha N estado S/sub resultado R mensagem M perfis N ops N`. Expected `nf_level_check` values for spot checks: `07000001` 234
placed / 97 without geometry; `07000014` 596 / 438; `0700000a` 632 / 168.

## Decompilation workflow

1. Export the function: `tools/ghidra_headless.sh decomp <Name>` (C into `out/decomp/`) and the
   disassembly with the `ExportDisasm.java` post-script (see `docs/setup.md`). Find callers with
   `tools/ghidra_headless.sh callers <Name>`, call trees with `callgraph <depth> <Name>`.
2. Read the disassembly whenever VU0 macro instructions appear (`lqc2`, `vmul.xyz`, `vcallms`...):
   Ghidra drops the destination mask, and the mask decides whether the `w` lane is written.
3. Port into `src/engine/<module>.cpp`, keeping the original name and parameter order (signatures
   come from the mangled names in `out/functions.tsv`; `_VECTOR`, `MATRIX`, `quaternion_tag` are in
   `src/engine/ps2types.h`). Put `// [decomp] 0x<address> <Name>` right above the definition.
4. Add tests in `tests/` that check **independent properties** (identities, round trips, known data
   values) and document preserved quirks. Never test an implementation against a copy of itself.
5. Run `python tools/decomp_status.py` (progress, and it fails on wrong or duplicate markers).
   `--missing <prefix>` lists what is left in a module.

Regenerate the analysis inputs after Ghidra changes: `ExportAllCalls.java` → `out/functions.tsv`,
`out/calls.tsv`; `ExportHwUse.java` → `out/hwuse.tsv`; then `python tools/layers.py --json out/layers.json`.

## Engine conventions to remember

- Matrices are **row-vector** (`v' = v * M`), translation in row 3; `RotPreTransVec` proves it.
  The D3D11 shaders use `row_major` so engine matrices go in unchanged.
- Placement rotation is `RotMatrix(euler)`; the quaternion stored in block `0x1A` is the conjugate and
  is stale in ~1% of entries. `Quat_EulerToQuat` uses a different axis order than `RotMatrix`.
- Quaternions: most code is `(x, y, z, w)`; `Quat_QuaternionMultiply` and `Quat_QuaternionToMatrix`
  are scalar-first `(w, x, y, z)`.
- `PS2Sinf*` is a degree-9 polynomial on the VU0, not `sinf`; ported with the same coefficients.
- Frame timing: logic reads `FRAME_RATE_MUL` etc. from `GS_SetRefreshRate`; the PS2 runs 60 fps and
  drops to 30. 237 functions use these globals; some divide by `FRAME_RATE_INT` (only exact for
  divisors of 60). Done: 60 Hz logic (30 under load, as on the PS2), `Game_Draw` once per tick, video frames
  interpolated between the last two ticks (see "Tick interpolation").
- Hashcodes: the high byte is the type (`01` map item, `02` entity, `04` animation, `05` skin,
  `06` script, `07` level pack). The global table lives in `src/engine/hashtable.cpp`.
- **Two builds, x86 (the reference) and x64** (`platform/ptr32.h`, `platform/arena.h`, header comments; docs/port.md
  "64 bits"). Game structures and loaded data hold 4-byte pointers, so struct layouts match the PS2 and decompiled
  code needs no pointer translation. x86 (`build.bat Release`, `vcvarsamd64_x86.bat`, `/LARGEADDRESSAWARE`) is the
  default and the reference: every check and capture compares against it. x64 (`build.bat Release x64`,
  `-DNF_ARCH64=ON`, `vcvars64.bat`, build/x64/Release) keeps the same layouts: a pointer field of a game structure is
  `P32<T>` (on x86 an alias of `T*`, so x86 compiles exactly as before; on x64 a 4-byte class holding the offset from
  `__ImageBase`, 0 = null) and a function pointer field `F32<Sig>` (the code's offset in the image). Everything a field
  can point to lives in the 4 GB window that starts at the image: the image itself (code, globals, literals) and the
  arena (`nf::arena`, reserved in the largest free region of the window after the image) that holds the game heap
  (`psiMem_Init`), every `new`/`delete` and the STL (`operator new` replaced, static CRT `/MT` on x64) and the stack the
  game runs on (`nf::run_main` / `run_on_game_stack`: decompiled code stores addresses of locals). `NF_P32_CHECK` (on
  by default in x64) stops with the call stack when a pointer outside the window is encoded. `ps2types.h` asserts
  `sizeof(void*) == 4 || NF_PTR64`, and `/we4311 /we4312 /we4302` make pointer truncations errors on x64. Platform,
  render and UI code keep native pointers; so do port-only structures never shared with game data
  (`ProvisionalObject`, `PcTab`, `MenuHandlerEntry`, `CREDIT_SOURCE`, `CONTROLFUNCS_tag`, the driving `Simulation`).
  Rules for new code (the x64 build catches some at compile time; the others are silent there, so test both builds):
  - a pointer field of a structure with the PS2 layout (or one stored in game memory) is `P32<T>`, a function pointer
    `F32<Sig>`; a struct with such fields gets a `static_assert(sizeof(...))`;
  - a pointer word read or written by offset: `nf::p32_load<T>(addr)` / `nf::p32_store(addr, p)`, never
    `memcpy(&p, addr, 4)`, `*reinterpret_cast<T**>(addr)` or `rd<T*>`/`at<T*>` (the raw accessor templates refuse a
    native pointer type on x64 through `nf::is_native_ptr_v`: write `rd<P32<T>>`);
  - a pointer stored as an integer: `nf::p32_u(p)` and `nf::p32_ptr<T>(v)` (also for sentinels such as `0xffffffff`);
  - arrays of pointers in the game heap are `P32<T>*` with `n * 4` bytes, never `sizeof(T*)`; `QuickSort` over them
    uses element size 4;
  - the native pointer of a field (for `reinterpret_cast`, `auto*`, `?:` with a `T*`): `nf::raw(field)`;
  - float math: `nf::fm::sin/cos/tan/asin/acos/atan/atan2/exp/log/log10/pow` (`platform/fmath.h`), not `std::` on a
    `float`: the x64 CRT's float functions differ from x86's (which compute in double) by 1 ulp;
  - a local `MATRIX` built by `RotMatrix*`/`Quat_QuatToMat` has only its rotation written: zero it (`MATRIX m{}`)
    when the whole matrix is used afterwards (`Concat`, `Mat_Copy`), or it carries stack garbage.
  Results: every check output and capture of x64 is identical to x86's. Internally the float state can still differ by
  an ulp: MSVC x86 returns `float`s in the x87 `ST(0)` and keeps computing there at 53-bit precision until a store (79
  functions, ~30 in game logic); an x86 build compiled with `/Gv` (an experiment, not in the repository) is
  bit-identical to x64 tick by tick (docs/port.md "64 bits"). Reads of stack garbage differ between the builds: give
  matrices and vectors whose whole value is used an initial value.
- Memory: one heap (`Mem_*`, `src/engine/memory.cpp`) with 12-byte block headers; `psiMem_Init`
  reserves `psiMemHeapSize` bytes with `VirtualAlloc` (x64: `nf::arena::alloc_pages`, inside the 4 GB window). `ResetMap_Load` re-runs `Mem_Init` on every
  level load. Allocation tags: high byte =
  category (`Mem_Info` sums per category), low byte = default alignment (never 0 in practice).
- Original loader: `LoaderLoad` is a state machine called until it returns 0; `ResetMap_Load` loads
  the `07F` (animations) pack with `LOADER_ANIMS`, then the `070` pack with `LOADER_LEVEL`. Map items
  stay loaded in the heap and the runtime structs point into them. `psiFile*` reads 0x800-byte
  sectors from `FILES.BIN` using a copy of `FileList` (filled from the ELF by the caller, then
  `psiFileSetup`).
- `nightfire.exe` loads levels exactly like the game: `GF_LOAD_MAP` calls `ResetMap_Load` every tick
  (two ticks: the `07000500` load-screen pack, then the level) until it swaps the state for
  `GF_LOCKS_INIT`. GPU resources are created during the load through `g_psi_gfx_backend`
  (`platform/psi_gfx.h`): the ported `psiCreateMapTextures` / `psiCreateEntityGfx` do the PS2
  bookkeeping and call the backend instead of uploading to VRAM. The backend decodes the 0x2d
  block **before** the pointer fixups rewrite it.
- Objects: `obj_tag` (0x110-byte header + type data, `engine/object.h`) live in `DynamicObjList` and
  in their room's list (`cel_tag::objects`). `control_funcs` (generated by
  `tools/gen_control_funcs.py` into `src/engine/control_funcs.cpp`; rerun it and add the name to
  `PORTED` when an update/collision/delete function gets ported) holds each type's callbacks.
  `parsemap_create_dynamic_objects` is ported; each `*_Create` not ported yet goes through
  `pending_create("Name", ...)`, which logs it and records `{glist, View_RotTransMatrix, flags}` in
  `g_provisional_objects` so the renderer still draws it. Port a `*_Create` by replacing its
  `pending(...)` line in `objects.cpp` with the real call from the decompilation. Objects
  that are not visual (Env) or not created outside the main map are no longer drawn, which matches
  the original.
- Collision (`engine/collide.h`): a test (`HITTEST_tag`: sphere 0x101, ray 0x201, capsule 0x800,
  triangle gathering 0x2000...) goes through `Collide_Pick` (broad phase: rooms reached through
  portals by `Collide_StraddleCels`, their child cels, objects filtered by `Collide_PickObj`) and
  `Collide_Intersect` (narrow phase in the cel's or object's local space: box tree + quantized
  triangles; the local copy's +0x7c `matrix` points back to local->world). Faithful quirks: rays reuse
  the sphere test's global `DeltaP`; the sphere moves by the accumulated push, not the increment.
  The capsule (`Intersect_CylGeom`, hand-written VU0) moves start/end by each push, sets `found`
  bit 0 on floor contact (normal within ~45 degrees of the end->start axis, contact beyond `end`),
  stops scanning a leaf at the first triangle whose first vertex x is past the segment box, and
  leaves `-normal` in `hit->dir`.
  `Collide_PickObj` overwrites `test->sphere` with the object's sphere (x1.5 radius for capsules).
  Jointed objects without colldata (`mode & 8`) go through `Collide_Jointy`: capsule vs the other
  object's capsule (full push in single player, half in multiplayer); ray/sphere vs the joint boxes of
  `AnimGetCollData` (ported with the AI body, `drone_move.cpp`). Type 0x2000 (`Intersect_CylTriGeom`) only appends
  world-space triangles to `TriHeap`/`TriOnCnt` for `Collide_PlaceObject`. The Collide module is complete:
  `Collide_CylinderIntersect` (a caller-built capsule test vs the geometry, sorted hits; only the rigid-body pendulums
  use it; the port restores `test->tester` afterwards, the original leaves it pointing at its stack) and
  `Collide_PlaceObject` (does a radius-r object fit on the floor below pos: down ray 3 m, pos to floor + normal x r,
  a 0x2000 capsule gathers up to 0xa6 triangles, any plane closer than r - 0.13 refuses, near-coplanar ones (0.96)
  within 0.2 form the floor box, which must reach the half width in |side| + |front| at both corners; used by
  `GT_DeployMiniGun`).
  `build_link_objects_to_rooms` uses the static collision to hang static cels (types
  0x8017-0x8019) under rooms (0xc023/0xc047) via `cel_tag::children`/`parent`: ~97% of props get a
  room on the real data (`nf_loader_check` prints `em_sala`).
- `out/clean.sh` hides `uStack_xx = ...` lines; in functions that build structs on the stack (the
  collision tests) use `out/clean2.sh`, which keeps them.
- Drawing goes through the game's own `Game_Draw`, once per logic tick (see "Tick interpolation"). In free-camera
  mode, every video frame `main.cpp` writes the free camera into `glb_viewer[0]->camera` (row 2 forward, row 3
  position, **determinant +1**, which keeps the frustum planes facing inward; `fov`/`aspect` from the window) and runs
  `Camera_Update(0)`/`Camera_CheckLocation` (tracks the camera's room through portals); the next tick's `Game_Draw`
  uses it. The PC `psi*` draw functions
  (`platform/psi_draw.cpp`) record each `psiDrawObjectMatrix` with the current camera and blend
  state in `g_psi_frame`; `RuntimeGfx::collect_frame` keeps camera 0's draws and adds the provisional
  objects (which the game does not know, so they skip visibility). With a player, camera 0 is the
  game's (`Player_PositionCamera` via `Camera_SetUpdator`, updated per tick by `Camera_UpdateAll`)
  and `main` only sets `fov`/`aspect`; the free camera (F2, `--cam`, or levels without a player
  start, where `main` calls `Camera_Enable(0, ...)` itself) overwrites the matrix every video frame
  and runs `Camera_Update(0)` with the updater detached. The renderer's view matrix comes from the interpolated
  camera of the tick records (row 1 up, row 2 forward, row 3 position), or the live free camera.
- Visibility (`engine/view.cpp`): from the camera's room, `Vision_Recurse` clips each front-facing,
  open portal against the current frustum (planes of depth d at `PortalPlanes[d * 10]`, contiguous
  like the original) and recurses with the planes through the clipped polygon; `View_AddCels` puts
  the visible rooms, their child cels and objects in the three lists (0 opaque, 1 alpha, 2 overlay).
- First-person view model and the depth clear (docs/port.md "A arma em primeira pessoa e a limpeza do Z"): list 2 is
  the gun in hand, the arms and the muzzle flash (obj flag 0x2000; the gun has 0x1a0a0). On the PS2, `Game_Draw` (with
  `Tots[2] != 0`) puts camera 5 in use and calls `psiClearZ` before cameras 0-3's list 2: the `ZClearPacket`
  (`CreateZClearSprite`) writes Z = 1 (farthest for the GS GEQUAL test) over the whole screen without touching colour,
  so the view model never clips into the world and only depth-tests against itself; cameras 4-9 draw their list 2
  without a clear. `WipeZBuffer` starts at 1 and nothing in the ELF writes it. The port records where each clear falls
  (`PsiFrame::clear_z_at`, before `draws[i]`; the `psiPreDraw` one at 0 becomes no item), `collect_frame` inserts a
  `DrawItem::clear_depth` there (`render/frame_markers.h`, which also places the fog item: same index, fog first, as it
  reads Z; provisional objects go before the clear) and both renderers clear the scene depth at that point (D3D11 also
  with MSAA/render scale; D3D9 `Clear(D3DCLEAR_ZBUFFER)`). The tick interpolation inserts kept list-0/1 items before
  it. In the free camera (F2, `--cam`) `main.cpp` drops the clear items: the gun sits at the player's head
  (`Player_PositionGun`), not in front of that camera. Before this fix the port only counted the call and the gun was cut
  by nearby walls and props.
  A camera outside every room ends up in `glb_world->first` and sees almost nothing, like the game.
  `switch_ForceDrawAll` (key P, `--drawall`) is the game's debug switch and stops at the 64 entries
  of `VisCelList`, so it is **not** a "draw everything" reference.
- The sky (`GfxList.sky`, `SKYOBJ_tag`) is drawn by `View_DrawSky` only when a visible room lets the
  sky through (`cel_tag::type` bit 28 clear); models with flag 2 follow the camera. Entries can have a
  lightning model (`glist_flash`) swapped in at random intervals. `ResetMap_Load` clears the
  provisional list, the sky slots and `g_player_init_call` before `Mem_Init` (port-only: the original clears the sky
  in `Env_Reset`, from `ResetMap_GameInit`, which load step 0 of a front-end load does not reach). Clouds
  (`Cloud_Create`) are sky slots carrying their object. `Game_Draw` keeps per-frame state (lightning and thunder counters, the
  rotating sky of `0700001b`, `GameState` +0x30 `draws`, `GlobalBlur`, `Rand` draws for flag-4 jitter); it runs once
  per tick, so that state advances at the PS2 rate at any refresh rate (with `--no-interp` it runs every video frame
  again, the old behaviour: 2.4x too fast at 144 Hz, and captures whose outcome depends on `Rand` vary with the
  machine's speed).
- Tick interpolation (`render/frame_interp.h`, header comment; docs/port.md "Interpolação entre ticks"; port-only):
  after each tick that asked to draw, `main` runs `Game_Draw` once, keeps `RuntimeGfx::collect_frame`'s output plus
  the camera in use (camera 0, or 4 in a scripted scene; matrix and fov) as a `TickRecord`, and `FrameInterpolator`
  keeps the last two. Every video frame `build(alpha)` gives the items and camera at `alpha` = the fraction of the
  current tick elapsed (`psi_clock_vblanks()` - the tick's VBlank, over the VBlanks between records; the image is one
  tick behind): camera position lerp, rotation slerp, fov lerp; face morph weights (`DrawItem::morph`) lerped weight by
  weight (`interp_weights`, a missing side counts as zeros); rigid matrices and every skinning-palette bone
  decomposed (translation, rotation quaternion, per-row scale), interpolated and recomposed (exact at the endpoints).
  Matching: `DrawItem::interp` (id from the draw's owner, which `psi_draw_set_owner` records: `View_DrawObjList` per
  room/cel/object with the list 0/1/2, `View_DrawSky` per sky slot, `draw_attachments` per mark; plus the glist,
  `obj_tag::created`, the call order within the owner, skinned or not). Unmatched, duplicated ids, another mesh or
  bone count: the current tick's item. Items jumping > 2 m or > 45 degrees per tick snap (teleports, the muzzle flash's
  random roll). Cuts (no interpolation at all): the first record after `reset()` (every level load, restarts
  included), camera 4 switching on/off, the camera moving > 1.5 m or turning > 45 degrees per tick, fov changing > 25%.
  Particles (the snow too), shards, 2D, fog, lights, tweak and env-map rows are always the current tick's (2D never
  interpolates; the Modern menus draw per video frame as before). Screen fades (`DrawItem::fade`) lerp their alpha
  between the two ticks when both have a fade at the same position of the list (`FadeSlot`), else the current one. Visibility stays per tick; previous-tick items with no match that
  are still valid (cels always, objects alive and drawable in that camera with the same `created`; only owners the
  current tick no longer draws at all, so a model swap never shows both) are kept in the frame (list 0 at its end,
  list 1 at its start), so the interpolated camera does not lose what the previous tick
  saw. With one video frame per tick or less (EMA of frames per tick, hysteresis 1.08/1.15) and in captures without
  `--fixed-video-hz`, `alpha` = 1 (no added latency). Instant mouse look (`[video] instant_mouse_look`, default on):
  render-only, the camera also turns by (1 - alpha) of the mouse turn the current tick applied
  (`g_player_mouse_look`, recorded in `Player_Update`) plus the mouse motion since the tick (`psiInput_PeekMouseLook`),
  yaw about world y, pitch in the camera's forward-up plane clamped to +-90 degrees like `Player_ViewClamping`; list-2
  draws (first-person gun, muzzle flash) rotate with it about the eye (`rotate_overlay`). Only in first person under
  mouse control (`Player_PortMouseLookGate`), no scripted camera, no menu, not in the free camera. Known limit: the
  culling is the tick's, so at the leading screen edge of a fast turn something can appear one tick late. The free
  camera is drawn live (not interpolated); objects still interpolate. `--no-interp` / `[video] interpolation = off`
  restores the old loop. Frame cap: `[video] fps_cap` (off, 30...240, `--fps-cap`), `next_frame_deadline` + a
  high-resolution waitable timer after `present`; at 30 the logic runs in the PS2's 30 fps mode (skip 2). Captures:
  `--fixed-video-hz <n>` (simulated n Hz display: `psi_set_clock_vblanks`, 60/n VBlanks per video frame),
  `--frames-dump <dir> --frames <k>` (k PNGs + `frames.tsv` from the capture frame; `--frames-no-png`),
  `--interp-alpha`, `--only-2d`, `mlook=dx,dy` in `--input`; the report line `ritmo ...` (alpha, cut, items, pairs,
  moving, snapped, kept, bones, CPU cost per frame and per tick, frames per tick, the drawn camera, `Game_Draw` count,
  sky spin, lightning). Audio is unaffected: `VBlankCount` comes from the clock and `SFXUpdate` runs per tick. Tests:
  `tests/test_frame_interp.cpp`, `view_draw_owner_identity`.
- Doors (`engine/door.h`): type-0x1c objects that follow their own path (`framelist_tag`, block
  0x19: position + quaternion per frame) through `Spline_Interp`; `DOORINFO::pos` goes 0..1 (swing
  doors are closed at 0.5, sliding doors at 0). `build_LinkDoors2Portals` stores the door in the
  portal it covers (`portal_tag::unknown_0c`), so `Vision_Recurse` stops at closed doors. Door logic
  (`Door_Update`, `Doors_Calc`, `Door_Activate`) is ported but nothing calls it yet (no
  `Game_Run`/object handler), so doors stay in their initial state. `Door_Activate` with a null
  player returns 0 where it needs the player's position (port-only guard).
- GS state: every 0x2d chain writes its own `ALPHA_1`/`TEST_1`/`ZBUF_1` with A+D pairs (REF texture,
  FLUSHE, DIRECT) and every vertex batch on the disc has all three; `decode_gfx` stores the state in
  force at each batch's MSCNT (plus the GIF tag's PRIM.ABE) in `SubMesh::gs`, with sub-meshes in chain
  order (consecutive batches with the same texture and state merge). The renderer resolves per
  sub-mesh: the mesh's own registers, then the draw's (`DrawItem::gs`: `psiSetUpColourBlend` 0..3 =
  ALPHA 0x48/0x44/0x2a/0x42, `psiSwitchZBufferRead/Write`), then the old rule (opaque: alpha test 50%
  and Z write; blended: no Z write). `gs_blend_equation` maps (A - B) * C + D to blend factors (FIX
  in the blend factor, Ad = render-target alpha; every draw writes As to the target alpha, like the
  GS); the alpha test (ATST/AREF, GS scale 0x80 = 1.0) runs in the pixel shader; pixel alpha =
  texture alpha x vertex alpha x tweak alpha (TFX MODULATE, TCC 1). Provisional objects are inserted
  before the first blended item. `nf_loader_check` prints the state table (`estado do GS`).
- Tweak (`psiSetTweakARGB`, 0..255 = 0..1): `DrawItem::tint` is `TAmbient`; 0xff everywhere is a
  no-op.
- Lighting = the VU1 microprogram (`docs/formats.md`, "Microprograma da VU1"; read the code with
  `tools/vudis.py`). The batch GIF tag w (8 common, 0x10 env map) + the routine index from
  `FillMatrixChainRot/Skin` (+4 lights, +2 tweak) pick 0x25e/0x2a3/0x340/0x2e0. Per vertex: colour x
  `TAmbient`; with lights `min(255, TAmbient*c + TColor1*s1 + TColor0*s0)`,
  `s = max(0, max(0, 1/d² - 1/r²) * (n . (L - p)))`, n = normal / 128, lights in object space via
  `(L - t) * R^T` (world for skinned); env map: no lights, `ST = 0.5 + (n_int . camera row 0/1) / 256`;
  FTOI0 truncates. `decode_gfx` keeps vertex colours raw (GS scale, 0x80 = 1.0, rgb up to 0xff), rigid
  normals as int / 128 (not normalized), skinned normals as disc / 64, and `SubMesh::env`; the vertex
  shader runs the routine (`src/platform/vu1.cpp` is the CPU reference used by tests), the pixel
  shader does GS MODULATE (`Ct*Cv/128`, `At*Av/128`). Skinned normals for lighting rotate by the
  first bone only (the VU0 `SkinIt`). Key V / `--debug-shading` swaps in the old fixed directional
  light; `--no-lights` drops the VU1 lights. The env-map routine 0x2e0 is the vertex shader's `vu1_env_st` for every
  part with `SubMesh::env` (chrome, glass, water: 4 parts in 07000001's start view, 27 in 07000004's; report line
  `mapa_ambiente partes N ligado 1`); `--no-env-map` (debug) draws those parts with the mesh UVs, as a comparison.
- Fog (levels with `FogOn` from `psiPreGameRun`) is not per vertex: `psiPostSolid` -> `psiDrawFogPass`
  copies Z bits 8-15 into frame alpha and blends the fog colour where Z < 0x7fff:
  `keep = (Z >> 8) / 128`, GS `Z = A / z + B` from `Ps2Far1` (`vu1::gs_z`), cleared Z = 1. The port
  records `PsiFrame::fog_at`; `collect_frame` inserts a `fog_pass` item and the renderer does a
  full-screen pass reading the depth buffer (R32_TYPELESS, distance from `FrameParams::z_near/z_far`).
  `Ps2Far1` only scales the GS Z (the PS2 far clip is `Ps2Far2` = 2^24 m). In the alpha list with fog,
  `psiDrawObjectMatrix` fades the tweak alpha by the distance of box 0's centre (`vu1::alpha_fog`,
  `psiSetTweakA`, which also lowers `olda` for later draws, as on the PS2). `--no-fog` skips the pass.
- Particles: `psiDrawParticleList` records positions, GS-scale colours, `count & ~3`, the `Tex` slot
  and a world half-size (`psi_particle_half_size`, ~0.432 * size: the PS2 sprite size at 60 degrees);
  `collect_frame` builds camera-facing quads (texture rotated 180 degrees, centre culled outside 2x the
  screen or nearer than 0.1 m) and the shader modulates like the GS (alpha up to 2.0, additive
  premultiplied). Emitter objects are type 0x32 (mode 0x104); `Emitter_Draw` is called from
  `View_DrawObjList` (mode 0x100).
- Screen fades, screen blur and the load screen (`platform/psi_draw.h`, `platform/psi_loading.h`, `engine/resetmap.h`,
  header comments; docs/engine.md "Esmaecimentos, rastro e tela de carga", docs/port.md "Esmaecimentos, rastro e tela de
  carga"). Fades: `Camera_SetFade(index, ticks, 0xRRGGBBAA, lock)` animates `viewer_tag::fade` and `Game_Draw` calls
  `psiFadeView` for each camera with a fade in progress, after that camera's sprites: on the PS2 a full-screen untextured
  sprite (ALPHA_1 0x44, alpha in GS scale so 0x80 is opaque, no Z test). The PC records a `PsiDrawCall` with `fade = 1`
  in draw order; `RuntimeGfx::add_fade` makes it a full-window quad (`DrawItem::fade` = the camera, texture
  `kQuadNoTexture`: a 0x80808080 texel so MODULATE gives the vertex colour), never hidden by `hidden_2d`. Users: camera 0
  fades in at every level start (`Player_Init`, 45 ticks) and after a scene (22 ticks), camera 4 fades out at a scene's
  end and when it is skipped, camera 6 is GameFlow's (deaths, restarts, the mission end, level changes, 20 ticks of black
  before a loadable NIS: state 0xd), and `Player_SetFlashBang` (flash grenade, the quake `Apocalypse_*`) drives camera
  0 white with `BLData::blur`. Order matters: camera 0's fade lies under camera 5's HUD, camera 6's covers everything.
  Screen blur: `Game_Draw` calls `psiSetScreenBlur(GlobalBlur)` (`ScreenBlur = 0x80 - blur / 4`, then `GlobalBlur`
  drops 16), and the next `psiPreDraw`'s `CreateSqueezeFrame` copies the new frame over the displayed one with ALPHA FIX
  = ScreenBlur: a trail. `PsiFrame::screen_blur` carries it; `main` calls `Renderer::screen_blur(keep)` after `render`,
  keep = (1 - ScreenBlur / 128) raised to the frame's VBlanks (the same trail per second at any refresh rate; a copy
  of the target is kept only while keep > 0; Modern menus reset it). Users: the flash (`BLData::blur`), zoom changes
  (0x7f) and the underwater camera (`Camera_Shear`, 0x91). Not reproduced: `SqueezeColourIn` 0x91, the colour of that
  copy (it would brighten every frame by 13% against all earlier captures; to confirm on PCSX2 or hardware). Load screen: `ResetMap_Load` calls
  `ResetMap_GenLoadScreen` on every load but the front end (unless `--no-load-screen`): list-99 sprites (the level's
  picture from `ResetMap_LevelCode2Img`, label 0x3c1, the mission name and a random tip: per-level tip tables and 43
  generic ones from the ELF, `Rand_Random`), drawn on camera 10; `psiStartLoadingScreen` + `psiAllocateLoadingBlobs` (7
  DMA chains of six 16x16 cells of texture 0x3000175, lit 0x8029a0ee / dim 0x800c315c, x = 55 + 16 i, y 309-325) and the
  `VBlankInt` steps the lit blob every 11 VBlanks, ping-pong 0..6, until `psiEndLoadingScreen`. No minimum hold: it
  stays as long as the load (seconds of DVD on the PS2, ~0.2-0.7 s here). The PC: `g_psi_loading_hook` (Wipe, Screen,
  Blob, End) lets `main` (`LoadingPresenter`) present the black of `PS2WipeVram`, then the screen (kept in a snapshot,
  since the next `Mem_Init` frees its textures) and its blobs, driven by `psi_loading_pump` from the loader loop with
  the wall clock; in captures and `--offscreen` nothing is presented (`--load-screen <png>` writes the first one,
  `--load-screen-blob <n>` with chain n lit). Report lines `esmaecimento N | camera c cor rgb alfa a estado s tempo t
  passo p`, `clarao borrao B resta R total T rastro S | tremores ...`, `tela_carga telas limpezas bolinhas passos bombas
  ms textura imagem apresentadas`. `nf_loader_check` runs `GenLoadScreen` on every load (its `Rand` draws are part of
  the run).
- Weather and scenery (`engine/weather.h`, `engine/scenery.h`, header comments; docs/engine.md "Chuva, neve, raios e
  nuvens" and "Enfeites"): all with the game's code. Drops (`DropVector/DropState[4096]`): the room environment's kind
  (`Env_Create` parameter 0; `Env_Reset` sets 0xff none) picks 0 particle snow (4096, texture 0x3000045), 1 mesh rain
  (0x2000290, 128 in 07000009-0b else 64; Paris adds 0x2000039 billboards with tweak 0x24), 2 rising bubbles (2048,
  0x3000087). `Env_Update` (every tick: `DrawSnow` is 1 in the ELF) runs `UpdateDrops` only when the camera in use sees
  the sky: a 30 m cube around it is clipped by the rain boxes (placement 0x32, `RainBox_Create`: type-1 objects outside
  rooms chained by `obj_tag::owner` from `RainBoxRoot`), up to 21 new drops per tick, wind + one of 32 `RandTable2`
  speeds; `DrawDrops` (in `Game_Draw` after the shards, cameras 0-3 and 4) draws kinds 0/2 as one `psiDrawParticleList`
  and kind 1 as one model per live drop (none with camera 0 under the room's water). Lightning beams (placement 0x2c):
  two with the same parameter 1 pair into 8 segments (model 0x200032c, `PositionBeam`, re-zigzagged by `FractalThing` on
  each `Reflicker`) shown only while the sky shows a reflickering lightning (`SuperHappyLightningFlag`): never in
  07000001's normal play; `--force-lightning` (debug) forces it. Clouds (0xd4): sky slots that slide with the player.
  Scenery: ripples (0x21, rings of model 0x2000129 through `Gas_Init`), leaf generators (0x2b) and leaves (type 0x2e,
  256 ticks), trees (0xf3, animated skin with a random start frame), rotors (0x71, spin while their channel is off),
  one-sided walls (0xd5, hidden when the camera is behind), `Env_Breath` (white breath in kind-0 snow outside covered
  rooms, else only in vision mode 2), `Particles_*` (empty on the PS2: `psiCreateParticles` returns its v0, 0 here).
  Divergences are in the file headers (drops updated in place instead of through the scratchpad, guarded null reads
  counted in `g_weather_guards`). The faithful `Rand` draws (96 per load in `InitDrops`, `Rand_Rand(20000)` per tick in
  `Env_Update`, ripples, leaves, trees, the load screen's tip) change every `Rand`-dependent capture and probe against
  older builds; `--no-env` (nightfire.exe and `nf_loader_check`, debug) leaves weather and scenery off and provisional
  and skips the load screen, giving the old images and probe results (07000012 at 8 s: 0 pixels; 07000003 and
  0700000a probes identical to main). Report lines `tempo tipo max vivas novas criadas fora caixas ativas vento raios
  pares desenhados nuvens guardas` and `enfeites ondulacoes aneis geradores folhas vivas arvores helices lados
  particulas bafos`; `nf_loader_check` adds `tempo= caixas= raios= nuvens= enfeites=` per level. Cost on the Radeon iGPU
  at 1080p (`--bench`, turning): 0700000b's rain 353 -> 204 fps (the drop models and ~150 ripple rings, +2 ms of GPU),
  07000005's snow 288 -> 252 fps, 0700000c and 07000001 within noise. Tests: `tests/test_presentation.cpp`.
- Skinned meshes (0x2d blocks with the SkinInfo struct) decode in bind pose into `SkinnedVertex`
  (two skeleton bone indices, already mapped through the mesh's bone list, and the first bone's
  weight) and are skinned on the GPU. The engine draws them with
  `psiDrawSkinObjectMatrix(glist, matrix, obj, palette)` (`platform/psi_draw.h`): `palette` is one
  world-space MATRIX per skeleton bone with the inverse bind pose baked in (`p * palette[bone]`),
  `matrix` is ignored; the call copies `palette[0 .. max bone in the list]` into
  `g_psi_frame.palettes` and `collect_frame` puts it in `DrawItem::palette` (128 bones max,
  `kMaxSkinBones`). A skinned mesh drawn rigidly (or the reverse) is skipped. Faces (morphs): `SkinnedVertex::morph`
  is the SkinIt input's morph index (high 16 bits of w: the 0x2B block element where the vertex's deltas start, -1
  none); `RuntimeGfx::on_entity` decodes the entity's block 0x2B (`entgfx_tag::morph`, `nf::decode_morph`, header
  elements zeroed: the VU0 has no denormals) into `EntityMesh::morph`, and the `Renderer` keeps the deltas in a
  `StructuredBuffer<float3>` per mesh. `psiDrawSkinObjectMatrix` records the weights in force (`Morph_Weight[k]` at
  target `Morph_Index[k]`, 22 floats in `g_psi_frame.morphs`, `PsiDrawCall::morph`) under the PS2's conditions (the
  mesh has the block, `Morph_Count != 0`, and outside a scene the smallest `closeness` byte is <= 2); `collect_frame`
  puts them in `DrawItem::morph`; a draw with any non-zero weight uses `vs_skin_morph` (`pos += w[k] * delta[m + k]`
  before skinning, = `nf::morph_vertex`; normals unchanged, like the SkinIt), all other skinned draws keep `vs_skin`
  (identical images). `--no-morphs` draws the rest pose. The D3D9 Remix path does not apply morphs. See "Faces".
- `--screenshot` ignores mouse and keyboard: the window pops up in front and a click or key used to
  move the camera of the capture (that made earlier comparisons flaky). Only `--input` drives it (keys, the
  simulated pad and the menu mouse).
- The game loop is the ported `GameFlow_Main` (one call per 60 Hz tick from `main.cpp`). After
  boot the stack is `[GF_BOOT, GF_WAIT_START]`, and `GF_WAIT_START` (2) is the playing state: it
  runs `Game_Run` (objects, doors, collision, cameras) and asks to draw. The boot requests the front
  end (`07000048`); `main` pops that request and asks for the command-line level with
  `ResetMap_LevelToLoad(level, 0, 1)` (no intro movie). Level switches (PgUp/PgDn) also go through
  `ResetMap_LevelToLoad`, which only accepts requests in `GF_WAIT_START` (and the fade/camera
  states). `g_gameflow_hooks.draw` (the only hook left) lets `main` turn the draw into a flag and run `Game_Draw` once
  after the tick (every video frame with `--no-interp`); without the hook (tests) `GameFlow_Main` calls `Game_Draw`
  itself. The fade and countdown requests use camera 6 like the PS2: `GF_FADE_REQUEST`/`GF_CAMERA_REQUEST` start
  `Camera_SetFade(6, ticks, colour, 1)` and push only if it started, `GF_FADE_WAIT`/`GF_CAMERA_ONLY` wait for its
  `fade_state`, `GF_COUNTDOWN` counts `viewer_tag::fade_speed` down (test fixtures that push them need a camera 6).
- Port-only debug helpers in `main.cpp`: Space in the free camera with no menu open (`--use` in
  captures) activates the door in front of the camera with a stand-in object; `--wait <s>` delays the
  capture by game time; every `--screenshot` also writes `<png>.txt` with the camera room, the visible
  rooms of camera 0 and the draw count. The visible-room linked list at `GfxList + 4` belongs to
  whichever camera captured last (cameras 5 and 7 capture after 0), so read camera 0's rooms from its
  list 0 (keys <= -80000).
- `nf_loader_check` also runs two seconds of game (`Game_Run` + `Game_Draw`, all doors activated)
  after each load, to catch crashes in ported logic on real data; it prints `jogo: abertas=N`.
- HD textures (`data/texture_pack.h`): the user's PCSX2 replacement pack lives in `textures/`
  (ignored by git). PCSX2 names files `<tex hash>-<clut hash>[-r<region>]-<bits>.dds` with XXH3-64 of
  GS memory: region textures (the game packs textures into VRAM "spaces" and uses CLAMP
  REGION_REPEAT) and sub-block textures hash the linear indices, 1 byte per texel; others hash the raw
  256-byte GS blocks (PSMT8 16x16, PSMT4 32x16, column-swizzled; `gs_block_offset8/4` reproduce the
  GS tables). We try both hashes. CLUT hash = XXH3 of the 32-bit palette in logical order (CSM1
  unswizzled). Exact (texture, CLUT) pairs only; 89% of level textures match. `RuntimeGfx::on_texture`
  loads the DDS (`Renderer::set_texture_dds`; no-mip files are blitted to RGBA8 and get GPU mips);
  T toggles HD. Texture alpha everywhere is GS-range (0x80 = opaque) and the shader scales it by
  255/128.
- The player (`engine/player.h`): object type 3 created by `Player_Init` at the start chosen by
  `Player_Start`; `BLData` (0x970 bytes) in `obj->data`, `obj->hit_test` = `&BLData::hit_test`. The
  animation object (`ANIMOBJ_tag`, 0x120 bytes, `obj->sphere_src`) is the real one (skin 05000007
  or the level's); `root_height` (hip above the feet) comes from the animation's root + `root_offset`
  (1.089 standing, lower crouched). `obj->pos` is the hip: `Player_StandAtNewPosition` snaps to the
  floor + 1.1,
  eyes are `eye_height` 0.7 above (`Player_GetHeadPos`), the body is a capsule (type 0x800, radius
  0.55) from pos + 0.275 down to root_height below pos; `Player_FeetOnPoint` (ray down) sets "on
  ground" (anim flag 8) when the floor is within root_height + 0.1. Gravity `WldGravity` (0, -9.8, 0)
  in m/s, `BLData::fall` integrated per tick with `REC_FRAME_RATE`. Per-tick velocities in
  `BLData::vel` are local (x strafe, -x is right; z forward); rot.y grows turning left. `pitch` is in
  units of 90 degrees. Use (action 14, key E) goes through `Player_WeaponFiring` (use, or reload
  when nothing was used) to `Player_Activate` (sphere of radius 1 one metre in front of the eyes).
  Damage: `MP_RegisterBulletHit` (true outside multiplayer) -> `Player_HandlePain` (difficulty
  multipliers `Plr_DMod_*` copied from the ELF, `BLData::armour` absorbs shots, `CheatInfo` +0 is god
  mode, `BLData::pain_flash` is the hit's red: the PS2 draws no full-screen red, it is the alpha of the red hit-direction
  arrows, HUD pane 1 sprites 20-23 by `BLData::hit_dir` in `HUD_UpdateHealthPane`, fading `FRAME_RATE_MUL` per tick;
  drawn). `Player_CheckForDeath` (every tick)
  kills at 0 health: type 0x12, state 2, substate 0xd, channel 0x5e; `Player_HandleDeath` then shows
  the blood curtain (HUD pane 0xc) and sets channel 0x62 (mission failed) when it covers the screen
  (~7.5 s) or Start (action 25: Enter or Space) is pressed; `Mission_Update` takes it from there (see
  "HUD and mission").
  The screenshot report has a line `vida H colete A vermelho R morto D missao_falhou F recomecos N`;
  `nf_loader_check` adds `vida_jogador=H morto=D` to `tiros_ia`.
  The free camera starts at the player's eyes (`g_player_init_call`);
  packs without a player start fall back to the overview from `suggest_view`.
- Animation (`engine/anim.h`): `AnimLoadFile` loads `<hash>.bin` from `FILES.BIN` (05 skins,
  04 sequences, 06 scripts) during `LoaderProcess`; `AnimProcessSkinData` swaps the skin file in the
  hashtable for an `ANIMSKIN_tag`, `AnimProcessSeqData` overwrites the sequence header's first
  three words (channel pointer, size, cursor), `AnimSkeletonProcess` fills `pSkeletons[id]`. An
  animated object (`obj->mode` bit 8) has a list of scripts (`ANIMSCRIPT_tag`, data = 06 file:
  frame count, end mode, commands 0 sequence-in-range, 1 sound, 4 event); each script owns two
  sequences (body, face) whose channels (`AnimFrameCopy`: degree-5 polynomial per channel, absolute
  frame) decode to translation + quaternion per bone. `AnimObjectUpdate` ticks scripts
  (`speed` frames per 60 Hz tick, so sequences play at 60 fps by default), decodes, and
  `AnimFrameResolve` blends scripts (fade weights, sync scripts, per-bone blends), moves the object
  by the root delta and sets `root_height`. `psiBuildMatrixPalette` builds bone world matrices
  (world quat = parent ⊗ local, Hamilton product in (x, y, z, w)) and the skinning palette (bone
  rotation, inverse-pose translation through the bone); `AnimObjectDraw` calls the PC
  `psiDrawSkinObjectMatrix`. Bond's body uses anim sets (`ANIMSET_tag`; in single player all speeds
  play 06000a00, a gun stance) and the crouch transition scripts. Faces and head tracking: see "Faces"; torso aiming:
  see "Torso aiming" (scene scripts: see below).
  The free camera shows the player's body (port-only; the game hides it from its own camera).
- Faces (`engine/anim.h` "rostos", docs/formats.md "Morphs do rosto", docs/engine.md "Rostos", docs/port.md "Rostos"):
  every morph skin (63 on the disc, 63 head meshes with block 0x2B, all with 22 targets: 0-9 mouth visemes, 10 blink,
  11/12 eyes up/down, 13/14 eyes -x/+x, 15 brows, 16-20 expressions, 21 jaw) allocates slot 1 (face) in each
  `ANIMSCRIPT_tag`; face sequences are type-4 items with flags bit 3 and 22 channels (one weight each, the
  single-channel mode of `AnimFrameCopy`; 142 on the disc). Face scripts by tag: 0xe0000000 the still expression of
  the alert level (`Male/Female_Facial_State_Anims[0..3]`, speed 0), 0xe0000001 the blink (`[4]`), 0xe0000002 speech
  (`AnimFacialAppend`) and, during a scene (`ScriptCam`), every new script of a morph skin (`AnimScriptInit`).
  `AnimFacialUpdate` (state machine in `ANIMOBJ_tag::face_state` +0x109, timer `face_timer` +0x102; reads
  `Drone_AlertStatus`, `Drone_tag` +0x51c) picks them; `AnimFacialBlend` builds `morph_weights` (expression, then
  blink and speech where |w| > 0.01, then the eyes: targets 14/13 from `eye_x`, 11/12 from `eye_y`); in a scene only
  the speech script's frame. `AnimObjectHeadTrack` (bone 5 in `psiBuildMatrixPalette`, flags 0x10/0x20) turns the
  head toward `head_target` (yaw up to 1.1 rad, pitch -1..0.6, `Ang_RotateToBearing` with `head_speed`) and puts what
  the head still lags in `eye_x/eye_y` (0..255, 127 centred). `AnimObjectDraw` keeps the 8 largest weights
  (`Morph_Count/Index/Weight`). Quirks kept: in single player the closeness bytes of cameras 1-2 stay 0, so every face
  counts as close (decodes, blinks, draws morphs) at any distance; eyes at 0/0 (no head tracking) put targets 12 and 13
  at 1 (eyes down and to the character's right: to confirm on PCSX2/hardware). Guards (null script from a full list,
  null tracking target) count in `g_anim_face_guards`. The capture report has a line `rosto desenhados N com_pesos M |
  dist pos giro tipo pele estado timer olhos cabeca scripts e0/e1/e2 pesos k:w ...` (4 nearest faces); `--frames-dump`
  adds `rosto_dist rosto_pele rosto_estado rosto_pesos` (`NF_FACE_SKIN=<skin hex>` picks the face); `nf_loader_check`
  prints `rostos=meshes/skins` per level and `rostos: peles= morphs= alvos= vertices_com_morph= no_cabecalho= falhas=
  guardas=` at the end. Tests: `tests/test_face.cpp`, the morph cases of `tests/test_skin.cpp`.
- Input (`engine/input.h`): 40 actions per player in `PlayerSetting[4]` (`PlayerInput_tag`, 0x158
  bytes); `Input_Update` clamps, compensates sticks and computes the state bits (1 down, 4 pressed
  this tick, 8 auto-repeat) with a per-action lock (bit 0x10 = released; the first update after
  `Input_Init` only sets it). Two sources feed player 0, merged at the end of the ported
  `psiInput_MapInputs` (`platform/psi_input.cpp`, header comments of `psi_input.h`, `psi_pad.h`,
  `input_bindings.h`, `gamepad.h`):
  - the pad, through the virtual PS2 pad: `nf::PadSource` (`platform/gamepad.h`: XInput 1.4, or a
    `ScriptedPadSource` in tests and captures) -> `tSlot[8]` (`PsiPadSlot`, the fields of the libpad
    `TSLOTDATA`; slot 0 = first connected XInput pad, slot 4 = second) with SCE_PAD bits (A = cross,
    B = circle, X = square, Y = triangle, Back = select; `[controls] pad_shoulders = triggers` (default)
    makes RT/LT = R1/L1 and RB/LB = R2/L2, `bumpers` the reverse) and stick bytes (0-255, centre 127, y
    down) -> the ported `psiInput_PollDevices` (dead zone 40/127 on each axis, `pad_deadzone`) ->
    `psiInput_MapInputs` (the 8 styles of `PlayerSetting::style`, `psi_pad_map_player`, button values are
    the raw bit `float(b & mask)`, clamped later; the common menu/Start actions; the rumble timers), so
    the game's own `StickCompensation*`/`MapAnalogStick` curves apply afterwards;
  - keyboard and mouse through the bindings table (`nf::Bindings`, `kBindCommands`: 38 commands (`menu_tab_prev/next`
    and `menu_defaults` only for the Modern menus; the last, `weapon_wheel`, read held by main.cpp), each
    with up to 2 key/mouse/wheel inputs and 1 pad input, targeting one or two (action, sign) pairs, or a port-only
    action (`nf::PortAction`, `BindTarget::action` = 40 + n: `reload`, `gadget_1`..`gadget_5`, `weapon_wheel`; the PS2 has no such
    action; read by the engine through `psiInput_PortPressed`, a pressed edge per tick for player 0). A pad input bound
    in the table (also to a port action) is masked out of the virtual PS2 pad (`psiPadMask`) and acts through the
    table instead.
  PC-standard defaults (user, 2026-10-10; `test_input` checks them against a written spec, `pc_mapping`; docs/port.md
  "Controles" has the table): WASD, arrows turn/look, Space jump, C/Ctrl crouch, LMB fire, RMB aim, E use/reload (the
  PS2's use button: `Player_WeaponFiring` uses, or reloads when nothing was used), R `reload` (port-only), MMB/X
  `fire_mode` (alternate fire / attachment), N `vision`, wheel down/Z `weapon_next`, wheel up/Q `weapon_prev`, G
  `gadget_next` (`gadget_prev` unbound), 1-5 `gadget_1..5`, Enter/Esc `pause`; menus: arrows/WASD, Space select, Tab
  circle, Esc/Backspace back, E square. Tab (and L3 on the pad, the only button none of the 8 styles uses: `test_input`
  checks) holds the weapon wheel (`weapon_wheel`; see "Modern aiming and the weapon wheel"). No default is a
  main.cpp debug key (V, H, P, T, F2-F4, F9, F12, PgUp/PgDn). Port-only rules:
  - Esc = Start in play, back (triangle) in menus: with a game menu open, a `pause` input also bound to a Menu
    command (`nf::pause_menu_shared`: Esc) acts only through the menu command (Start and triangle together cancel in
    `Menu_InputAction`), and after the menu closes it stays out of `pause` until released (no reopen). The Modern
    menus read `pause` without it (`NavState`). At the top of the Original pause, back (the 99 that
    `Manager_SendMessage` 0x5f sends with an empty stack) closes the pause like Start (`pause_page` in
    `menu_pc_options.cpp`: clears 0x22 and sets the close action for the same tick's `MenuManager_Monitor`;
    `MenuPcStats::back_closes`); backing out of the RESTART/QUIT question only closes it. `window.cpp` no longer
    releases the mouse on Esc: menus release it (`set_menu_mode`); `main.cpp` releases it on Esc in the free camera,
    and when no menu opens 0.5 s after an Esc in play (a scene, death, a load: `Window::release_mouse`). Alt+Tab
    always releases.
  - R reloads through the use branch's reload of `Player_WeaponFiring` without `Player_Activate` (no door opens),
    also while scoped (the reload drops the aim through states 6-8, like the empty-clip auto reload).
  - The wheel: one notch = one pressed tick of `weapon_next`/`weapon_prev`. While aiming with a zoomable weapon
    (`zoom_max > 1`, the case where the d-pad zooms), `Player_Zoom` requests it (`psiInput_SetWheelZoom`): the next
    tick's notches skip the table and `psiInput_TakeWheelZoom` multiplies the zoom target by 1.025^9 per notch (up
    zooms in), clamped to 1..max; the game's easing (15% per tick, the blur) follows, and the class keeps the target.
  - 1-5: `Player_PortSelectGadget(obj, n)` picks the n-th owned gadget in the group-1 cycle order of
    `Player_WeaponChange` (0x4a..0x5e, `selectable == 1`, `have`), with its refusals (traversal substates, gun
    mid-animation, aiming, nothing or 0x5d/0x5e in hand), the class variant and the last_weapon/last_gadget/
    prev_weapon bookkeeping; numbering ignores ammo; an empty slot, the gadget in hand or one without ammo does nothing.
  - Old ini files: an explicit `[bindings]` entry wins; a command left at its defaults loses an input that an explicit
    command of the same group uses (`fire_mode = r` leaves `reload` without R), unless a required command would have
    no key (then it keeps its defaults).
  Merge: actions 0-5 (sticks) come from the last device used (new key/button, mouse motion, wheel vs
  new pad button, trigger or stick movement; `psiInput_ActiveDevice`), the other actions take the larger
  magnitude (buttons OR-ed). `pad_sensitivity` scales the pad's turn/aim/look actions before the curves.
  If the pad in use disappears, player 0 reads `connected = 0` until it returns or the keyboard/mouse
  is used, so the game's `MenuManager_Monitor` shows the "controller disconnected" page 0x4000002f and
  then the pause. The main loop fills `g_psi_keyboard_mouse` (keys, mouse delta, wheel) every video
  frame; `nf::g_pad_enabled` = window focus (unfocused pads read neutral, still connected). Mouse look
  is a PC divergence: `psiInput_TakeMouseLook(player, yaw, pitch, aiming, zoom)` adds radians directly
  to the yaw velocity and pitch in `Player_Update` (between `Player_Aiming` and `Player_ViewClamping`),
  scaled by `mouse_aim_sensitivity / zoom` while aiming and `mouse_invert_y` (the turn it applied is kept in
  `g_player_mouse_look`; at render time the instant mouse look of "Tick interpolation" adds it and the unread motion,
  `psiInput_PeekMouseLook`, to the drawn camera only). Rumble:
  `Input_RumbleStart(player, ticks, strength)` (needs `PlayerSetting::unknown_09` = vibration and
  `GameState.unknown_51`) -> `psiInput_RumbleStart` (small motor on, big = strength x 2.5, a tick
  timer counted down in `psiInput_MapInputs`; a paused game stops all) -> XInput (big -> low-frequency
  motor). `psiInput_BeginCapture` (keys page) zeroes player 0's actions until a new input is pressed
  and released. `Input_ChangeControllerStyle` (pause CONTROLS tab) clears the player's actions;
  `psiInput_Init` copies `[controls] pad_style/pad_invert_y/vibration` into `PlayerSetting[0]`, and
  changes made by the game's menus are written back to the ini (only when `nf::g_controls_persist`; a codename's own
  options are not: the rule of "Codename options", `platform/game_options.h`).
- Weapons (`engine/player_weapon.h`): `weapon_definition_tag` (0x10c bytes, 115 in `weapon_data`;
  fields named by use: class, skin with arms and gun, scripts, clip, ammo type, burst per fire mode,
  `fire_delay`, `fire_frame`, flags at +0x68/+0x6c, screen offsets) and `PLRWEAPON_tag` (0xc bytes,
  114 in `BLData::weapons`: zoom, rounds in the clip, owned, fire mode, variant); `BLData::ammo[33]`
  is the stored ammo per `ammo_data` type. A weapon's clip lives in its class slot when its variant
  shares the class's ammo (`clip_slot`). The gun in hand (`BLData::gun`) is a type-0 object drawn
  only in the player's view (flags 0x1a0a0: forced list, overlay); its skin is the weapon's
  (`AnimObjectNew` in `Player_SetWeaponAnim`, first mesh swapped for the player's sleeve) and its
  `obj_tag::state` is the state machine of `Player_SetWeaponAnimObj` (0 idle, 1/2 holster, 3 swap
  model, 4 draw, 5-7 reload, 8 variant, 9 firing, 10-12 throw, 13/14 lose, 15/16 aim in/out).
  `anim->weapon` is in hand, `next_weapon` requested, `prev_weapon` being holstered. Scripts only
  advance when they play a sequence (`AnimSeqTick` clears the script's start bit): tests use a
  one-sequence script. Releasing the trigger cancels the rest of a burst (`BLData::burst = 0`).
  `Player_Init` writes the level's punch skin into weapon 1. The screenshot report has a line
  `arma W estado S pente N municao M mirando A zoom Z`.
- Modern aiming and the weapon wheel (`engine/player_ads.h`, `engine/weapon_wheel.h`, `ui/modern_wheel.h`,
  `platform/pc_gameplay.h`, header comments; docs/port.md "Mira moderna e roda de armas", docs/design.md "A roda de
  armas"; port-only, user decisions 2026-10-10):
  - Aim style `[game] aim_style = auto|modern|classic` (`--aim-style`; Modern GAMEPLAY > AIMING "Aim style", Original
    CONTROLS last row): auto = modern while `psiInput_ActiveDevice()` is keyboard/mouse, the game's own with the pad.
    Modern keeps the game's aim mode (action 19, the "Manual aim" Toggle/Hold rule, auto-aim off while aiming) and adds:
    walking forward/back while aiming (`ads_move_while_aiming`: the game's aim mode zeroes forward and turn in
    `Player_Move`, the PS2 only strafes; modern keeps forward at 85%, any weapon; 07000012 RMB+W 2 s: 3.5 m, classic
    0); scoped weapons (flags 0x40) and zoom weapons without a scope (`zoom_max` > 1: 0x18/0x19, 0x2a/0x2b, which carry a
    scope model) and the rocket launchers (0x2c-0x31) keep the game's view; every non-scope gun gets the crosshair fixed
    at the centre (`ads_fixed_crosshair`: `Player_Aiming` takes the scope branch, aim input turns the view, `aim_x/y` 0,
    shots from the centre); guns with a measured sight line (`kAdsSights`, `player_ads_table.inc`, by skin hash) and no
    zoom get the ADS pose: `ads_tick` (end of `Player_WeaponRecoil`) moves a linear weight 0..1 by FRAME_RATE_MUL / 10 per
    tick (smoothstep) while aiming and the gun state is 0/9 (reload, switch, variant drop out and come back), blends
    `gun->pos/rot` toward `ads_pose` (rear sight at the hip depth x 0.85 clamped 0.3-0.8 m, times the total zoom, front
    sight on the same centre line, no roll; sway x 0.25, recoil kept), sets the light view zoom (1.25 pistols, 1.2
    shotgun, 1.35 others; `ads_view_zoom`) and hides the HUD crosshair (`HUD_UpdateCrossHair`); `ads_pose_arms` (after
    `AnimObjectUpdate(b->gun)`) lowers bones 0, 1, 19, 37 by 0.3 m (2, 20 by 60%) in the look frame, or the shoulder
    would sit in front of the eye. Mouse sensitivity divides by `ads_effective_zoom` (game zoom x light zoom) in
    `Player_Update` and `Player_PortMouseLookGate`. The ADS weight, pose and zoom go through the tick records, so the
    interpolation and the instant mouse look (list 2 rotated about the eye) need nothing special.
  - The zoom fix (both styles): main.cpp's `zoomed_fov` narrows the drawn fov of camera 0 by `view0->lod_scale`
    (= `BLData::zoom`, which the PS2 multiplies into the projection scale in `PS2WorldViewVU1Kick2`; the port ignored
    it, so scopes did not magnify) times `ads_view_zoom()`; the game camera keeps the full fov (visibility unchanged).
    In windows wider than 4:3, an enabled scope pane (6, 11, 17, 18, 19) gets black side bars (Direct2D fills).
  - Sight table: `nf_loader_check <data> --miras` (`tests/sights_measure.cpp`) poses each gun skin at frame 1 of its idle
    script with an identity gun matrix, takes the rigid gun parts (`ANIMSKIN_tag::discrete`, bones 38+) whose centre is
    in the barrel corridor (the skin extra 0, the muzzle datum), and `ads_find_sights` keeps vertices within 4 cm of the
    muzzle x and from the right hand (bone 21) - 10 cm forward, builds the top profile (max y in 64 z bins), its upper
    convex hull and the longest near-flat edge (|slope| < 0.2, at least min(1/4 length, 20 cm)); failing that, the
    barrel axis 3.5 cm below the centre (quality 2). The table rows are printed for pasting into
    `src/engine/player_ads_table.inc` (numbers only). `ads_debug_extra_entry` adds a row in tests.
  - Weapon wheel: command `weapon_wheel` (Tab, pad L3; `nf::kPortWeaponWheel`, so a rebound pad button leaves the virtual
    PS2 pad). main.cpp runs `WeaponWheel::frame` every video frame after the Modern UI (held = `command_value`; while
    open it takes `g_psi_keyboard_mouse.look_dx/dy` as the selection vector, 260 counts to the edge, and the right stick:
    the largest push since leaving the centre picks the ring, the pointed slot stays when the stick returns), sets
    `psiInput_SetWheelOpen` (player 0 loses turn/look/aim/fire/use/reload/gadgets/fire mode/vision/weapon switch; a mouse
    button held at close stays out until released) and draws it with `ModernUiApp::draw_wheel` after the game's frame
    (`build_weapon_wheel` gives three layers: back and front through `Renderer::draw_modern`, the icons through D3D in
    between; needs Direct2D; not in d3d9ff). Slots: `weapon_wheel_slots` (owned classes 1-0x46, the remembered
    variant, the 07000007 party's group 2 rule; gadgets 0x4a-0x5e), names from `weapon_data` +0x38 (the HUD's), ammo
    clip-stored. Fixed-size slots (user, 2026-10-11): `weapon_wheel_ring_slots` gives 8 outer slots of 45 degrees (12, 16,
    24 with more weapons; kWheelMaxWeapons 24: the all-weapons cheat gives 40 classes) and 6 inner (8, 12, 16); the owned
    ones fill the first from the top in the game's order, the rest are drawn as empty glass and `weapon_wheel_pick`
    returns the slot index (pointing at an empty slot picks nothing). The hovered slot is an accent, not a gold block:
    warmer glass, a gold gradient rising to the rim, a 3 px gold rim with two corner ticks, an ember glow beyond it
    (gradient sectors: `DrawCmd::Sector` with `dir` 1). Icons (`WheelSlot::icon`, `weapon_wheel_icon`): the front-end
    dossier's images by class through `ds_weapons`/`ds_gadgets` (a port table, as HUD and dossier names differ; the
    pistols by `P_DSWEAPONS_Handler`'s four upgrade images, the shaver by `P_DSGADGETS_Handler`'s rule; none for the
    punch, Kowloon Type 80, Delta Repeater, torpedo launcher, Q-Pen, Oddjob's hat, flare gun). They live only in 07000048,
    so `ModernUiApp::load_wheel_icons` (main.cpp at boot, after `install_game_tables`) decodes them from FILES.BIN
    (`decode_pack_images`: the hash of each texture is in the palette header block 0x0F; 34 images, ~5 MB read once),
    `ui::wheel_icon_image` cuts the dossier's green radar disc out (outside the disc, and where red and blue stay under
    ~60% of green) and makes them one tone (lifted luminance), `Renderer::set_ui_image` keeps them across levels, and
    `ImageCmd::port` + `tint` draws them tinted by state (accent_hi hovered). No HD replacement for them (they do not go
    through `Tex`). The prompt sits in a dark pill (the "black bar" the user saw left of it in 07000012 was the scene: a
    wall panel's black plinth). Opens only on a fresh press with `weapon_wheel_can_open` (player 0 alive, active, first person, no
    menu, no `ScriptCam`, not paused, no Modern modal); closes without choosing otherwise. Release picks
    (`weapon_wheel_select`: `Player_WeaponChange`'s refusals and last_weapon/last_gadget/prev_weapon, then
    `next_weapon`; a busy gun keeps the choice pending up to 1 s); the centre picks nothing.
  - Slow motion: `plan_tick` (`render/frame_interp.h`) replaces main's tick block; with the wheel open it consumes the
    VBlanks in real time but runs only `[game] wheel_slowmo` of them as 60 Hz ticks (0.25 default: 15 ticks/s; 1 off;
    0 pause; `--wheel-slowmo`; Modern GAMEPLAY > WEAPON WHEEL), records last `vblanks_since_draw` VBlanks so
    `tick_alpha` stays continuous; VBlanks without a tick call `audio_out_catch_up` (sound in real time, no pitch
    change). The mission timer is real time (`psiGetTimeIn100ths`), as on the PS2: wheel time counts. The `--input`
    clock is ticks, so it runs at 1/4 while the wheel is open.
  - Report lines `ads estilo aparelho moderno fixa peso zoom pele linha arma_pos arma_giro alca x y z massa x y z` (the
    table's sights through the gun matrix into camera 0 space) and `roda aberta mostrada armas aparelhos espacos imagens
    apontado vetor escolhas ultima aberturas | <weapon of each slot, * with icon> / <gadgets> | lento ticks alpha
    icones_carregados`. `--weapon <hex>[,<hex>...]|all` (debug) equips weapons after the load (the first in hand; `all`
    = every selectable class like the cheat, also those whose skin is not loaded). `nf_loader_check` prints `roda de
    armas: 47 classes com imagem, nomes batem 47 de 47` (HUD and dossier names share a word) `, imagens no 07000048 34 de
    34`. Tests: `tests/test_ads_wheel.cpp` (finder on a hand-built pistol, pose maths, blend, style and ini, slot picking,
    the fixed slot geometry and picking with 2 and 9 weapons, the icon mapping by class, the radar-disc cut, a hand-built
    pack for `decode_pack_images`, `plan_tick` 15 ticks/s and continuous alpha, the wheel's draw list),
    `ads_raises_the_gun_to_the_sights_and_back`
    and `weapon_wheel_release_selects_through_the_game_path` in `tests/test_weapon.cpp`.
- Gameplay polish (docs/engine.md "Mira do tronco", "Armas do jogador", "Jogador", "Corpos rígidos"; docs/port.md
  "Polimento da jogabilidade"; header comments of `engine/anim.cpp` `AnimObjectAimAt`, `engine/rigid_body.h`):
  - Torso aiming: `AnimObjectSetAiming` (AI `NDrone2_DoTracking`; the player only in multiplayer) sets anim flag 0x80;
    `psiBuildMatrixPalette` calls `AnimObjectAimAt` right after bone 2 (spine): the barrel (skin extra "datum + 3", or
    + 1, in the unaimed pose from `AnimGetBoneWorldTrans2`, which chains the frame's matrices to the root because later
    bones are not built yet; it points along -x) is turned onto the target by a yaw about "up" (hips -> spine) and a
    pitch about "side", each measured in a plane through the hips (line-sphere intersection, acos, sign by the triple
    product), pitch clamped to +-0.9 (0x4000), yaw only below -0.9 (0x2000; quirk kept: the upper yaw clamp compares the
    pitch), front targets only for non-players; `aim_yaw/aim_pitch` (+0xf4/+0xfc) move by `Ang_RotateToBearing`
    (`aim_speed`, 0.2 for the AI) and the spine's world quat becomes yaw x pitch x anim. Inside `aim_dist_sq` it sets
    0x1000, which makes `DroneWeap_Ready2Fire` wait for the barrel to point at the target (20 degrees): AI firing
    changed slightly (`tiros_ia`). `--no-torso-aim` (debug) turns it off; report line `mira_ia ... erro_cano` (barrel
    to target angle); guards in `g_anim_aim_guards`.
  - Auto-aim: `Check_AutoAim` (player_weapon.cpp, constants `Autoaim_*` from the ELF: 10-degree cones, range 35, lock-on
    x2, difficulty x1.25/x1/x0 for 00 Agent) for each drone and player from `Player_AutoAim` when
    `PlayerSetting::autoaim` is on and not aiming (weapons 0x50/0x51: always, type-0x51 objects): cone x
    `weapon_definition_tag::autoaim` (+0x14, %) x (1 - dist/35); inside both cones and with line of sight the target's
    spine goes to `BLData::aim_target` and its screen position to `aim_cur_x/y` (crosshair, gun and bullet follow). PC
    choice: the same rule for mouse and pad (the game has no mouse and never disables it per device; RMB is the game's
    aim and disables it). With auto-aim on, captures with enemies in view differ from before (the crosshair moves);
    with `[game] auto_aim = off` they are pixel-identical. Report line `mira_auto ligada alvo dist tela`.
  - Lasers and the watch: `Player_LaserPointer` (weapons with flags 8: 6-9, 0xf, 0x17, 0x18; toggled by the gun
    animation's event 4/1, `BLData::gun_flag8`), `Draw_LaserBeam` (the watch laser 0x4e/0x4f, flags2 0x100, from
    `Player_MuzzleFlash`), `Draw_TaserBeam`/`FractalThing` (2-32 zigzag segments), all stretched models on the
    `BLData::fx[]` objects (`PositionBeam`), not loose triangles; `Player_SetWatch` (gun skin 0x500004b: the dial and
    the three hands `Hours/Minutes/Seconds`, now `obj_tag*`, at the mission's start time of day + the mission timer).
    Report line `laser ...`; `nf_loader_check` prints `armas: mira_laser= raio_laser= taser= relogio= mira_auto=`.
  - Water: `Camera_Shear` (in player.cpp; the underwater camera wobble: 2.5% shear/scale rotating with a phase in
    viewer +0x1e0, `GlobalBlur` 0x91 which the PC renderer does not draw yet) and `Env_WaterRipple` (effect.cpp, steps
    on materials 13/14). Swimming, air and drowning were already ported; a player put straight into water with `--at`
    never had a body pose, so `Player_MonitorAir`'s head is zero and he does not drown (faithful): drop him from above
    the water. Report line `agua nivel olhos cabeca submerso ar ondulacao borrao`.
  - Rigid bodies (`engine/rigid_body.h`, `RIGIDBODY` 0x160 bytes, object 0x1a, placement 0xd8; 34 on foot, all kind 1
    pendulums: Mayhew's paper lanterns, hanging cloth): wind, bullets (impulse dir x damage x 0.5 at the hit, axes by
    kind), a restoring impulse to vertical, angular drag, the pendulum capsule (`Collide_CylinderIntersect`), a map light
    carried along (param 3), the hit sound (param 4); kind 0 falls with drag (terminal 9.8 m/s) and settles (the
    original's restitution formula halves the normal velocity without reversing it). Quaternion scalar-first at
    the unaligned +0x64 (copied), orientation reset to identity at creation (quirk). Report line `corpos_rigidos ...`
    (`NF_LIST_RB=1` lists each); `g_rigid_body_stats`.
  - `GT_DeployMiniGun` (turret.cpp): firing the dismounted minigun 0x52 places a kind-0 turret where
    `Collide_PlaceObject` finds room (else the round is refunded).
  - `nf_loader_check`'s campaign line has a `polimento:` part (`mira_auto=desligada:a ligada:b`, `corpos=`/`pendulo=`,
    `relogio=`, `mira_laser=`, `metralhadora=`, `agua=`; docs/engine.md "Depois do porte"). Tests:
    `tests/test_aim.cpp`, `tests/test_rigid_body.cpp`.
- Effects (`engine/effect.h`): `EffectInfo` (23 materials x 0x88, `HEINFO_tag`) is copied from the
  ELF at boot like `weapon_data` (its +0 name pointer is zeroed: `TableCopy::data_field`).
  `Effect_Create(hit, obj, flags)`: 0x20 sparks (`Emitter_Create`) and debris, then 1 bullet,
  2 step, 0x10 laser, 0x80 body. Marks are `ATTACH_tag` (= the game's `Effect_tag`, 0x60 bytes)
  from `EffectList` (200, oldest recycled), hung on the hit room's or object's `attach` list (object
  marks in object space; `Effect_RemoveSObjs` returns them). Smoke is type 0xe (`GAS_tag`), its
  colour in `obj_tag::unknown_103` (r, g, b, a) which `View_SetupRenderModes_Tweak` turns into the
  draw's tweak. Effect models carry their own GS state in the 0x2d packet (A+D writes of
  `ALPHA_1`/`TEST_1`/`ZBUF_1`: flash and smoke additive without Z write, bullet holes subtractive);
  `psiSetUpColourBlend` is only the default. The screenshot report has a line `efeitos marcas N
  fumaca N detritos N rastros N balas N`.
- Pickups (`engine/pickup.h`, header comment): items are type-0x2f objects with a `PICKUPINFO`
  (0x40 bytes: fall velocity, matrix-minus-sphere offset, state 0 falling / 1 resting / 2 waiting to
  respawn, kind, item, count, switch channel, sound, respawn in 10 s units (0 gone, 0xffff stays),
  lifetime in ticks, text label, spin) in `obj->data`. Kinds (named from the game's own texts in
  `USATxt.dat`): 0 weapon (ammo if the class is already owned), 1 ammo, 2 code key (`BLData` +0x70c
  + item, `Player_EquipKey`), 3 armour (to 50 if below), 4 mission item (its own label and the
  channel), 5 invisibility (`BLData::visibility` = 0), 6 "007 Bonus" token (only once the mission's gold
  medal is recorded in player 0's profile: `PlrStats_HasGoldMedal`; the 50 tokens on the disc appear in
  `nf_loader_check`'s last pass, which loads every level with all medals set), 7-11 multiplayer weapon-set slots (`PickupMatrix`/`UseableGuns`, `kTables` rows). There
  is no health item. Placed items (placement type 0xf0, params 0-6) rest; `Pickup_CreateSimple`
  drops a weapon (`DroneWeap_DropWeapon` when a drone dies; the player's own only in multiplayer)
  that falls with `WldGravity` (floats in the zero-g 0700001b) and lives 2 min. `Pickup_Update` (the
  type's only `control_funcs` callback) gives the item to the first live player (state 1, not type
  0x12) within sphere radius + 1.1 m of its sphere centre with line of sight (`Pickup_Handler`); the
  item stays when nothing was taken (ammo full, armour at 50, the weapon's skin not loaded in the
  level). Items are tinted by the room light (flag 0x800, `obj_tag::color`, dark in dim rooms) and
  glists with 0x80000 pulse with `FlashMod` within 4 m of the camera; `Check_Target` (ported in
  `pickup.cpp`) computes `FlashMod` (stuck at 0 it drew the near items black). `MP_*`, `BOT*`
  and text stay stubs; guarded cases count in `g_pickup_guards`. `nf_loader_check` prints
  `itens: criados=N tipos=kind:count pegos=N/M inventário_mudou=N item_pego=centre(kind,item)` (the
  player stands on each item and its `Pickup_Update` runs; his inventory is restored afterwards) and
  `morte_ia: arma=W itens_soltos=N pousados=N queda=m` (an armed enemy is killed and its weapon must
  fall as an item); the screenshot report has a line `itens N perto x y z dist d tipo k item i
  estado s`.
- Destruction (`engine/break.h`, `engine/explode.h`, `platform/psi_shard.h`, header comments):
  breakables are type 0x20 (`BREAK_tag`: kind, strength, ignored bullet classes), destroyables type
  0x21 (`DESTROY_tag`: smashed glist = `lod` or the next entity, strength, blast radius, delay, in/out
  switch channels, explosion kind 1/2/other = scripts 0x6000027/0x600004f/0x6000052), hurt zones type
  0x27 (`HURT_tag`: path, speed, damage, channel, touch mask), explosions type 0x0f (`EXPLODE_tag`).
  Bullet damage arrives through the hit list (`HITDATA_tag::unknown_08`); `Collide_FilterBullets`
  drops bullets whose `weapon_definition_tag::stats` (+0x10) hits the object's mask (breakables always
  add 0x106). Glass (kind 2): at load `psiCreateBreakableData` walks box 0's DMA chain and keeps
  vertices (`SHATTER_VERT`, 0x40 bytes: pos, uv, GS-scale colour floats) and strip triangles
  (`BREAKTRI_tag`: texture = `Tex[i].dma`, three indices **relative to their batch**, an original quirk)
  in a 0x18-byte record hung on the 0x2d header's third word. `Break_Kill` splits each triangle
  (`ShatterPolyRecurse`: longest edge² down to clamp(dist²/2, 0.5, 100) from the hit) into shards
  (`SHARD_tag`, `ShardList`), positioned by `obj->pos` without the object's rotation (like the
  original); they wait dist x 15 ticks, fly along the shot for 25 ticks and fall. Non-glass
  breakables drop `Debris_Create_Loop` debris. Shards draw through `psiDrawShard`: the PC records
  world-space triangles (`PsiFrame::tri_vertices`, `PsiDrawCall::triangles`) that `collect_frame`
  turns into `DrawItem::triangles` (consecutive calls merged) and `Renderer::draw_triangles` draws with
  a dynamic vertex buffer, sprite shading (GS-scale colour, no tweak: the PS2 uses VU1 routine 0, so
  `DrawAllShards`' tweak alpha does nothing) and `psiSetShardRenderStates` (Z test, no Z write, 0x44).
  `psiDeleteShard` frees the chain at once (the original queues the field's address, which would free
  the middle of a freed node). Explosions: `Explode_CollisionHandler` spreads the damage once
  (`Explode_Propagate`, sphere mask 0xc0, damage x (1 - distance to the object's sphere / radius)):
  drones get message 9 with a stack `DroneImpact_tag` (+0x00 damage, +0x04 radius, +0x10 centre, +0x20
  source), the player `Player_Hurt`, breakables `Break_ApplyDamage`, destroyables `Destroy_Smash`.
  The fire, flash, light, smoke and sounds of an explosion are its effect script (0x6000052: 165
  frames, 37 streams), played by the real script player (`Explode_Create` writes the radius in
  `SCRIPTINFO::scale`, aligns `SCRIPTINFO::base` to the direction and lives `SCRIPTINFO::end` frames;
  `Explode_Update` runs `Script_Update` until it stops). `nf_loader_check` prints
  `destruicao: quebraveis= vidros= triangulos= destrutiveis= zonas= granada=radius/damage explosoes=
  atingidos= quebrados= cacos= desenhados= destruidos= vidro_alvo=x,y,z,yaw item_granada=` (a hand
  grenade's `Explode_Create` at the first breakable and 2 m ahead of the player, then 2 s of game);
  the screenshot report has a line `destruicao quebraveis N quebrados N cacos alive/created triangulos
  N explosoes created/alive destrutiveis N quebrados N zonas N`.
- Scene scripts (`engine/script.h`, header comment; format in `docs/formats.md`, "Scripts de cena";
  `tools/scripts.py list|dump|stats`): item type 7 (`06xxxxxx`, 468 on the disc, one per NIS section
  pack) goes through `Script_Load` with data at load time, which registers a `SCRIPTDEF_tag` in the
  hashtable (pointing into the loaded file; -1 for a bad version); `Script_Load` without data makes a
  `SCRIPTINFO` instance (0xac0 bytes, up to 62 `SSTREAM` of 0x28). Every stream is a command list
  (wait, entity, animation, camera, sound, light, sprite, text, sub-script, event, fade; each command
  with a handler ends in a closing byte) and a range of 0x30-byte keys (pos, quat, scale, alpha, frame);
  `Script_Update` runs all streams then adds `FRAME_RATE_MUL` to the times; `Script_Interp` puts each
  stream's object at the interpolated key (linear, or Catmull-Rom with the original's quirk of using
  `Weights[1]` as the fraction), adding the parent streams and the script's own matrix. Entities are
  plain type-0 objects with `obj_tag::script` = the owning `SCRIPTPLAYER_tag` (`Player_Activate` ->
  `SP_Activate`), unless the command's type maps to a creator (`Script_CreateEntity`: breakable, switch,
  triggers, AI volume, pickup, emitter, destroyable, `Ladder_Create`). Events (cmd 0x12)
  pause (`SCRIPTINFO::wait`, -1 until `SP_UnPause`), disable/enable the player and the AI, set
  channels, create drones, call the owner's callback (fuse boxes register their fuses this way) and
  change level. A camera stream pushes every camera's state, disables cameras 0-6 and drives camera 4
  (`Script_UpdateCamera`: matrix and `fov = key scale x aspect`); `Script_ActiveCamera()` tells
  `main.cpp` to draw camera 4 instead of 0 (`camera_script` in the screenshot report). Skipping a scene: while a
  script player plays (state 2), action 0x19 held (`Input_Action(-1, 0x19, 1)`: Enter or Space, the bindings `pause`
  and `menu_select`; Start or A on the pad) calls `Script_FFwd` unless it is a death NIS (`Script_IsDeathNIS`); a
  script without the skippable bit (`SCRIPTINFO::state` 0x10) or already skipping ignores it; otherwise camera 4 fades
  to black over min(45, frames left) ticks while `Script_Update` runs the script to its end, and camera 0 fades back
  in over 22 ticks (07000001's intro: `--input "none:12 enter:0.1"`). The script player
  (type 0x1b, placement 0xd9 or `SP_Create`) is a state machine over two scripts (0 wait trigger, 1
  loaded, 2 playing, 3 played, 4 playing B, 5 done/delete, 6 looping, 7 load next tick) whose triggers
  are its channel, bullets on its entities (`SP_Hit`, `Collide_FilterBullets`), use, and the player or a
  drone touching them. A script of a loadable section (`isLoadable`) pushes GameFlow state 0xd and then
  `LoadableLoad`s the NIS pack over the level's animations; `Script_Free` reloads them
  (`LoadableReload`). Switches (type 0x39, no model: the script entity is the lever) flip their channel
  when used from the front while their looping script is paused, and follow the channel when it
  changes elsewhere; fuse boxes (0x3c) turn their channels on when every registered fuse of a list is
  shot. `nf_loader_check` prints `cenas_fase=registered/items sp= switches= fusiveis= dicas= com_som=` (hints, with
  `DrawInfo[3]` on as after `bootup_bootup`) and, after the 2
  s of game, `cenas_jogo: instancias cameras secoes entidades sp_vivos`; the screenshot report has lines
  `scripts ...`, `playscript ...` (drones in state 3) and one `switch ...` per switch. Levels whose intro
  NIS starts in those 2 s (07000001/02/04/0d/13/1b) disable the player and the AI on purpose, so their
  `tiros_ia`/`morte_ia` show no shots.
- AI characters (`engine/drone.h`, see `docs/engine.md`): objects of type 2 (0x4d with an owner)
  with a `Drone_tag` (0xd20 bytes) in `obj->data`, linked in `NPCGlobals.drones` (node at +0, obj at
  +0xc). `Drone_Create` keeps the placement parameters in `Drone_tag::divars` (+0xc00);
  `Drone_PostLoad_Init` runs `NDrone2_DefaultInit` for each. The static tables (`DroneTypeSettings`,
  `DroneModeSettings`, `Drone_AnimInfo/Tables`, `DroneAnimStates` with its `DASC_*` lists,
  `NDrone2_StateFuncs`, `bitDescs/bitMasks`, captain modifiers) come from the ELF; add a ported
  function to `Drone_ResolveCode` (`engine/drone_tables.cpp`) when a table points to it, and port a
  state by adding its `NDrone2_DSTATE_*` there. Unported states return 0 and are counted in
  `g_drone_state_pending` (`nf_loader_check`: `estados da IA sem porte`). Callees not ported yet are
  in `engine/drone_stubs.*`; guarded out-of-place accesses count in `g_drone_guards`. Messages:
  `MsgObject` (msg, required state, sender, dest or 0 = all, sent, due, data); states get 1 on
  enter, 2 on leave, 3 every tick, unhandled messages go to state 0. `Drone_InitComms` (once per tick,
  from `control_movement_object_handler`) delivers delayed messages. `nf_loader_check` sets
  `GameState.difficulty = 2` (the boot value; it decides which characters exist) and prints
  `inimigos=N animados=N scripts=N`; the screenshot report has a line `inimigos N perto x y z giro r
  dist d estado s danim a pele p`. Navigation (`engine/ainetwork.h`, header comment and
  `docs/engine.md`): `AINetwork` (paths and bounds from block 0x05), `CoverNodes`/`AIPoints`
  (pointed to by `NPCGlobals` in `Drone_PreLoad_Init`), `AIRoute_tag` (0x100 bytes; the drone keeps
  two, 0x110 apart); guarded cases count in `g_ainetwork_guards`; `nf_loader_check` prints `ia:`
  (paths, nodes, covers, AI points, a test A* route `rota_teste=ok` and an emitter).
  Vision (`engine/drone_vision.cpp`, header comment): `Drone_InitComms` builds the aware points,
  updates each side's opponent (`Drone_ProcessOpponents`) and runs `DroneVision_ProcessDroneSight`
  (round-robin line of sight, one `Drone_View_Bones` bone per tick, reaction time, then sight bit 4
  and message 0xf); broadcast alerts live in `NPCGlobals.alert` (`DroneAlert_tag`) and reach states as
  messages handled by `DroneVision_EnemyAlerts`. `Drone_SfxInfo`/`Drone_View_Bones` come from the ELF;
  `switch_BLIND_DRONES` is the game's debug switch; guarded null paths count in
  `g_drone_vision_guards` (`nf_loader_check`: `guardas da visão da IA`).
  Body (`engine/drone_move.cpp`, `drone_damage.cpp`): `NDrone2_Move`/`NDrone2_Steer` only turn toward
  `Drone_tag::move_angles.y` (the move target set by `NDrone2_FollowRoute`; root motion moves the
  drone), avoiding other drones and the player. `NDrone2_Collision` builds the 0.4 m capsule and
  `Drone_CollisionHandler` (type 2) does the floor ray (`Drone_FeetOnPoint`, which accepts another
  drone as floor, like the original), push, hits, gravity (`Drone_tag::fall`) and the 1/8-per-tick
  settle to `root_y`. Rays and spheres hit the joint boxes from `AnimGetCollData`
  (`HumanCollisionBones`/`AstronautCollisionBones` from the ELF; the joint id is the hit location). A
  bullet hit becomes message 8, which a state must pass to `DroneFunc_HandleImpact`; damage goes
  through `NDrone2_HitDamage` with `DroneDamage_*`/`DroneArmour_*` copied from the ELF. The hurt
  broadcast (message 0x12) reuses `NPCGlobals.alert` with no object and the hurt drone as sender.
  `nf_loader_check` prints `ia_corpo: caíram=N variação=X` (height change of the drones over the 2 s)
  and `em_inimigos=N` (pistol bullets that hit a drone).
  Weapons (`engine/drone_weapon.cpp`, header comment): a state asks for fire with `DroneWeap_Fire`
  (`Drone_tag::firing`; `one_burst` for a single burst, also set by `Drone_SM_RouteMsg` on the death
  transitions) and `NDrone2_ControlSTANDARD` calls `DroneWeap_HandleFiring` every tick: it keeps
  `target_pos` = opponent + `aim_offset` and fires bursts of `weapon_data` +0x2c (the third fire
  mode) through `DroneWeap_DoFiring` -> `Ready2Fire` (opponent alive, aiming animation: `anim_kind`
  4, or 2 with no blend script) -> `FireWeapon` (accuracy, muzzle light and flash debris, the hand
  weapon's projectiles with the player's `Bullet_init`, casing, clip). Timing in `burst`,
  `next_shot`, `last_shot` (`DroneWeap_NextBulletTime`, `DroneWeap_BurstDelay`); a shot only hits
  after 2 s of sight or within 3 m (`DroneWeap_DoBulletAccuracy`, else `aim_error`). The
  `DroneFiring_*` constants are one `kTables` row (`DroneFiring`, up to
  `DroneCaptain_Mod_BulletDamage`). `Drone_tag::look`/`look_variant` are in fact the grenade and
  its count. Drone bullets hit the player (his hit list), but `MP_RegisterBulletHit` (stub: false)
  and `Player_HandlePain` (stub) keep his health; friendly fire is filtered by side in
  `NDrone2_DealWithObjHit`, like the original. `nf_loader_check` makes every armed enemy fire one
  burst at the player after the 2 s (`fire_from_drones`: enabled, `WaitForever`, vision on,
  turning with `DroneWeap_AimTarget_IsOpponent`, 2.5 s of aim, then `lost_ticks` forced to 0) and
  prints `tiros_ia: atiradores mirando visada vendo balas rajadas acertos_no_jogador`; it restores
  the global state it touches (Rand seeds, `GameState`, switch channels, stub/state counts) and
  lists its own stub calls in `stubs chamados pelos tiros da IA`. `ResetMap_Load` clears the switch
  channels on cold loads (`Init_SwitchChannels`), so alarms no longer carry over between levels.
  Spawners (`engine/drone_spawner.cpp`, header comment; `drone.h` section "Geradores"): placement 0xf1
  makes a type-0x30 object with a `DroneSpawner_tag` (0x3c bytes: kind 2 refill / 3 waves, group,
  `max_alive`, `waves`, start/stop/done/kill channels, minimum distance from the player) registered in
  `NPCGlobals.spawners[group]`. After the load, `NDrone2_SetupGroups` calls `DroneSpawner_Init` for each
  group with a spawner: the group's drones become templates (`DroneSpawnEntry_tag`, 0xd0 bytes: their
  `DIVars_tag`) and are deleted from the map, and `group_counts[group]` becomes 0xffff (`groups[group]`
  keeps pointing at the deleted first template, like the original). So `inimigos=` at load no longer
  counts them: `inimigos` + `modelos` (in `geradores:`) is the old count. `DroneSpawner_Control` (type
  0x30's update; the dummy player of `Drone_PostLoad_Init` is also a zeroed type 0x30 and does nothing)
  waits for the start channel, frees the slots of dead drones (health <= 0 or flags 0x600) and creates
  at most one drone per tick from a random template not used in this wave, farther than the distance
  from the player and not visible (`Drone_PositionVisible`; the stored template room is null, so in
  practice only the distance counts), through `NDrone2_CreateObj` + `NDrone2_PostLoad_Init` (the
  load path: `NDrone2_DefaultInit`, state machine; spawned drones join no group). A wave spawns
  `max_alive` drones; kind 2 refills the dead while waves remain, kind 3 waits for the whole wave to
  die; after the last wave the spawner deletes itself (with a done channel, only once all are dead,
  then it turns the channel on). The kill channel removes the spawned drones (in 07000011-13/16/4a only
  those not drawn recently), the stop channel ends the spawner. `Drone_Delete` releases a deleted
  drone's slot (`DroneSpawner_DroneDelete`). Port-only counts in `g_drone_spawner_stats` (created,
  templates, spawned, waves, released, guards). `nf_loader_check` prints `geradores: criados modelos
  nascidos vivos` after the 2 s and, at the end of each level with spawners, `geradores_forçados:
  canais nascidos vivos levas mortos apagados repostos vagas_soltas restantes` (`forced_spawners`:
  every waiting start channel turned on, god mode, 10 s; then every spawned drone is killed through
  the death state and 15 s more run; it restores the global state like `fire_from_drones` and lists
  its stub calls in `stubs chamados pelos geradores forçados`); the screenshot report has a line
  `geradores N modelos M nascidos S vivos A levas W` (spawners on the map and spawned drones in their
  slots now; templates, spawned drones and closed waves since boot).
- HUD and mission (`engine/hud.h`, `engine/mission.h`, `docs/engine.md` "HUD e missão"): `BLData::hud`
  is a `HUDINFO_tag` (0x2c8 bytes, heap): the crosshair sprite and 22 panes (`HUDPANE_tag`, 0x20
  bytes: create description, extra words, sprite array, update function, x/y/w/h, sprite count,
  `state` at +0x1a (`HUD_State`), `enabled` at +0x1c, `valid` at +0x1f; the byte `Player_HandleDeath`
  reads at +0x1a4 is pane 0xc's `enabled`). `HUD_Init` walks `PaneList` (`MPPaneList` in multiplayer):
  0 ammo, 1 health/armour, 2-4 and 21 message panes, 5 air, 6 sight, 7 night vision, 8 lens flare,
  9 Redeemer, 10 RC car, 11 spy camera, 12 blood curtain, 13/14 MP score and radar, 15 X-ray, 16
  security camera, 17 OICW, 18 Ronin, 19 laser, 20 space station; each pane's create function makes
  its sprites from `SpriteInfo`s and its update runs every tick (`HUD_Update`, from `Player_Update`).
  The static tables (`PaneList`, the pane descriptions and sprite infos, `CrossHair`,
  `HUDCrossCoords`, `BulletImg.189`, `TexUV.291`, `MissionData` with its objectives, the menu's
  `sp_level`) come from the ELF in `game/hud_tables.cpp` (`init_hud_tables`, called by
  `install_game_tables` and `nf_loader_check`): it runs the HUD's static constructor (it builds four
  message panes in .bss), copies the panes reachable from the two lists into `HudPanePool`, their
  sprite infos into `HudSprInfoPool` and the font format strings into `HudFormatPool`, and resolves
  create/update pointers through `Hud_ResolveCode` (an unported one is an error). The panes' sprites
  are the 2D layer's (`sprite`/`SpriteInfo`, `Sprite_Create2`; the HUD's "mode" byte is
  `sprite::priority`, -1 hides) and `Game_Draw` draws them like any other sprite. `HUD_Describe` turns the panes into the
  screenshot report line `hud paineis 0,1,... vida H (lit/7) colete A (lit/8) municao C-S sangue F
  (arrows) mira X cortina E/S`. Missions: `Mission_Init` (from `ResetMap_GameInit`) finds the level in
  `MissionData` (base map = the mission's first level, order) and resets this and later levels'
  objectives; `Mission_Update` (in `Game_Run`) runs `Mission_MonitorObjectives` (reveal, complete,
  "don't let it happen" objectives with flag 2; channel 99 = failed, 100 = all done) and the end
  flow: channel 0x62 (death) -> status 3, 1 s; channel 99 -> status 2, 5 s; channel 100 -> status
  6, 4 s, then `ResetMap_LevelToLoad(LevelToEndTo)` (the front end or the mission's end movie). Levels
  without a `MissionData` entry get status 5, where `PlrStat_OkToUpdate` refuses health changes: in
  single player the player dies at spawn there (07000041/43/46/49, as on the PS2; multiplayer levels
  loaded through `ResetMap_LevelToLoad` set the MP flag and are fine). On failure `Mission_Update`
  opens the real end-mission menu (`MenuManager_Create(0x80000004, 0x40000042, ...)` and
  `GS_PauseGame(1)`; see "Menus"); `g_mission_port.restarts` counts the requests to reload the same
  level (`ResetMap_LevelToLoad`, port-only). After a mission success the front end pack (07000048)
  shows the results page (0x40000036, `P_NFRESULTS_Handler`), whose "continue" goes on to the game's mission map
  (0x4000001c, `P_NFMAP_Handler`; see "Front end"), where `Menu_SetLevelBonus` has unlocked the next mission. A
  restart keeps the `--input`/`--wait` clock of the first load. The
  report has a line `missao objetivos D/T fracassados F visiveis V missao S passo I base B ordem O
  falha L recomecos N`; `nf_loader_check` prints `hud: paineis=N ligados=N
  missao: objetivos=N estado=S` at load and `hud_fim=[...]`, `missao_fim=[...] recomecos=N` after the
  game tests. Test fixtures that create a player must set a running mission
  (`Mission_SetStatus(kMissionRunning)`), as `Mission_Init` does in a mission level.
- 2D layer (`engine/txt.h`, `font.h`, `text.h`, `sprite.h`, `platform/psi_2d.h`, header comments; docs/engine.md
  and docs/port.md, "Camada 2D"; formats in docs/formats.md): game text is `<lang>Txt.dat` from FILES.BIN
  (`Txt_LoadLanguage` every level; labels are `table << 24 | index` through the file's FixupTable; the
  language is `LanguageToLoad` = 6, USA, which `main` sets with `Txt_SetLanguage` in place of the front
  end). The three fonts are ELF data (`kTables` rows with two pointer fields and the "*" any-table ref);
  their images are textures `0x03000005/03/d8` of item `010001d8`, in every pack; `psi_2d_find_font_textures`
  (end of `CreateSpaceAndPackTextures`, from `psiPreGameRun`) fills `FontList`. Sprites (`sprite`, 0x44
  bytes, 512 in `SpriteList`) are images (type-03 textures, `hashtable_set_sprite`) or texts in the PS2
  screen (512 x 448); `Game_Draw` runs `Sprite_BuildList` + `View_DrawSprites` per camera, glyphs come
  from `__Font_DrawText`, both fill `SprBuffList` and `psiDrawSprites` records it. PC record: one
  `PsiDrawCall::sprites2d` per call, with `Psi2DQuad`s (PS2 rectangle, ST, GS colour, Tex slot, ALPHA_1,
  point/linear filter) that each keep a `Psi2DOrigin` (image/glyph/symbol, main/shadow/outline pass, the
  game `sprite*`, texture hash, font, character, text): `collect_frame` maps them to the window with
  `psi_2d_viewport` (a 4:3 area centred, scaled by height; scissored to it; `psi_2d_map_rect` stretches
  elements as wide as the PS2 screen, such as the cinema borders, the blood curtain or the hit arrows, to
  the window edges) and takes 2D from every camera (the HUD and subtitles are camera 5, the cinema borders
  camera 4). This is the hook for a future HD UI. Messages: `Text_AddMsg` queues word-wrapped lines (types 1 info, 2 objective, 3 mission, 4/5
  subtitles, 6 item); subtitles show through `Text_Update/Text_Update2Line` only with `DrawInfo[2]` (the
  subtitles option; `main` turns it on unless `--no-subtitles`; the US PS2 default is off); the HUD's
  message panes show the other types (`HUD_UpdateStatusPane` over `Text_UpdateMsg`). Speech subtitles come
  from the real sound path: `Sound_ReqestPlaySfx` calls `Sound_DoSubtitle` (`Snd2Lbl`, `SFXOutputData` from the
  ELF) when it sends a speech sound to the IOP (see "Audio"). Test fixtures that create a player must also set
  up the 2D lists like `ResetMap_GameInit` (`sprite_debug_power_on` + `Sprite_InitLists`,
  `text_debug_power_on` + `Text_Init`): otherwise `SpriteList`/`MsgQueue` still point into an earlier
  test's heap. `nf_loader_check`
  prints `2d: idioma= textos= fontes= mensagens=type:n linhas= sprites= quadros2d= glifos=`.
- HD 2D layer (`render/ui_hd.h`, header comments; docs/port.md, "Camada 2D em alta definição"): optional
  (`--hd-ui`, F4; the original 2D stays the default and the reference). With it on, `collect_frame` runs
  `ui_hd_build` over the whole frame's 2D record (`PsiFrame::quads2d/lists2d`) and emits one `UiHdBatch`
  per `psiDrawSprites` call (`DrawItem::ui`) instead of `add_2d`; layout and timing stay the game's. Text:
  consecutive glyph quads of one `__Font_DrawText` call (`Psi2DOrigin::run`, a per-call counter; the origin
  also carries `align`, `scale` and `baseline` = the pen's Pos.y) become the original string again (chars
  and spaces from the text via `char_index`, which counts like `__Font_DrawText`: `~X` uses two bytes;
  one line per baseline; per-glyph GS-scale colour spans; 0x92/0x85/0x99 back to ’ … ™); the preceding
  shadow/outline passes become effects (four (±1, ±1) copies = a 1-PS2-pixel stroke via
  `GetGlyphRunOutline`, other copies = offset copies). DirectWrite draws each line in the font
  `hd_ui/map.json` maps the game font to, sized so the vector font's cap height equals the game font's 'H'
  height x text scale x screen scale, placed as a block at the original glyphs with the format's alignment,
  compressed horizontally down to `fit_min` if wider. `~X` symbols stay images; text around them is pinned
  left. Images: still Quad2D through `draw_2d` (same tint, alpha, 0x100/0x200 blend, mirroring, tiling);
  a `map.json` entry (texture hash + texel rect, contained match, `extent` inclusive/exclusive per axis;
  or HUD pane/sprite identity, which wins) swaps in an SVG (Direct2D `ID2D1SvgDocument`, rasterized at the
  on-screen size rounded up in <=12.5% steps, cached) or PNG (WIC, mips); replacement texels are stored
  un-premultiplied with GS-range alpha like game textures. Unmapped images use the original texture with
  bilinear + mips (`Quad2D::smooth`). Anchoring (`"anchor": "edges"`, `--hd-ui-anchor`): each frame the quads of
  one HUD pane that touch (8 PS2 px gap) form a group (`ui_hd_anchor_groups`; pane rects are useless:
  `HUDPANE_tag::w/h` are 0 on the disc and several panes place sprites in their update) and the group's
  third moves it to that window edge (`ui_hd_anchor_shift`), clipped to the window; `anchor_skip`
  (default pane 8 lens flare and 0:7 target marker) stays put. The device is created with `D3D11_CREATE_DEVICE_BGRA_SUPPORT`; after each Direct2D text
  run `Renderer::bind_frame_state` restores the D3D state. Without Direct2D the layer stays off.
  `--ui-inventory <dir>` appends every distinct image element (hash, texel rect, size, pane:sprite, frame
  count) to `<dir>/inventory.tsv` and writes crops and whole textures as PNG (keep it under `out/`: game
  data). `NF_UI_HD_DUMP=<dir>` writes every SVG rasterization as PNG. Menu pieces (docs/port.md,
  "Inventário dos menus") have their own SVGs from `tools/ui_hd_assets.py` (the panel 0x03000070, the pause
  silhouette 0x030000db, the 0x03000042 pieces, the results 007 0x03000140 and corner 0x0300014c; only the
  "007 nightfire" logo 0x0300016d is still the texture); label images copy their model sprite, so
  `sprite_texture_hash` also finds the hash by `sprite::texture` (the texture record).
  Frames (port-only, `psi_2d_find_frame`/`psi_2d_nine_slice` in `platform/psi_2d.h`): a quad showing the
  whole texture of a frame (the panel 0x03000070, insets 64/72/64/80 texels) whose window-pixel texel
  aspect is within 10% of the frame's reference (1 = square) is drawn as one quad, so the game's pages are
  pixel-identical; otherwise it becomes 9 pieces: corners at the less stretched axis' scale on both axes,
  edges stretched along their length, the centre both ways (the PC video page). Both `add_2d` (original)
  and `ui_hd_build` (HD: the 9 pieces share one rasterization at the corners' scale) apply it.
  Button prompts (HD only; docs/port.md "Símbolos de botão por aparelho"): each `~X` symbol is drawn from the
  set `[ui] prompts = auto|ps2|keyboard|xbox` (`--prompts`; `UiHd_SetPromptSetting`) resolves to: ps2 =
  `map.json` `prompts.ps2[code]`, xbox = `prompts.xbox[ui_hd_xbox_name(code, psiPadTriggersR1)]` (the PC pad
  button that makes that PS2 button, `psi_pad_convert`), keyboard = a key cap op (`UiHdOp::Key`, Direct2D)
  labelled with `ui_hd_prompt_keys`: menu commands of `nf::controls().bindings` in menu texts, the game
  command of the current style (`psi_pad_style_inputs`) in the pause CONTROLS tab's button map (cells of
  list 0x10000028 tag 10) and for shoulders/sticks/select; a wider cap pushes the rest of the line and the
  block keeps its alignment; no key = the PS2 vector symbol. Auto follows `UiHd_SetPromptDevice`, which
  `main.cpp` feeds every frame from `psiInput_ActiveDevice()`. The capture report's `ui_hd` line has
  `simbolos <setting>/<in force>`. Tests: `tests/test_ui_hd.cpp` (9-slice maths, prompt mapping, map.json).
- Menus (`engine/menu.h`, header comments; docs/engine.md "Menus"; file format in docs/formats.md "Menus
  (item tipo 8)"): the menu file is the pack's type-8 item (`08000001` in every level: menu 0x80000002
  with the pause page 0x4000004b, the controller-disconnected warning 0x4000002f and five more, menu
  0x80000004 = end mission 0x40000042, and menu 0x80000003 with one page; `08000002` in the front end
  07000048: 52 pages of menu 0x80000002), kept by `MenuManager_Load` (`file_start`; port-only
  `g_menu_file` = its hash). `MenuManager_Create(menu, page, player, index, close_action, pause_sounds,
  status)` reads the file's records into manager `index` (`menu_manager[5]`, `M_MANAGER` 0x1d8 bytes;
  `status` 2 controller-disconnected warning, 3 pause, 4 end mission, 5 front end): every page of that
  menu with its controls (platform byte 0 or 3 only), scripts, keyframes, messages and component sets,
  then shows `page`. Everything is an `M_CONTROL` (0x94-byte header, type at +0x7b: 1 button, 2
  checkbox, 5 label, 6 list, 7 manager, 8 page, 9 radio, 10 scroll bar, 12 text box, 13 window, 200
  memo, 0x72 sub-control). Messages go through `__Menu_SendMessage` (by type, unhandled ones to the
  owner); UI events (0x4b select, 0x4c page enter, 0x4d leave, 0x49 changed, 0x4e/0x4f focus, 0x50 page
  tick, 0x5d/0x5e other buttons, 0x5f back) reach `Handler_HandleMessage`, which calls the handler
  registered for the control's id (`kHandlers` in `menu_handlers.cpp`, 103 ids from the ELF's table;
  unported ones are a null entry that counts `pending_call(name)` and returns 0 = not handled).
  `Game_Run` runs `MenuManager_Update` (input through `Menu_InputAction`, scripts, the page tick) and
  `MenuManager_Monitor` (closes a menu on its close action; every 10 ticks outside the front end checks
  the pads and swaps the menus for the warning page); `Player_Update` opens the pause on Start. Menus
  draw through the 2D layer on camera 7 (`Menu_CreateSprite`; file coordinates are 640x480,
  `FixupResolution` maps them to the PS2 screen), so the HD layer sees them like the HUD. Pad buttons on
  the keyboard: Enter = Start (actions 25 and 30; opens/closes the pause, and selects in menus with
  status > 3; Esc also: see "Input"), Space = X (25 and 31, select), Tab = circle (33), Esc or Backspace = triangle
  (34, back; at the top of the pause, port-only, it closes the pause),
  arrows/WASD = d-pad (26-29), E = square (32, `menu_square`: the dossier on the mission map and the results, "Create"
  on the codename wheels; the same key as the Modern map's DOSSIER tab, `menu_tab_next`; nothing else in the menu group
  uses E and the square action is read only by the menu controls, so in play E stays "use"). Port-only pages and items: build them on the same
  structures (`Page_Init` + `Manager_SendMessage(0x43)`, controls from a `NEW_CONTROL` through
  `Manager_SendMessage(0x42)`, components of a loaded set) and register their handler with
  `Menu_PortRegisterHandler(id, name, fn)`; registered ids take precedence over the game's table (a port
  handler can also replace a stub), and new ids should follow the disc's (pages after 0x40000053,
  controls after 0x10000244). `Menu_Describe` is the screenshot report line `menus status S criados N
  fechados N paginas N controles N | gerente i pagina X foco id/tag`; `nf_loader_check` prints `menus:
  arquivo= aberto_no_jogo= pausa=pages/controls pausou= foco= fechou= despausou= fim=pages/controls
  linhas= fechou=` per level (`menu_principal pagina= paginas=` in 07000048: after its 2 s of game the front end
  stands on Press START, 40000009), run last with the Rand
  seeds, `GameState` and stub counts restored, and `guardas_menus:` when a guard fired. Test fixtures
  that open menus must call `menu_debug_power_on`/`menu_handlers_debug_power_on` and `MenuManager_Init`
  after the 2D lists, and clear `glb_players` if no player exists (`P_ENDMISSION_Handler` calls
  `Player_RamLoad`).
- Front end (`engine/menu_frontend.h`, header comment; docs/engine.md "Menu principal"; docs/port.md "Menu
  principal"): `nightfire.exe` without `--level` keeps the boot's front-end request (07000048) and the game runs its own
  single-player path, every page drawn Original with `--menu-style original` and as the Modern front end otherwise (`src/ui/modern_front.h`,
  see "Modern menu style"; the same handlers decide): 0x40000034 language (`P_LANGUAGE_Handler`: no choice in the US version,
  `Txt_SetLanguage(LanguageToLoad)`) > 0x40000048 memory card at boot (`P_PS2MEMCARDINIT_Handler`,
  `psiDoBootupCheck`) > 0x40000032 intro movies (`P_INTRO_Handler`) > 0x40000009 Press START (`P_START_Handler`; the
  legal text and 240 ticks without input the first time; 45 s idle shows 0x40000035, `P_ATTRACT_Handler`) > 0x4000004a
  (`P_PARISENUM_Handler`: looks for a profile on the card) > 0x40000043 (`P_ESTHERO_Handler`, a movie) > 0x40000002
  main menu (`P_MAIN_Handler`; `C_GONIGHTFIRE` 0x40000025, `C_GOCODENAMES` 0x4000001b, `C_GOMULTIPLAYER` a port-only
  "not available yet" box, `PcText::FeMultiplayer`). Campaign: 0x40000025 codename wheel (`P_NFSELECT`, `C_SBNFCN`: 0
  "New Codename" > the keyboard, 1 "Play without saving" > the warning box 9 > OK, 2+ a card codename, read with
  `LS_Load`) > 0x40000023 difficulty (`P_NFDFCTY`, `C_SBNFDFCTY`: `GameState.difficulty` from the `difficulty`
  table) > 0x4000001c mission map (`P_NFMAP`, `C_SBNFMAP`: `sp_level` wheel; a mission sets `GameState.load_file`,
  `ResetMap_LevelToLoad(level, 0, 0)` (with the intro movie code), the fade, and closes the menu). Codenames:
  0x4000001b (`P_CNSELECT`, `C_SBCNSELECT`: edit, Y create, X delete with the card's question), 0x40000020 on-screen
  keyboard (`P_CNNAME`, `C_KEYBOARD`: 0-9, A-Z, space, Del, End; up to 8 characters; the first key clears the
  suggested "BOND"; End checks `Menu_IsValidCodename` (trimmed, uppercase, not containing "NEW CODENAME"/"DEFAULT"),
  `PlrStats_ResetScoring` and saves a new profile, then 0x4000001d), 0x4000001d codename menu (`P_CNMENU`,
  `C_SBCNOPTIONS`: Secret Unlocks (the keyboard in code mode, `Menu_SpecialCodenameCheck`), the option pages,
  "Save Codename" (saves, then the page in the wheel's tag: 0x40000025 or 0x4000001b); see "Codename options"). After
  creating a codename the game shows "Controller Setup" (0x40000022). Wheels (`Menu_UpdateWheel`,
  `Menu_UpdateCodenameWheel`: a tiny scroll bar with the focus plus five labels, tags 0-4, the middle one current;
  items are `M_ITEM`, 0x18 bytes: image, name, text, value, enabled, locked text) and the iris
  (`Menu_StartIris/PlayIris/ChangePageCloseIris`: images 030000bf-c3 every other tick, the page change 10 ticks
  later). The profile in use is `ls` (`MENU_LS`: active operation, op 0 load/1 save/2 list/3 delete/4 list first, done,
  stale list, slot 0/1 the defaults `def_codename` (copied from the ELF), 2+ card, 999 none/new, name, next page);
  `Menu_UpdateMessageBox` runs one tick of the operation and `PS2_DoCardFlow` shows the card box 0x4000002a (opened
  only while the card shows a message; it stays at least 60 ticks outside the name, codename-menu and results pages;
  its end sends 0x6a to the page below, which is how a loaded codename goes on). Quirk kept: a save without
  `ls.can_save` (set by the main menu) or with an empty name leaves `recurse_160` set and the card stops answering.
  Movies (see "Movies"): the movie pages (`P_INTRO`, `P_ATTRACT`, `P_ESTHERO`, `P_TRAILER`, `P_FMVPLAYER`,
  `P_WINGAME`) play their movie and wait for `psiMovieFinished`; the pages' background loops play behind them. Driving
  missions (09xxxxxx, DRIVING.ELF): `C_SBNFMAP` > "LOADING..." > `ResetMap_LevelToLoad` > `GF_LAUNCH_DRIVING`; after
  `psiLaunchDriving` (a stub) `GameFlow_Main` calls `Menu_PortDrivingSkipped` (port-only): the mission counts as done
  without a medal (`Menu_SetLevelBonus(level, 5, 0)`, the next one unlocked) and the front end reloads on the map
  with a notice box (`PcText::FeDriving`). The handler table entries are filled in `kHandlers`; the rest of the
  front end (multiplayer, `P_FMVTEST`) stays null. The dossier and the cheats: see "Dossier and cheats". `main.cpp` skips video frames during
  `GF_LOAD_MAP` (the previous level's structures are gone; in 07000048 `glb_viewer` is only cleared at load step 3).
  Tests: `tests/test_frontend.cpp`.
- Dossier and cheats (`engine/menu_dossier.h`, `engine/menu_cheats.h`, header comments; docs/engine.md "O dossiê" and
  "Trapaças e a página de depuração"; docs/port.md "Dossiê" and "Trapaças"): square (message 0x5d) on the mission map
  (`C_SBNFMAP_Handler`, through the iris), the results and the bonus page opens the dossier 0x4000002b
  (`P_DOSSIER_Handler`, wheel `C_SBDOSSIER_Handler` over `ds_options`): records 0x4000003a and rewards 0x4000003b at once
  (`P_DSRECORDS/DSREWARDS_Handler`: a radio over the 12 `sp_level` missions on GameState +0x48; `C_RBDSRECORDS_Handler`
  shows `PlrStats_GetLevelTotals` best and row 0 (Bond moments), the kind-4 rewards owned and the best medal with its
  image; `C_RBDSREWARDS_Handler` the four `Menu_GetLevelBonuses` rewards; a medal or reward only counts with every lower
  medal), gadgets 0x4000003c and weapons 0x4000003d after the iris (`C_SBDSGTSCROLL/DSWPSCROLL_Handler` wheels over
  `ds_gadgets` (14) and `ds_weapons` (27), copied from the ELF with `ds_options` in `kTables`, plus the upgrade line;
  `P_DSWEAPONS_Handler` swaps the pistol item by `Menu_GetObjectUpgradeLevel(0)`, `P_DSGADGETS_Handler` the Shaver image
  by `VIDEO_FRAME_RATE`). The pages show 2D images (150x150 labels, like the mission map): no 3D model, no special
  camera. In the Modern style the dossier is a view of the Modern front end (`src/ui/modern_dossier.h`, see "Modern menu
  style"): the map's DOSSIER tab and the results' DOSSIER row and tab press square (`kMmSquare`) as before, and back
  returns where the game returns (the map or the Modern results). The `C_CHCH*` checkboxes live on page 0x40000007 of the in-game menu file
  (`08000001`), created with every pause and never shown by the retail game (no code or script builds that id;
  `ComboKeysList` stays empty); each handler shows its global on 0x51 and writes it on 0x4b (the table is in
  docs/engine.md): `CheatInfo` +0 god mode, +4/+8 all weapons, `hud_state` (all 22 panes), `switch_SHOW_COORDS`,
  `DrawInfo[1]`/`switch_CONFIG_WIDESCREEN` (on PC the window sets the aspect), `switch_NO_DRONES` (inverted),
  `switch_BLIND_DRONES`, `Drone_DummyPlayer`, `switch_AllowFreeze`, `SoundInfo[0]` (music volume; `SoundInfo` is set by
  `bootup_bootup` now), `switch_ForceDrawAll`, fly (`debug_scanmode`, substate 5: `Player_SSScanMode`), zero-g (`Set.214`,
  substate 9: `Player_ZeroG`), `menu_unlock_everything`; `bDebugControls`, `bDebugLockup`, `bTweakBrightness` and
  `Drone_DebugSpr` are read by nothing. Port-only: the checkbox names (`PcText` Dbg*, the disc has "1"), the six rows of
  the empty 0x4000004c list (`P_CHEATMEDAL_Handler` wrapper; `medal_from_cheat` is read by nothing), `MenuCheats_Install`
  (main.cpp after `MenuPc_Install`; tests after `menu_handlers_debug_power_on`), `--debug-menu` (F9 in a level:
  `Menu_PortShowCheats`, pause + the page pushed over it) and `--unlock-all` (debug: `Menu_PortUnlockAll`, every
  mission won with platinum through `Menu_SetLevelBonus`, copied into both `def_codename` slots; a card profile keeps
  its own; the upgrades also apply with `--level`: 07000005 starts with weapon 8, the upgraded pistol, instead of 2); guards in `g_menu_cheat_guards` (a player cheat without a player). The screenshot report has `dossie ...`
  and `trapacas ...` lines. Retail unlocks are the medal bits (rewards, upgrades), the 007 tokens (only with the gold
  medal) and the "Secret Unlocks" codes (`Menu_SpecialCodenameCheck`, see "Codename options"). The page's other
  controls (the level list, "Previews", the tweaks) are in "Codename options". Tests: `tests/test_dossier.cpp`.
- Codename options (`engine/menu_options.cpp`, `menu_secret.cpp`, `menu_tweaks.h`, `platform/game_options.h`,
  `platform/psi_sound.h`, header comments; docs/engine.md "As opções do codinome"; docs/port.md "Opções do codinome"):
  the codename menu's wheel (`C_SBCNOPTIONS`) opens the game's own pages, drawn Original and run by their handlers:
  "Controller Setup" 0x40000022 (`P_CNCONTROLS`, radio `C_RBCONTROL`: the 8 style names, the drawn pad's labels and
  lines per style through `Menu_DisplayControllerStyle` (labels `base+1..+13` of each style, lines 0x10000061 by tag bit),
  circle toggles "Inverted"/"Normal", X writes `PlayerSetting::style` (`Menu_ChangeControllerStyle`) and `invert_look`),
  "Advanced Options" 0x4000002d (`P_CNOPTIONS`: radios for `PlayerSetting[0]` +9 vibration, +1 auto aim, +8 crosshairs,
  +4 crouch and +3 manual aim (Toggle 1 / Hold 0), +0xa weapon auto switch, +0xc flashing objects (On 2 / Off 0), +0xb
  HUD always on (On 0 / Off 1)), "Multiplayer Options" 0x4000002e (`P_CNMPOPTIONS`: the codename's MP radar
  `MPSettings` +0x28, handicap +0x2c, `autoaim_mp`; profile only), "AV Options" 0x40000031 (`P_CNAVOPTIONS`: music and
  effects volume bars 0-20 = volume / 5, live while on the page, subtitles `DrawInfo[2]`, split screen `DrawInfo[0]`,
  speaker `SFXSetMode` 0/1/4, widescreen `DrawInfo[1]`, "Screen Adjust" 0x40000047 (`P_SCREENADJUST`, bars
  `C_SBSCREEN`: `psiAdjustScreenPos`, x by 12 up to +-0x1b0, y by 4 up to +-0x30), "Restore Defaults", "Credits",
  the trailer). On these pages X on any control confirms the whole page and back discards (the AV volumes return to
  the old values); quirk kept: the widescreen radio is not cleared, so each visit from the codename menu adds another
  On/Off pair. Widescreen, split screen and the screen position are stored only (the window sets the
  aspect, Hor+; there is no TV overscan): they reach `DrawInfo`, `psiSetScreenPos` and the profile, nothing in the
  renderer reads them (decided with the user); the speaker mode reaches the sound driver (mono: no phase-inverted
  rear voice). Volumes: `SFXMusicSetVolume/GetVolume`, `SFXSetVolume/GetVolume`,
  `SFXSetMode/GetMode` keep the driver's own values (`MusicMasterVolume` 70, `SfxMasterVolume` 100, `SFXModeNumber` 1:
  the AV page, the GSET block and the ini use these; `SoundInfo[0]` is the music cheat's separate copy) and send
  the driver's commands (heard at once: see "Audio"). Credits 0x40000030 (`P_CREDITS`; from AV Options, or after `P_WINGAME` back to the main
  menu): a new line every 14 ticks at y = 0x200 over 26 reused label pairs, all rising 2 px per tick, the music fading in
  the last 50 lines; `Menu_SetupCredits` builds the 578 lines from `CreditsSource`, which `game/menu_tables.cpp` takes at
  boot by running the ELF's own `Menu_SetupCredits` (22 KB of code) in the MIPS interpreter (labels kept as labels,
  `.rodata` strings copied). "Secret Unlocks": `Menu_SpecialCodenameCheck` (52 codes: mission masks and `bonus` bits,
  the XOR ones undo when typed again, the weapon upgrades through `Menu_UpgradeCheat`); `nf_loader_check` runs the ELF's
  function in the interpreter for every code and three input states and compares (`codinomes especiais: 162 de 162`).
  The debug page 0x40000007 (only with `--debug-menu`/`--open-menu-page 40000007`; see "Dossier and cheats") also reaches
  `P_TWEAKS` 0x40000044 ("Total Score:": `DroneDamage_*`, `DroneArmour_*`, `Plr_DMod_*` on bars, X saves, square goes to
  `P_TWEAKS2` 0x40000046 with `DroneFiring`), "Previews" 0x4000000c (`P_NIS`, list `C_NIS`: 70 NIS entries; a script
  loaded in the level plays over the paused game) and the level list (`C_LBPMMAP`: only "Main Menu" on the disc); both
  lists come from the ELF at boot (`menu_tables`, their 0x51 code run in the interpreter). Game options and
  `nightfire.ini` (`platform/game_options.h`): each value lives in its game location (`PlayerSetting[0]`, `DrawInfo`, the
  SFX values) with accessors `nf::game_option/set_game_option` (menu units: on = 1); the ini remembers the ones with a PC
  meaning: `[controls] vibration/pad_style/pad_invert_y`, `[ui] subtitles`, `[game] auto_aim, crosshairs, crouch,
  manual_aim (toggle|hold), weapon_auto_switch, flashing_objects, hud_always_on, music_volume, effects_volume`.
  Precedence: (1) at boot the keys the ini has override the PS2 defaults (`psiInput_Init` -> `game_options_apply_ini`);
  (2) a card codename read (`LS_LoadPlrSettings/LS_LoadGlobalSettings`) brings its own options, as the game does, and the
  ini does not change (`game_options_rebase`); (3) the default profiles ("New Codename", "Play without saving", "Default":
  `Menu_MapDefaultCodename`, and the subtitles `Menu_WillRunMiniMission` turns off in the US version) take the ini's keys
  over `def_codename` (the ini plays the role of the default profile the PS2 loses at power-off); (4) a change made in
  any menu (a difference since the last rebase, seen every tick by `game_options_sync` at the start of
  `psiInput_MapInputs`, and by `nf::persist_controls` before it writes: `game_options_note_changes`) becomes the ini's
  value and is written at once; only the changed option is written. The file only gets the keys the ini knows (read
  from it, or changed in a menu), the three `[controls]` ones included (`save_game_options` writes them, not
  `save_controls`): saving another setting (mouse speed, a key) no longer writes the style, inversion and vibration in
  force. Before, `pad_style = 0` went out with any controls save and the default profiles then started in style 0
  (NightFire) instead of `def_codename`'s 7 (Classic Bond); an ini that already has the key keeps using it. All of this
  only with `nf::g_controls_persist`: without an ini to write (captures without `--config`, tests) the port behaves as the
  PS2. The screenshot report has `opcoes_jogo ...` (values; `*` = the ini knows it) and `olhar` (`BLData::pitch`).
  Tests: `tests/test_options_pages.cpp`.
- Memory card and saved profile (`platform/psi_card.h`, `engine/loadsave.h`, header comments; docs/port.md "Cartão de
  memória"): the game's `HandlePS2CardState` + `MemCard_*` state machine is ported with the libmc calls (`sceMcOpen`,
  `sceMcWrite`, `sceMcGetDir`...) on an `nf::PcCard` (`FolderCard`: a folder, `saves\` next to the exe or
  `--save-dir`; `MemoryCard`: tests, `nf_loader_check`, captures without `--save-dir`); each operation completes at the
  next poll (`PS2PollMemoryCard`, every front-end menu tick). A profile is a folder `BASLUS-20579<NAME>` with the icon
  (`007.ICN`, the front-end pack's type-15 item), `icon.sys` and the data file `BASLUS-20579<NAME>`: the `LS_*`
  blocks (`SaveIFFBlocks` from the ELF: PLRS player options, MSSN unlocked missions + each mission's record and Bond
  moments + game finished, MPSG, GSET volumes/language/subtitles/split screen/screen position, CHET, BNUS the 64 medal
  and bonus bits) and their `crc32buf` (1026 bytes); the game checks the folder after saving (5 entries, 78 KB) and
  the crc when loading ("data corrupt"). Overwriting asks (the game's question), deleting asks. `saves/` is in
  `.gitignore`.
- PC text in place of PS2-hardware text (`engine/pc_text.h` "Textos do jogo trocados no PC", docs/port.md "Textos do
  PC no lugar dos do PS2"; port-only): `kLabelOverrides` (`engine/pc_text.cpp`, 25 rows, en + pt by `[ui]
  language_pc`) maps the `USATxt.dat` labels that talk about the memory card, the DUALSHOCK®2 and the multitap (the card
  messages 0x267-0x27c and 0x3e6/0x3e7, the controller 0x3c0/0x3c9, the multitap 0x10000ba/0x10001df) to PC text
  ("Saving data. Please do not close the game.", "Controller 1 is disconnected."). `Txt_BindLabel` consults it
  (`PcText_GameLabel`) only with `SelLang == TXTLANG_USA`, so it reaches the Original boxes and the Modern dialogs
  (which read the box's memo); the game file is untouched. Guards: the game string must contain the row's `match`
  (another file version passes through) and the PC text may only use a prefix of the game string's conversions
  (`PcText_FormatPrefix`: the game uses it as an `snprintf` format with the original's arguments, slot "1" and 82 KB;
  `%.0s` swallows the slot). `nf_loader_check` checks every row against the real file (`textos do PC: 25 de 25`).
  The port-only Modern titles follow: the card box title is "Save data" (`FeCardTitle`), a saved codename's tag
  "Saved" (`FeTagCard`), the listing "Reading save data…" (`FeListing`). The boot check page 0x40000048 keeps the
  game's flow: with the folder always ready the check ends at once and its box never stays on screen.
- PC settings (`platform/settings.h`, port-only): `nightfire.ini` next to the executable (or `--config
  <path>`) is an INI with sections (`[video]`, `[ui]`, `[controls]`...); `nf::settings()` reads it at
  start and rewrites it keeping comments, order and unknown keys (keys added by `set()` stay in the order
  they were added). `apply_settings` in `main.cpp` turns it into the starting `Options` (`[video]` and
  `[ui]` through `nf::load_video_settings`: width, height, window_mode (the old `fullscreen = 1` reads as
  borderless), vsync, render_scale, msaa, texture_filter, fov, hd_textures, fps_cap (off|n), interpolation,
  instant_mouse_look, adapter (see "Graphics adapter"), ui hd/anchor/language_pc; plus
  ui subtitles and controls mouse_sensitivity) and command-line flags override it (`--window-mode`,
  `--render-scale`, `--msaa`, `--filter`, `--lang-pc`, `--adapter` besides the old ones); with `--screenshot` (and
  `--bench`) the file
  is only read, and only written, when `--config` is given, so captures neither depend on nor touch the
  player's options. The file is in `.gitignore`. `[controls]` and `[bindings]` belong to the controls page:
  `nf::load_controls` (called from `apply_settings`) reads `[bindings]` (one key per command of
  `kBindCommands`, e.g. `fire = mouse1, rctrl`; names from `nf::input_name`: letters, digits, f1-f24,
  num0-9, space, enter, tab, backspace, ctrl/lctrl/rctrl, shift..., up/down/left/right, mouse1-5,
  wheel_up/down, pad_a/b/x/y/lb/rb/lt/rt/back/start/ls/rs/up/down/left/right, pad_lx+ ... pad_ry-; empty =
  none; a missing key keeps the default) and `[controls]` mouse_aim_sensitivity, mouse_invert_y,
  pad_sensitivity, pad_deadzone (0-0.9 of the stick travel, 0.315 = the PS2's 40/127), pad_invert_y,
  vibration, pad_style (0-7 or the menu name), pad_shoulders (triggers|bumpers); `nf::save_controls` writes
  the PC ones (plus mouse_sensitivity, `nf::kDefaultMouseSensitivity` 0.0022 by default) when a menu changes one, but
  only keys the file already has or whose value is not the default (the same per command of `[bindings]`): a default
  is never written just because another key changed. `[game]`, `[ui] subtitles` and `[controls]` vibration/pad_style/
  pad_invert_y are the game's own options (see "Codename options": `nf::load_game_options` in `apply_settings`,
  written by `nf::persist_controls` through `save_game_options` only when the ini knows them).
- PC options in the menus (`engine/menu_pc_options.h`, `platform/video_settings.h`, header comments;
  docs/port.md, "Opções do PC no menu"; port-only): the pause list (0x10000028 tag 6) gets a 4th row
  "OPTIONS" (data 3) through wrappers of `P_PAUSE_Handler`/`C_GCPAUSE_Handler` registered by
  `MenuPc_Install` (they chain to the previous handler, the game's or another port wrapper; the list grows
  to fit; re-entering the pause from the PC page skips the game's 0x4c, which would duplicate the style
  rows). With `[ui] menu_style = modern` (the default) the row first calls the hook of `MenuPc_SetOptionsHook`,
  which opens the Modern options instead (see "Modern menu style"); otherwise (Original) it opens one tabbed page 0x40000060, built on first use per menu (`MenuPc_ShowPage`: 0x43, controls
  via 0x42 with `MenuPc_AddControl`, then restore the current page and show with 0x44 so it is pushed and
  back returns) from the game's pieces: the pause panel 0x03000070 at the file's 45,107 550x266 (no
  stretch), a tab bar of set-2 buttons in font 2 over the panel's band (82..558, like the pause's 84..555;
  colours 0x645a49ff focused / 0x201707ff others, the open tab keeps the light colour), the help bar
  (0x060100ff, `~X` symbols, so the HD per-device prompts work) and, per tab, rows like the front end's
  option pages ("AV Options", "Advanced Options" of `08000002`): the name in a disabled font-3 label with
  the control's id and tag (it lights with the control's focus) and the control on the right. Row kinds
  (`MenuPc_AddRow`): Slider = a proportional `MCT_SCROLL` in component set `kPcComponentSet` (0x50), which
  `MenuPc_EnsureComponents` creates per menu with the pieces of `08000002` set 10 (the AV volume bar: arrows
  48,0/48,16 on focus, fill 54,50 stretched; the pause file has no such set); Choice = an `MCT_RADIO` of set
  3 (centred text, arrows on focus; toggles are Off/On radios like "Subtitles"); Button = a set-2 text
  button in the name column ("Apply changes"); Info = two disabled labels; List = an `MCT_LIST` of set 3
  (frame and arrow bar) filling the area; Status = a centred label under it. `kPcRowsVisible` (7) rows at
  25 px; more rows scroll (focus or wheel) and a disabled set-5 bar shows where. Tabs (`PcTab`, registered
  with `MenuPc_RegisterTab`: build, enter, tick, leave, help text): VIDEO (resolution, display mode, VSync,
  FOV slider 40-100 step 5, frame rate limit `kPcRowFpsCap`, frame interpolation, instant mouse look, apply: eight
  rows, so the list scrolls one), GRAPHICS (render scale, MSAA, filter, HD textures, DLSS/DLAA dimmed
  "coming soon", apply), INTERFACE (HD UI, HUD anchor, subtitles `kPcRowSubtitles` (the game's `DrawInfo[2]` through
  `nf::set_game_option`, applied at once, not pending), menu style `kPcRowMenuStyle`, language, apply), AUDIO (the AV volume bars dimmed and
  "coming soon"; shown only if the six names fit the band: with font 2 they do not), CONTROLS and KEYS
  (`menu_pc_controls`). Navigation is the page's (navigation 0; `options_tick` on 0x50): on the tab bar
  left/right switch tabs at once (like the pause) and down enters the rows; up/down walk the focusable rows
  (from the first, up goes back to the tab; the KEYS list goes up only from its first row); the focus moves
  like `Menu_FindControl`, by putting the manager's cursor on the control so `Page_Update` sends 0x4f/0x4e.
  Select (or a click) on a tab opens it; the page reopens on the last tab. VIDEO/GRAPHICS/INTERFACE edit a
  copy of `nf::VideoSettings` (radios and the slider send 0x49; select on a radio advances it); "Apply
  changes" (dimmed and skipped with nothing pending) hands it to `nf::video_controller()`; leaving the page
  discards it. Resolution or display-mode changes open the game's option box (`Menu_CreateOptionBox`, page
  0x4000002f) over the hidden rows with a 15 s countdown (`kVideoConfirmSeconds`, menu ticks; the page gets
  0x50 as `m->previous` during the transition); no answer, NO, back or closing the menu (`MenuPc_Frame`)
  reverts the whole apply; everything else is stored at once. Two menu-system quirks the page works around:
  `Scroll_Update` calls `Scroll_Init` every tick, which resets a scroll bar's state to 0 (a disabled bar
  would take focus and clicks: the page sets 2 again each tick); `Label_Update` starts a 15-tick fade
  (process 0xe1) to the off colour of that moment on the first unfocused update, and processes run even for
  hidden controls (colours go before the text, and `set_off_colour` deletes the running fade). main.cpp
  sets the controller's apply hook (window, `Renderer::set_scene_quality/set_texture_filter/set_vsync/
  set_exclusive_fullscreen`, `UiHdApp::set_enabled/set_anchor_edges`, the live FOV) and store hook
  (`nf::store_video_settings` + save), and keeps the live settings in sync with T, F4, Alt+Enter and
  manual window resizes. Renderer: at 100% without MSAA nothing changes (single pass, identical images);
  otherwise 3D items go to the scene target (depth there; the fog pass runs per sample with
  `Texture2DMS` + `SV_SampleIndex` under MSAA), then `ResolveSubresource` and `ps_down` (up to 4x4 bilinear
  taps over the pixel footprint) into the back buffer, then the 2D items at window size; failures fall back
  to no MSAA, then 100%. Alpha-to-coverage (port-only, docs/port.md "Opções do PC no menu"): with MSAA, a draw that
  `gs_alpha_coverage_candidate` accepts (GEQUAL/GREATER alpha test with blending off, or the (Cs - Cd) * As + Cd blend
  with a test that only passes full alpha: TEST 0x507fd, the bamboo and plants, 330 parts; the common 0x5001b GEQUAL 1
  blended parts stay as they are) uses `ps_a2c` and a blend state with `AlphaToCoverageEnable`: coverage =
  saturate((a - threshold) / fwidth(a) + b), b = 1 when the threshold is the top of the alpha (every pixel that passes
  at 1x stays whole, the one-pixel ramp goes outward; centred b = 0.5 speckled thin leaves), else 0.5. Without MSAA (and
  with `--no-alpha-coverage`) captures are pixel-identical; the `video` report line and `--bench` say `cobertura on/parts`;
  iGPU cost at 1080p MSAA 4x within noise (07000001's intro 307.8/305.6 fps). Labels: `PcText` (pt/en by `[ui] language_pc`); text is converted to Windows-1252
  and characters without a glyph in font 2 fold to the bare letter (the fonts lack Ã Õ Ç Ô ã õ...), unless
  the HD layer is on (`PcText_SetHdText`, it redraws the string). Ids: page 0x40000060, controls
  0x10000300-0x100003ff (tabs 0x10000310+, rows from 0x10000320; 0x10000400-0x100004ff are the controls and
  keys tabs'). `--open-menu-page 40000060 --menu-tab <video|graphics|interface|audio|controls|keys>` (debug)
  opens the pause and the page on that tab half a second after the load. Tests: `tests/test_pc_options.cpp`.
  The screenshot report has lines `video ...` (what the renderer uses) and `pc_opcoes ...` (page, tab, tabs
  shown, focus, row, scroll, box, pending and live settings).
- Graphics adapter (`platform/gpu_adapter.h` GPU-free, `render/gpu_adapters.h` DXGI, header comments; docs/port.md
  "Adaptador de vídeo e benchmark"; port-only): `[video] adapter = auto|integrated|discrete|<index>|<name substring>`
  and `--adapter` (`parse_adapter_preference`/`adapter_preference_name`, case-insensitive; numbers up to 3 digits are
  indices, "5090" is a name). `auto` keeps the old path exactly (`D3D11CreateDevice(nullptr, HARDWARE)`: the system
  default, which follows Windows' per-app "Graphics preference"). Otherwise `enumerate_adapters` lists the adapters with
  `IDXGIFactory6::EnumAdapterByGpuPreference` (UNSPECIFIED = the index, the `EnumAdapters1` order; MINIMUM_POWER and
  HIGH_PERFORMANCE give `power_rank`/`perf_rank`), falling back to `EnumAdapters1`; the software adapter is never
  chosen. `integrated` = unified memory from a throwaway D3D11 device per adapter (`D3D11_FEATURE_D3D11_OPTIONS2`,
  only when the preference needs it and in `--list-adapters`), else dedicated VRAM < 1 GiB. `choose_adapter`: integrated
  = the first integrated by power rank, discrete = the first non-integrated by perf rank, an index, or the first name
  match; nothing = -1 (the default, and the report says `(nao achado: o padrao)`). The `Renderer` creates the device on
  that `IDXGIAdapter1` (`D3D_DRIVER_TYPE_UNKNOWN`); Direct2D/DirectWrite (Modern menus, HD layer) come from the same
  device, so they follow. `Renderer::adapter()` (name, index, integrated by UMA) is in the report's `video` line
  (`adaptador N "name" integrated|discrete pref P`). Rendering on a GPU without the monitor (this machine: the Radeon
  iGPU, monitor on the RTX) works through DXGI's cross-adapter copy at `Present`; `output_of` finds the monitor's output
  on any adapter (display modes), exclusive fullscreen is not possible there. DXGI reports dedicated VRAM capped at ~3 GB
  to a 32-bit process. The same GPU can be listed several times (the RTX 5090 here is indices 0, 2 and 3; only 0 has
  the monitor): the menus show each name once (`adapter_choice_values`), the name picks the first. Menus: "Graphics
  adapter" in VIDEO (Modern `Opt::Adapter`, a dropdown Automatic/Integrated/Discrete/names whose description names the
  adapter in use; Original `kPcRowAdapter` "Adapter (restart)", short names `adapter_short_name`), a pending option
  stored by Apply; the apply hook ignores it (the device is created once), the Modern screen toasts "Restart the game to
  use the new graphics adapter". `VideoController::set_adapters` (main: `enumerate_adapters(false)` and the name in use).
  The D3D9 prototype takes the D3D11 choice when Direct3D 9 lists an adapter with the same VendorId/DeviceId (it only
  lists adapters with a monitor), else `D3DADAPTER_DEFAULT`; its `d3d9ff` report line says which.
- Benchmark (`--bench <s>`, `platform/bench_stats.h`, main.cpp; docs/port.md "Adaptador de vídeo e benchmark";
  port-only): like a capture (only `--input` drives the player, the ini is read only with `--config`, the card in
  memory, no audio device: the IOP and SPU still mix) but with the real clock (alpha from the clock, not 1), no VSync
  and no frame cap (`--bench-vsync` keeps both from the options); it starts `--wait` seconds after the load (1 s by
  default; `--shot-level` works too) and measures `s` seconds of wall time, then prints to stdout (`console_write`:
  the redirected handle, or `AttachConsole`) and appends to `--bench-out <txt>`: the settings and adapter, frames, avg
  fps, frame time avg/p50/p99 ("1% low")/p99.9 ("0.1% low") by nearest rank, the mean of the slowest 1%/0.1%, max;
  GPU ms per frame from D3D11 timestamp queries (a `TIMESTAMP_DISJOINT` per frame, a 5-frame ring read without
  waiting) split by `GpuPhase` (scene, fog, resolve, 2D, blur, Direct2D: `Renderer::gpu_phase` stamps where the draw
  kind changes); CPU ms per tick (logic, `Game_Draw` + collect + interp push) and per frame (interpolation, submit,
  present); items and draw calls per frame (`RenderCounters`), tearing and occluded presents. `--offscreen` draws
  into the offscreen texture without `Present` (the GPU's own cost) and `present()` then waits for the frame two back
  (an EVENT query, like a swap chain's queue); in a window it is topmost (an occluded window's `Present` shows
  nothing). `--bench-frames <tsv>` writes each frame. `tools/bench_matrix.py` runs scenes x adapters x resolutions x
  window/offscreen and writes a markdown table plus the raw output. GPU timestamps include idle gaps when the CPU is
  the bottleneck (the RTX's numbers in a window), so read them as GPU cost only when GPU-bound. Without VSync,
  `Renderer::present` uses `DXGI_PRESENT_ALLOW_TEARING` when `DXGI_FEATURE_PRESENT_ALLOW_TEARING` is supported (the
  swap chain is created with the flag; not in exclusive fullscreen): before, a windowed flip-model chain was still
  held to the monitor's refresh with VSync off. Tests: `tests/test_bench.cpp` (preference parsing and ini round trip,
  the choice rule, the menu values, percentiles on known frame times), `modern_options_graphics_adapter_row`.
- PC controls in the menus (`engine/menu_pc_controls.h`, header comments; docs/port.md, "Controles";
  port-only): `MenuPcControls_Register` (main.cpp, after `MenuPc_Install`; tests after
  `menu_pc_debug_power_on`) registers the CONTROLS and KEYS tabs (in-game text is English by default; `pt`
  in `PcText`). CONTROLS: the device (info), mouse and aim sensitivity, pad sensitivity and dead zone
  (volume-bar sliders, the value in the row name; the mouse sensitivity slider is logarithmic in both pages,
  `nf::mouse_sens_from_pos/to_pos`: 61 positions, 8 per doubling (9% a step), the default 0.0022 rad/count at 44,
  0.0000486..0.0088 = 410..2.3 cm per 360 degrees at 800 DPI, so 400-3200 DPI mice fit 5-100 cm; shown x 1000, the
  Modern description's meter gives the cm per turn at 800 and 1600 DPI, `nf::mouse_cm_per_360`), invert mouse Y,
  invert pad Y (= `PlayerSetting::
  invert_look`), vibration (= `PlayerSetting::unknown_09`), triggers = R1/L1 (Off/On radios) and the aim style (Automatic/Modern/Classic, `[game] aim_style`, port-only): ten
  rows, so the last three scroll in. KEYS: one list (set 3, the scroll bar's `wrap` off) of the 38 commands with three
  columns (name, keyboard/mouse, pad: the table's pad input, or in parentheses what the current style does,
  `psi_pad_style_inputs`) and a RESTORE DEFAULTS row; selecting or clicking a row starts
  `psiInput_BeginCapture` and the tab tick applies `nf::bind` (conflicts: same group moves the input, a
  required command swaps or refuses) or `clear_binds` (Delete) and shows the result in the status row.
  Every change is written at once (`nf::persist_controls`, only with `nf::g_controls_persist`: off in
  captures without `--config`). Ids 0x10000400-0x100004ff. Labels come from `PcText` (`Ctl*`, `Keys*`) and
  command names from `kBindCommands`, both through `PcText_ToGame`. The pause CONTROLS tab shows the game's
  own button map per style (`Menu_DisplayControllerStyleList`, symbols `~A` cross, `~B` triangle, `~Y`
  square, `~X` circle, `~C` L2, `~D` R2, `~L` L1, `~R` R1, `~F`/`~E` left/right stick, `~H`/`~V` d-pad, `~T`
  select). The screenshot report has a line `entrada aparelho D controle P estilo S inverte I vibra V teclas
  <changed commands> | <last keys-tab message>`.
- Mouse in menus (`engine/menu_mouse.h`, header comments; docs/port.md "Mouse nos menus"; port-only): the
  ELF has no mouse code (its `CursorInfo` is the manager's hidden focus marker, one texel of 0x03000042, not
  a pointer), so the mouse goes through the game's own paths. `MenuManager_Update` calls `Menu_MouseUpdate`
  (a menu-core hook, with the Modern menus' `Menu_SetPortTick` right after it; `menu_debug_power_on` resets both) at the start of the active manager's
  tick. It reads `psiInput_MenuMouse()` (`platform/psi_input.h`: position in the PS2 screen through
  `psi_2d_viewport` inverted, moved, left button held/pressed, right or Mouse4 pressed, wheel notches,
  `active` = the mouse moved or clicked after the last key or pad input). Hover: the target is the
  `Menu_GetControl` rule at the mouse (`Menu_MouseHitTest`; a disabled row-name label with an enabled control
  of the same id and tag on the same row counts as that control) and the manager's cursor goes there, so
  `Page_Update` changes the focus with the game's events; controls sharing the focus's id are one group (the
  game compares focus by id: the pause's tabs and lists are all 0x10000028) and keep the focus; hovering a
  list picks the row under the mouse (0x1d, as up/down: in the pause CONTROLS tab that changes the style,
  like the d-pad). Left click: buttons, checkboxes and labels get the select action (`Input_SetAction(0x1f)`
  for one tick, read by the controls like Space); a pause tab (a button of the 0x10000028 group) only gets the
  focus (hover does not switch tabs), other same-id buttons (the front end's keyboard keys 0x10000074) are ordinary
  buttons; on a front-end wheel (focused scroll bar of `kWheels`) clicking its middle label selects, the other labels
  turn the wheel to them, and the mouse wheel turns it anywhere on the page; a
  list row is picked and selected; on a list's bar the arrows step and the middle jumps; a radio steps back
  on its left arrow and forward elsewhere; a scroll bar steps on its arrows and takes the track value
  (`Menu_MouseScrollValue`, the fill end of proportional bars) and keeps following a drag. Steps use
  `Scroll_Update`'s path (position, sound, 0x49 to the owner). Wheel: lists one row per notch, radios and
  horizontal bars one step (up = more); unused wheel is left for the page (`Menu_MouseTakeWheel`, the
  options page scrolls its rows). Right button or Mouse4: the back action (0x22). Nothing happens while a
  key is being captured or the manager is frozen. Platform: while any menu is open the mouse buttons and the
  wheel do not reach the bindings (no shot, aim or weapon change), and buttons still held when it closes stay
  out until released. `main.cpp`: `Window::set_menu_mode` releases the mouse (no `ClipCursor`, a click does
  not capture) while a menu is open and recaptures on close if it was captured; `set_cursor_visible(
  psiInput_MenuMouse().active)` shows the Windows arrow (`WM_SETCURSOR`) only while the mouse is used; the
  keyboard stays active in the menu even uncaptured. The screenshot report has a line `mouse dentro x y ativo
  alvo id/tag tipo | focos cliques escolhas passos valores linhas voltar`. Tests: `tests/test_pc_options.cpp`
  (hit test, slider value, clicks, tabs, wheel on the pause list) and `tests/test_input.cpp` (KEYS row by
  click, clicks kept out of the game).
- Modern menu style (`src/ui/modern_ui.h`, `modern_kit.h`, `modern_input.h`, `modern_options.h`, `modern_pause.h`,
  `modern_end.h`, `src/engine/menu_modern.h`, `src/render/ui_modern_draw.h`, `ui_modern_app.h`, header comments;
  **docs/design.md** is the design system, docs/port.md "Menus no estilo Modern"; port-only): `[ui] menu_style =
  modern|original` (`nf::MenuStyle` in `VideoSettings`, default modern; `--menu-style`; a "Menu style" row in
  INTERFACE in both styles, applied with the other pending options and effective the next time a menu opens).
  Original = the game's menus as ported + the tabbed PC page (unchanged, pixel-identical). Modern is a port-side UI
  drawn with Direct2D/DirectWrite at window resolution, always (independent of the HD 2D layer; without Direct2D the
  Original style stays), inspired by 007 First Light's menus. It covers the **pause** (with the PC options as a
  tab, and SCORE in single player too), **mission failed**, the **results**, the **front end's single-player path** (with
  the **dossier** and the **credits**) and the **controller-disconnected** warning; the codename's multiplayer options,
  the screen adjust and the driving controller page stay the game's pages, drawn Original. Pieces:
  **tokens** (`Tokens`: colours, `TextStyle`s in Rajdhani 300-700 with tracking and caps, the spacing grid, motion
  durations and cubic-bezier curves) with C++ defaults overridable by `hd_ui/design/tokens.json` (`parse_tokens`,
  read at start; the report line `moderno ... tokens` says if it failed); the **canvas** (`make_canvas`: a
  1920x1080 virtual canvas scaled uniformly, height-fit with side margins on ultrawide, width-fit on 4:3;
  `left/right/top/bottom` are the window edges in canvas units for full-window elements); **motion**
  (`curve_eval`, `Tween` that continues from where it was when retargeted; real time per video frame, the ticks'
  clock in captures); the **draw list** (`DrawList`: fill, stroke, line, text with style/align/wrap, ellipse, ember
  glow clipped above a line, radial vignette, gradients, chevron, padlock, diamond, Xbox glyph SVG, clips) built by
  the **kit** (`modern_kit.h`: background, `TabBar` (with locked tabs), `CategoryList`, `BigMenu` (the large caps
  list of the reference's main menu: focused row gold with the square marker, the inline sub-title and the
  underline), `FocusMark` = gold underline + ember glow animated between rows, row label/hairline/section, dropdown
  value + chevron, toggle, slider (`slider_track`), radio option, info, locked, action, scroll indicator, key cap
  and prompt icon, `PromptBar`, `draw_description` (returns where its text ended), `Dialog`, `Toast`;
  `text_width`/`wrap_lines`/`fit_style` use the `Measure` callback, DirectWrite in the exe and `estimate_width` in
  tests). **The game's menus stay the game's** (`engine/menu_modern.h`): the game opens them (`Player_Update` the
  pause 0x80000002/0x4000004b status 3, `Mission_Update` mission failed 0x80000004/0x40000042 status 4, the front end
  07000048 the results 0x40000036 with stats 0x40000037 and bonus 0x40000038, status 5) and its handlers decide;
  `MenuModern_Snapshot()` reads manager 0 (the page, the page under the message box 0x4000002f or the front end's
  card box 0x4000002a (its memo 0x100000fb and answers 0x100000fc: saving the profile after the results), list rows with the
  file's spaced letters collapsed, labels and tab buttons, the open confirmation: pause list tag 0xd with question
  tag 0xc or the message box; objectives through `Mission_*` (visible ones in `Mission_ObjectiveState` order, then
  the hidden ones, states 0/1; `MenuModern_ObjectiveState`: hidden/active/done/failed); the button map list tag 10;
  mission name/description from `sp_level` +4/+8, level from `MissionData`, difficulty `GameState` +0x28 (1
  Operative, 2 Agent, 3 00 Agent: label 0x14c + d); the fail reason `Mission_FailLabel`, "Killed In Action" 0x2fe on
  death, or the failed objective; results labels, stats list 0x10000022), in UTF-8 (CP1252 converted). Choices go
  back as the game's controls would produce them: `MenuModern_Request` queues `Tab` (the manager cursor on the tab
  button: `Page_Update` sends 0x4f/0x4e and `C_GCPAUSE_Handler` builds the panel), `Pick` (list 0x1d, as up/down: 0x49
  to the owner) and `Press` (a PS2 button for one tick through `Input_SetAction`: Start 0x1e, X 0x1f, square 0x20,
  circle 0x21, triangle 0x22, read by the controls' updates and `MenuManager_Monitor`), run by the port tick hook
  `Menu_SetPortTick` (`MenuManager_Update`, right after `Menu_MouseUpdate`; installed by `ModernUiApp`, a no-op with the queue empty, as always in the Original style; a Tab or a
  Press ends the tick; `kMmAnyTag` picks the first list of an id). RESUME = row 0 of list tag 6 + X; Start/Enter/back
  at the top = Start (the Monitor closes and unpauses); RESTART/QUIT = row 1/2 + X, then the game's YES/NO (list 0xd,
  "no" preselected) drawn as the kit's `Dialog` and answered by its rows (back = "no"); tabs PAUSE MENU/OBJECTIVES/
  CONTROLS/SCORE = focus on tab buttons 0/1/3/4; the button layout = a row of list tag 9; invert Y = circle; REPLAY/
  QUIT = a row of list 0x100001a4 + X; CONTINUE/STATS = X/circle on button 0x10000165; stats BACK = triangle; the
  message box = its list 0x10000120, the card box = its list 0x100000fc. No case needed to call handler functions directly. OPTIONS (the row and the
  tab) are port-only; DOSSIER presses square (the game's dossier page, the Modern dossier view). Screens: `PauseScreen` (`modern_pause.h`: top tabs PAUSE MENU ·
  OBJECTIVES · CONTROLS · SCORE · OPTIONS with Q/E or LB/RB (the game's pause shows SCORE in single player too); SCORE =
  the game's list tag 8 that `C_GCPAUSE_Handler` builds on the tab's focus (single player: header Category / Current /
  Target and the `PlrStat_GetScore(1)` rows, indented "  ..." rows indented, rows without numbers as group headings;
  multiplayer: the scoreboard), read-only, the mission and difficulty on the left; PAUSE MENU = `BigMenu` RESUME,
  RESTART MISSION, QUIT MISSION, OPTIONS + the mission panel (name, description, current objective, level,
  difficulty, objectives meter); OBJECTIVES = counter, rows with status icon/text/status label, the hint in the
  description; CONTROLS = device, layout picker, Y axis toggle, the two-column button map with the device's glyph
  (`symbol_glyph` via `ui_hd_xbox_name`) or bound keys (`symbol_keys` via `ui_hd_prompt_keys`); OPTIONS = the
  embedded `OptionsScreen` (`set_embedded`: background and top bar are the pause's; Q/E and back from the
  categories become `take_tab_request`/`take_back_request`)), `FailScreen` (`modern_end.h`: "MISSION FAILED" over
  the blurred scene with the game's blood curtain crisp behind (`build_back` veil, the curtain, then the text), a
  slow fade, tracking opening, hairlines growing, the reason and the game's rows side by side; options answer only
  after 0.8 s) and `ResultsScreen` (opaque `ground` background with a faint `results_007.svg`; RESULTS · STATS ·
  DOSSIER following the game's page; "MISSION COMPLETE", mission, score, medal target, medal (what
  `P_NFRESULTS_Handler` wrote from `PlrStat_GetScore`), CONTINUE/STATS/DOSSIER; stats table from the game's list
  (`P_NFSTATS_Handler`) or its category labels with "—" before the game fills it). `OptionsScreen` (`modern_options.h`): categories VIDEO (+ MOTION: frame rate
  limit, frame interpolation, instant mouse look), GRAPHICS, INTERFACE (+ Subtitles), GAMEPLAY, AUDIO, CONTROLS (the
  device and MOUSE: mouse sensitivity, aiming sensitivity, invert mouse Y; the player looked for the mouse there), KEY
  BINDINGS (`Cat::MouseKeyboard`: the keys table ACTION | PRIMARY | SECONDARY |
  CONTROLLER, rebinding per cell through `psiInput_BeginCapture` and `nf::bind_slot`/`clear_bind_slot`) and
  CONTROLLER (sticks with invert controller Y, buttons); no option appears in two categories (`--menu-tab mouse` opens
  CONTROLS, `keys`/`bindings` KEY BINDINGS); the same options and backends as the Original page (`video_controller()` with the 15 s confirmation
  as a Modern dialog, `controls()` + `persist_controls`, `PlayerSetting`, `Menu_ChangeControllerStyle`, `PcText`
  with an English/Portuguese description per option `MdDesc*`; port-only strings of the game-menu screens are
  `Mp*`/`Mf*`/`Mr*`). The game's own options (decided with the user: in Modern the PS2 option pages are not shown as
  such; the useful options join this screen, the obsolete ones (widescreen, screen adjust, split screen, speaker mode)
  stay on the game's pages in Original) read and write the game's value through `nf::game_option/set_game_option` and
  apply at once, also in a running mission (the game reads each byte every tick): GAMEPLAY (AIMING: Auto aim,
  Crosshairs, Manual aim Toggle/Hold; MOVEMENT: Crouch Toggle/Hold; ITEMS & HUD: Weapon auto switch, Flashing objects,
  HUD always on: `PlayerSetting[0]` +1, +8, +3, +4, +0xa, +0xc, +0xb), INTERFACE > ON-SCREEN TEXT > Subtitles
  (`DrawInfo[2]`), AUDIO (Music volume, Effects volume: `SFXMusicSetVolume/SFXSetVolume`, sliders 0-20 = the AV page's
  bars, volume / 5; heard at once, the description's notice says so) and CONTROLLER > Vibration (+9, the same
  value as the game's "Vibration"); the ini follows the "Codename options" rule. Their descriptions are the game's own
  help texts (`help_table`: one per value, e.g. "Feel the action" / "Don't feel the action") with a Portuguese
  translation, and follow the value in force. R restores: GAMEPLAY from `def_codename` (the game's default profile), AUDIO
  to the AV page's Restore Defaults (70, 100), INTERFACE's subtitles to that page's rule (off in the US version),
  CONTROLLER's layout to `def_codename`'s style (7). Deep link: `OptionsScreen::open_at(Cat, back_leaves = true)` opens
  straight into a category (the Modern front end opens GAMEPLAY / CONTROLLER / AUDIO from the codename menu); with
  `back_leaves`, back from the rows leaves the screen (`request_close`, or `take_back_request` when embedded) instead of
  going to the category column; outside the pause the host uses `set_embedded(true)` and draws the background and top
  bar itself; `parse_cat`/`cat_name` are the `--menu-tab` names (video, graphics, interface, gameplay, audio, controls,
  keys, controller). Input per video frame (`UiInput`; the game-menu screens read it through `NavState` in
  `modern_input.h`: the menu commands of the bindings table + Enter/Esc fixed, the pad's A/B/LB/RB/Start/d-pad/
  stick, the mouse mapped to the canvas, key repeat, swallow until release on open and when a confirmation opens;
  in the pause Start (the pad's, or the `pause` key, Enter by default) resumes and Space selects; in mission failed
  and results Enter selects, like the game's status > 3); commands `Cmd`/`Nav` for tests. Glue (`ModernUiApp`):
  each video frame it follows the snapshot (opens the screen the open game menu asks for, only with the Modern
  style live; closes it when the game's menu goes away, also by itself: controller warning, restart),
  `psiInput_SetModal(true)` while a screen is live (released as soon as the game's menu closes; everything still
  held is swallowed), `RuntimeGfx::set_hidden_2d(hidden_2d())` hides the game's 2D under the screen (all of it under
  the pause, the results and an objective failure; only the menu camera 7 when the player died, so the blood curtain
  stays), then `Renderer::blur_background(amount)`
  (copy of the target, 1/4 downsample with `ps_down`, two separable gaussian passes `ps_blur`, mix `ps_blur_mix`),
  `Renderer::render_2d(overlay)` (the 2D that stays, crisp) and `Renderer::draw_modern` (`UiModernDevice`: Rajdhani
  from `hd_ui/fonts_ofl/rajdhani` in a private DirectWrite collection, fallback Bahnschrift; text sized in window
  pixels with the caps centred in the rect; hairlines snapped to pixels). The pause's OPTIONS row of the game still
  calls `MenuPc_SetOptionsHook` (Modern: the OPTIONS tab of the Modern pause). Captures: `--open-menu-page 4000004b
  --menu-tab <pause|objectives|controls|score>`, `--open-menu-page 40000060 --menu-tab <video|graphics|interface|
  gameplay|audio|controls|keys|controller>` (the OPTIONS tab); `--input` gets `cmouse=x,y` (canvas units). The screenshot
  report has `moderno ...` (the options: categoria zona foco coluna lista_aberta opcao rolagem dialogo captura
  pendente aviso mouse | desenho ... tokens ...), `pausa_moderna ...` (aba abas foco lista dialogo foco_dialogo
  esperando aba_jogo linha_jogo mouse), `fim_moderno ...` (tempo foco opcoes motivo), `resultado_moderno ...` (vista
  foco dialogo) and `ponte_moderna ...` (pedidos abas linhas botoes perdidos fila); `NF_MENU_DUMP=1` adds every
  control of each manager's current page (id, tag, type, state, rect, texts, list rows). New menu commands in the
  bindings table (37 now): `menu_tab_prev` (q, pad_lb), `menu_tab_next` (e, pad_rb), `menu_defaults` (r, pad_x);
  they have no game action (`command_has_action`), are read with `command_value` by the Modern menus only, and
  never mask a pad button out of the virtual PS2 pad. Tests: `tests/test_modern_ui.cpp`, `tests/test_modern_menus.cpp`
  (the bridge against the real menu code on a hand-built menu file: tabs, layout, invert, restart yes/no, resume,
  Start, replay, continue, stats; the screens with hand-built snapshots).
  The front end (`src/ui/modern_front.h`, `FrontScreen`, header comment; docs/port.md "O menu principal no estilo Modern";
  docs/design.md "As telas"): `MenuModern_Snapshot` gives `MmScreen::Front` for the single-player pages of 07000048
  (`MmFront`, `MenuModern_FrontView(page)`: Title = 0x40000034/48/32/09/35/4a/43, Main 0x40000002, Campaign 0x40000025,
  Codenames 0x4000001b, Name 0x40000020, CodenameMenu 0x4000001d, Difficulty 0x40000023, Map 0x4000001c, Options =
  the codename option pages 0x40000022/2d/31, Dossier = 0x4000002b/3a/3b/3c/3d, Credits = 0x40000030; None = the pages
  that stay Original): the page title, help bar and
  description memo, the legal text and the "Press the START button" label (empty during the legal delay), the main
  buttons with their `help_table` texts and the focus, each wheel (scroll bar id, position, limits) with its items
  from the game's tables (`sp_level`, `difficulty`, `cn_options`, `codename_buf` with the wheel's description labels;
  enabled = `enabled` or `menu_unlock_everything`, the locked text otherwise; on the map `Menu_IsDrivingLevel`, the
  best medal by the dossier's rule (each only with every lower one, `Menu_GetLevelBonuses`) with its label and
  `PlrStats_GetLevelTotals` best), the on-screen keyboard's keys (tag, text, file rect) and `codename_entry`,
  `del_when_keypressed` (the suggested name) and `cheat_mode`. Two more requests: `Focus` (the manager cursor on any
  control, like `Tab`) and `Step` (a wheel walks one notch at a time to a position, each with the sound and 0x49 to
  the owner, as the d-pad's `Scroll_Update`); `MenuModern_Key(tag)` = Focus on the key 0x10000074 + X, so typing goes
  through `C_KEYBOARD_Handler` (8 characters, the keyboard's characters, `Menu_IsValidCodename`,
  `Menu_SpecialCodenameCheck`); `MenuModern_KeyFor` maps a character to its key. Only a queued Press makes the front
  end wait (`MenuModern_Queued`); Steps and Focuses do not hold the navigation, and the screen follows the game's
  wheel position whenever none is queued. Where a page plays its background loop (07330048/07350048) the movie
  replaces the art in the same 4:3 rect behind the blur (see "Movies"). Views: Title (the game's art 0x03000074 with its logo at window height,
  sharp, edges fading into `ground`, the game's prompt with the Enter key / Start glyph breathing, the legal text;
  select = X on the page's full-screen label); Main (the game's logo 0x0300016d, `BigMenu` NIGHTFIRE / MULTIPLAYER /
  CODENAMES = Focus + X on buttons 0x10000002-4, compact OPTIONS = the embedded options (port-only), CREDITS (port-only:
  in Modern the AV Options page, the game's way to the credits, is the Modern AUDIO category) = `Show` of the game's
  credits page 0x40000030 and QUIT GAME = a port dialog, also on back; `take_quit` makes `main` leave its loop; the game's help text under the list); the
  wheel views (left: the page's title in `page_title`, the campaign steps CODENAME / DIFFICULTY / MISSION; middle:
  one row per item, Step on focus, Step + X on select; right: `draw_description` with the game's text): Campaign and
  Codenames (tags Create / Not saved / Default profile / Memory card; Delete key or pad X = circle on a card name;
  pad Y = square, create), Difficulty (one to three diamonds), Map (numbered rows; locked = padlock and select sends
  nothing but a toast; driving = "Driving" with the skip note; the best medal; the mission picture, the item's
  round texture, at the left with a dark ring hiding the square corners of HD replacements; top bar MISSIONS ·
  DOSSIER, E/RB/pad X = square: the game's dossier, the Modern dossier view), CodenameMenu (Secret Unlocks and Save Codename =
  the game's; Controller Setup / Advanced / AV = `OptionsScreen::open_at` CONTROLLER / GAMEPLAY / AUDIO instead of
  the game's pages, `MenuModern_CodenameOptionsBegin/End` set `cn_modified_flag` if the player settings, `DrawInfo`
  or the sound values changed, like the pages' X; Driving Controller and Multiplayer Options locked); Name (eight
  slots with the suggested name marked, the caret, a key grid built from the game's keys; letters, digits, space,
  Backspace/Delete and Enter of the PC keyboard type, those keys never reach the menu commands; pad: d-pad grid, A
  key, X delete, Y space, Start done); Options (a codename option page the game showed by itself, Controller Setup
  after creating a codename: the options at the category, leaving = the game's back); Dossier (`DossierView`,
  `src/ui/modern_dossier.h`, header comment: one screen with top tabs RECORDS · REWARDS · GADGETS · WEAPONS, the
  `ds_options` names; each tab is the game's page reached through its own navigation, one page request at a time and
  waiting for the page to change (`MmSnapshot::accepts`: no transition, past the page's `input_delay`): a tab = the
  game's back (the section returns to the wheel page 0x4000002b), `Step` of the wheel 0x1000010c to the section and X
  (`C_SBDOSSIER_Handler`: records and rewards at once, gadgets and weapons after the iris); arriving at the wheel page
  (from the map or the results, the wheel reset) enters RECORDS; back = the game's back twice, so the game returns to the
  map or the results; the old tab's content fades while the game navigates. `MmDossier`: the section, the four
  sections, the records the handler wrote (labels by their `Txt` label: 0x302, 0x165, 0x305; values 0x10000175/77/78,
  medal 0x100001f8 and its image 0x10000176), the four rewards (images 0x100001d6+i, texts 0x100001d2+i, hidden until
  earned, with the medal label each needs), the gadget/weapon memo (with the upgrade line) and each item's
  `Menu_GetObjectUpgradeLevel`; the missions of records and rewards are the radios 0x10000174/0x1000017a (the rows of
  `MmFront::items`, completed from `sp_level`), walked by `Step`, which now also steps a radio's bar as the d-pad does
  (the 0x49 goes to the radio, which picks the option and passes 0x49 on to `C_RBDSRECORDS/DSREWARDS_Handler`); the
  gadgets/weapons wheels 0x1000016f/0x10000173 likewise. Left: the page title, the section's `ds_options` text, the
  round picture (the mission's `sp_level` image or the item's) with the dark ring; middle: 12 numbered missions with the
  best medal (records) or the rewards earned as four diamonds (rewards), or the items with "Upgrade" and one diamond
  per level; right: the records (stat numbers, the medal's 2:1 007 image and label), the four rewards (the game's image
  and text, or a padlock and "Requires: <medal>"), or the item's text with the upgrade line in gold. Images are game
  textures through `ModernUiApp::draw_images` (`ImageCmd::whole`: the texture's half-texel inset from its size); HD
  replacements apply. Up/down (left/right on missions) step the radio or wheel, Q/E/LB/RB/clicks switch tabs, back,
  Esc, B or right click leave); Credits (`draw_credits`: the visible label pairs 0x10000213/0x10000214 that
  `P_CREDITS_Handler` fills, with their position (PS2 screen mapped to the canvas: height 448 -> 1080, centre on the
  centre) and the font of its format (`MmCreditLine::font`: 1 the companies, `hero` light caps with a short gold rule; 3
  the roles, small spaced caps; 2 the names and the legal text, `label`), clipped with fading edges, the game's end fade
  (the alpha of the full-screen label 0x10000215) over it; the game's timing and its return by itself; back = the
  game's back; coming back the main menu keeps CREDITS focused). The game's message box and
  card box become the kit's `Dialog` (titles Notice / Confirm / Memory card from `PcText`; no answers = `busy`, a
  sweeping gold dash; the panel grows with long texts). Drawing in layers (`ModernUiApp`): `build_back` (opaque
  `ground`), `backdrop` (the art, `ui::ImageCmd`), `Renderer::blur_background(blur())` (0 on the title, 1 in the menus,
  0.9 s), `build_veil`, `images` (logo, mission picture) and `build`; images are game textures drawn by D3D as
  `Quad2D`s (`psi_2d_texture_slot(hash)` gives the Tex slot; HD replacements apply). The controller-disconnected page
  (status 2, 0x4000002f) is `WarningScreen` (`modern_end.h`): the kit's dialog without buttons over the blurred scene,
  the game's text and what to do on PC; it is not modal (the game waits for the pad or a key and reopens the pause).
  `--input` gets `pad_off` (the simulated pad disconnected in that step). Report lines `frontend_moderno ...` (vista
  pagina foco linhas roda dialogo opcoes desfoque nome esperando mouse; in the dossier `| dossie_moderno aba alvo secao
  foco linhas enviado conteudo`, in the credits `| creditos linhas escurecer`) and `aviso_moderno ...`; `ponte_moderna`
  also counts `focos passos paginas` (`Show`: the 0x44 a control's script sends to the manager, target 0xfffffffd; the
  current page goes on the stack). Tests: `tests/test_modern_front.cpp` (the bridge on hand-built front-end pages: views,
  Focus, Step + X to the difficulty and the map, typing through the game's keys with its limit and validation; the
  screen: main rows, CREDITS and QUIT, locked missions, the game's boxes as the dialog, the codename options, title and
  warning) and `tests/test_modern_dossier.cpp` (the dossier pages read, the radio stepped, the tabs and back through the
  game's wheel page and iris, the view's drawing and requests; the credits lines with positions and fonts, the page shown
  by `Show` and returning by itself; the single-player SCORE tab from `PlrStat_GetScore`; the square key).
- Player statistics and score (`engine/player_stats.h`, header comment; docs/engine.md "Estatísticas e
  pontuação"): the whole `PlrStat_*`/`PlrStats_*`/`PlarStat_*` module. Counters in `PlrMissionStats` (8 players x
  `PLRMISSIONSTATS`, 0x38 bytes: detected, shots fired/hit, dispatched, disabled, surrendered, spawned, health, Bond
  moments, 007 tokens, timer) and `BondMoments` (channels 0x65-0x6e), zeroed by `PlrStat_ResetForMission`
  (`Mission_Init` on a mission's first level; `PlrStat_Init` at boot; `Player_RamSave/RamLoad` keep them across a warm
  restart). The loggers are called by the ported code (vision, drone death/knock-out/surrender, `NDrone2_HitDamage`,
  `Player_WeaponInitBullet`, `Destroy_Smash`/`SP_Update`, `Player_SetHealth`, `Trigger_Calc`, `Pickup_Handler`) and
  only count while `PlrStat_OkToUpdate` (mission status 0/1 or multiplayer); the timer (`psiGetTimeIn100ths`, real
  time) and spawned drones always count. A first Bond moment sets `BLData::bond_moment` = 200 (the spinning 007, HUD
  pane 1 sprite 16). The score tables come from the ELF (`kTables`: `HT_Level_Driving_Paris_ST`..`ScoringTable` = 12
  x `SCORETABLE_ROWS` (9 rows of 0x14, stride 0xb8), `ScoringTable` (12 x `SCORETABLE`, +0x18 rows pointer
  translated), `ScoringTableDifMul` 0/1/2). `PlrStat_GetScore(peek)` (returns the `SCORETABLE*` as `uint8_t*`) finds
  `Mission_BaseMapHCode`'s entry (7 on-foot missions 07000001/05/09/0c/11/14/1b, 5 driving 09xxxxxx fed by
  `NewScoresRef`, null in the port), fills the rows (accuracy = hits x 100 / fired, 100 with no shots; health 0 if
  status 3) and scores each row with a target: detections lose `detect_penalty` (0.03 if 0) per detection over the
  target; time = points x target / max(t, 1 s), uncapped (a `--win 1` run scores millions: faithful); 007 tokens all or
  nothing; others points x min(actual, target) / target; ceil with 1e-5 tolerance, fraction in hundredths clamped to
  [0, 1], sum accumulated in float; bonus = sum x the difficulty multiplier; with peek 0 also total, best and
  `DoneBetter` (defined in `menu_handlers.cpp`). Outside status 6 the points are zeroed. It runs with the EE's
  round-toward-zero (`_controlfp_s`, `#pragma fenv_access`), or the 1e-5 ceil would differ by a point on edge cases.
  The medal is `P_NFRESULTS_Handler`'s (port helper `PlrStat_MedalFor`: highest threshold reached, 0 none). Everything
  is static memory, so it survives `ResetMap_Load`/`Mem_Init`; `Mission_Init` leaves `BaseMap` and the status alone in
  the front end, where the results read them. `PlrStats_HasGoldMedal` = `Menu_HasMedal(BaseMap, 3, 0)`. Port-only
  guards (`g_player_stats_guards`): a Bond moment channel outside 0x65-0x6e or a player without `BLData`. The
  screenshot report has a line `estatisticas tiros acertos abatidos desacordados rendidos vistos gerados vida momentos
  fichas tempo parado | missao pontos bonus total recorde medalha` (`PlrStat_Describe`); `nf_loader_check` checks the
  copied table (`pontuação: 12 missões ...`), zeroes the stats before each level, logs the pistol bullets as
  `Player_WeaponInitBullet` would, advances `VBlankCount` with every game tick, prints `estatisticas=[...]` and
  `pontuacao_se_cumprida=[...]` (`PlrStat_GetScore(1)` in status 6, the table restored afterwards) and, in a last
  pass with every medal set, `fichas 007 com a medalha de ouro: ... total=50`. Tests: `tests/test_player_stats.cpp`
  and `pickup_bonus_token_needs_the_gold_medal`.
- Audio (`engine/sound.h` with `sound.cpp` and `music.cpp`, `platform/psi_sound.h`, `platform/sfx_iop.h`,
  `platform/spu2.h`, `platform/audio_out.h`, header comments; docs/formats.md "Áudio", docs/engine.md "Som",
  docs/port.md "Som"): the game's three layers, all ported. (1) EE logic: `Sound_*` (a request is a `DYNAMICSOUNDS` node,
  0x60 bytes, of `DynamicSoundList` (256); the handle is the node pointer; `Sound_Ready` loads the level's bank during
  `LoaderLoad`; `Sound_UpdateListeners` (from `Game_Run`, every tick) puts the listener on the camera, sends the camera
  room's reverb, switches the map's ambient sounds (block 0x28) and runs `Sound_UpdateSounds` and `SFXUpdate`;
  `Sound_ReqestPlaySfx` gives speech its subtitle and drops far "true 3D" sounds; `Sound_Alertness` is what the AI hears,
  now real), the map's sound triggers (placement 0xf9, type 0x40) and music triggers (0xfb, type 0x44), and the music:
  `Music_Event` fills `EventList[64]` for one tick (0/1 level loaded, 2 AI alert, 3/8 mission end, 5/11/12 scene scripts,
  6/7 death and victory, others from the AI and the player's room `cel_tag::misc[9]`); `UpdateMusicalEvents` (from
  `SFXUpdate`) starts the level's track from `MapMusic` (0x13 words per level from the ELF: track, start section,
  death/victory/end sections, attack counters, 5 combat and 5 stealth sections), counts the attack and calls the
  level's `UpdateMFX_*` (21, with `PickRandomStealth/Combat`), which request sections with `SFXJumpToMusicMarker`.
  `MapMusicData` (`MapMusicIndex`) starts at row 0 and stays on the last level, so the front end keeps running that
  level's function, like the PS2. The AI speaks through `NDrone2_SpeechSFX` (`Drone_SfxInfo`); `NDrone2_DeathTalk`,
  `NDrone2_HandleTalking`, `NDrone2_PowerstationTalk` (`PowerSfx` from the ELF), `Drone_MayhewTalk` and
  `NDrone2_BondTalk`/`DroneFunc_DoBondTalk` (07000007's guests talk to the player; table in docs/engine.md "Som") are
  ported. Most AI lines have a face script (`Drone_SfxInfo` +4, 06xxxxxx) whose command 1 is the sound: `SpeechSFX`
  appends it to the drone's animation list (`AnimFacialAppend`, tag 0xe0000002, end mode 3, `ANIMOBJ_tag::face_state`
  2) and the sound plays when the script crosses its frame; the same script's face sequence moves the mouth (see
  "Faces"). Hint objects (placement 0x33, type 0x43, `Hint_Create/Update` in
  `trigger.cpp`) show their text (and would play their sound; the 48 created at difficulty 2 have none) when the player touches them.
  (2) EE to IOP: every `SFX*` appends a command to `SFXBatchCommands` (0x800 words; full at 0x79d, which forces an
  `SFXUpdate`); `SFXUpdate` (once per VBlank) closes the batch with 0x19 (scenes), 4 and 5 and sends it through
  `nf::sfx_iop_rpc` (a direct call), which returns `SFXActiveData` (the live sounds, the music section). Divergence:
  `SFXReset2` sends the batch if the reset stayed in it (the PC's reset is instant). (3) The IOP driver, EuroSound's
  `SFX.IRX` (a relocatable IOP ELF with symbols and STABS: `tools/ghidra_irx.sh import|decomp|disasm`, outputs in the main
  checkout's `out/decomp_irx/SFX/`; port markers `// [irx] 0x<addr> <Name>`, 152 of its 247 functions; the rest is the CD,
  threads, DMA and stream buffering, which the PC does otherwise): banks `SB_<slot>` from `SBINFO.SBI`, 40 items, the
  voice budget `15 - SFXStartedCount / 400`, priority stealing, `MaxVoices`/`MaxReject`, `VolumeOverRide` = 100 (the
  game's volume argument is ignored, faithful), `ES_CombineVolumes` squared, 3D in integer decimetres with the driver's
  Newton square root (it overshoots far away: 100 m counts as 129.7 m), the listener's right = `ListenerNorm`, the rear
  voice of a 3D sound with inverted phase (Pro Logic; not in mono), 2D pan, the ducker, fades, pause = pitch 0 (voices
  freeze), `PauseInNis` and the 20-VBlank fade of sounds started during a scene, the room reverb (the "studio C" preset
  from LIBSD.IRX's .data), speech on 2 stream channels (read whole at once), the music on channel 0 (`MusicStream`
  follows the SMF marker timeline unit by unit: loops 6/7, end 9, a requested section at the next non-7 marker or at
  once with `IsInstant`/instant; a section beyond the track's table is ignored, port-only: the IRX does not check).
  Port-only guard in `StartSample`: a sample outside the item's bank is not started and counts in
  `g_sfx_iop_stats.guards` (`guardas_iop=` in `nf_loader_check`): an item rejected by `SFXSetup` (full voices: the new
  lowest-priority sound steals itself) stays in the list until the next `SFXUpdate`, which processes it like the others,
  and a polyphonic one's cue (`CuedSamples`/`CueSampleWaiting`) was never filled (null in a never-used item: the IRX
  would read IOP address 0; the old crash of 0700000a's thunder with `--no-interp`; test
  `iop_rejected_polyphonic_item_does_not_start_samples_it_never_cued`).
  Under it `nf::Spu2`: 48 voices, 2 MB, PS-ADPCM with loop/end flags, 4-point cubic interpolation (the SPU's is
  Gaussian: a small timbre divergence), the driver's ADSR, the two cores as LIBSD.IRX's `sceSdInit` leaves them (its
  function at 0x00000ee0 of the module, no symbols: MMIX 0xff0/0xffc, AVOL of core 1 = 0x7fff, BVOL 0; each core
  saturates its 24-voice sum to 16 bits, core 0's output x MVOL, saturated, enters core 1's dry mix through AVOL, core
  1 x MVOL is the DAC; docs/formats.md "A mistura da SPU2"), MVOL and the voice volumes in the 15-bit fixed format
  (0x3fff = ~1.0), EVOL/AVOL as 16-bit signed (0x7fff = ~1.0: the driver's maximum EVOL 0x3332 is 0.4), one reverb per
  core at 24 kHz, and a DC blocker (the PS2's output capacitor; without it the paused voices left an offset). The gain
  from a voice to the DAC is 1.0 (the driver's MVOL 0x3fff on both cores, the movies' too): a centred full-volume
  effect peaks at about -6 dB per side and a few loud ones together saturate, on the PS2 too (docs/port.md "O nível da
  saída": no limiter, recommended). The output is deterministic: before each RPC
  `audio_out_catch_up(VBlankCount)` renders 800 frames per elapsed VBlank (at most 600), so the sound follows the game's
  clock; the frames go to XAudio2 2.9 (`xaudio2.lib`, 48 kHz s16 stereo, 50 ms prefill, at most 8 queued chunks), to a WAV
  (`--audio-dump`, header rewritten every second; on close it prints to stderr `spu2: saturacoes vozes0 N vozes1 N
  nucleo0 N saida N pico_sem_corte X dB`, where the SPU2 saturated and the output peak without the final clip) or nowhere (`--no-audio`; the default with `--screenshot`, in
  `nf_loader_check` and `nf_tests`). Data: the disc's `PS2\` folder next to `FILES.BIN` (`nf::sfx_iop_set_root`); never
  commit decoded audio (`out/` only). The screenshot report has `audio <aparelho|wav|mudo> vozes tocando falas banco reverb
  pausa cena | jogo pedidos tocados falas nos | iop pedidos comecados recusados ausentes roubados | saida pico rms frames`
  and `musica fase faixa trecho pedido ataque fim`; `NF_SFX_LOG=<file>` logs requests (`ee Sound_Play3D`), starts,
  rejections and music; `nf_loader_check` prints `sons: pedidos tocados falas legendas recusados ausentes banco vozes
  gatilhos_som gatilhos_musica musica=track/section`. Tests: `tests/test_sound.cpp` (a fake disc in a temp folder:
  ADPCM, SPU loops/mix/DC, the driver's sqrt, 3D/2D volumes, voice limits, stealing, speech, pause, music markers, the
  EE music logic, `Sound_Play` to the IOP and back).
- RTX Remix path (`render/remix_d3d9.h`, header comment; **docs/remix.md** is the study with dated sources; docs/port.md
  "Caminho Direct3D 9 para o RTX Remix"; port-only, a prototype; the user's first try did not show Remix and RTX is
  parked, see Known gaps 8b): Remix only hooks Direct3D 9
  fixed function (D3D11 is invisible to it), so `--renderer d3d9ff` draws the same `DrawItem` list (the
  `collect_frame` output or the interpolated frame) through `RemixD3D9Renderer`. `RuntimeGfx::set_mirror(GfxMirror*)`
  hands it every texture and mesh created at load (same `Tex` slot, same mesh id as the `Renderer`): `D3DPOOL_MANAGED`
  VB/IB per mesh (FVF, 16-bit indices when they fit), one `DrawIndexedPrimitive` per part with the exact vertex range
  (stable Remix geometry hashes), A8R8G8B8 textures with CPU mips and the GS alpha doubled (0x80 -> 0xff; the Remix
  texture hash is the XXH3-64 of that level 0, so never change the upload format once mods exist); `SetTransform`
  world/view/projection (normal depth, not reversed); skinning by indexed vertex blending (`D3DFVF_XYZB2 |
  D3DFVF_LASTBETA_UBYTE4`, `D3DVBF_1WEIGHTS`, `D3DTS_WORLDMATRIX(n)` = the palette; more than 9 matrices go through
  software vertex processing on the mixed device: dxvk-remix reports `MaxVertexBlendMatrixIndex` 8); colour = texture x
  vertex colour x 2 (`D3DTOP_MODULATE2X`) x tweak (`D3DTA_TFACTOR` in stage 1); GS blend/alpha test/Z through D3D9
  states (a copy of `resolve_state` with normal depth); the 2D after all 3D with an orthographic projection and no Z
  write (Remix's UI rule). Approximations: no VU1 lights in the raster (they go to Remix as `D3DLIGHT9` point lights with
  `D3DRS_LIGHTING` off), env-map parts keep their mesh UVs (fixed-function `TCI_CAMERASPACENORMAL` texgen broke every
  later draw of the frame on native D3D9), the GS fog becomes linear vertex fog through the curve's start and 50% point,
  alpha above 1.0 saturates. The mode forces the Original menu style and the HD 2D layer off (both are Direct2D on
  D3D11); D3D11 still exists on a hidden window (the 2D layers use its device) and never presents; `d3d9.dll` is loaded
  with `LoadLibrary` only in this mode (Remix's bridge `d3d9.dll` next to the exe; its Detours never enter the D3D11
  mode); the device uses `D3DCREATE_FPU_PRESERVE`; the back buffer does not follow window resizes. Options:
  `--d3d9-no-lights`, `--d3d9-viewmodel <z>` (list 2 in a 0..z viewport depth range for Remix's view model),
  `--d3d9-remix-conf <file>` (a suggested `rtx.conf`: scale 0.01, axes, `orthographicIsUI`, and the texture hashes seen
  in the 2D, sky, decals and particles; game-derived, keep it in `out/` or next to the exe, never in git),
  `NF_D3D9_DUMP=<dir>` (both paths' textures per slot). Compare captures with `--fixed-video-hz 60` on both sides: the
  windowed D3D9 `Present` holds the loop near 60 fps and real-clock captures then take two-tick 30 Hz steps and diverge.
  The report gets a `d3d9ff ...` line. Results (07000012 2.9 mean error, 07000001 intro 4.4, 0700000c with fog 5.8) and
  the steps for the user to try Remix are in docs/remix.md.
- Campaign completion (`engine/player_traverse.h`, `ssys.h`, `turret.h`, `copter.h`, `simple_script.h`, header
  comments; docs/engine.md "Campanha: o que falta para completar" and the sections after it): the objects the on-foot
  missions need are ported with the game's code. Traversal: ladders (placement 0x22 or a script entity of type 0x22,
  object 0x2d; touching one from the front puts the player in substate 1 `Player_Climb`: forward climbs 0.02 m per
  tick, past the top he steps off), the grapple (0x3a, object 0x51: with touch icon 2 the 0x50 gadget fires and
  `Player_SetGrapplePoint` + substate 2 `Player_SSGrapple` pull him to the point), wires (0x3e, object 0x2c: parameter 1
  is 0 rope, substate 6 `Player_Wire`, 2 zipline, substate 0xf `Player_Zipline`; the wire's channel, parameter 3, is on
  while he is on it), creep walls (0x3f, object 0x2b, substate 7), touch icons (0xfa, object 0x42: they set
  `BLData::touch_type`, which `Player_CollisionHandler` resets every tick and `Player_WeaponFiring` reads) and the
  third-person cameras (0xe7, `ThirdPersonCameras`, followed by `camera_tracking`, camera mode 0xb). `BLData` fields
  named by this work: `ladder` (the ladder, wire or creep wall in use), `ladder_top/bottom`, `climb_yaw`,
  `traverse_step`, `traverse_wait` (ticks before anything can grab him again), `grapple_point/speed/obj`, `rope`,
  `grapple_wait`, `move_locked`. Gadget targets: locks (0x2e, object 0x3a: kind 0 keypad = page 0x40000021 of menu 0x80000003, 1 laser (weapons
  0x4e/0x4f heat it), 2 decryptor only; all open with their key and, kinds 0 and 2, with the decryptor 0x56/0x57 that
  finds the four digits of `KeyCodes` (drawn at `Locks_Init` in `GF_LOCKS_INIT`) one by one into `GlobalVars`; opening
  sets channel parameter 5), the Q-Worm target (0x35, object 0x49: hit by weapon 0x58 or used, it reveals a code and sets
  channel parameter 1) and camera subjects (0xfd, object 0x48: a camera 0x54 projectile sets channel parameter 0).
  Turrets (0x27, object 0x36, `GUNTURRET` 0x100 bytes): two scene scripts (body and base) whose first streams register
  as flash, barrel and part; kind 1 automatic (channels: parameter 4 on and 5 off), kind 0 the mounted gun (200 rounds;
  use dismounts it into the 0x52 item); states 0 closed, 1 opening, 4 active (`GT_Track`, weapon 0x60 every 0.1 s),
  2/3 closing, 5 stunned (taser), 6 destroyed. Copters (0x2f, object 0x3b, `COPTER` 0x7c bytes in `CopterList`): every
  placement is a path of a group (kind 0 also makes the copter, 2 the exit path); hidden until channel parameter 6; it
  patrols, attacks a player inside a 45-degree cone (`Intersect_ConeSphere`, `COPT_COS/COPT_SIN` from its static
  constructor, ported in `copter.cpp` because the ELF symbols have no type) within 1 km and in sight (gun 0x61/0x71 or
  missiles 0x62/0x6d), takes damage on its nine parts by `Bits` (ELF table, `kTables`), flees by its exit path above
  299 x `DiffMod` (explodes in 07000012/0700000b) and sets channel parameter 7. Simple scripts (0xe2, object 0x25,
  `SIMPLESCRIPT_tag` 0x2c bytes): models that follow their path (loop, now, or waiting for their channel, a touch of
  the classes in parameter 2 or a use), stop at parameter 5, and with parameter 7 carry the room's `water_y`.
  `nf_loader_check` adds per mission level ` campanha: provisorios=N subestados= aparelhos= objetivos=channel:who,...
  objetos= canais_provisorios= travessia: <probes> stubs_jogador= bloqueios=` (`tests/campaign_audit.cpp`; "?" = no
  object sets that objective channel: a NIS event or another level; `--campanha` adds `provisorios_por_funcao=`,
  `stubs_carga=`, `stubs_jogo=`) and `bloqueios da campanha (fases):` at the end. The probes (2 s of game first, all
  state restored afterwards, the player in god mode): `escadas=up/held/total [escadas_fora_de_sala=N] escada=x,y,z,yaw(subiu fim_y)`,
  `tirolesa=`, `corda=`, `ganchos=ok/total gancho=`, `parede=`, `cadeados= laser= decodificador= teclado= cadeado=`, `qworms=`,
  `fotos=`, `helicopteros= viram_na_partida= atacaram= tiros= vencidos=`, `objetivos_ligados=` (objective channels the
  probes turned on), `roteiros= laco= esperando= com_canal= dispararam= andaram=`, `torretas= automaticas= abriram=
  tiros= danificadas= destruidas=`; their `x,y,z,yaw` work as `--at` for captures. Port-only counters:
  `g_traverse_stats`, `g_ssys_stats`, `g_turret_stats`, `g_copter_stats`, `g_simple_script_stats`. Tests:
  `tests/test_traverse.cpp`, `test_ssys.cpp`, `test_turret.cpp`, `test_copter.cpp`, `test_simple_script.cpp`.
- Security and the remaining gameplay objects (`engine/sensor.h`, `searchlight.h`, `hazard.h`, `gunimp.h`,
  `space_station.h`, the inverter in `trigger.h`, header comments; docs/engine.md "Câmeras de segurança, monitores e
  holofotes" and the sections after it; docs/port.md "Segurança"): security cameras (0x28, object 0x37, `SENSOR` 0x64
  bytes in `CameraList` unless parameter 5): sweep their path, see the player by the beam ray (`Sensor_SetBeam`, which
  also stretches the beam model, parameter 0, and turns the stand, parameter 7) or the cone with line of sight
  (`Sensor_InCone`); not while camera 4 is active; seen > 30 ticks: state 1 (follow with `Vec_Track`, beeps), after 2 s
  the alarm channel (parameter 3) is set every tick, lost > `FRAME_RATE_INT` ticks: state 2 (slerp back) and the alarm
  clears (state 0 clears it every tick); the channel of parameter 4 switches the camera off (state 3, LOD model).
  `Sensor_ApplyDamage` is empty in the game (a "destroyed" camera is a destroyable setting parameter 4). Monitors (0x30,
  object 0x38): use shows the group's first camera that is not off (`SENSOR::group`, parameter 6): the player's viewer
  loses its updater and gets the security camera's matrix every tick (`viewer_tag::camera`), the player is disabled and
  HUD panes 0/1 give way to 16; use again steps to the next camera of the group, after the last the player comes back
  (`Player_PositionCamera`); with parameter 1 = 0 one shot breaks the monitor. No render-to-texture: the renderer draws
  camera 0 as always (the tick interpolation cuts the jump; the instant mouse look is off with the player disabled).
  SSys countdowns (`SSysItems` items 1-4, the channel written by a script switch entity): 240 s with the voices of the
  ELF tables `Laser1.134`/`Laser2.135`/`Cam1.132`/`Cam2.133` at `MIN3`..`MIN01` (`kTables`), then the channel goes off.
  Searchlights (0xde, object 0x22, `SEARCHLIGHT_tag` 0x40): up to five parts joined to the lamp (kind 1) through the
  room; the spot (kind 2) owns a kind-2 light (`light_tag::obj`) and is moved to where the lamp's 100 m ray hits; state 3
  off until channel parameter 1, 0 sweeping (a circle in the arc of parameters 4/5), 1 following (the spot within 2 m
  of the player; alarm parameter 3 set every tick, never cleared by the lamp), 2 shot (parts and light removed); channel
  parameter 2 puts it out for good. Mines (0xfe, object 0x4b): any object in the hit list explodes them
  (`Explode_Create`, inline in the original). Shooters (0xd2, 0x4e): one shot of weapon parameter 1 when channel
  parameter 0 turns on. Inverters (0x41, 0x53): channel 0 on clears channel 1 every tick. Quakes (`Apocalypse`, 0x4a,
  0x54): rumble, `viewer_tag::shake` of camera 0 and `Player_SetFlashBang` for parameter 1 seconds. The mounted gun
  (`GunImp`, 0x34, 0x47; `TurretGuns` from the ELF, entry 11 in single player): use mounts it (substate 0xc, frozen,
  camera mode 0xe, FOV 30 degrees), the sticks aim (pitch -90..+45 degrees), fire every `TURRETGUN::delay` ticks, heat 2
  per beam shot and overheat above 200; `PositionBeam` (ported with it) places beam models. The space station
  (0700001b): `Create_SpaceLaser` (charges every 10 s at the player, fires weapon 0x6b, after its countdown sets channel
  99), `Create_SpaceMissile` (arms after its delay, `MissileDeploy` for HUD pane 20, launched or sabotaged by channel
  1-8; the cases of `SpaceMissile_Update` come from the jump table 0x002fcc30). The capture report has a line
  `seguranca cameras v/s/r/d alarmes N monitores N mostrando x,y,z(grupo_g)|- holofotes v/s/q/a minas N explodidas N
  canhao ...|- misseis a/s/l` (`Security_Describe`); `--channel <hex>` (debug, repeatable) sets a channel right after
  the load. `nf_loader_check` adds ` seguranca: cameras= com_alarme= viram= alarmes= reagiram=personagens:,gerados:,
  torretas:,canais: espera= ligou=channel(who)+... camera=x,y,z,yaw monitores= mostraram= imagem= painel16=
  cameras_vistas= voltou= holofotes= acesos_a_forca= varrendo= manchas= acharam= alarmes= seguiram= iluminam= ...
  minas=N(de M) explodiram_tocadas= explodiram_baleadas= canhao=montou:,tiros:,superaqueceu:,desmontou:
  atiradores= tiros= inversores= tremores= tremeram= estacao: lasers= cargas= tiros= misseis= armados= lancados=
  sabotados=` (`tests/campaign_audit.cpp`; consumers now include turrets, sound triggers and emitters; `NF_CANAL=<n>`
  lists the placements with n in a parameter). Port-only counters: `g_security_stats`, `g_searchlight_stats`,
  `g_hazard_stats`, `g_gunimp_stats`, `g_space_stats`. Tests: `tests/test_security.cpp`.
- Movies (`platform/psi_movie.h`, `pss.h`, `movie_decoder.h`, `render/movie_layer.h`, header comments; docs/formats.md
  "Vídeos", docs/port.md "Filmes"): the 22 `MOVIES/30_FPS/<hash>.PSS` (MPEG-2 program streams of 16 KB packs: video
  512x448 4:3 30 fps in stream 0xE0, TMPGEnc, B frames in the ones with sound; audio in private stream 1 after a 4-byte
  header `ff a0 00 00`, Sony "SS" format: `SShd` type 1 = 16-bit LE PCM, 48 kHz, stereo, 0x200-byte blocks per channel;
  the two menu loops have no audio). Where: mission intros 07100001/05/09/0c/11/14/18 (`ResetMap_LevelToLoad` adds
  0x100000 to 07000001-16, 0700001b -> 07100018; codes without a file, 072xxxxx and the other 071xxxxx, fail
  `UsingMovie` and `GF_MOVIE` pops at once), outros 07300004/08/0b/0d/13/17/1b (`LevelToEndTo`, then the results),
  07370048/07380048 EA/MGM (`P_INTRO`, not skippable), 073b0048 Esthero (`P_ESTHERO`, skippable after 180 ticks; also
  attract), 073a0048 attract, 07390048 the DAD trailer (AV Options), 073f0048 making of (`P_WINGAME`), 07330048 main-menu
  loop and 07350048 the other pages' loop (page record's third field; `Menu_PlayMovie(movie, 0, 1, 0, 0, 0)`); names from
  `P_FMVTEST_Handler`. The game's functions are ported (`UsingMovie`, `psiStart/StopBackgroundMovie`,
  `psiDecodeBackgroundMovieFrame`, `psiSwapBGFMVFrame`, `changeMaster/InputVolume`, `StartUp/Decode/CleanUp/SwapBGFMV`,
  `audioDecCreate/Start/Reset`, `strFileOpen/Close`, `PS2UploadBGMImage`); below them the PC player replaces libmpeg, the
  IPU and the IOP: `PssFile` demuxes in 64 KB reads; the video goes to the system's MPEG-2 decoder MFT (`MFTEnumEx`
  `MFVideoFormat_MPEG2`, the first that activates: on this machine Microsoft's "MPEG2VideoExtension" Store package; the
  in-box `msmpeg2vdec.dll` refuses activation with `c004f011`; none = the movie does not open, like a missing file), NV12
  -> BGRA with the IPU CSC coefficients (BT.601 limited, /128), ~0.75 ms per frame on the CPU; the PCM goes into the
  SPU2's core-0 data input (`Spu2::input_push`, `BVOL` = `MovieVolume` x 100 = 0x2710 = 0.305 at the default volume, dry
  and wet by `MMIX` 0xff0) and out through `audio_out`. `main.cpp` with a movie playing runs the logic only on even VBlanks
  (30 ticks/s, like the PS2 `main`) and calls `psiDecodeBackgroundMovieFrame` before each tick (`psiSwapBGFMVFrame`
  between ticks). Sync (port-only): every video frame `movie_image` shows frame `floor(heard / 1600)`, heard = movie
  samples the SPU2 consumed minus the XAudio2 queue (`audio_out_queued_frames`); silent movies (or after the sound ends)
  use the SPU2's own clock (800 samples per VBlank); the movie ends with its video (the tick whose clock passes the last
  frame). The renderer draws the frame first (`Renderer::set_movie_frame/set_movie_background`: target black, image in
  the 4:3 PS2 screen, Catmull-Rom), the game's 2D on top (subtitles from the movie's script, created by `ResetMap_Load`
  with subtitles on); `psiDrawFogPass` skips with a movie (PS2). Modern: a page's background loop replaces the title art
  behind the blur (`FrontScreen::set_movie`, `ui::kMovieImage`); a full-screen movie hides the Modern screen and frees
  the input (Start skips, `Menu_StopMovie`). Keyboard input reaches the game during `GF_MOVIE` (no player, no menu).
  `[video] movies` (default on), `--movies/--no-movies`; captures and `--bench` start **without** movies (old captures are
  pixel-identical) unless `--movies`; `--movie-frame <n>` (with `--movie <hash>`) captures at a movie frame (deterministic:
  the clock is the SPU2's, one frame per tick), `--intro-movie` makes `--level` request the movie code. Note: the
  `--input`/`--wait` clock (ticks/60) runs at half speed during a movie (30 ticks/s). `main.cpp` no longer treats the
  `GF_MOVIE` pushed in the middle of `ResetMap_Load` as the loaded level. Report line `filme ...` (and `filme N` in
  `frontend_moderno`), `--bench` adds `filme ... cpu_ms por_quadro | decodificacao media max`, `NF_MOVIE_LOG=<file>` logs
  the clock per video frame. Divergences: no game-heap allocation (`AllocateMovieMemory`), the sound starts with the first
  frame (PS2: after 0x6000 bytes reach the IOP), no 07360048 trap. Tests: `tests/test_movie.cpp` (demuxer on a hand-built
  stream, SS audio, units, clock, CSC, SPU2 input, the player's end/loop/BVOL/refusals with a hand-built PSS and no
  decoder, the menu and `GF_MOVIE` skip rules), `modern_front_title_and_warning`.
- The user's disc (`platform/disc_source.h`, `disc_pack.h`, `ui/modern_setup.h`, `render/setup_app.h`, header comments;
  docs/port.md "Instalação fácil"; port-only): every read of the disc's files goes through an `nf::DiscSource` (never a
  path into the data folder): `ACTION.ELF` (`install_game_tables(const DiscSource&)`, `FilesBin(disc)`), `FILES.BIN`
  (`psiFileSetup(DiscFile)`), `PS2\` (`sfx_iop_set_disc`), `MOVIES\30_FPS` (`movie_set_disc`; `PssFile::open(DiscFile)`),
  `DRIVING\`/`DRIVING.ELF` in the driving tools (`drv::Bigf::open(disc, path)`). Paths are the disc's, '/'-separated,
  case-insensitive, without ";1". Sources: a folder (the repo root for developers), an ISO 9660 image (`parse_iso9660`:
  files are contiguous extents; no UDF), a DVD drive (a folder at `X:\`; `Mount-DiskImage` behaves the same), the
  personal copy (the files appended after the exe's PE image with an index and a 64-byte footer, `NFPC-DISC-PACK-1`;
  `--pack-disc <src> [--out x.exe] [--pack-no-movies]` or the setup screen; ACTION.ELF, SYSTEM.CNF, FILES.BIN, PS2\,
  MOVIES\ and the port's `hd_ui` as `PORT/HD_UI/`, extracted to `%LOCALAPPDATA%\Nightfire PC` when the copy runs alone;
  never written inside a git working tree). The version is `SYSTEM.CNF`'s BOOT2 (`SLUS-20579`; `nightfire_disc()` in
  `game/boot.h`); other versions are refused with the reason; a folder without SYSTEM.CNF but with ACTION.ELF and
  FILES.BIN is accepted unverified (the old way). `--disc <iso|folder|drive|exe>` (`--data` is the old name) alone;
  otherwise `find_disc`: the personal copy (itself), the exe's folder (files or an `.iso`), the cwd and the 3 folders above
  the exe (developers), `[game] disc` in the ini, the CD/DVD drives. Nothing found (or `--setup`): the Modern setup screen
  before the game (window and renderer are now created before the data is read; `IFileOpenDialog` on its own STA thread;
  drives rescanned every 2 s; the choice goes to `[game] disc` only when the ini may be written). Captures: `--setup-pick`,
  `--setup-input`, the report's `configuracao` line; the capture report's `disco` line (kind, location, origin, version).
  The HD texture pack defaults to `textures\SLUS-20579\replacements` next to the disc (its folder, the ISO's folder),
  then next to the exe and above, then the cwd (with a DVD drive pass `--textures` or keep it next to the exe). With the
  data in a folder everything is exactly as before (checks and captures identical); from the ISO, the mounted drive and
  the personal copy too (2026-10-12).
- Release zip (`tools/make_release.py`): x86 `nightfire.exe` + `nightfire.pdb` (the default), x64 as `nightfire64.exe` +
  `nightfire64.pdb` (CMake names the x64 PDB so; experimental in the readme), `msvcp140.dll` + `vcruntime140.dll` (x86 is
  `/MD`; x64 is `/MT`), the tracked `hd_ui` files, `tools/release/README.txt` and `LICENSES.txt`; it refuses game files
  and scans for the git account name/e-mail and the Windows user name. Release executables embed only the PDB file name
  (`/PDBALTPATH:%_PDB%`); `build.bat` points TMP/TEMP at `build\<config>\tmp`, or the PDB would carry the linker's
  `%TEMP%` paths (with the Windows user name). The zip goes to the main checkout's `out/release/`; nothing is published.
- Crashes: `nightfire.exe` and `nf_loader_check` install `nf::install_crash_report()` (`platform/crash_report.h`); an
  access violation prints the call stack with file and line (Release builds carry a PDB).
- Static constructors: tables filled at PS2 boot by GCC global constructors (`weapon_data`) are never
  stored in the repo. `install_game_tables` (`game/boot.cpp`) runs the ELF's own `__do_global_ctors`
  in `nf::MipsMachine` (`data/mips_interp.h`: decoded R5900 subset, EE FPU rounding toward zero; any
  unknown opcode or call to a function that is neither allowed nor hooked is an error) and copies
  the tables by symbol name, checking sizes (`kTables` in `game/static_init.cpp`; add a row to copy
  another table; `TableCopy` also takes a second code field, a pointer field into another copied
  table, and a range ending at another symbol). Code pointers in copied tables go through
  `Engine_ResolveCode` (`engine/code_resolve.h`: each module's resolver, mangled ELF name -> port
  function, or 0), never as PS2 addresses. `nf_loader_check` prints what each constructor writes.
- Not-yet-ported callees are stubs that call `pending_call("Name")`; `nf_loader_check` prints the
  counts, which is a good guide to what to port next. Places where the original would touch an
  invalid address are guarded and counted in `g_parsemap_guards` (all zero on the real data).
- `GameState` (88 bytes) is `GAMESTATE_tag` in `engine/gamestate.h`; add fields there as they are
  identified (the old `GameFlowSkipDraw` is `GameState.skip_draw`).
- Code layers (`tools/layers.py`): logic 68.5%, logic with inline VU0 15.1%, platform 7.1%,
  runtime 3.9%, Sony SDK 5.4%. The engine's platform interface uses the `psi` prefix; the logic
  calls 106 platform functions.

## Tooling gotchas

- Long bash heredocs containing quotes sometimes fail with `unexpected EOF while looking for
  matching '`. Write the script to a file with the Write tool and run it instead.
- Arguments with `|` passed to `analyzeHeadless.bat` are interpreted by `cmd`: pass one regex per
  argument (`"re:^Vec_.*" "re:^Mat_.*"`), not an alternation.
- Run `build.bat` through PowerShell as shown above; `cmd //c build.bat` from Git Bash can fail to
  find the script.
- To check a rendering change, build the previous commit in a `git worktree` (its own `build.bat`),
  render both with the same `--cam x,y,z,tx,ty,tz --screenshot`, and compare with
  `python tools/pngdiff.py a.png b.png`. Put the camera inside the map: portal visibility hides
  almost everything from outside. `nf_loader_check` prints each level's player start
  (`jogador=x,y,z frente=fx,fy,fz`); the eye is 1.6 above it.
- An exception escaping a test is caught by `nf_tests` and reported as a failure; an access violation prints the call
  stack (`nf_tests` installs `nf::install_crash_report()` too).
- `nf_tests` starts every test with a disconnected `ScriptedPadSource` (`tests/test_main.cpp`): a real controller
  plugged into the machine used to leak into the tests' actions (random failures) and could rumble.
- `out/clean.sh` used to cut decompiled functions at 400 lines (fixed); re-read anything long that was
  decompiled before.
- The viewer server (`.claude/launch.json`, port 8137) is stopped when the session restarts; start it
  again with the preview tool before using the browser pane.

## Status (update this section with every milestone)

Done:
- Formats: `FILES.BIN`, level packs, map items, textures, static and skinned geometry, placement,
  object types, skeletons, skins, animation sequences, loadable sections (NIS cutscenes), level catalog.
- Exporters: levels, characters (with skeleton), animation libraries to glTF; web viewer.
- `nightfire.exe`: D3D11 level renderer, any resolution, Hor+ FOV, borderless fullscreen, raw mouse,
  GameFlow state stack and PS2-style logic pacing, entity resolution through the ported hashtable.
- Decompiled and tested: all of `Vec`, `Mat`, `Quat`, `RotMatrix*`, `ATAN2_APPROX`, `PS2Sinf*`;
  the memory manager (`Mem_*`, plus `psiMem_Init/Info`); `hashtable_*` core;
  `GameFlow_GetState/PushState/PopState`, `GS_SetRefreshRate`, `psiGet/InitTimeIn100ths`, `amemcpy`;
  the original level loader: `LoaderLoad`, `LoaderProcess`, `LoaderShrink`, `Loadable*`, all
  `parsemap_*` handlers except `parsemap_create_dynamic_objects`, `parseentity_*`, `build_alloc_*`,
  `Plane_PlaneEq`, `BIN_Get*`, `psiFile*`; `ResetMap_Load`, `ResetMap_GameInit`,
  `GetLevelToLoadFromMovieCode`; `psiCreateMapTextures`, `psiCreateEntityGfx`, `psiResetResources`
  and their helpers (`crc32buf`, `GetMinWidth`, `FillTextureDMA`...); the object core: `LList_*`,
  `control_*`, `Control_*`, `build_FindCel`, `build_LinkToRoom`, `Controls_StraddleTest`,
  `View_RotTransMatrix`, `View_RotTransScaleMatrix`, `View_AddSkyObj`,
  `parsemap_create_dynamic_objects`, `DynamicObject_Create`, `Break_Create`, `Env_Create`; the
  collision core (`Collide_Pick/PickObj/Intersect/Jointy/SphereIntersect/RayIntersect/
  StraddleCels`, `Intersect_PointGeom/RayGeom/CylGeom/CylTriGeom/RayBox/Portal/PointOOBox`,
  `ASH_*` box and triangle helpers, `vecutil_*`, hit heap): spheres, rays and the capsule collide
  with level geometry and with object geometry such as doors,
  `build_link_objects_to_rooms`, `QuickSort` and its comparators; the view module: cameras
  (`Camera_Create/Enable/Reset/Update/CheckLocation/UpdateAll/CreateCameras/CalcViewAngles`),
  portal visibility (`Vision_*`, `ClipPortalWithPlane`, `Cel_ObjectLeftCel`), draw lists and drawing
  (`View_CaptureScene/AddCels/AddObjects/AddForcedObjects/DrawObjList/DrawCelGlist/
  DrawObjectGlist/DrawSky`, `Game_Draw`), `Concat`, `Rand_*`; `Player_Start`,
  `Player_AddNewStartPos`; the whole `Door` module and `build_LinkDoors2Portals`; `Spline_Interp`,
  `Spline_Eval3D`, `KeyFrame_Interp`; the game loop: `GameFlow_Main`, `Game_Run`, `bootup_bootup`
  (partial), `GS_IsPaused`, `Reset_MapLoadSettings`, `ResetMap_LevelToLoad`,
  `control_movement_object_handler`, `control_handle_cel_change`, `Collide_Update`, `Trigger_Calc`,
  `Lights_CalcAmbientLight`, `ClrStep`, `View_SetDrawInNoViews`, `psiPreGame_Run/PostGame_Run`,
  `psiMovieFinished`; the input module (`Input_*`, `StickCompensation2/3`, `MapAnalogStick`); the
  player core: `Player_Init`, `Player_Update`, `Player_SSWalk/SSCrouch`, `Player_Move`,
  `AccelFunc0`, `Player_Aiming`, `Player_ViewClamping`, `Player_MonitorAir`, `Player_HandleJump`,
  `Player_InWater`, `Player_Collision`, `Player_CollisionHandler`, `Player_FeetOnPoint`,
  `Player_DealWithObjHit`, `Player_Activate`, `Player_PositionCamera`, `Player_GetHeadPos`,
  `Player_SetCamMode`, `Player_ChangeSubState`, `Player_StandAtNewPosition`, `Player_InShadow`,
  `Player_SetHealth`, `PlrStat_OkToUpdate`; `Camera_Set/SetUpdator/SetFade/ApplySwing/SetToPlayer`,
  `View_SetDrawIn*`, `MulMatrix0`, `Collide_LineOfSight`, `build_PointOnFloor`,
  `Control_InheritVelocity`, `Control_Plr2Ind`; the animation module (loading, scripts,
  sequences, frame decode and blending, `AnimFrameResolve`, sets, `AnimObjectNew/Update/Delete/
  Draw/InitialPose`, `AnimGetBoneWorldTrans`, `psiBuildMatrixPalette`, distance tables,
  `PlayerAnim*`) and `hashtable_hashcode_to_celglist/celglist_to_hashcode/
  set_object_to_entity_gfx`; the player's weapons (`Player_InitWeapon`, `Upgrade_Weapon`,
  `Player_EquipWeapon/EquipAmmo/InitAmmoWeapons/CheckWeaponsLoaded/WeaponNone`, `Player_RamLoad/
  RamSave`, `Player_SetWeaponAnim/SetWeaponAnimObj/PositionGun/WeaponRecoil`, `Player_Weapon`,
  `Player_WeaponSelect/WeaponChange/WeaponHasAmmo/GetBestWeapon/IsBetterWeapon`,
  `Player_SetFiringAnim`, `Player_WeaponFiring`, `Player_WeaponInitBullet`, `Player_ReloadAmmoType/
  HandleHasNoAmmo/AmmoInGun/RoundToFire`, `Player_GetAimingPoint`, `Player_Zoom`, `Player_AutoAim`,
  `Player_MuzzleFlash`, `Player_ResetWalkAnim`, `AnimScriptAddSpeed/AddReverse/SetPlayDirection`,
  `AnimObjectSetSleeveType`); the whole `Bullet` module (bullets, grenades, mines, homing
  projectiles, casings), `Debris_Update`, `Effect_RicochetProb`, `Rand_FRand_MVar2`,
  `vecutil_cartesian_to_spherical_acc`; the impact effects (`Effect_*`, `Gas_Init`/`gases_smoke`,
  `Swoosh_*`, `Debris_Create*`, `Draw_MuzzleFlash`, `DList_*`), the emitters (`Emitter_*`,
  `ColourGetValue`), `View_SetupRenderModes/2/_Tweak`, `psiDrawParticleList`; the light module
  (`Light_*`, `Lights_CalcClosestLights`, `Corona_Create`, `Flicker_*`), `psiLight_SetLights`,
  `psiPreGameRun`, `psiDrawFogPass`, `psiPostSolid`, `psiSetTweakA`; the AI core: `Drone_Create`,
  `NDrone2_CreateObj`, `Drone_PreLoad_Init/LevelReset/PostLoad_Init`, `NDrone2_PostLoad_Init`,
  `NDrone2_DefaultInit`, `NDrone2_DoModeSettingsOLD/NEW`, `NDrone2_DoTypeSettingsOLD`,
  `NDrone2_GetDroneTypeAttackTypeFriend/GetDTYPENEW/ChangeToAttackMode`, the 33 `NDrone2_init_DMODE_*`
  and `NDrone2_initDTYPE_*`, `behaviour_util_*`, `NDrone2_SetupGroups/AddToGroup/Enable`,
  `Drone_Control`, `NDrone2_PreDroneControl/ControlSTANDARD/PostDroneControl/DoAnimation`,
  `Drone_InitComms`, `Drone_PauseNoDraw`, `Drone_Delete`, `Drone_GetOpponentInfo`, `Drone_DCVfromOBJ`,
  `DroneInit_*`, `DroneFunc_SetAsAttacking`, the drone animation layer (`DroneAnim_*` used by the
  above, `DroneData_*`, `DAnimFunc_Run`, `AnimScriptCallOnEvent/OnStop`, `AnimScriptSetAnimRotation`,
  `AnimObjectSetHeadTracking`), the state machine (`Drone_SM_*`, `NDrone2_ProcessStateMachine`),
  `ApplyMatrixLV`; 25 AI states (`NDrone2_DSTATE_Global/WaitSwitch/Disabled/Idle/Alert/InitPatrol/
  Patrol/CivilianInit/Civilian/CivilianPatrol/ReturnToPatrolPath/CivilianMission/CivilianMissionWait/
  AllyLeadInit/PartyGirlInit/PartyGirl/Death_Anim/DeathByExplosion/Dead/Fade/FadeFast/EnemyRunToPoint/
  DeleteMe/JustStand/WaitForever`) with their routes (`NDrone2_AssignAIPath/InitAIPath/FollowRoute/
  UpdatePatrolRoute/MoveToGoalPosition/FindRunToPoint/ClosestAIPoint/NearestNodeDCV`) and behaviour
  helpers (`drone_behaviour.cpp`: idle/walk animations, patrol talk, alerts to all, death, explosives,
  forced attack, combat music count); the whole `Trigger` module, `Init_SwitchChannels`; the AI navigation network (`AIPath_*`, `AIBounds_*`,
  `AINetwork_*`, `LinkCreep_*`, `Drone_CoverCornerNode/CoverLowNode/AIPoint/AIVolume_*`,
  `Drone_IsCoverNodeUsable`, `Drone_ProcessCoverNodes`, `Drone_PointInAnyAIBox`,
  `NDrone_InitDoorNodes/KickNodes`, `NDrone2_MoveTest*`, `NDrone2_FeetPos/FindCel/NearestNode`); shared AI
  helpers (`DroneAnim_CallAnim/CanDoAnimState/CurrentMoveState`, `Drone_AlertStatusSet`, `Drone_Message`,
  `NDrone2_ObjectIsArmed/GetHeadPos/GetHeadPosOld`, `Drone_IsTalking`, `DroneFunc_GetBonePos/
  SetMissionFailReason`); the AI's vision and perception (the whole `DroneVision` module,
  `NDrone2_FindOpponent/SetOpponent/SetOpponentAimPos/CanSeeObject`, `Drone_ProcessOpponents`,
  `Drone_BuildDynamicAwarePoints`, `Drone_VisibilityForPosition`, `NDrone2_SetupAlertTarget/
  DroneAlertToObject/DroneAlertToPosition`, `DroneFunc_HandleSoundAlerts/ReactionTime/
  CheckAlarmRaised`, `NDrone2_PlaySFX/SpeechSFX`, `DroneWeap_GetWeaponInfo`); the AI's body (the whole
  `DroneMove` module, `NDrone2_Move/Steer/EvaluateObstacle/CanReachGoalPosition`, `SphereSphereSweep`,
  `NDrone2_Collision/DoCollision/DoGravity`, `Drone_FeetOnPoint`, `Drone_CollisionHandler`,
  `DroneFunc_NearPlayerCollision`, `AnimGetCollData`, `AnimObjectIsAiming/IsHeadTracking/SetAiming`,
  `NDrone2_AimTest/DoTracking/DoHeadTracking/DoHeadTrackingBehaviour`, `Hurt_GetDamage/GetType`,
  `Drone_BulletHit`, `NDrone2_DealWithObjHit/HitDamage/DoHitEffects/PunchImpact/BulletImpact/
  ExplosiveImpact/PainTalk/HitTalkExtra`, `DroneFunc_HandleImpact/SendHurtMessage/InjuredTalk`,
  `DroneAnim_LocationImpactAnim`); the AI's weapons (the whole `DroneWeap` module,
  `DroneAnim_PlayFiringAnim`, `NDrone2_CanMoveToRelativePos/SetAngleToObj/BondIsFacingMe`,
  `Drone_ModPlayerHitDamage/ModBulletDamage`, `BOT_getAggressionMul` (the single-player burst size
  uses it), `MulMatrix2`); the AI's combat (`drone_combat.cpp`: `NDrone2_DSTATE_Attack`, the
  combat states 0x57-0x5f, 0x61, 0x66-0x7a and 0x7f, `DroneFunc_FirstAttack/FirstSightState/
  FirstAlertState/AttackNoWeapon/CombatState`, `NDrone2_ChooseCombatMove/EvasiveMove/Can*` moves,
  `NDrone2_MoveToObject`, cover search `NDrone2_FindCover/CoverNodeOK/GetPosForCornerCover`,
  `DroneAnim_SetCombatMoveAnim/GetMoveQuad`, `NDrone2_AttackTalk/SeenPlayerTalk`; the
  cover states 0x8c and 0x93-0x9c with `DroneAnim_CoverAnim/ValidateCoverAnim` and the `CCModes`/
  `LCModes` tables from the ELF, `NDrone2_SetAngleToCover/SlideToPosition`; the snipers 0x29-0x2c,
  `GrenadeThrow`; doors and route-node actions: `NDrone2_ReachedDestNode/OpenDoor/DoorIsOpen/
  FindElevatorDoor/KickObject/MoveTo_RouteNode`, states 0x81-0x84, `DroneAnim_SetScript`; going to
  points and searching (`GoToGoalPosition` and the ally/hostage variants, `AlertToPosition`,
  `SearchArea`, `Investigate`, `EnemyMission`), the reactions to sights and sounds (0x9d-0xa5 with
  `NDrone2_SetSoundAlertRoute/SetDroneAlertRoute/SetAngleToAlert`), the civilian guards
  (`CivilianGuard`, `CivilianDoorGuard` with `NDrone2_SetDronePartner/DoorGuardHandler`), the alarm
  (`RunToAlarm/PressAlarm/DonePressAlarm`, `NDrone2_FindAlarmPoint`) and scared civilians; the ninjas
  (0xa6-0xb3), `SpaceDrake` and the astronauts of 0700001b (0xbd-0xc1), `NDrone2_LogDroneDeath`; the
  impact, stun, taser, dart, smoke, surrender and knock-out states (0x3f-0x43, 0x46, 0x4a-0x55, 0x7b-0x7d,
  `DroneFunc_RecoverTime/AnyoneNear`, `NDrone2_CheckUnsurrender`), the truck driver (0x32-0x35), abseiling
  (0x8e-0x92), running from explosives (0x86-0x88) and a few small ones (`ElevatorJumper`,
  `HoldItRightThere`, `FailMission`, `StandBlind`, `HangUp`), the interrogation (0x39-0x3e), the allies
  who lead or follow the player (0x1d-0x27) with `NDrone2_ReFindMissionPath`, Kiko (0x19/0x1a), the
  hostages and their takers (8-0xf, `NDrone2_SetupHostageKiller/SetupHostageExecute/
  MonitorTalkToMissionObj/TalkToMissionObj`, `DroneFunc_HostageSaved`), the ambush (0x37/0x38,
  `NDrone2_FindAmbushCover`), `CastleChatGuard1` and the test states 0xb7-0xba: every state but the
  bots' and `PlayScript`); the player's damage and death (`Player_HandlePain/CheckForDeath/
  HandleDeath`, `MP_RegisterBulletHit`) and `Camera_Shake`; the whole `Pickup` module
  (`Pickup_Create/Update/Handler/CreateFromSet/CreateSimple/MakeRandomWeaponSet`),
  `Player_EquipKey` and `Check_Target`; the destruction modules: `Break_Update/ApplyDamage/DoBreak/
  Kill`, `ShatterPolyRecurse`, `InitShards/UpdateAllShards/DrawAllShards`, `Destroy_Create/Update/
  CollisionHandler/Smash`, `Hurt_Create/Update`, `Explode_Create/Update/Propagate/Delete/
  CollisionHandler`, `Debris_Create_Loop`, `Drone_BrokenObject`, `Drone_ExplosiveHit`, `Player_Hurt`,
  `control_ClosestPlayerPnt`, `Collide_FilterBullets/GetDamageNObjects`, `GT_ApplyDamage`,
  `Sub_ApplyDammage`, `Sensor/Monitor_ApplyDamage`, and their platform side `psiCreateBreakableData/
  GetTriList/CreateShard/DeleteShard/DrawShard/SetShardRenderStates/FinishedObjectShatter`,
  `GetTextureFromDMA`, `psiFont_SetTexture` (PC); the scene scripts: the whole `Script` module of the levels
  (`Script_Load/Run/Update/Interp/GetInterp/FFwd/Free/Play/...`, every stream start/end, `EventHandler`,
  `CreateEntity`, `KillStream`, `GetSplineWeights`), the script player (all 17 `SP_*`), `Switch_*`,
  `FuseBox_*`, `Sensor_Init`, `NDrone2_DSTATE_PlayScript` and the helpers `Stack_*`,
  `Camera_PushStates/PopStates`, `AnimGetFirstTrans`, `AnimGetNDiscreteObj`, `Player_Disable/Enable/
  Cam2Mode`, `Drone_EnableAll`, `Drone_CoderCreate`, `Input_RumblePause/ClearAllActions`,
  `PS2_ResetFrameCount`; the whole HUD (`HUD_*`, 43 functions) and mission (`Mission_*`, 11) modules,
  `View_3DPoint2Screen`, `Player_AmmoIndex`, `PlrStat_ResetForMission`; the 2D layer: all of `Txt_*`,
  `Font_*` (with `__Font_DrawText`), `Sprite_*`, `Text_*` (both `Text_Init`s), `TextBox_Update`,
  `Text_SendMessage`, `Text_SetupChar`, `CirBuf_Init/Add`, `LList_InsertAt`, `hashtable_set_sprite`,
  `View_DrawSprites/AddSprite`, `psiDrawSprites`, `psiStartSprites/EndSprites`, `Sound_DoSubtitle` and
  the subtitle static constructor; the AI spawners (all of `DroneSpawner_*`, `Drone_PositionVisible`);
  the menu system: `MenuManager_*` (9), `Manager_*`, `Handler_HandleMessage`, `Page_*`,
  `Window_*`, `Process_*`, `LinkedList_*`, `Component_*`, the menu scripts (`Script_*` at 0x002201c8 and
  after), `Label_*`, `Button_*`, `Checkbox_*`, `Scroll_*`, `List_*`, `Radio_*`, `Memo_*`, the `Menu_*`
  core (`__Menu_SendMessage`, `__Menu_Send*`, `Menu_FindControl/GetControl/GetFirstControl/Update/
  InputAction/CreateSprite/...`), `GS_PauseGame/PausePlayer`, `P_PAUSE_Handler`, `C_GCPAUSE_Handler`,
  `P_ENDMISSION_Handler`, `P_NFRESULTS/NFBONUS/NFSTATS_Handler`, `C_LBMSGOPTIONS_Handler`,
  `Menu_CreateOptionBox/UpdateOptionBox`, `Menu_PlayerHasQuitOrRestarted`, `Menu_GetLevelIndex`,
  medals and rewards (`Menu_GetBonus/SetBonus/SetLevelBonus/GetLevelBonuses/HasMedal/GetBestMedal`,
  `PlrStarts_*`, `PlrStats_DoneBetter`, `Set_Upgrade`, `ResetUpgrade`), `Menu_UpdateDefaultCodename`,
  `Menu_ChangeControllerStyle`, `SeparateNumber`, `Timer_Seconds2String`, `Input_RumbleStop`; the pad
  side of the platform: `psiInput_PollDevices`, `psiInput_MapInputs` (the 8 button styles), `psiInput_Init`,
  `MapPlayerToSlot`, `psiInput_RumbleStart/RumbleStop`, `psiStopAllRumble`, `psiFlushAllInput`,
  `psiInputReady`, and `Input_RumbleStart`, `Input_ChangeControllerStyle`,
  `Menu_DisplayControllerStyleList`; the player statistics and score (all of `PlrStat_*`, `PlrStats_*`,
  `PlarStat_*`); the front end's single-player path (`P_LANGUAGE/INTRO/ATTRACT/START/PARISENUM/ESTHERO/MAIN`,
  `C_GONIGHTFIRE/GOMULTIPLAYER/GOCODENAMES`, `C_LANGUAGE`, `P_NFSELECT/NFDFCTY/NFMAP`, `C_SBNFCN/SBNFDFCTY/SBNFMAP`,
  `P_CNSELECT/CNMENU/CNNAME`, `C_SBCNSELECT/SBCNOPTIONS/KEYBOARD/KEYPAD`, `P_PS2MEMCARDINIT`, `C_LBERROPTIONS`,
  `P_FMV/TRAILER/FMVPLAYER/WINGAME/CHEATMEDAL`, `PS2_DoCardFlow`, `Menu_UpdateMessageBox/EnumSaves/FlushEnum/
  PutCodenamesIntoControl/MapDefaultCodename/IsValidCodename/SelectCodenameInControl/UpdateWheel/
  UpdateCodenameWheel/StartIris/PlayIris/ChangePageCloseIris/AddItemsToControl/SelectItemInControl/GetItemFromHash/
  SetNightfireStatus/WillRunMiniMission/RunMiniMission/IsBotGood/PlayMovie/StopMovie/UpdateMovieCounter`), the saved
  profile (all of `LS_*`, `BIN_PushBits/PullBits`) and the memory card (`HandlePS2CardState`, all 23 `MemCard_*`,
  `InitaliseMemoryCard`, `PS2PollMemoryCard`, `psiSaveData/LoadData/SavingData/LoadingData/DeleteData/DeletingData/
  DoBootupCheck/StartSaveEnum/GetNextSave/EndSaveEnum`, `AsciiString2Sjis`, `Ascii2Sjis`, `psiSet/GetScreenPos`);
  the dossier (`P_DOSSIER`, `P_DSRECORDS/DSREWARDS/DSGADGETS/DSWEAPONS`, `C_SBDOSSIER`, `C_SBDSGTSCROLL/DSWPSCROLL`,
  `C_RBDSRECORDS/DSREWARDS`, `Menu_GetObjectUpgradeLevel`), the 18 debug-page cheat checkboxes (`C_CHCH*`) and
  `Player_ZeroG`, `Player_SSScanMode`; the codename's option pages (`P_CNCONTROLS`, `C_RBCONTROL`, `P_CNOPTIONS`,
  `P_CNMPOPTIONS`, `P_CNAVOPTIONS`, `P_SCREENADJUST`, `C_SBSCREEN`, `P_CREDITS`, `Menu_SetupCredits/InitCredits/
  KillCredits`, `Menu_DisplayControllerStyle`), `Menu_SpecialCodenameCheck`, `Menu_UpgradeCheat`, the debug subpages
  (`P_TWEAKS`, `P_TWEAKS2`, `P_NIS`, `C_NIS`, `C_LBPMMAP`), `SFXMusicSetVolume/GetVolume`, `SFXSetVolume/GetVolume`,
  `SFXSetMode/GetMode`, `psiAdjustScreenPos`, `psiRestoreScreenPos`; the sound module (all of `Sound_*` and `SFX*`,
  `Music_Event`, `UpdateMusicalEvents`, `PickRandomStealth/Combat`, the 21 `UpdateMFX_*`, `SoundTrigger_*`,
  `MusicTrigger_*`, `NDrone2_DeathTalk/HandleTalking/PowerstationTalk/BondTalk`, `DroneFunc_DoBondTalk`,
  `Drone_MayhewTalk`, `AnimFacialAppend`), `Hint_Create/Update` and, outside ACTION.ELF, 152 functions of the IOP sound
  driver `SFX.IRX` (`// [irx]` markers); the campaign's objects: ladders, the grapple, wires (rope and zipline),
  creep walls, touch icons and third-person cameras (`Ladder_Create`, `Player_CollLadder/Climb`,
  `Grapple_Create`, `Player_SetGrapplePoint/SSGrapple/GrappleSetRope`, `Wire_Create`, `Player_CollWire/Wire/Zipline`,
  `CreepWall_Create`, `Player_CollCreepWall/Creep`, `ThirdIcon_Create/Update`, `ThirdCam_Create`, `camera_tracking`),
  locks and the Q-Worm (`Lock_*`, `Locks_Init`, `PCQWorm_*`, `Player_UnEquipKey`), camera subjects (`CamSubject_*`),
  gun turrets (all of `GT_*`), the whole `Copter` module with `Intersect_ConeSphere`, and the simple
  scripts (`SS_*`); the faces (`AnimFacialUpdate`, `AnimFacialBlend`, `AnimObjectHeadTrack`, `Drone_AlertStatus`,
  `Ang_RotateToBearing`, `Plane_PutPntOnPlane`);
  the movies' platform functions (`UsingMovie`, `psiStart/StopBackgroundMovie`, `psiDecodeBackgroundMovieFrame`,
  `psiSwapBGFMVFrame`, `changeMaster/InputVolume`, the `*BGFMV*` and `audioDec*` the PC player replaces); the security
  system and the remaining gameplay objects (`Sensor_*`, `Monitor_*`, `SSys_Msg/Monitor/Init/RamLoad`, `Searchlight_*`,
  `Mine_*`, `Shooter_*`, `InverterTrigger_*`, `Apocalypse_*`, `GunImp_*`, `Create_SpaceLaser`, `SpaceLaser_*`,
  `Create_SpaceMissile`, `SpaceMissile_Update`, `Player_SetFlashBang`, `PositionBeam`); the gameplay polish
  (`AnimObjectAimAt`, `AnimGetBoneWorldTrans2`, `Check_AutoAim`, `Player_LaserPointer`, `Player_SetWatch`,
  `Draw_LaserBeam`, `Draw_TaserBeam`, `FractalThing`, `Camera_Shear`, `Env_WaterRipple`, the whole `RB` module (19),
  `Collide_CylinderIntersect`, `Collide_PlaceObject`, `GT_DeployMiniGun`: the Collide module is complete); the
  presentation (`psiFadeView`, `psiPreDraw`, the load screen `ResetMap_GenLoadScreen`, `ResetMap_LevelCode2Img`,
  `psiStart/EndLoadingScreen`, `psiAllocateLoadingBlobs`, `CacheLoadingBlobs`, `ResetLoadingBlobs`; the weather
  `InitDrops`, `UpdateDrops`, `AddDrop`, `DrawDrops`, `Env_Update`, `Env_Reset`, `RainBox_*`, `InitRainBox`,
  `Lightning_*`, `Cloud_Create`, `cloud_update`; the scenery `Ripples_*`, `LeafGen_*`, `Leaf_*`, `Env_Tree_*`,
  `rotor_init/update`, `OneSided_*`, `Env_Breath`, `Particles_*`, `psiCreateParticles`, `psiParticleUpdate`)
  (2103 functions, 79.25% of the bytes; see `python tools/decomp_status.py`).
- Lighting and fog as on the PS2: the VU1 microcode is decoded (`tools/vudis.py`, formulas in
  `docs/formats.md`) and the renderer runs its routines per vertex: the levels' baked vertex
  colours, the tweak as the ambient term, the two nearest lights (`1/d² - 1/r²` times N·D, from the
  game's light list: map radiators, placed lights, shot flashes, flickering lights), the
  environment-map routine, skinned meshes with the first bone's normal, and the fog pass that
  `psiPostSolid` draws (Z-based, per-level colour and `Ps2Far1` from `psiPreGameRun`). V,
  `--debug-shading`, `--no-lights`, `--no-fog` switch parts off.
- Shot effects: muzzle flash, smoke and dust by material, bullet holes on rooms and objects, debris
  and trails with the game's own code; `EffectInfo` comes from the ELF like `weapon_data`. Particle
  emitters (sparks, flames, glows; 18 definitions per level, placed ones in 07000002/14/15/46) are
  drawn as camera-facing sprites. The renderer honours each mesh's own GS state (`ALPHA_1`, `TEST_1`,
  `ZBUF_1` from the packet: additive flash and smoke, subtractive bullet holes, translucent paper and
  glass, soft shadows) and the per-draw tweak colour.
- The ELF's static constructors run in a MIPS interpreter at boot: `weapon_data` and `ammo_data`
  come from the user's ACTION.ELF (95 weapons with a skin, 26 ammo types).
- AI characters exist: every campaign level creates its enemies, civilians and hostages with the
  game's own code (0 to 58 per level at load at difficulty 2, plus 270 spawner templates), with their
  skins, weapons, modes and the idle animation, standing on the floor; their state machines run the
  ported states: most wait for a switch channel (hidden until a trigger turns it on), others stand,
  patrol (following their patrol path's route and turning toward it while the animation walks them),
  act as civilians or die. They
  have a body: a capsule that collides, gravity, steering around each other and the player, head
  tracking and aiming, and bullets hit their joint boxes, push them and send the hit message (health
  only drops in states that handle it). They see:
  the game's vision finds the player, alerts its neighbours and passes shouts and sounds along, so
  snipers aim and fire and enemies attack: they close in on the player, fire standing or crouched,
  reload, dodge, roll, strafe, take cover behind the level's cover nodes, search, react to noises and
  bodies, open doors on their routes, surrender, interrogate, guard doors and run to alarms; ninjas,
  allies, hostages and their takers follow their own states (`drone_combat.cpp`). They shoot with the
  game's own weapon code (`DroneWeap_Fire`: bursts, fire rate, accuracy rules, muzzle flash, casings,
  grenades, melee hits, dropping the gun when they die): in 07000013 the lobby guards open fire within
  5 s (`efeitos marcas` ~95). `nf_loader_check` also drives one burst per armed enemy (`tiros_ia`,
  drone bullets hit the player on 07000004/12/13). Triggers work:
  touching, using and channel logic turn channels on, which wakes the drones that wait for them
  (`nf_loader_check` prints `estados_ia=state:count` after 2 s; the screenshot report lists `canais`
  on and `gatilhos`). Spawners work: 79 on the disc in 16 levels (07000001/05/06/08/0a/0b/0c/0d/11/12/
  13/14/15/16/1b/4a) take their groups (270 drones at difficulty 2) off the map at load and bring them
  back during play with the game's own rules (07000001: two of the five yakuza appear at random spots
  when the intro ends, channel 0xb, ~38 s; 0700000c/16 spawn 5-6 right at the start; with their
  channels forced on, 0700000d's spawners refill their groups wave after wave).
- Skinned models: all 582 on the disc decode (`nf_loader_check` `skin=`), the renderer skins them on
  the GPU, and animated objects draw through `AnimObjectDraw` with the game's own poses.
- Faces move: the 63 heads' 22 morph targets (block 0x2B) are summed on the GPU before skinning, with the weights the
  game's own face logic gives (speech and scene face sequences, blinks, expressions by alert level, eyes leading the
  head) and interpolated between ticks; characters turn their head and eyes toward what they look at
  (`AnimObjectHeadTrack`: 07000007's guests look at the player, or at the guard who talks). 07000001's intro NIS:
  Mayhew's mouth opens on the frame his voice starts ("Ah, Mr. Bond", 10.3 s), closes in the 15.0-15.5 s pause, and
  the lower-lip opening correlates with the voice RMS (r = 0.47; `tools/morph.py mouth`). Captures without a face
  are pixel-identical to before; with `--no-morphs` the faces' captures are too.
- HD textures from a PCSX2 replacement pack (`--textures`, key T), matched by reproducing PCSX2's
  texture and palette hashes.
- `nightfire.exe` draws what `Game_Draw` asks for camera 0, starting at the player start. At every
  level's start, against the previous commit (which drew every cel), renders are identical except
  where the game itself differs: the sky now surrounds the camera (with lightning, the sun and the
  rotating sky of `0700001b`), and editor markers the game never draws are gone. Doors are real
  objects placed by their path (identical renders except a few doors that now sit closed in their
  frame instead of at the placement), and transparent/additive draws blend (soft shadows, glass,
  fences). The game loop runs: doors open and close when activated and their rooms show
  through portals.
- You play as Bond (his body is animated and visible from the free camera): mouse and keyboard
  walk, turn, look, jump and crouch with the game's own movement, gravity and capsule collision
  (walls, floors, doors), and E opens doors through `Player_Activate`. Each level gives its own
  weapons; the arms and gun are drawn in first person with the game's animations (draw, holster,
  idle, fire, reload), and the player aims (RMB), switches weapons (the wheel, Z/Q) and gadgets (1-5, G), reloads
  (R) and fires
  (LMB: bursts, fire rate, clip and automatic reload). Bullets fly, ricochet and hit the level and
  objects with the game's own projectile code, leaving flash, smoke, holes and sparks, and the
  shot's flash lights the room and the arms. `nf_loader_check` fires 8 pistol bullets from the
  player's eyes on every level (`balas:`). `nf_loader_check` puts the player in front of every
  unlocked closed door and uses it (`usar=`; ~94% open, the misses are the placement heuristic) and
  reports where the player ends after 2 s standing still (`andou=`, `chão=`). The enemies' bullets
  hurt the player with the game's own rules (difficulty, armour) and he dies at 0 health (07000012:
  hit from ~6 s and dead at ~9 s standing at the start, the HUD going 7/7 -> 2/7; 0700000d, the old
  example, no longer kills him: its early attackers were spawner templates, now off the map at load).
- Items: every level's ammo, weapons, armour, keys and mission items are real objects (328 on the
  disc at difficulty 2), drawn by `Game_Draw` with the room tint and the near-camera pulse; walking
  over one gives it with the game's own rules (`nf_loader_check`: the player takes 1 to 15 per
  campaign level, the rest are armour he no longer needs or ammo already full) and a dying enemy
  drops its gun, which falls to the floor and can be picked up.
- Destruction: shooting a glass breakable (255 glass objects with 1358 triangles on the disc at difficulty 2) shatters
  it into translucent shards that hang for a moment, fly along the shot and fall (07000001: one pistol
  shot at the display case by the samurai armour makes 135); other breakables fall apart into debris;
  destroyables (189) take bullet damage, swap to their broken model, set their switch channel (after
  their delay) and may explode; hurt zones (64) follow their path when touched or switched on.
  Grenades, rockets and exploding destroyables explode: the camera shakes and the damage spreads to
  drones, the player, breakables, destroyables and other explosives (`nf_loader_check`: a grenade at
  the first breakable of each level breaks up to 14 objects and makes up to ~1300 shards). The explosion's
  own fire, flash, light and smoke come from its effect script (07000016: a hand grenade's fireball
  fills the corridor by the door 5 s after the throw).
- Scripted scenes: every type-7 item of the 36 packs registers (`cenas_fase`), 172 placed script
  players (214 with the switches' and fuse boxes'), 33 switches and 9 fuse boxes are created on the
  real data, and their scripts run with the
  game's own code: switch levers and alarm boxes animate when used (07000009: E on a garage alarm turns
  channel 0x7c on and every alarm on it follows), lifts and props move along their keys (0700004a's
  lift carries the player down the shaft; he no longer falls from the start), fuse boxes register
  their fuses, and the intro NIS
  cutscenes load their section and play with their own camera, characters and props, then give the
  player back at the scene's marker (07000001: Kiko and Mayhew for 35 s, then the ninjas' attack).
  Drones in state 3 play their entry animation script (07000003: a guard searching drawers).
- Text, subtitles and sprites reach the screen: the game's text (USATxt.dat), its three fonts with
  kerning, the subtitle sprites and the cinema borders of scripted scenes, speech subtitles (07000001: from
  ~11 s into the intro NIS, "Ah, Mr. Bond - at last..." in SerpLight with the outline) and the HUD, drawn
  as a separate 2D record in a 4:3 area of the window (full-width elements stretched to its edges).
- HD 2D layer (`--hd-ui`, F4): the HUD, messages and subtitles redrawn at window resolution: text in
  Bahnschrift through DirectWrite (with the game's colours, shadow and outline, wrapping and paging), and
  own SVG replacements for the crosshair, the message band, the 9 mm bullet stack and the gun-barrel health
  gauge (7 spiral blades + ring); other images fall back to the original texture with bilinear filtering;
  optional edge anchoring for wide windows; `--ui-inventory` lists and crops every 2D image. The menus too:
  every piece of the pause, mission-failed, YES/NO box, results and PC option pages is vector (panel with
  bronze frame, gold tab band, dark-red glass and help bar; silhouette; arrows, slider thumb, checkboxes,
  highlight; the results 007 and corner), crisp at 3840x2160, and the button symbols follow the device:
  PS2 vector symbols, the bound keyboard keys as key caps, or Xbox buttons (`[ui] prompts`, auto = last
  device used). The menu panel no longer deforms on taller pages (9-slice, also in the original 2D, where
  the game's own pages stay pixel-identical).
- HUD and mission: every level creates the player's HUD with the game's own code and tables (20
  panes in story mode, 7 on by default: ammo, health and armour, the three message panes, lens flare,
  items), and the panes draw every tick through the 2D layer: health and armour (the gun-barrel
  gauge turning red), the ammo count, fire mode and bullet stack, the red hit arrows fading with
  `pain_flash`, the crosshair (aim, scope panes), night vision/X-ray with the battery, the objective,
  info and item messages ("NEW OBJECTIVE", "Press Action to open door", "Picked up 20x 9mm rounds"),
  the blood curtain when the player dies (07000012 at `--wait 9`). The mission module
  runs the objectives (`nf_loader_check`: objectives complete in 07000006, 07000012 and 07000016
  during the game tests; an alarm fails 07000007), and death, failure and success follow
  the game's flow through the real menus (see the next item).
- Menus: the game's menu system runs from the level's menu file, drawn through the 2D layer with the
  game's components, fonts and scripts. Enter pauses the game and opens the pause menu (tabs
  MISSION/OBJECTIVES/CONTROLS/SCORE; continue, restart with a YES/NO box, quit; the objectives list;
  the controller style and look inversion) and closes it again (07000012: `--input "none:1 enter:0.2"
  --wait 2`); death or a failed objective opens "Mission Failed" (REPLAY/QUIT) over the paused game,
  and REPLAY restarts the level warm with `Player_RamLoad` (07000012: `--input "none:10 space:0.3"
  --wait 12.5` shows the menu; `"none:10 space:0.3 none:2.5 space:0.3" --wait 16` gives `recomecos 1`
  and full health); after a mission success the front end shows the results page (score, medal
  target, "Continue / Stats / Dossier"; `--win 1 --shot-level 07000048 --wait 2`) and continue loads the
  next mission.
- Statistics and score: the game counts shots, hits, kills, knock-outs, surrenders, detections, health, Bond
  moments, 007 tokens and the mission time, and the results page, the stats page and the pause SCORE tab show the
  real score, medal target and medal (Modern and Original). 07000012 with one enemy shot down (`--at
  22.49,1.09,19.155,-1.68 --input "none:4.5 pad_ry=-0.6:0.25 none:0.25 lmb:0.1 none:0.2 ..." --win 7.6 --shot-level
  07000048 --wait 2`): 7 shots, 1 hit, 1 dispatched, 14% accuracy, ~50% health, 8 s, 4,599,808 points, platinum
  (the time row pays points x target / seconds, so instant wins score millions, as on the PS2). The 50 "007 Bonus"
  tokens appear once the mission's gold medal is recorded.
- PC options in the pause menu (port-only; this is the Original style, the Modern one is below): OPTIONS opens one page laid out like the pause, with the
  pause's panel at its own size and a tab bar like MISSION/OBJECTIVES/CONTROLS/SCORE (VIDEO, GRAPHICS,
  INTERFACE, CONTROLS, KEYS; AUDIO with the game's volume bars dimmed when the tab fits, which with the
  game's font 2 it does not), each tab a list of rows built from the game's own option controls (the "AV
  Options" volume bar, the On/Off and left/right radios, text buttons, the list with frame and arrows).
  VIDEO sets resolution, display mode (window, borderless, DXGI exclusive fullscreen), VSync and the vertical
  FOV; GRAPHICS render scale 50-200% (supersampling), MSAA 2/4/8x (the fog pass per sample), texture
  filtering (bilinear, trilinear, anisotropic 2-16x), HD textures (DLSS/DLAA a dimmed "coming soon" row);
  INTERFACE the HD UI, HUD anchoring and the options' language (pt/en): applied at run time with "Apply
  changes", with the game's YES/NO box and a 15 s countdown for resolution/mode changes, saved to
  `nightfire.ini` and restored at start (command-line flags win). Also through the HD layer. Exclusive
  fullscreen is implemented but not exercised by captures (they render offscreen and leave the window
  alone).
- Controls: an XInput pad plays through the game's own virtual PS2 pad (its dead zone, curves, aim
  acceleration and the 8 button styles of the pause CONTROLS tab, which now shows each style's button map),
  with rumble from the game's hits and weapons (`Input_RumbleStart`, the vibration option); keyboard,
  mouse, wheel and pad buttons are rebindable (`[bindings]`, defaults = the old keyboard mapping) and the
  options' CONTROLS tab sets mouse/aim/pad sensitivity, dead zone, invert Y for mouse and pad, vibration
  and the trigger layout, with a KEYS tab that rebinds any command by pressing the new key or button
  (conflicts moved or swapped, defaults restorable), all saved in `nightfire.ini`. Keyboard and
  pad work together (the last device used drives the sticks); unplugging the pad in use shows the game's
  "controller disconnected" page. Captures can drive a simulated pad (`--input "pad_ly=1:2 pad_rt:0.5"`):
  07000012 walks ~8.6 m, turns and fires with it; with `fire = f` in a `--config` ini, F fires.
- Mouse in every menu (port-only, the game has no mouse code): hover moves the focus, left click
  selects (rows, buttons, tabs), the wheel scrolls lists and steps sliders and pickers, sliders follow a
  click or a drag on their track, right click or Mouse4 goes back; the Windows cursor shows while the mouse
  is used and hides on a key or pad button; the mouse is free while a menu is open and captured again when
  it closes, and clicks never reach the game (07000012: the pause's tabs and rows, "Mission Failed" REPLAY by
  click restarts with full health, a KEYS row rebinds by click, a slider dragged in CONTROLS).
- Dossier and cheats: the front end's dossier works with the game's handlers (records with the profile's best score,
  Bond moments, upgrades and best medal; rewards appearing with the medals; gadgets and weapons with their images and
  upgrades), from the mission map, the results (also the Modern DOSSIER) and the bonus page; the in-game debug page's 18
  cheat checkboxes are ported (07000012: HUD off, blind characters (alive at 12 s, no shots), fly, zero gravity, all
  weapons...), reachable on PC with `--debug-menu` (F9) since the retail game never shows that page; `--unlock-all`
  gives the default profiles every medal. `Player_ZeroG` makes 0700001b's zero gravity playable (W moves ~6.7 m in 2 s).
- Front end (single player): `nightfire.exe` without `--level` boots into the game's own front end, run by its own
  handlers and drawn in the Original style (also with `--hd-ui`): Press START, the main menu, the campaign's codename
  wheel ("New Codename", "Play without saving", the card's codenames), the difficulty, the mission map (only unlocked
  missions start; a driving mission is skipped with a notice), the codenames page, the on-screen keyboard (pad, keys
  or mouse clicks) and the codename menu; saving and loading codenames go through the game's own card flow (its
  "Saving data..." / "Loading data..." boxes, the overwrite and delete questions) onto `saves\` (`--save-dir`), with
  the same bytes the PS2 card would hold. Verified flows (1920x1080): boot > NightFire > New Codename > JAMES typed
  on the keyboard > saved > codename menu > Save Codename > JAMES loaded > Agent > The Exchange loads; a second run
  with `--save-dir` lists JAMES; 07000005 won > results > bonus > the map with Alpine Escape unlocked > Alpine Escape
  (driving) skipped > Enemies Vanquished and Double Cross unlocked; a codename's mission won from the front end >
  results > "Do you want to save your progress?" YES > the overwrite question YES > the profile rewritten with the
  medal (in both menu styles: the Modern results show both boxes as its dialog). Movies end at once (no video
  module). Capture: `--win 2 --shot-level 07000048:2` after a front-end script that starts a mission. The codename
  menu's option pages work with the game's handlers: Controller Setup (the 8 style names and the drawn pad's labels,
  invert), Advanced Options, Multiplayer Options, AV Options (volumes stored, subtitles, speaker, split screen and
  widescreen stored only), Screen Adjust (stored only), the scrolling credits (578 lines from the ELF) and the "Secret
  Unlocks" codes (checked against the ELF). An option changed there carries into the mission (07000001 with
  `--unlock-all`: subtitles on in AV Options > the intro NIS shows "Ah, Mr. Bond - at last..."; Controller Setup's
  invert flips the pad's look in 07000005), into the saved profile and, with an ini to write, into `nightfire.ini`.
  The debug page's tweaks, "Previews" and level list work too (`--debug-menu`).
- Modern menu style (the default; `--menu-style original` or the INTERFACE row brings the game's style back): a
  design system of its own (docs/design.md: Rajdhani, gold and ember on warm near-black, hairlines, letter-spaced
  caps, motion) drawn with Direct2D at window resolution over the blurred scene, and the PC options built with it:
  PAUSE MENU · OPTIONS tabs with Q/E (LB/RB), categories on the left, rows with dropdowns that open in place,
  toggles, sliders, locked rows, the keys table with PRIMARY/SECONDARY/CONTROLLER cells, a description panel with
  the current value and a render-resolution meter, device-aware prompts (key caps or Xbox glyphs), the 15 s
  confirmation dialog, toasts, and the open/focus/tab/dropdown/value animations. Keyboard, mouse and pad drive
  it and nothing leaks into the game. The game's own menus in the same style, still run by the game's handlers
  (the Modern screens read their state and send the events their controls would): the **pause** with top tabs
  PAUSE MENU · OBJECTIVES · CONTROLS · OPTIONS (RESUME / RESTART MISSION / QUIT MISSION in the large list of the
  reference's main menu, the mission panel with the current objective and the objectives meter; the objectives
  with their status and hints; the game's button layout, Y axis and button map with Xbox glyphs or the bound keys;
  the options as a tab), the game's YES/NO as the kit's dialog, **MISSION FAILED** over the blurred scene with the
  blood curtain (slow fade, tracking opening, the game's reason, REPLAY/QUIT) and **MISSION COMPLETE** (score,
  gold target, medal, stats, CONTINUE/STATS, DOSSIER (the game's dossier, Original)). 07000012: pause → tabs → options → back → resume and
  the game continues; RESTART → YES restarts the mission (`recomecos 1`, full health); death → REPLAY restarts;
  QUIT reaches the front end's mission map like the game's; results → CONTINUE loads the next mission. Captures at
  1920x1080, 3840x2160, 2560x1080 and 1024x576 look clean; with `--menu-style original` the pause, mission-failed,
  results and PC options screens are pixel-identical to before. The Modern options also carry the game's own options:
  GAMEPLAY (auto aim, crosshairs, manual aim and crouch toggle/hold, weapon auto switch, flashing objects, HUD always
  on), subtitles in INTERFACE and the music/effects volumes in AUDIO (heard at once), each with the game's own
  help text (07000012: Crosshairs off in pause > OPTIONS > GAMEPLAY and the crosshair is gone after resuming, `mira 0`;
  07000001 with `--no-subtitles`: Subtitles on in the pause and the intro NIS shows "Ah, Mr. Bond - at last..."); the
  Original page's INTERFACE tab got the subtitles row too.
  The front end's single-player path is Modern too: the game's title art with "Press the START button" and the legal
  text; the main menu with the game's logo (NIGHTFIRE, MULTIPLAYER with the game's "not available yet" box,
  CODENAMES, OPTIONS, QUIT GAME with a confirmation); the codename lists, the difficulty and the mission map (locked
  missions with the padlock, driving ones marked, the best medal, the mission picture, the DOSSIER tab); the codename
  menu opening the Modern options at CONTROLLER / GAMEPLAY / AUDIO; a codename typed on the PC keyboard or picked on a
  Modern key grid (pad, mouse) through the game's keyboard; the memory card's boxes as the dialog; the
  controller-disconnected warning as a Modern overlay. Verified flows (1920x1080): boot > NIGHTFIRE > New Codename >
  JAMES typed > saved > the game's Controller Setup as the Modern CONTROLLER > back > Save Codename > overwrite YES >
  JAMES > Agent > The Exchange loads; a second run with `--save-dir` lists JAMES; won with `--win 2` > Modern results >
  "Do you want to save your progress?" YES > overwrite YES > bonus > the Modern map with the next missions; the same
  path with the simulated pad only (codename Z, Operative); with `--menu-style original` every front-end page is
  pixel-identical to before.
  The last Original pages of the single-player path are Modern too: the **dossier** (RECORDS · REWARDS · GADGETS ·
  WEAPONS over the game's pages, each tab reached through the game's wheel page and iris, the missions on the game's
  radios, the game's images: mission pictures, the 007 medals, the reward cards and portraits, the gadget and weapon
  pictures; back to the map or the results), the **credits** (the game's 578 lines rolling at its pace in the kit's
  type, companies, roles and names by the game's fonts, from a CREDITS row of the Modern main menu, returning by
  themselves) and the pause's **SCORE** tab in single player (the game's mission stats: 07000012 after five shots, 1 hit,
  1 dispatched, 20% accuracy, 65 health, 00:09). Keyboard, mouse (tab clicks, row hover, right click back) and pad (X
  on the map, LB/RB, d-pad, B) all drive them; with `--menu-style original` the dossier, credits, pause, SCORE, results,
  map and main pages stay pixel-identical (only the HD layer's square prompt changes: it now shows E).
- Sound (see "Audio"): the game plays its own sounds through its own code and the ported IOP driver, out of
  XAudio2: weapons, impacts, ricochets, casings, footsteps, doors, pickups, explosions, the AI's barks and death screams,
  the map's ambient sounds and sound triggers, the menus' sounds and music, speech (with its subtitles) and the
  interactive music, which follows the game (07000012: the track `MFX_8` from section 13, the instant jump to 15 when
  the guards see the player at ~4.5 s, 12 on death; 07000005 `MFX_1`; the credits `MFX_16`), with the volumes of the
  game's options, 3D panning, the room reverb, pause and scenes as on the PS2. Verified on WAV dumps: 07000001's intro
  NIS (Kiko and Mayhew's speech at -24 to -30 dB RMS over ambience at ~-40 dB, no clipping, the subtitles in step;
  10-34 s: speech and ambience -27.2 dB RMS, the music alone -38.8 dB, 11.6 dB under the speech, the game's own
  levels); 07000012's gunfight (`--wait 14 --fixed-video-hz 60`: peaks at 0 dBFS, 341 clipped samples in 14 s, RMS -11
  to -14 dB in the 5 s of fire, the unclipped peak +3.7 dB: the SPU2's own saturation, checked against SFX.IRX, LIBSD.IRX and the EE: the chain has
  unity gain); a door in 07000006; the front end; the music volume at 35 instead of 70 is 12.3 dB lower (the squared
  formula predicts 12.2); 07000007's guests talk to the player (`--at 42.96,44.2,-71.81,-1.571 --wait 15.95`: the guard
  says "Enjoy the party." with its subtitle, -19.3 dB RMS over ~-26 dB of ambience and music).
- Smooth rendering above 60 fps with the logic at 60 Hz (see "Tick interpolation"): `Game_Draw` runs once per tick
  (the sky spin, lightning, thunder, `GlobalBlur` and its `Rand` draws at the PS2 rate at any refresh rate: 0700001b's
  sky and 0700000a's lightning count the same at 60, 144 and 240 Hz; captures no longer depend on the machine's speed)
  and every video frame is drawn between the last two ticks: camera, rigid objects and skinned palettes matched by the
  draw's owner, cuts in scripted scenes and on teleports, previous-tick rooms and objects kept so the interpolated view
  loses nothing, the 2D always the latest tick. Measured at 144 Hz: 07000012 turning with the pad advances 0.0861
  degrees every frame (60 Hz steps of 0.207 before), 07000013's guard walks 0.0256 m every frame, 07000001's intro cuts
  without blending. Instant mouse look turns the drawn view (and the first-person gun) at once and evenly (0.700
  degrees per frame at 800 counts/s, about 2 degrees ahead of the plain interpolation). Frame cap (off, 30-240) and
  the interpolation and mouse switches in both options pages and `[video]`. CPU cost: ~0.005 ms per video frame and
  ~0.05 ms per tick at 3840x2160 / 240 Hz. `--no-interp` gives the old images exactly.
- RTX Remix study and prototype (docs/remix.md): Remix is viable for this port as an optional Direct3D 9
  fixed-function path through its 32-bit bridge; `--renderer d3d9ff` renders the levels with the same geometry, camera,
  textures, skinning, blending and 2D as D3D11 (differences only in dynamic lighting and fog), produces stable texture
  hashes and a suggested `rtx.conf`. The user's first try under Remix 1.5.2 showed no Remix layer (no logs yet); RTX
  is parked (Known gaps 8b says what to ask for when it resumes).
- The first-person gun and arms no longer clip into walls and props: the depth clear the PS2 `Game_Draw` does before
  list 2 (`psiClearZ`) now reaches both renderers (07000012 and 07000001 against a wall: the gun and arm whole, the
  muzzle flash over the wall; away from walls the images are pixel-identical to before).
- No memory card on PC: the game's card, DUALSHOCK®2 and multitap texts read as PC text ("Saving data. Please do not
  close the game.", "Load failed! Check the save folder and please try again.", "Controller 1 is disconnected.") in
  both menu styles, through a small label-override table checked against the real `USATxt.dat`.
- The on-foot campaign has the objects its objectives need (see "Campaign completion" in the conventions and
  docs/engine.md "Depois do porte"): ladders, the grapple, ropes, ziplines, creep walls, keypad/laser/decryptor locks,
  the Q-Worm, micro-camera subjects, gun turrets, copters and the simple scripts (gates, platforms, rising water), all
  with the game's code. `nf_loader_check` ends with `bloqueios da campanha (fases): nenhum`; provisional placements went
  from 763 to 441 with `inimigos=` unchanged. Its probes climb ladders in 02/03/05/08/0c/0d/16 (up to 8.6 m), ride the
  ziplines of 0c/0d, grab ropes, grapple in 05/0c/0d/11, creep in 0d, burn and decrypt locks, and turn on the objective
  channels of the Q-Worm (0a: 72, 0b: 76), the photos (0c: 28, 0d: 30) and the copter (08: 75); turrets open and fire,
  two are destroyed in 0b; copters attack, flee or explode. Captures (docs/port.md "Campanha a pé"): 07000005's ladder
  27.10 -> 34.63 m, 0700000c's and 07000008's ziplines, the 0c photo (channel 28) and the 0b/0a Q-Worm (76, 72).
- Graphics adapter choice and a benchmark (see "Graphics adapter" and "Benchmark" in the conventions; docs/port.md
  "Adaptador de vídeo e benchmark"): `[video] adapter`/`--adapter` puts the D3D11 device (and Direct2D) on the chosen GPU
  (`auto` = exactly as before), `--list-adapters` lists them, a "Graphics adapter" row in both VIDEO pages (stored by
  Apply, used at the next start); `--bench` prints fps, frame-time p99/p99.9 ("1%/0.1% low"), GPU time per pass from
  timestamp queries, CPU costs and draw counts, in a window or `--offscreen`. Measured 2026-10-10 on this machine
  (d074740 + this work; a fresh ini's defaults with the 2.8 GB HD pack; 8 s each; `python tools/bench_matrix.py`), fps
  average at 1920x1080 offscreen / windowed: on the **Radeon iGPU** (2 RDNA2 CUs; the monitor is on the RTX) 0700000c
  turning 176 / 144 (1% low 108 / 86), 07000014 377 / 290, 0700001b 365 / 292, 07000012's gunfight 319 / 255, the Modern
  pause 186 / 166, the Modern options 203 / 161, the Modern main menu 263 / 205, the title 835 / 521 (1280x720 about
  doubles): **the 60 fps target holds with 2.4x or more**, so no default changed. iGPU time: the 3D scene (0700000c 5.0
  of 5.65 ms), its fog pass 0.5 ms, the Modern blur 1.6-1.8 ms and Direct2D 0.8-1.4 ms. The 0.1% lows under 60 (42-77
  fps in 07000012's gunfight, pause and options) are isolated 15-25 ms iGPU frames in random passes (also with
  `--no-hd`, never on the RTX): the card's, not the port's. A window on the iGPU pays DXGI's cross-adapter copy (18-25%
  fewer fps). The **RTX 5090** is CPU-bound everywhere (1000-5400 fps, 0.03-0.15 ms of GPU per frame; ~1760 draw calls in
  0700000c cost ~0.34 ms to submit). With VSync on (the default) in a window: iGPU 135 fps in 0700000c, RTX 240.0 (the
  monitor). Captures at `auto` are pixel-identical to main (the Modern footer's build hash aside); on the iGPU they
  differ only by rasterisation (at most 0.15% of pixels by more than 8 with `--fixed-video-hz 60`). VSync off now
  really is off in a window (`DXGI_PRESENT_ALLOW_TEARING`; before, the flip model held it to the monitor's 240 Hz).
- Security cameras, monitors, searchlights, mines and the last gameplay objects (see "Security and the remaining
  gameplay objects"; docs/engine.md "Câmeras de segurança, monitores e holofotes"; docs/port.md "Segurança"): with the
  game's code, so alarms no longer come only from guards. 0700000a/0b: a camera that sees the player for 2 s turns its
  alarm on and, through an OR trigger, the ceiling turrets deploy and fire (capture: the turret down and 33 bullet marks
  at 9 s; main: no channel, nothing); 07000014: 12 armed cameras feed the siren and the guards waiting on 0x96 (11
  react in the probe), and E on a monitor shows the group's cameras full screen with HUD pane 16 (the PS2 has no
  render-to-texture: camera 0 takes the security camera's matrix, so the renderer is unchanged); 07000006: the
  searchlights sweep, find the player under their spot, follow him, light the spot (their kind-2 light) and their alarm
  0x37 wakes 27 guards; 07000015: a crossbow bolt sets off a laser mine (Explode_Create, the player 100 -> 32.9);
  0700000d: the mounted laser cannon is used, fires, overheats and is left; 0700001b: the station's laser charges and
  fires at the player and the missiles arm and launch. Provisional placements went from 441 to 270 with `inimigos=`,
  the guards and `bloqueios da campanha (fases): nenhum` unchanged. Performance on the Radeon iGPU (1080p offscreen,
  main vs this work): 07000005 293.6/294.7 fps, 07000006 with the searchlights lit 343.6/337.0, 07000014 372.1/376.9 and
  325.7 looking through a monitor.
- Gameplay polish (see "Gameplay polish" in the conventions; docs/port.md "Polimento da jogabilidade"): enemies turn
  their torso so the gun points at the player (`AnimObjectAimAt`: 07000012 with the player crouched, the nearest
  guard's barrel misses him by 0.5 degrees instead of 6.3 with `--no-torso-aim`, and the AI now waits for the barrel to
  point before firing, as on the PS2); the "Auto aim" option works (`Check_AutoAim`: 07000012, five pad shots from the
  same spot, 2 hits and a kill with it on, 0 off and 0 on main; the crosshair snaps onto the crouched guard's chest;
  `nf_loader_check`: 0 vs 5 hits in 07000006, 0d and 12, 1 vs 5 in 13); the watch laser draws its beam to the lock and
  the watch shows its dial and
  hands at the mission's time (07000008), the upgraded pistols draw the laser sight and its red dot, the taser draws
  its zigzag; underwater the camera wobbles and asks for the screen blur (07000016: submerged, air dropping); the 34
  rigid bodies (Mayhew's paper lanterns, hanging cloth) swing with wind and bullets and take bullet holes (07000003: 3
  shots, 15 degrees, then swinging; main: static and shot through); the dismounted minigun can be placed again
  (`GT_DeployMiniGun` with `Collide_PlaceObject`; 0c, 12, 13 in the probe). Provisional placements 270 -> 235 with
  `inimigos=`, the guards and `bloqueios da campanha (fases): nenhum` unchanged; captures away from these features are
  pixel-identical (with auto-aim off: with it on, the crosshair follows the enemies in view).
- Movies (see "Movies" in the conventions; docs/port.md "Filmes"): the 22 PSS movies play where the game plays them,
  through its own functions: the EA and MGM logos before "Press START", the Esthero clip after it, the attract movies,
  each mission's intro (with the game's subtitles over it) and outro, the making of after the last mission, the trailer
  (Original style) and the animated backgrounds of the main menu's pages (behind the Modern blur, or under the Original
  pages exactly as on the PS2). Decoded by Windows' own MPEG-2 decoder through Media Foundation (nothing downloaded), the
  sound through the SPU2's data input at the game's movie volume, the picture following the audio clock. Verified: a WAV
  dump of 07100001 correlates 0.9998 with the file at 0 samples and gain 0.3051 (BVOL); on the XAudio2 device the heard
  clock runs at 47 978 samples/s within 2.4 ms (median) of a line and the shown frame is always the clock's. Cost at
  1080p: the Radeon iGPU plays a mission intro at 857 fps offscreen / 637 windowed, the Modern main menu over its loop at
  244 / 212; decoding + conversion 0.74-0.87 ms per movie frame on the CPU (0.03-0.12 ms per video frame).
- Presentation of the campaign (see "Screen fades, screen blur and the load screen" and "Weather and scenery" in the
  conventions; docs/port.md "Esmaecimentos, rastro e tela de carga" and "Tempo e enfeites"): the screen fades are drawn
  where `Game_Draw` asks for them (07000012 fades in from black at the start, `alfa 153` at 0.3 s; REPLAY after death
  fades through camera 6; 07000001's intro fades out at its end and when skipped, then the level fades in), the white
  flash of the quake and the flash grenade with the screen blur's trail (07000016 with `--channel f5`), the underwater
  blur (`GlobalBlur` 0x91), the load screen with the level's picture, the mission name, a tip and the moving blobs
  between levels (shown in a window for as long as the load takes, 0.2-0.7 s here; no minimum hold, as on the PS2), and
  skipping a scene works with Enter/Space or the pad's Start/A as the game allows (not death scenes). The pain flash was
  already there: the PS2 has no red screen, `pain_flash` is the red hit arrows of the HUD. Rain (0700000b: ~120 drops
  live, puddle rings), snow (07000005: 4083 flakes), Paris' billboards, bubbles, the room wind, the lightning beams
  (shown only with a reflickering sky flash: `--force-lightning` to see them), clouds, ripples (07000001's pond),
  falling leaves and animated trees (07000004), rotors, one-sided walls and the breath in the snow are the game's own;
  provisional placements 235 -> 29 with `inimigos=`, the guards and `bloqueios da campanha (fases): nenhum`
  unchanged. The env-map routine 0x2e0 was already in the shader (f654ae6). Captures away from these features are
  pixel-identical to main with `--no-env` (they now draw `Rand` like the PS2, so `Rand`-dependent results shift without
  it); iGPU cost at 1080p: 0700000b 353 -> 204 fps, 07000005 288 -> 252, 0700000c and 07000001 unchanged.
- The port builds as 32-bit x86 (the default and the reference) and, since 2026-10-12, as 64-bit x64 (`build.bat
  Release x64`; stage 1 of the macOS port, see "Two builds" in the conventions and docs/port.md "64 bits"): game
  structures keep their PS2 layouts with `P32`/`F32` fields holding offsets into a 4 GB window from `__ImageBase` (the
  image plus an arena with the game heap, `operator new` and the game stack). The x86 build is unchanged (every check
  and capture identical to main). x64 passes every check: `nf_tests`, `nf_level_check`, `nf_driving_check` and
  `nf_driving --check --verify` identical to x86 (bar the process memory); `nf_loader_check` identical to x86
  (addresses masked); the 16 reference captures pixel-identical (reports equal bar timings), as are a movie frame, a
  14 s gunfight WAV and a saved codename. Comparing x64 tick by tick against an x86 build compiled with `/Gv` (no x87;
  an experiment) found
  two x64 bugs (`rd<T*>`/`at<T*>` reading 8 bytes from 4-byte pointer fields) and two stack-garbage reads of the
  original that x64 exposed (the turret barrel's `RotMatrixZ` matrix, a pose-less drone's dropped weapon), all fixed;
  what remains between x86 and x64 is the x87 precision of x86's `float` returns (1-ulp internal differences that reach
  no check output and no capture pixel). `--bench` at 1080p: the same on the Radeon iGPU (GPU-bound: 0700000c 237 vs
  236 fps offscreen), about 1.6x faster on the RTX 5090 (CPU-bound: 2690 vs 4393 fps). The x64 process commits ~0.8 GB
  more private memory, nearly all in the 64-bit graphics driver's 32 MB blocks (the arena holds ~90 MB; the capture
  report's `arena` line, x64 only).
- Easy install (user request, 2026-10-12; see "The user's disc" and "Release zip" in the conventions; docs/port.md
  "Instalação fácil"): the game reads the user's disc from an `.iso`, a DVD drive (or the mounted image), the extracted
  folder or a single-file personal copy (the exe with the disc appended, 1,909,214,396 bytes with the movies; Windows 11
  runs it as x86 and x64, the first start a few seconds slower while Defender reads it), found on its own or chosen on a
  Modern first-run setup screen (stored in `[game] disc`). `nf_disc_check`: the ISO's 177 files equal the extracted copy
  and the mounted drive byte for byte; `nf_level_check`/`nf_loader_check` give the same output from the ISO; captures
  (07000012 at 2 s and 9 s, 07000001 at 11.7 s, the Modern and Original main menu, a movie frame, a mission intro, their
  WAVs) are pixel-identical from the folder, the ISO, the drive and the personal copy, and the folder ones identical to
  main. The release zip (`tools/make_release.py`) was unzipped into a clean folder outside the repo, pointed at the ISO
  through the setup screen and captured.
- `nightfire.exe` calls `Mem_Init` at boot like the PS2 `main`; nothing allocates from the heap yet.
- PC-standard keyboard and mouse (user request, 2026-10-10; see "Input"; docs/port.md "Controles" has the table): Esc
  pauses (and is back in menus, closing the pause at its top in both styles; the mouse is released by the menu, and in
  the free camera or when no menu opens), R reloads without using anything (07000005: clip 4 -> 7), the wheel switches
  weapons and zooms while scoped (0700000c's rifle 3.16 -> 6.16 with three notches), 1-5 pick the n-th gadget
  (07000005: taser, laser, grapple, micro-camera), G cycles gadgets, MMB/X is the fire mode, N the vision, Tab holds
  the weapon wheel. The mouse sensitivity moved to CONTROLS (with aiming sensitivity and invert Y) with a logarithmic slider
  for 400-3200 DPI mice and the cm per turn; the keys category is KEY BINDINGS; invert controller Y is in CONTROLLER.
  Old `[bindings]` entries are kept.

- Modern aiming and the weapon wheel (user decisions, 2026-10-10; see "Modern aiming and the weapon wheel"; docs/port.md
  "Mira moderna e roda de armas"; docs/design.md "A roda de armas"): with keyboard and mouse (`[game] aim_style = auto`)
  holding RMB raises the gun to the eye in ~0.17 s with its sights on the screen centre (14 gun skins measured from the
  game's own geometry by `nf_loader_check --miras`), a light 1.25-1.35x zoom, the crosshair hidden and fixed at the
  centre, the arms lowered so the forearms rise to the hands; scoped and zoom weapons enter the game's scope or zoom
  view, whose zoom is now drawn (it was ignored: `lod_scale` never reached the PC projection) with black side bars in
  wide windows; the pad keeps the game's own aim (captures without RMB, with the pad, in scenes and menus are
  pixel-identical to main, the Modern footer's build hash aside). Holding Tab (L3) opens a Modern radial wheel (weapons
  outside, gadgets inside, names and ammo from the game) over the game in 25% slow motion (15 ticks per second,
  interpolated: one tick every 4 frames at 60 Hz, alpha continuous; sound in real time); releasing picks through the
  game's own switch path (07000012: the Raptor -> unarmed, the holster and draw animations play). iGPU at 1080p with the
  wheel open: 353 fps offscreen, 311 in a window (365/327 closed). `nf_loader_check` unchanged (36 packs, 0
  differences, the same `inimigos=`), `nf_level_check` 35 levels, 0 unresolved.
- Weapon wheel polish and alpha-to-coverage (user request, 2026-10-11; see "Modern aiming and the weapon wheel" and
  "PC options in the menus"; docs/design.md "A roda de armas", docs/port.md): the wheel has fixed-size slots (8 outer of
  45 degrees, 6 inner; 12/16/24 and 8/12/16 with more), the owned weapons from the top and the rest as subtle empty
  glass, a refined hover (gold rim, corner ticks, gold gradient, ember glow) instead of a gold half-ring, and a picture
  per weapon and gadget: the front-end dossier's images decoded from FILES.BIN at boot, cut out of their green radar
  disc and drawn in one warm tone (gold when pointed); the prompt sits in a dark pill (the black bar beside it was the
  scene's wall-panel plinth). Readable at 1920x1080, 3840x2160 and 1024x576 with 2, 5, 10 and 38 (cheat) items; iGPU
  1080p with the wheel open 343.8 fps offscreen / 315.1 window (the old wheel 344.5/309.8). With MSAA, the alpha-tested
  opaque cutouts (07000001's intro bamboo, the plants) use alpha-to-coverage: the 1x shape kept whole, the edges and thin
  leaf tips smooth; captures without MSAA pixel-identical to main; iGPU cost within noise.

- `DRIVING.ELF` study (docs/driving.md; tools only, no game code ported): `DRIVING.SYM` decoded and matched to the disc's
  ELF (`tools/driving_sym.py match --evaluate`: 3,455 anchors, 97-100% of the size-equal matches right in hold-out tests),
  names applied in a separate Ghidra project (`tools/ghidra_driving.sh`), the `BASE.ELF` loader and the UberBond hand-off
  block read on both sides, the `DRIVING/` archives listed and decompressed (`tools/driving_data.py`: BIGF, RefPack,
  EAGL ELF objects, CARP, SHPS, EA-XA streams, BNKl, galleries, MPC movies).
- Driving physics prototype (docs/driving.md section 11; `src/driving/physics`, `src/platform/vu_interp.h`,
  `ps2_float.h`, `vu0_macro.h`): DRIVING.ELF's rigid body ported with PS2 layouts (`RigidBody` 0x80, `RigidBodyInfo`
  0x500): constructor, levers, ground contact (`CollideWithGround`, `TempGetHeightInformation` over a `WorldCollision`
  interface; `FlatGround` for tests), sleep, damping, the frame forces, `GenerateImpulse`, `ResolveCollision` (the
  body-body response, with `ModifyLevers`/`ScaleObjObjForces`; its caller `CollideWithObject` is not ported),
  `SimulateGame`; the
  VU0_physics microprogram (quaternion to matrix, `MultiplyMatrix`, `ResolveLeverForces`,
  `UpdatePositionAndOrientation`) hand-ported to C++; a generic VU0/VU1 interpreter (all instructions of the three VU0
  programs and the 115 EAGL render methods decode); the MIPS interpreter runs COP2/MMI. `nf_driving_check`: 32,000
  microcode cases (original in the interpreter vs hand port) and 600 frames of the whole step both ways, all bit for
  bit; the original EE functions in the MIPS interpreter (with the VU0) vs the C++ port, including 600 frames of
  `SimulateGame` with 6 bodies (the whole EE check runs 35 million original MIPS instructions and 10,690 microprograms): 0 differences. Boxes dropped on a
  flat ground settle within 2 cm of their half-height and sleep. `tools/pcsx2_pine.py` records the bodies from PCSX2
  (PINE) for later ground truth. Not done: the Vanquish drop (its dimensions and collision points come from the model;
  `PBondCar`), `CollideWithObject`, the world collision.
- Driving render prototype (docs/driving.md section 10; `src/driving`, `src/driving_app`): `nf_driving` draws an EAGL car
  the game's way: BIGF/RefPack and SHPS readers (port-only), the CARP as EA's UGroup tree, `EAGL::DynamicLoader`
  (typed symbols, relocations, the split `ELFd`/`ELFr` object), `EAGL::Model`/`GeoPrim::Draw` (the command lists become
  DMA chains), `GeoPrimState`/`RStateManager`/`TAR`, a DMA + VIF1 simulation, and the car parts placed by the CARP's
  `in#0000` instances with the `vd#0001` outside-view draw list (`RSceneObj::GetViewDrawList`). One render-method family
  read from the VU1 microprogram and redone as a D3D11 shader + CPU reference: `MatCarTexturePacked` = `MatCarChrome`
  (same code), `MatCarTextureDullPacked`, `MatCarDash` (3 passes: base, specular sphere map, reflection; damage zones, 3
  directional + 4 point lights, fog); the other 4 car methods use a flagged fallback (glass draws opaque black). The
  Vanquish renders whole (plate "KE02 EWW" readable, wheels in place), 1.76 ms/frame at 1080p MSAA 4x on the Radeon iGPU.
  All 63 car `.crp` of the 9 archives load (0 loader/VIF errors except `MISC.VIV`'s `bus_tokyo`, which lacks a mission's
  shared data); the family covers 90.1% of their 7,872 batches and, run against the original microprogram in
  `ps2::VuMachine` (`--verify`), 494,099 vertices match (colours off by 1 in 80 components, XY within 1/16 px, fog equal;
  the PS2's clip-cull ADC is the documented divergence). Census: 115 methods, 97 distinct microprograms, 13 families.
  Not done: the other car methods, the per-frame `RReflection`/`RLightManager`/`RVehicle` inputs (SpecularMat, reflection
  texture, lights, wheel bones), the track world.
- The Xbox version as a reading reference for the driving port (docs/driving.md section 9; tools only, nothing derived
  from the disc in git): `Driving.xbe` (MSVC x86, built the same day as the disc's `DRIVING.ELF`, no symbols or RTTI) is
  loaded in Ghidra as a flat PE (`tools/xbox.py pe`, `tools/ghidra_xbox.sh`; 7,016 functions with the 639 that only MSVC
  vtables point to) and matched to `DRIVING.ELF` by `tools/xbox_match.py`: 2,332 pairs, 1,823 at confidence >= 2 (~97%
  right when half the string anchors are hidden and re-derived), covering 57% of the bytes of the named game code and 88%
  of the physics. The physics is plain C++ on Xbox with the same `RigidBody` layout and tuning constants, including what
  the PS2 runs as VU0 microcode (the rigid-body step, the lever forces, the visibility curtains) and VU0 macro code
  (`GenerateImpulse`, `ResolveCollision`): an algorithm oracle, not a numeric one (x87/SSE `rsqrtss` versus the VU0;
  64 rigid-body slots versus 48). The render methods' NV2A vertex programs agree with the VU1 programs of the same name
  (checked on `LitGouraud`, `LitTextureEnvmap_Skin`) but only ~17 names are shared. Estimate: 27-41 days instead of
  30-45. `default.xbe` (on-foot) is the same Eurocom code and adds little to the ported `ACTION.ELF`.

Known gaps / next steps (agreed order: 1 next, then the rest):
1. AI (`NDrone2`), so there is something to shoot. Done: the core, the navigation network, the
   vision, the body, the weapons, the triggers, the spawners (`DroneSpawner_*`) and every state except
   the bots' (`Bot*`); 3 `PlayScript` is in `drone_script.cpp` and `estados da IA sem porte` is empty.
   Active drones that start at the same spot climb each other (`Drone_FeetOnPoint` accepts a drone as
   floor, like the original); the stacked `NinjaStand` drones of 07000016 (`variação` ~10 m) were
   spawner templates, which the spawner now takes off the map at load (`variação` 0.00). Add each
   state to `NF_DRONE_STATES` in `drone.h` (that also registers it in `Drone_ResolveCode`). The hostage messages
   (`Text_AddMsg`) are queued and wait for the HUD.
   Places where the combat states would read an invalid address are guarded and counted in
   `g_drone_combat_guards` (`nf_loader_check`: `guardas do combate da IA`, printed only when nonzero).
   Spawner leftovers: the radio call of the kind-3 waves in 0700000c/0d (`NDrone2_PlaySFX` with a null
   `DCVars_tag`, guarded in the port, and no disc spawner reaches it).
   Then the rest of the menus (item 6), multiplayer (bots: `BOT_*`).
2. Lighting leftovers (the lightning beams `Lightning_*` and `Particles_*`, empty on the PS2, are done; the
   searchlights are done too: their spot carries a kind-2 light; to confirm on hardware that the VU1 light really sits at the spot's placement, as
   `psiLight_SetLights` reads `light_tag::pos`, while the 4 m reach follows the moving spot); to
   confirm against PCSX2 or hardware: the fog's Z-to-alpha mapping (`CreateMoveRG2BA`), the fog on
   the back sky layer, the 07000041 dest-alpha mask meshes, the room-light tint of objects with
   flag 0x800 (`View_SetupRenderModes_Tweak`: the items in 07000001's room 59, light (40, 30, 26),
   come out at ~15% brightness), and `SqueezeColourIn` 0x91 (the PS2's copy to the display buffer brightens every
   frame by 13%; not applied). `BodyGlow_Create` (the glow of bodies in infrared vision mode 2) is still a stub;
   `Bubble_Update`, `Sunreflection_Update` and `Corona_Update` are not ported (no placement reaches them in single
   player by `nf_loader_check`, and nothing in the ELF calls `Corona_Update`).
3. HD texture leftovers: PNG replacements (WIC), animated texture frames beyond the first,
   textures used with palettes the pack lacks (could fall back to the same texels with another
   palette, at the risk of wrong colors).
4. Player leftovers. Done (see "Gameplay polish"; rain, snow and the env-map routine 0x2e0: "Weather and scenery",
   "Lighting"): torso
   aiming, auto-aim, the laser pointer, the watch and the beams, the underwater camera and ripples, rigid bodies, the
   deployable minigun and the rest of collision; the screen blur they ask for is drawn ("Screen fades, screen blur and
   the load screen"). Their leftovers: the probes that
   need the player active do nothing in 07000002's intro scene (the enemy 44 m away, the water dive); a capture of the
   deployable minigun (only the probe and a unit test place it); the player's body aims only in multiplayer
   (faithful). Campaign (docs/engine.md "Campanha: o que falta
   para completar"; ladders, grapple, ropes, ziplines, creep walls, locks, the Q-Worm, camera subjects, turrets,
   copters, simple scripts, security cameras, monitors, searchlights, mines, shooters, inverters, the mounted gun, the
   quake, the space station and the rigid bodies are done and the audit finds no blocker): still provisional, none
   blocking an objective by the audit: none in the campaign levels (the cosmetic ones are done: "Weather and scenery");
   the 29 left are the multiplayer arenas' cars and one in 07000046 (`Car_Create`); a
   full scripted playthrough of a mission (the probes and captures prove each mechanism, not every room-to-room path);
   07000002's two monitor cameras (no alarm, no beam) never see the player from the probe's spot in front of them (to
   check in a capture). In the renderer: the 3D views of the special
   cameras 5-10 (4 is drawn during scripted scenes; their 2D is; the monitors do not need them: the PS2 shows a
   security camera through camera 0). The screen fades, the flash and the load screen are done.
5. Animation leftovers: `AnimDebug*`. Faces and torso aiming
   are done (see "Faces", "Gameplay polish"); to confirm on PCSX2 or hardware: the eyes of characters that track nothing
   (targets 12/13 at 1) and that every face counts as close in single player; the D3D9 Remix path draws faces in the
   rest pose.
   Scene scripts: the fades of GameFlow state 0xd before a NIS and of cameras 0/4, and skipping a scene (Enter/Space,
   pad Start/A: action 0x19 -> `Script_FFwd`), are done; a dedicated "skip" binding is not (the game reads the Start
   action, so the pause key skips).
6. Rest of `hashtable_*`.
   HUD and mission leftovers: multiplayer panes and the
   radar are ported but unexercised (`MP_*` stubs).
   Menu leftovers: the front end's other handlers (null entries in `kHandlers`, counted as stubs): multiplayer
   (`P_MP*`, `C_SBMP*`, `C_RBMP*`, `C_SBBOTS`, `C_MPDBG`), `P_FMVTEST`; the driving missions (DRIVING.ELF; the port skips them; study and plan in docs/driving.md); the language page's display branch is
   ported but never shown in the US version; the debug "Previews" plays only the scripts loaded in the current level, like the original;
   the stats' callers outside the ported code (`GT_Update` shots, `Copter_Update` hits, `BOT_*` health, the
   DRIVING.ELF exchange `Boot_*PTPData`), the
   multiplayer score tab of `C_GCPAUSE_Handler` (abbreviated), `Combo_*`/`Spin_*` (no such control on
   the disc). (The 0x10000184 image of the pause panel, texture 0x030000db at alpha 0x50, additive, is the
   game's art: blurred party lights and a man raising a glass; its 8-bit CSM1 palette decodes smooth, the
   raw order would be noise. The small squares next to RESTART/REPLAY and the video page's "60" are the
   green exit sign on 07000012's far wall seen through the panel's glass.)
   PC options in the menus (user request, 2026-10-08). Done (see "PC options in the menus" in the
   conventions): the pause row and the tabbed page 0x40000060 (resolution, display mode, VSync, FOV, render
   scale, MSAA, texture filtering, HD textures, HD UI, HUD anchoring, language, controls, keys) on the
   game's own controls, saved in `nightfire.ini`, and the mouse in every menu. Left: the AUDIO tab (hidden:
   the six tab names do not fit the band in font 2; its volume rows are still dimmed; the Modern AUDIO
   category has working volume sliders), the volume bars' look when
   not focused (the game draws only the fill: a value of 0 shows nothing), wheel scrolling of a tab's rows with the
   mouse over a slider or picker (the wheel goes to that control), DLAA/DLSS (item
   8; the row is there, dimmed), following the game's language for the
   port-only labels, exclusive fullscreen checked on hardware (it switches the monitor mode, so captures
   do not exercise it). (Alpha-to-coverage with MSAA is done: "PC options in the menus".) The game's own option
   pages for reference (front end, `08000002`): "Advanced
   Options" 0x4000002d (`P_CNOPTIONS_Handler`: vibration, auto aim, crosshairs, crouch and manual aim
   toggle/hold, weapon auto switch, flashing objects, HUD always on; bytes of `PlayerSetting[0]`),
   "AV Options" 0x40000031 (`P_CNAVOPTIONS_Handler`: music and effects volume, subtitles
   `DrawInfo[2]`, split screen `DrawInfo[0]`, speaker mode, widescreen `DrawInfo[1]`, screen adjust,
   restore defaults, credits, trailer), "Controller Setup" 0x40000022 (`P_CNCONTROLS_Handler`: 8 button
   styles `PlayerSetting::style`, Y-axis `invert_look`); `Menu_UpdateDefaultCodename` copies them into
   the codename profile (`def_codename`, 0x38 bytes) that the memory card saves.
   Controls (user request, 2026-10-08). Done (see "Input" and "PC controls in the menus" in the
   conventions): XInput through the virtual PS2 pad and the 8 styles, rumble, rebindable keyboard/mouse/
   wheel/pad, the controls and keys pages, `[controls]`/`[bindings]`, button prompts per device in the HD
   layer (see "HD 2D layer"). Left: prompts in the original 2D (still the PS2 symbols of 0x03000075: would
   need new symbol textures or text fallbacks); a physical pad test (only the
   scripted pad was exercised: XInput reading, rumble strength feel, the disconnect page with a real
   unplug); pads for players 1-3 (slot 4 is read but multiplayer is not ported; no multitap); DualShock/
   DualSense through raw HID (XInput only sees Xbox-compatible pads); `Input_Check4Combo` (cheat combos)
   and `Input_Ready` (needs `ComboKeysList`); the main.cpp debug keys (F2-F4, F12, PgUp/PgDn are refused
   by the capture; V, H, P, T are not and still toggle their debug views when bound); a rebinding entry for
   the free camera.
7. Platform: VU0 intrinsics header for the 233 inline-VU0 functions, saves. Easy-install leftovers: other disc versions
   (PAL `SLES-...`, Japanese) are refused (their data and offsets differ: a port of their own ELFs); images that are not
   2048-byte ISO 9660 (a raw `.bin/.cue`, a UDF-only image) are refused; a signed executable (SmartScreen warns about an
   unknown publisher); the x86 single-file copy needs the Visual C++ runtime (installed, or the zip's DLLs next to it);
   the HD texture pack is not looked for next to a DVD drive; drag-and-drop of an `.iso` on the setup window. 64 bits (done for Windows, see "Two
   builds"): the macOS port itself (a platform layer for window, input, rendering, sound, movies and crash reports; the
   arena with `mmap` near the image and the game stack as a `pthread` stack in the arena; clang with
   `-ffp-contract=off`; its libm differs from the UCRT, so identical results would need the port's own math functions);
   the x64 `RemixD3D9Renderer` path is untested (RTX is parked); x64 interactive play was exercised only through
   captures and `--bench` (no hand-played session yet). Done: the movies (see "Movies"); their
   leftovers: the D3D9 Remix path does not draw them, the movie's memory is not taken from the game heap
   (`AllocateMovieMemory`), and a machine without any MPEG-2 MFT simply skips them (the Store's "MPEG-2 Video Extension"
   is the one that works here). Done: the tick interpolation (see "Tick interpolation"; the view is smooth above 60 fps, with the frame
   cap). Its leftovers: particles and shards are drawn at the latest tick (no per-particle identity; the
   billboards face the tick's camera), the leading-edge culling of the instant mouse look on very fast turns (the
   game's frustum is per tick; widening it would change `unknown_fc`-based logic), the env-map rows and lights of
   the tick, checking the feel on real 120-240 Hz hardware (only simulated displays were captured).
   Adapter and benchmark leftovers (none needed for the 60 fps target): the Modern blur without the full-screen copy
   (downsample straight from the target, mix as a constant-factor blend: identical at full blur, ~0.6 ms less on the iGPU
   at 1080p), caching the Modern menus' `IDWriteTextLayout`s between frames (CPU), per-adapter defaults (e.g. a lighter
   texture filter on integrated GPUs) only if a weaker iGPU ever misses; checking `--adapter` on a machine whose monitor
   is on the iGPU (a laptop) and exclusive fullscreen there (it cannot cross adapters).
   Wide screens (user, 2026-10-08: 16:9 up to 4K is the target; super-ultrawide only if it is not
   too buggy): FOV is vertical 60 degrees (Hor+), so 16:9 shows 91 degrees across, 21:9 106 and 32:9
   128. Offscreen captures at 3840x2160 and 2560x1080 of 07000001 look clean; 7680x2160 renders too,
   with these to polish if ever wanted: a FOV option with a horizontal cap, a separate first-person
   weapon FOV, pillarboxing the scripted scenes (their cameras were framed for 4:3), the scope and
   night-vision overlays kept round with black sides instead of stretched, HUD anchoring to a 16:9/21:9
   safe area, and one capture showed a tall yellow streak from the ceiling to an enemy's muzzle flash
   (probably a bullet trail passing next to the camera, visible with the wide FOV; to confirm).
8. Next-gen visual mode (decided with the user, 2026-10-10): DLSS/DLAA is **not** a performance need (a PS2 scene is
   trivial for any modern GPU; the 200% render scale + MSAA already give supersampled quality), so it is no longer
   next. The baseline renderer must keep running at 60 fps on integrated GPUs without DLSS; keep the defaults light
   (measured 2026-10-10 with `--bench`: the Radeon iGPU runs the defaults at 1080p at 144 fps or more, see Status; rerun
   `python tools/bench_matrix.py` after renderer changes).
   The user is interested in an optional path-traced look through **NVIDIA RTX Remix** (path tracing, replaceable PBR
   materials; DLSS / Ray Reconstruction then make sense) and later DLSS 5 on top; the original look stays the
   default. Studied and prototyped (item 8b, docs/remix.md: a Remix-compatible D3D9 fixed-function path for the x86
   port through Remix's 32-bit bridge, `--renderer d3d9ff`). The cheap quality item instead of DLAA, alpha-to-coverage so
   MSAA smooths alpha-tested edges, is done (2026-10-11).
   Old plan, kept for reference — DLAA / DLSS Super Resolution (user request, 2026-10-07): NVIDIA Streamline over the D3D11 renderer. Needs per-pixel motion vectors (each
   draw's previous-frame matrix and previous skinning palette: the `DrawItem::interp` identity already pairs each
   draw with its previous tick, so keeping the previous video frame's interpolated matrices per id gives them;
   particles/effects handled apart, e.g. a reactive mask), the depth buffer (already there for the fog pass),
   sub-pixel camera jitter (per video frame, applied after the interpolated view) and the texture mip bias. The user has an RTX 5090 to test it. Later, optionally: DLSS 5 (generative
   "neural rendering", announced at GTC 2026), which also wants G-buffer inputs (albedo, normals;
   PS2 assets have no roughness/metal) and may require D3D12; it changes the original look, so it
   would be a toggle. The options page's GRAPHICS tab already has a dimmed "DLSS / DLAA: coming soon" row
   (`kPcRowDlss`) and the renderer already draws the 3D scene in its own target before the 2D (the
   render-scale/MSAA path), which is where an upscaler would sit.
8b. RTX Remix (user request, 2026-10-10; see "RTX Remix path" and docs/remix.md). Done: the study and the `d3d9ff`
   prototype. **Parked (2026-10-10):** the user's first test with the Remix runtime "did not work" (no Remix layer, no
   logs); the user decided to park RTX, the prototype stays as is. When resumed, first ask for: the command line and the
   working directory; `rtx-remix\logs\remix-dxvk.log`, `bridge32.log` and `bridge64.log` (Remix writes them in the
   **working directory**, not next to the exe), any `*.dmp`; the `rtx.conf`/`dxvk.conf`/`user.conf` in use (also read
   from the working directory) and `.trex\bridge.conf`; a capture report's `d3d9ff ... modulo` line; a photo of the
   screen. Seen in one offscreen capture with the Remix 1.5.2 install in `build\Release` (docs/remix.md "Situação"):
   the bridge and runtime start fine on the RTX 5090, but captures only show the 2D (Remix composes the path-traced
   image at Present), so only a windowed run can judge it; no `rtx-remix\logs` existed after the user's test, so the
   bridge may never have loaded (e.g. started without `--renderer d3d9ff`) or it ran from another folder. Next, in order: the user runs it under Remix 1.5.2 (camera, geometry-hash view, UI, skinning, lights,
   bridge frame rate); then a render-state cache and fewer calls (the 32-bit bridge pays per call), sending neighbouring
   rooms too (portal culling hides what path tracing needs) with Remix anti-culling, the level's lights through the
   Remix API via the bridge (stable hashes), a default `rtx.conf` validated in Remix, the Modern menus and HD 2D layer as
   a UI texture copied from D3D11, back-buffer resize, an options-page entry. Content (PBR textures, lights) is modder work.
9. HD HUD and UI (decided with the user on 2026-10-08: first the faithful HUD, done; then this).
   Done: the infrastructure (see "HD 2D layer" in the conventions: DirectWrite text, SVG/PNG image
   replacements from `hd_ui/map.json`, F4 toggle, edge anchoring, `--ui-inventory`, offscreen captures)
   and the first assets (crosshair, message band, 9 mm bullet stack, health gauge blades and ring); the
   menus (every piece in docs/port.md "Inventário dos menus" except the "007 nightfire" logo 0x0300016d),
   9-slice frames and the button prompts per device. Next: more HUD assets from the inventory (the copper
   armour blades, pane 1 sprites 0-7; the other bullet columns of `03000128`; touch/crouch icons; the Bond
   moment 007 (1:16) and arrows; scope, night-vision, X-ray and camera panes; lens flare; radar), the 0x0300016d logo, a check
   of the unseen 0x03000042 pieces (radio, volume bar, big arrows: front-end option pages) once those pages
   open, the tab bar of the tabbed PC page (done like the pause: text in the file's colours over the panel's
   band; a selected-tab piece, e.g. the highlight 0x03000042 [38, 19], if the text alone is too subtle), a
   DirectWrite layout cache,
   per-element anchor overrides if the touch grouping is not enough.
   Original plan: vector shapes (bars, frames, crosshair, radar) as SVG drawn with Direct2D, text with a
   vector (TrueType/OpenType) font through DirectWrite, both at window resolution over the D3D11 renderer;
   high-resolution images with alpha (PNG/DDS) only for illustrated icons (weapons, gadgets); a
   toggle between the original 2D and the HD layer. Checked in the faithful HUD (both are the game's
   behaviour): a new objective's band shows empty while its "NEW OBJECTIVE" title blinks
   (`HUD_UpdateStatusPane`, `fmod(time, 40) < 20`, text hidden for the first second), and in-scene
   subtitles page one full-width line at a time (07000001: "...my bodyguard. I " for ~5 s, then
   "assure you, she's quite skilled."; `Text_Update2Line` is the off-scene two-line kind). `Sprite_*`, `Text_*` and `Font_*` are ported and every 2D
   element reaches `RuntimeGfx::collect_frame` with its origin (`Psi2DOrigin`: sprite, texture hash,
   font, glyph, text, pass, run, alignment, baseline) and its PS2 rectangle, which is where the HD layer
   swaps the drawing per element (docs/port.md, "Camada 2D em alta definição"); the menu port
   (`MenuManager_*`) draws through it.
9a. Modern aiming and the weapon wheel (2026-10-10; see the convention). Left: the sight lines follow the top of the
   PS2 models (they have almost no rear-sight notch): 0x12/0x13 and 0x14/0x15 sit a little off (the rear aperture shows
   below the line); a hand-tuned row per skin if the user wants exact sights; the ADS arms are the hip animation with the
   upper arms lowered (no ADS animation exists), so the sleeve fills the lower centre; 0x42 and 0x45 (multiplayer maps
   only) are measured but unchecked; ADS for zoom weapons without a scope pane (0x18/0x19, 0x2a/0x2b) and the rocket
   launchers would need a look through their scope models; the wheel's icons (done, 2026-10-11: fixed slots, the dossier
   images cut out of the radar disc) have no HD replacement and are 128 px textures upscaled at 4K, and 7 classes have no
   dossier image (the punch, Kowloon Type 80, Delta Repeater, torpedo launcher, Q-Pen, Oddjob's hat, flare gun: text
   only); the first slow tick after opening waits one tick (the record length
   is only known at the next tick); real-hardware feel of the wheel with a pad (only the scripted pad was exercised).
9b. Modern menu style (user decision, 2026-10-08: Modern is the default, Original the faithful option). Done: the
   design system (docs/design.md, `src/ui`), the Modern PC options and the game's in-game menus (the pause with its
   tabs, mission failed, the results with stats) over the game's own menu logic (`engine/menu_modern.h`), the front end's single-player path and the
   controller-disconnected warning (`src/ui/modern_front.h`), the dossier (`src/ui/modern_dossier.h`), the credits and
   the single-player SCORE tab. Left: the pages that stay Original (the codename's Multiplayer Options, the screen
   adjust, the driving controller page); the trailer (the Modern AV Options is the AUDIO category, without its row);
   the game's mission
   picture is the 128x128 texture (sharp only with an HD pack), like the dossier's pictures; the multiplayer SCORE
   tab is wired but unexercised; the credits move in 60 Hz steps (the game's 2 PS2 pixels per tick, not interpolated);
   DLSS/DLAA (locked row); a visible mouse cursor in offscreen captures (the Windows cursor is
   not in the image); checking the feel of the animations and the pad (rumble-free, Start/B mapping) on hardware
   (captures only show single frames and a scripted pad).
9c. Audio leftovers (see "Audio"): the sounds of objects not ported yet, played from inside their own logic (the
   `*_Update` of cars, subs `Sub_*`/`MiniSub*` (turrets, copters, simple scripts, locks, rigid bodies, security
   cameras, searchlights, the SSys countdown voices, the mounted gun and the space laser are ported and play theirs);
   `Lightning_Create` is a beam between two objects and has no sound); the bots' (`BOT_soundEffect`,
   `BOT_handlePain`); an optional port-only limiter only if a listening test finds the faithful clipping harsh (docs/port.md
   "O nível da saída"); the SPU's Gaussian
   interpolation (cubic here); the timing of non-instant music jumps (the PS2 reads ~2 s ahead, the PC jumps after
   what already played) and the IRX's "just jumped" hold; the original Original-style AUDIO tab (dimmed rows); a
   listening check on real speakers and headphones (captures only measure WAVs) and of the XAudio2 latency.
10. Open questions: the common animation pack (`CommonAnimFile`). (Animation rate: scripts advance `speed` frames per 60 Hz tick; the exporter's
    30 fps assumption plays them at half speed.)
    `DRIVING.ELF` is no longer an open question: studied in docs/driving.md (2026-10-11). It is EA's engine (EAGL
    4.06.01 with 115 VU1 render methods shipped as data, REAL, CARP, SND with `SNDDRV.IRX`), sharing only the Sony SDK
    and the GCC runtime with `ACTION.ELF`; `DRIVING.SYM` comes from an earlier, 15% larger build, matched by
    `tools/driving_sym.py` (5,107 names in `out/ghidra_driving`, 68% of the game's functions); the hand-off is the
    0xA50-byte UberBond block at 0x100000 through `BASE.ELF`. A five-phase plan is estimated at two to three times the
    `ACTION.ELF` port so far (27-41 days with the Xbox version as the physics oracle, docs/driving.md section 9: read
    the Xbox pair of a function in `out/xbox/match.tsv` or `out/ghidra_xbox`, port from the PS2); the driving missions stay skipped (`Menu_PortDrivingSkipped`) until the user decides to
    start, and the recommended first step is a prototype that draws one EAGL car model with one render method plus the
    VU0 physics microcode under tests. The render half of that prototype is done too (docs/driving.md section 10: one car,
    the MatCar family verified against the original microprogram); its next step is the other car methods (glass, switch
    lights) and the track's world methods. The physics half is done (docs/driving.md section 11, 26-39 days now); its next
    step is the real ground (CARP `CGrd`/`CDat`, `WGrid`, `WWorldPos::FindClosestFace`) and the car (`PBondCar`
    construction, `ProcessPhysics`, `AddWheelForces`), then `CollideWithObject` (`ResolveCollision` is ported).
