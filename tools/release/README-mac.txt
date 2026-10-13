007: NIGHTFIRE - PC PORT, MAC VERSION
====================================

A native rebuild of 007: Nightfire for the PlayStation 2 (US version, SLUS-20579). Not an emulator: the game runs on
its own decompiled code, with features the PS2 lacked (any resolution, mouse and keyboard, high frame rates).

THIS IS THE FIRST MAC VERSION. It is built and tested automatically, but nobody has played it on a real Mac yet. You
are one of the first testers: please report what works and what does not (see "REPORTING A PROBLEM" below).

This zip contains NO game data. You need your own copy of the game: the US PS2 disc of 007: Nightfire (SLUS-20579).


WHAT YOU NEED
-------------

- macOS 11 (Big Sur) or newer, on Apple Silicon (M1 and later) or Intel. One app runs on both.
- Your own US PS2 disc of 007: Nightfire (SLUS-20579), or a disc image you made from it (see below). Other versions
  (PAL, Japanese) are not supported yet.


1. INSTALL
----------

1. Unzip the zip (double-click it). You get a "Nightfire PC" folder.
2. Drag "Nightfire PC.app" into your Applications folder (or any folder you like). Moving it with the Finder before
   the first start avoids a macOS quirk that runs downloaded apps from a hidden read-only copy.


2. OPEN IT THE FIRST TIME (THE APP IS NOT SIGNED BY APPLE)
----------------------------------------------------------

The app is not signed with an Apple Developer ID or notarized by Apple (that needs a paid Apple account), so macOS
cannot check who made it and blocks it the first time. You only do this once.

- macOS 15 (Sequoia) and newer: double-click the app; macOS says it was not opened (click "Done", not "Move to
  Trash"). Then open System Settings > Privacy & Security, scroll down to "Security" and click "Open Anyway" next to
  "Nightfire PC". Confirm ("Open Anyway" again, and your password or Touch ID). If the app does not start by itself,
  double-click it once more.
- macOS 11 to 14: Control-click (or right-click) the app in the Finder, choose "Open", then "Open" in the dialog.
- Or, in Terminal (any version), remove the download mark yourself:
      xattr -dr com.apple.quarantine "/Applications/Nightfire PC.app"


3. MAKE AN IMAGE OF YOUR DISC
-----------------------------

The game reads your disc. An image file on your Mac is the easiest (and loads faster than the disc itself).

- Disk Utility (with the disc in a DVD drive): select the disc in the sidebar, then File > New Image > Image from
  "<the disc's name>". Format: "DVD/CD master", Encryption: none. Save it. You get a ".cdr" file: the game takes it as
  it is (no need to rename it).
- Or in Terminal: find the disc with
      diskutil list
  (it is the "external, physical" /dev/diskN with the disc's name), then
      diskutil unmountDisk /dev/diskN
      hdiutil create -srcdevice /dev/diskN -format UDTO -o ~/Nightfire
  This writes Nightfire.cdr in your home folder (if macOS says "Permission denied", put "sudo " before "hdiutil").
- Or play from the disc itself in an external DVD drive: it shows up in the launcher on its own.
- An .iso image of the same disc made on Windows (or anywhere) works too.


4. PLAY
-------

1. Open Nightfire PC. A launcher opens before the game, with "Play" selected.
2. The first time it asks for your disc: the "Disc" row opens a file picker; choose your .cdr or .iso (or a DVD drive
   row appears when the disc is in a drive). Your choice is remembered. Shortcut: an image placed in the same folder as
   Nightfire PC.app is found on its own.
3. The launcher also sets the resolution, the display mode (window or full screen), VSync, the menu style (Modern or
   Original) and the HD 2D layer. Press Enter (or A / Start on a controller, or click "Play") to start.
4. Turn "Show at startup" off to start the game directly next time; hold Shift while the game starts to get the
   launcher back.

Controls (the defaults; everything can be rebound in OPTIONS > KEY BINDINGS):

Move: WASD                 Look: mouse               Fire / aim: left / right mouse button
Use: E                     Reload: R                 Jump / crouch: Space / C or Control
Weapons: mouse wheel, Q/Z  Gadgets: 1-5, G           Weapon wheel: hold Tab
Pause: Esc                 Menus: arrows or mouse, Space or Return to select, Esc to go back
Full screen: Option+Return or Control+Command+F       Quit: Command+Q

Click in the window to capture the mouse; Command+Tab or a menu releases it. macOS adds its own mouse acceleration to
the look; if aiming feels uneven, turn "Pointer acceleration" off in System Settings > Mouse > Advanced (macOS 14 and
newer).
Xbox, PlayStation (DualShock 4, DualSense) and Switch Pro controllers work (by cable or Bluetooth), without rumble.


WHERE YOUR FILES ARE
--------------------

Everything the game writes goes to:  ~/Library/Application Support/Nightfire PC/
(In the Finder: Go > Go to Folder..., then paste that path.)

nightfire.ini        your settings
saves/               your codenames (the memory card)
nightfire.log        what the game wrote during the last run (for bug reports)
nightfire.prev.log   the same for the run before it

Optional HD texture packs (the PCSX2 format) go in "textures/SLUS-20579/replacements" next to your disc image, or next
to Nightfire PC.app.


REPORTING A PROBLEM
-------------------

Please include:
- nightfire.log (and nightfire.prev.log if the problem happened in the run before), from the folder above. Its first
  lines tell us the version, your macOS, your Mac model and graphics chip; your home folder is shown as "~".
- Your Mac model and macOS version (Apple menu > About This Mac).
- What you did and what happened (a screenshot helps: Shift+Command+3).
- If the game crashed, the end of nightfire.log has the crash report.

On an Apple Silicon Mac you can also try the Intel build of the same app under Rosetta: select the app in the Finder,
File > Get Info, tick "Open using Rosetta". Tell us if a problem happens only one way.


NOT WORKING YET
---------------

- The driving missions: a driving mission counts as done (without a medal) and the campaign goes on.
- Multiplayer.
- Controller rumble on Mac.
- Raw mouse input (the look follows macOS's mouse acceleration, see above).
- Not checked yet: speed and image on real Mac graphics chips, Retina sharpness, 120 Hz (ProMotion) displays,
  keyboard layouts other than US, real controllers and speakers. That is what this test is for.


LEGAL
-----

This is an unofficial fan project. It is not affiliated with or endorsed by Electronic Arts, Eurocom, MGM, Danjaq or
EON Productions. 007, James Bond and Nightfire are trademarks of their owners. No copyrighted game content is
distributed here: you must own the game and use your own copy. See LICENSES.txt for the third-party pieces.
