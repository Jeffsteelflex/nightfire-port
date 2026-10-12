007: NIGHTFIRE - PC PORT
========================

A native Windows rebuild of 007: Nightfire for the PlayStation 2 (US version, SLUS-20579). Not an emulator: the game
runs on its own decompiled code, with PC features the PS2 lacked (any resolution, mouse and keyboard, high frame rates).

This zip contains NO game data. You need your own copy of the game: the US PS2 disc of 007: Nightfire (SLUS-20579).


HOW TO PLAY
-----------

1. Unzip this folder anywhere you like (for example "C:\Games\Nightfire PC").
2. Run nightfire.exe.
3. The first time, a setup screen asks for your disc:
   - "Select disc image" opens a file picker: choose the .iso image of your 007: Nightfire disc (PS2, USA);
   - or put the disc in your DVD drive: "Use the disc in drive X:" appears on its own.
   Your choice is saved in nightfire.ini. Then choose "Play": the game starts, and next time it starts directly.
   Shortcut: an .iso placed next to nightfire.exe is found without the setup screen.
4. Optional: "Make a single-file copy" writes one .exe with your game inside (about 1.8 GB), so you can play without
   the disc image. That file contains your copy of the game: it is for you only. Do not share it.

Other versions of the game (PAL, Japanese) are not supported yet: the setup screen tells you which version a disc is.


THE TWO PROGRAMS
----------------

nightfire.exe     the 32-bit build: the reference, used for every test. Start with this one.
nightfire64.exe   the 64-bit build: EXPERIMENTAL. It passes the same automated checks, but nobody has played it by hand
                  yet. It shares nightfire.ini and the saves with nightfire.exe.


CONTROLS (DEFAULTS)
-------------------

Move: WASD                 Look: mouse               Fire / aim: left / right mouse button
Use: E                     Reload: R                 Jump / crouch: Space / C or Ctrl
Weapons: mouse wheel, Q/Z  Gadgets: 1-5, G           Weapon wheel: hold Tab
Pause: Esc                 Menus: arrows or mouse, Space or Enter to select, Esc to go back

An Xbox-compatible controller works too (the PS2 button layouts are in the pause menu). Everything can be rebound in
OPTIONS > KEY BINDINGS.


FILES
-----

nightfire.ini   your settings (created next to the executable)
saves\          your codenames (the memory card)
hd_ui\          the interface's fonts and graphics (keep it next to the executables)
*.pdb           debug symbols: if the game crashes, the error report names the function


GOOD TO KNOW
------------

- Movies need Microsoft's free "MPEG-2 Video Extension" (Microsoft Store). Without it the game skips them.
- The driving missions and multiplayer are not ported yet: a driving mission counts as done (without a medal) and the
  campaign goes on.
- Command line, for advanced users:
    nightfire.exe --disc "D:\Games\007 - Nightfire (USA).iso"     use this disc (an .iso, a folder or a drive)
    nightfire.exe --setup                                          show the setup screen again
    nightfire.exe --pack-disc <disc> --out <file.exe>              make the single-file copy without the window
    nightfire.exe --level 07000001                                 start a level directly
- Windows may warn about an unknown publisher the first time (the program is not signed).


LEGAL
-----

This is an unofficial fan project. It is not affiliated with or endorsed by Electronic Arts, Eurocom, MGM, Danjaq or
EON Productions. 007, James Bond and Nightfire are trademarks of their owners. No copyrighted game content is
distributed here: you must own the game and use your own copy. See LICENSES.txt for the third-party pieces.
