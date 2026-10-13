007: NIGHTFIRE - PC PORT
========================

A native Windows rebuild of 007: Nightfire for the PlayStation 2 (US version, SLUS-20579). Not an emulator: the game
runs on its own decompiled code, with PC features the PS2 lacked (any resolution, mouse and keyboard, high frame rates).

This zip contains NO game data. You need your own copy of the game: the US PS2 disc of 007: Nightfire (SLUS-20579).


HOW TO PLAY
-----------

1. Unzip this folder anywhere you like (for example "C:\Games\Nightfire PC").
2. Run nightfire.exe.
3. A launcher opens before the game, with "Play" selected: press Enter (or A or Start on a controller, or click
   "Play") to start the game.
4. The first time, the launcher asks for your disc:
   - the "Disc" row opens a file picker: choose the .iso image of your 007: Nightfire disc (PS2, USA);
   - or put the disc in your DVD drive: "Use the disc in drive X:" appears on its own.
   Your choice is saved in nightfire.ini. Shortcut: an .iso placed next to nightfire.exe is found on its own.
5. The launcher also sets the resolution, the display mode (windowed, borderless or exclusive fullscreen), VSync, the
   menu style (Modern or Original) and the HD 2D layer (the HUD and the Original menus redrawn at full resolution).
   "Play" saves your changes in nightfire.ini; "Quit" leaves without saving them.
6. Turn "Show at startup" off and the game starts directly next time. Hold Shift while the game starts to see the
   launcher again (for example after picking a display mode your monitor does not like).
7. Optional: "Single-file copy" writes one .exe with your game inside (about 1.8 GB), so you can play without the disc
   image. That file contains your copy of the game: it is for you only. Do not share it.

Other versions of the game (PAL, Japanese) are not supported yet: the launcher tells you which version a disc is.


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

- Movies play with Windows' own MPEG-2 decoder when it is installed, and with the port's own decoder otherwise.
- The driving missions and multiplayer are not ported yet: a driving mission counts as done (without a medal) and the
  campaign goes on.
- Command line, for advanced users:
    nightfire.exe --disc "D:\Games\007 - Nightfire (USA).iso"     use this disc (an .iso, a folder or a drive)
    nightfire.exe --launcher                                       show the launcher (any other argument skips it)
    nightfire.exe --pack-disc <disc> --out <file.exe>              make the single-file copy without the window
    nightfire.exe --level 07000001                                 start a level directly
- Windows may warn about an unknown publisher the first time (the program is not signed).


LEGAL
-----

This is an unofficial fan project. It is not affiliated with or endorsed by Electronic Arts, Eurocom, MGM, Danjaq or
EON Productions. 007, James Bond and Nightfire are trademarks of their owners. No copyrighted game content is
distributed here: you must own the game and use your own copy. See LICENSES.txt for the third-party pieces.
