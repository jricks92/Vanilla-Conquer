# Vanilla Conquer
Vanilla Conquer is a fully portable version of the first generation C&C engine and is capable of running both Tiberian Dawn and Red Alert on multiple platforms. It can also be used for mod development for the Remastered Collection.

The main focus of Vanilla Conquer is to keep the default out-of-box experience faithful to what the games were back when they were released and work as a drop-in replacement for the original executables while also providing bug fixes, compatiblity and quality of life improvements.

Current project goals are tracked as [GitHub issues with the goal label](https://github.com/Vanilla-Conquer/Vanilla-Conquer/issues?q=is%3Aissue+is%3Aopen+label%3Agoal).

## Chat with us

There are rooms on multiple platforms for discussion:

- [The Assembly Armada](https://discord.gg/UnWK2Tw) on [Discord](https://discord.gg)
- [#vanilla-conquer:vi.fi](https://matrix.to/#/#vanilla-conquer:vi.fi) on [Matrix](https://matrix.org)
- [#vanilla-conquer](https://web.libera.chat/?channel=#vanilla-conquer) on [Libera.Chat](https://libera.chat]) IRC network

All of these rooms are bridged together so people can choose their preferred service. Please be nice to each other.

## Building

We support wide variety of compilers and platforms to target. Vanilla Conquer is known to compile with recent enough gcc, MSVC, mingw-w64 or clang and known to run on Windows, Linux, macOS and BSDs.

### Presets

A [CMakePresets.json](CMakePresets.json) file is provided that contains presets for common build configurations and is used by our CI scripts to build the release builds. These presets require the [Ninja](https://ninja-build.org/) build tool to be available in the system PATH in order to be used.

We also provide an example [CMakeUserPresets.json](resources/CMakeUserPresets.json.example) that can be copied to the root source directory and renamed. You can edit this file to create your own development presets that won't be included in git commits. A few example presets are provided which override the release presets to build a "debug" configuration.

To build using a preset, add `--preset preset_name` to the CMake command line examples below.

### Windows

#### Requirements

The following components are needed to build Vanilla Conquer executables:

 - [MSVC v142 C++ x86/x64 build tools](https://visualstudio.microsoft.com/visual-cpp-build-tools/)
 - Windows 10 SDK
 - CMake (installable from MSVC build tools)
 - [SDL1 or SDL2 development libraries, Visual C++](https://libsdl.org/download-2.0.php)
 - [OpenAL Core SDK](https://www.openal.org/downloads/)

Extract SDL2 and OpenAL somewhere you know. If you are building only Remastered dlls you can skip installing SDL2 and OpenAL.

#### Building

In a VS command line window in the Vanilla Conquer source directory:

```sh
cmake -DSDL2_ROOT_DIR=C:\path\to\SDL2 -DOPENAL_ROOT=C:\path\to\OpenAL -B build .
cmake --build build
```

This will build Vanilla Conquer executables in the build directory. If you are building Remastered dlls you need to configure cmake with `-A win32` and ensure your VS command line is x86.

### Linux / macOS / BSD

#### Requirements

- GNU C++ Compiler (g++) or Clang
- CMake
- SDL1 or SDL2
- OpenAL

On Debian/Ubuntu you can install the build requirements as follows:

```sh
sudo apt update
sudo apt install g++ cmake libsdl2-dev libopenal-dev
or
sudo apt install g++ cmake libsdl1.2-dev libopenal-dev
```

On Fedora/RedHat based system you can install the build requirements as follows:

```sh
sudo dnf install gcc-c++ cmake SDL2-devel openal-soft-devel
or
sudo dnf install gcc-c++ cmake SDL-devel openal-soft-devel
```

#### Building

```sh
cmake -B build .
cmake --build build
```

This will build Vanilla Conquer executables in the build directory.

#### macOS considerations

To create a portable bundle for macOS we run [macdylibbundler](https://github.com/auriamg/macdylibbundler) in our CI builds as an extra step to add the SDL2 and OpenAL libraries to the bundle. If you wish to create a portable bundle yourself, you will need to do this step manually as CMake will not currently do it for you.

### Icons

CMake will attempt to generate icons in an appropriate format for Windows and macOS if ImageMagick is found in the system PATH. Otherwise you will end up with generic "program" icons.

### iOS / iPadOS

Tiberian Dawn and Red Alert can be built as native ARM64 iOS/iPadOS apps
(SDL2 rendering through Metal, OpenAL audio, touch controls). This is a
community port; see [docs/port/IOS_PORT_PLAN.md](docs/port/IOS_PORT_PLAN.md)
for the full engineering log and
[docs/port/IOS_LESSONS_LEARNED.md](docs/port/IOS_LESSONS_LEARNED.md) for a
reusable methodology.

The iOS/iPadOS port was created with the help of
[Claude Code](https://claude.com/claude-code) (using the Fable 5 and
Opus 4.8 models).

Requirements: an Apple Silicon Mac with Xcode, CMake 3.25+, Ninja, and an
iPad/iPhone on iPadOS/iOS 15+. Build helpers live in `scripts/build/ios/`:

```sh
# 1. Build static SDL2 + openal-soft for arm64-iOS (once).
./scripts/build/ios/build-deps.sh

# 2. Configure and build the games for iOS.
cmake --preset ios
cmake --build --preset ios

# 3. Package an unsigned .ipa with your game data bundled in.
./scripts/build/ios/package-ios.sh td --data /path/to/TiberianDawn/data
./scripts/build/ios/package-ios.sh ra --data /path/to/RedAlert/data
```

The resulting `dist/ios/*.ipa` are unsigned. This port has only been tested
running through [LiveContainer](https://github.com/khanhduytran0/LiveContainer)
(no Apple Developer account required); in theory a signed build should also work
as a normal native sideload with an Apple Developer account, but that path is
untested. You supply your own game data, exactly as with the desktop builds.

## Releases

Binary releases of the latest commit are available from [here](https://github.com/TheAssemblyArmada/Vanilla-Conquer/releases/tag/latest), which is updated whenever new code is merged into the main branch.

## Running

### VanillaTD and VanillaRA

Copy the Vanilla executable (`vanillatd.exe` or `vanillara.exe`) to your legacy game directory, on Windows also copy `SDL2.dll` and `OpenAL32.dll`.

For Tiberian Dawn the final freeware Gold CD release ([GDI](https://www.moddb.com/games/cc-gold/downloads/command-conquer-gold-free-game-gdi-iso), [NOD](https://www.moddb.com/games/cc-gold/downloads/command-conquer-gold-free-game-nod-iso)) works fine.

For Red Alert the freeware [CD release](https://web.archive.org/web/20080901183216/http://www.ea.com/redalert/news-detail.jsp?id=62) works fine as well.
The official [Red Alert demo](https://www.moddb.com/games/cc-red-alert/downloads/command-conquer-red-alert-demo) is also fully playable.
The demo supports custom skirmish maps (except interior) and includes one campaign mission for both Allied and Soviet from the retail game.

While it is possible to use the game data from the Remastered Collection, The Ultimate Collection or The First Decade they are currently _not_ supported.
Any repackaged version that you may already have from any unofficial source is _not_ supported.
If you encounter a bug that may be data related like invisible things or crashing when using a certain unit please retest with the retail data first before submitting a bug report.

### iOS / iPadOS

Build the `.ipa` files as described above (the game data is bundled into the
app at packaging time). Install the `.ipa` with
[LiveContainer](https://github.com/khanhduytran0/LiveContainer) — either by
copying it onto the device or by hosting it and downloading over HTTP — then
install it from the LiveContainer app list.

What works today: full single-player campaigns for both games, touch controls
(tap to select, drag for a selection box, two-finger tap to deselect/right-
click, three-finger drag to scroll the map), tap to skip cutscenes, and the
on-screen keyboard for name entry. LAN multiplayer does **not** currently work
under LiveContainer — see the port docs for details.

This has only been tested under LiveContainer; a signed native sideload
(Apple Developer account) should also work in theory but is untested.

Known issues under LiveContainer:

- **Orientation is not locked.** The game is landscape, but LiveContainer
  overrides the app's own orientation handling, so rotating the iPad to
  portrait leaves the view clipped. Rotate back to landscape to fix it (or use
  the iPad's Control Center rotation lock while in landscape). Some
  LiveContainer builds also expose a per-app Orientation setting
  (long-press the app → Settings) that can lock it to landscape.
- **LAN multiplayer does not work.** iOS blocks UDP broadcast for sandboxed
  apps, which is how peer discovery works. See the port docs for details.

### Remastered

The build process will produce _Vanilla_TD_ and _Vanilla_RA_ directories in your build directory if you enable them with `-DBUILD_REMASTERTD=ON` and `-DBUILD_REMASTERRA=ON`.
These work as mods for the Remastered Collection.

To manually install a local Remastered mod, launch both games once then head to _My Documents/CnCRemastered/CnCRemastered/Mods_.
You should see _Tiberian\_Dawn_ and _Red\_Alert_ directories.

#### Tiberian Dawn

Copy the _Vanilla_TD_ directory to the _Tiberian\_Dawn_ directory.

The directory structure should look like this:

    My Documents/CnCRemastered/CnCRemastered/Mods/Tiberian_Dawn/Vanilla_TD/Data/TiberianDawn.dll
    My Documents/CnCRemastered/CnCRemastered/Mods/Tiberian_Dawn/Vanilla_TD/ccmod.json
    My Documents/CnCRemastered/CnCRemastered/Mods/Tiberian_Dawn/Vanilla_TD/GameConstants_Mod.xml

You should now see the new mod in the mods list of Tiberian Dawn Remastered.

#### Red Alert

Copy the _Vanilla_RA_ directory to the _Red\_Alert_ directory.

The directory structure should look like this:

    My Documents/CnCRemastered/CnCRemastered/Mods/Red_Alert/Vanilla_RA/Data/RedAlert.dll
    My Documents/CnCRemastered/CnCRemastered/Mods/Red_Alert/Vanilla_RA/ccmod.json

You should now see the new mod in the mods list of Red Alert Remastered.
