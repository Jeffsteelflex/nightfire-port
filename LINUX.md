# 007: Nightfire PC port on Linux

**Short version: there is no native Linux build of the game yet.** Linux users have two options:

1. Run the **Windows build through Wine or Proton**. This is untested so far: please report what works.
2. **Build from source** for development: the tests and the data checks run natively on Linux, but the game itself does
   not.

A native Linux version (window, input, sound, renderer) is not started. The port's platform layer is already split per
system (Windows and macOS have their own backends), so it is planned, but it has no date yet.

> **No game data is included.** You need your own copy of *007: Nightfire* for the PS2, US version (SLUS-20579).

## 1. Make an image of your disc

The port reads an `.iso` of your own disc. With the disc in the drive:

```sh
# find the drive (often /dev/sr0)
lsblk
# copy it (about 4 GB)
dd if=/dev/sr0 of="007 - Nightfire (USA).iso" bs=2048 status=progress
```

The extracted files of the disc in a folder also work.

## 2. Play the Windows build with Wine or Proton (untested)

Nobody has played the port on Linux yet. The steps below follow what the Windows build needs, but none of them is
verified. If you try it, please open an issue with your distribution, your Wine or Proton version, your GPU and what
happened.

### What the Windows build uses, and what Wine provides

| The port uses | Under Wine / Proton |
|---------------|---------------------|
| Direct3D 11 for the 3D | DXVK (included in Proton; install it for plain Wine, e.g. `winetricks dxvk`) |
| Direct2D and DirectWrite for the launcher, the Modern menus and the HD text | Wine's own implementation; may draw wrong or not at all (see "If the menus look wrong" below) |
| XAudio2 for the sound | Wine's built-in XAudio2 |
| XInput for controllers | Wine maps SDL controllers to XInput |
| Media Foundation for the movies | Usually no MPEG-2 decoder; the port then decodes the movies with its own decoder |

### With Steam (Proton), including the Steam Deck

1. Download `Nightfire-PC-<version>.zip` from the [releases](../../releases) and unzip it.
2. In Steam: *Games > Add a Non-Steam Game*, then pick `nightfire.exe`.
3. In the game's *Properties > Compatibility*, tick *Force the use of a specific Steam Play compatibility tool* and
   choose a recent Proton.
4. Put your `.iso` next to `nightfire.exe` (the port finds it on its own), or pass it in *Launch Options*:
   `--disc "/path/to/007 - Nightfire (USA).iso"`. Inside Wine a Linux path may need the `Z:` drive:
   `--disc "Z:\home\you\games\007 - Nightfire (USA).iso"`.

### With Wine

```sh
unzip Nightfire-PC-<version>.zip -d ~/games
cd ~/games/"Nightfire PC"
winetricks dxvk                 # Direct3D 11 through Vulkan (recommended)
wine nightfire.exe --disc "Z:\home\you\games\007 - Nightfire (USA).iso"
```

`nightfire.exe` is the 32-bit build, the reference; it needs a Wine with 32-bit support (most distributions' packages,
or a WoW64 Wine). `nightfire64.exe` is the experimental 64-bit build and may suit a 64-bit-only Wine better.

### If the menus look wrong

The launcher and the default "Modern" menus are drawn with Direct2D. If Wine draws them badly:

- any command-line argument that says what to run skips the launcher, for example `--disc <iso>`;
- `--menu-style original` uses the game's own PS2 menus, which are drawn with Direct3D only;
- once the game runs, OPTIONS > INTERFACE > Menu style keeps that choice in `nightfire.ini`.

Other useful switches: `--window-mode windowed`, `--width 1280 --height 720`, `--movie-decoder own` (the port's own
movie decoder), `--no-movies`.

Settings (`nightfire.ini`) and saves (`saves\`) are written next to `nightfire.exe`.

## 3. Build from source (developers)

On Linux the build is "headless": the shared game code, the unit tests and the tools that check the port against your
data, with no window, sound or renderer. This is what the project's CI runs on Ubuntu 24.04 with GCC and clang.

```sh
sudo apt install build-essential cmake ninja-build python3   # or your distribution's equivalent
git clone <this repository>
cd nightfire-port
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DNF_HEADLESS=ON
cmake --build build
./build/nf_tests                 # the unit tests (no game data needed)
```

The build is 64-bit only. Besides `nf_tests` it makes the data checks `nf_level_check`, `nf_loader_check`,
`nf_disc_check`, `nf_movie_check` and `nf_driving_check`; these take the extracted disc as their first argument (see
`CLAUDE.md`). So far they have only been run on Windows with real data.

See `docs/port.md` (in Portuguese) for how the platform layer is split, and `CLAUDE.md` (in English) for the
conventions.
