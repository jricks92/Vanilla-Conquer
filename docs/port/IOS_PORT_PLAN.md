# Vanilla Conquer iOS/iPadOS Port Plan

Target: **Tiberian Dawn and Red Alert 1 running natively on iPadOS** (primary device:
iPad Pro 11", M-series, 2388×1668 @ 120 Hz). Red Alert 2 is out of scope — its source
was never released and it is not part of this engine.

Methodology adapted from the C&C Generals iOS port
([ammaarreshi/Generals-Mac-iOS-iPad](https://github.com/ammaarreshi/Generals-Mac-iOS-iPad),
`docs/port/PORTING_PATTERNS.md` + `PORTING_PLAYBOOK.md`), scaled down to fit this
codebase — which is in dramatically better starting shape than Generals was.

## Why this port is tractable

The Generals port spent most of its effort translating DirectX 8 through
DXVK → Vulkan → MoltenVK → Metal, and reimplementing Win32 in a ~28-header compat
library. **None of that applies here:**

| Subsystem | Generals needed | Vanilla Conquer already has |
|---|---|---|
| Renderer | DXVK + MoltenVK + 15 patches | SDL2 render API (`video_sdl2.cpp`) — Metal-backed on iOS out of the box |
| Audio | OpenAL swap behind a manager seam | OpenAL (`soundio_openal.cpp`) |
| Win32 APIs | CompatLib shim layer | POSIX ports exist (`file_posix.cpp`, `paths_posix.cpp`) |
| 64-bit/ARM | A dedicated port phase | Already builds/runs on ARM64 macOS |
| Memory | ~3 GB resident, iOS jetsam kills | Tens of MB — a rounding error on an M-series iPad |

What does **not** exist yet, in order of effort: touch input semantics, iOS build +
packaging pipeline, iOS filesystem/asset routing, app lifecycle handling, and
points-vs-pixels correctness. That is the whole port.

## Architecture decisions (per-subsystem, Generals-style)

| # | Subsystem | Decision | Rationale |
|---|---|---|---|
| D1 | Renderer | **Keep** SDL2 render path unchanged | SDL2 officially supports iOS; streaming-texture + `SDL_RenderPresent` maps to Metal internally. No SDL3 migration — minimizes diff against upstream. |
| D2 | Audio | **Keep** OpenAL; build openal-soft for iOS (CoreAudio backend) | Cross-compiles cleanly with CMake. |
| D3 | Dependencies | **Static-link** SDL2 and openal-soft | Avoids the entire embed-dylibs / `install_name_tool` / re-sign dance the Generals port fought. Both support static builds; GPL v3 + zlib/LGPL is fine. |
| D4 | Entry point | `#include <SDL_main.h>` in both games' `startup.cpp` (iOS only) | SDL wraps `main` in `UIApplicationMain`; without it the app never starts. |
| D5 | Game assets | **Bundled into the IPA** (`package-ios.sh --data <dir>`), landing next to the binary inside the .app; the engine's existing portable-mode detection (`Paths.Init` finds the data marker at `Program_Path`) picks it up with no code changes. Files-app Documents remains the fallback (`Data_Path()` override). | Single self-contained IPA (~350 MB), no post-install data push. Caveat to verify: with bundled data, `chdir(Data_Path())` lands in the read-only bundle — CWD-relative *writes* (savegames?) may need `User_Path` routing; revisit at Phase 3/4. |
| D6 | Saves/config | Existing `User_Path()` → `$HOME/Library/Application Support/Vanilla-Conquer/<game>` | Already correct on iOS: sandbox `$HOME` is the app container; path persists across reinstalls and is backed up. Zero code change expected. |
| D7 | Touch input | **New deferred-tap state machine** feeding the existing synthetic-mouse seam (`Put_Mouse_Message` / `Move_Video_Mouse`), two-finger pan feeding the existing analog-scroll channel | Game logic stays untouched — same principle that made the Generals touch layer reviewable. |
| D8 | Packaging | **Unsigned IPA for LiveContainer** (user decision): a packaging script zips `Payload/<App>.app` with the CMake-built binary + Info.plist; no signing, no provisioning, no Xcode project. Installed by copying into LiveContainer's container: `afcclient --container com.kdt.livecontainer.B5L3CPH855 put <src.ipa> /Documents/<dest.ipa>` | Eliminates the entire signing pipeline. LiveContainer runs the app in-process and manages its per-app data container (`$HOME` points inside LiveContainer's sandbox, so `Data_Path()`=Documents and `User_Path()` still resolve). |
| D9 | One app or two? | **Two apps** (VanillaTD, VanillaRA), mirroring the desktop targets | Matches existing target structure, separate save dirs/icons; trivial to share all port code. |

## Phases and acceptance gates

Gates are behavioral, never "it compiled" (Generals lesson: verify artifacts, not
exit codes).

### Phase 0 — macOS baseline (de-risk, ~no code)

Build and run VanillaTD + VanillaRA on the Mac with real game data **first**.
This proves the asset set (which MIX files are needed), gives a reference for
"correct" behavior, and provides the fast dev loop for everything that isn't
iOS-specific (touch can be 90% developed on macOS with mouse-event injection).

- Acquire assets: TD freeware Gold CD ISOs (GDI/NOD) and RA freeware CD /
  Aftermath data; extract MIX files into a known directory.
- `cmake --preset release && cmake --build --preset release`, run both games,
  play a mission each.
- **Gate: both games playable on macOS from a user-writable data directory.**

### Phase 1 — cross-compile milestone

Everything links as an arm64-iOS binary. No device needed yet.

1. **Dependencies standalone first** (Generals lesson): build static SDL2 and
   openal-soft with `CMAKE_SYSTEM_NAME=iOS`, `CMAKE_OSX_ARCHITECTURES=arm64`,
   `CMAKE_OSX_SYSROOT=iphoneos`, deployment target ~15.0. Script:
   `scripts/build/ios/build-deps.sh` → installs into `build-deps/ios/`.
   Verify each artifact: `otool -l lib.a | grep -A2 LC_BUILD_VERSION` must show
   `platform 2` (iOS), and `lipo -info` must show arm64 only.
2. **CMake iOS support** in this repo:
   - New configure preset `ios` in `CMakeUserPresets.json` first, promoted to
     `CMakePresets.json` when stable: iOS system name, points at the dep
     install prefix, `BUILD_TOOLS=OFF`, `BUILD_TESTS=OFF`, `MAKE_BUNDLE=OFF`
     (our packaging handles the bundle).
   - Audit CMake platform conditionals: iOS does **not** match
     `PLATFORM_ID:Darwin` or the `APPLE`-guarded bundle logic paths we want to
     skip (Generals gotcha). `find_package(SDL2)`/`FindOpenAL` must resolve the
     static libs; static SDL2 pulls in iOS frameworks
     (`Metal`, `UIKit`, `CoreMotion`, `GameController`, `AVFoundation`, …) via its
     exported targets — verify at link time.
   - Icon generation (`BuildIcons.cmake`) skipped for iOS; icons handled in
     packaging (PNGs, **no alpha** — App-Store-format requirement even for
     dev installs).
3. **Source-level iOS cases**, all small and `TARGET_OS_IPHONE`-guarded:
   - `startup.cpp` (both games): `SDL_main.h` include (D4).
   - `paths_posix.cpp::Data_Path()`: return `$HOME/Documents` on iOS (D5).
   - Both games call `Paths.Init` in `startup.cpp` (TD line ~226, RA ~292), so
     the `Data_Path()` override covers both. Add `chdir(Paths.Data_Path())` on
     iOS after `Paths.Init` (Generals-port approach) as belt-and-braces for any
     CWD-relative file access.
   - Anything that fails to compile: whole-function stubs with a `// TODO(ios)`
     marker, never inline ifdef soup (Generals lesson).
- **Gate: `vanillatd` and `vanillara` binaries exist, `LC_BUILD_VERSION
  platform 2`, no undefined symbols.**

### Phase 2 — boots to menu on the iPad

The Generals playbook calls the menu "the true halfway point." Everything here
is packaging + filesystem.

1. **Shell app**: `ios/project.yml` (XcodeGen) — stub `main.m`, automatic
   signing with `DEVELOPMENT_TEAM`, `TARGETED_DEVICE_FAMILY: "2"` (iPad),
   landscape-only, `UIRequiresFullScreen`, `UIFileSharingEnabled`,
   `LSSupportsOpeningDocumentsInPlace`,
   `UIApplicationSupportsIndirectInputEvents` (trackpad/mouse),
   `NSLocalNetworkUsageDescription` (LAN multiplayer, iOS 14+ prompt).
   Build once with `xcodebuild -allowProvisioningUpdates` to mint the
   provisioning profile without opening Xcode.
2. **Packaging script** `scripts/build/ios/package-ios.sh <td|ra>`: copy shell
   app → replace stub executable with the real game binary → copy icon PNGs →
   re-sign with entitlements extracted from the shell
   (`codesign -d --entitlements - --xml`). Static linking means **no
   Frameworks/ embedding step at all.**
3. **Install & first-run loop**: `xcrun devicectl device install app` (absolute
   path), `devicectl device process launch --console` for stdout/stderr.
   Prereqs on the iPad: Developer Mode, cable pairing, trust. Unregistered
   device error `0xe8008012` → one
   `xcodebuild -allowProvisioningDeviceRegistration` build.
   Do **not** use `devicectl copy --remove-existing-content` (Generals lost an
   entire app container to it).
4. **Assets on device**: drop the MIX files into the app's Documents folder via
   the Files app (or `devicectl device copy to --domain-type appDataContainer`).
   `Paths` already probes for the data marker (`REDALERT.MIX` /
   `CONQUER.MIX`) — verify `Data_Path()`=Documents resolves them; fix up
   `CDFileClass::Refresh_Search_Drives` behavior if it assumes CWD.
5. **Logging**: `DBG_*` goes to stderr → visible via `--console` launches. Add
   a file logger into Documents only if icon-launch debugging demands it.
   Watchdog gotcha to remember: "runs from `devicectl`, dies from icon" = main
   thread stalled during init (CLI launches bypass the iOS watchdog).
- **Gate: main menu renders on the iPad; a mission can be started with a
  Bluetooth mouse or trackpad** (SDL2 supports indirect pointers on iPadOS —
  this gives a playable-ish fallback before any touch work, and isolates
  "game works" from "touch works").

### Phase 3 — playable with touch

The genuinely new engineering. Develop the state machine on macOS (mouse-drawn
fake touches or SDL touch simulation), tune on device.

1. **Deferred-tap state machine** — new `common/wwtouch_sdl2.cpp`, driven from
   `WWKeyboardClassSDL2::Fill_Buffer_From_System` (it runs every frame, so
   long-press can be *polled* — a still finger generates zero SDL events):

   `IDLE → PENDING → {TAP | DRAG | LONGPRESS | PAN}`

   - **Send nothing on finger-down.** Commit only when the gesture identifies
     itself (Generals' hardest-won rule: an early LMB-down "cancelled" later
     still set rally points).
   - **Tap** (finger up while PENDING): synthesize motion + LMB-down + LMB-up
     all at the *original* touch point, in game coordinates, through
     `Put_Mouse_Message`.
   - **Drag** (moves past ~8 pt dead zone): LMB-down anchored at the original
     point, then motion — selection boxes anchor correctly.
   - **Long-press** (~600 ms stationary): RMB click = C&C deselect. Polled from
     the frame loop.
   - **Two-finger pan**: feed the existing analog scroll channel
     (`Is_Analog_Scroll_Active()` / `Get_Scroll_Direction()` /
     `ScrollDirection`) that gamepads already use — the game polls it natively;
     no synthetic edge-scrolling hacks. No LMB event may ever leak from a pan.
   - **Pinch**: `VK_MOUSEWHEEL_UP/DOWN` (sidebar scrolling); TD/RA have no map
     zoom, so pinch is low-stakes.
   - Suppress SDL's built-in emulation: `SDL_HINT_TOUCH_MOUSE_EVENTS=0` **and**
     drop `which == SDL_TOUCH_MOUSEID` events (defense in depth).
2. **Coordinate correctness (points vs pixels)** — the Generals port's most
   recurring bug class. `SDL_FINGER*` events give normalized 0–1 coordinates;
   convert via renderer output size and the existing `Get_Video_Scale`
   letterbox math in `video_sdl2.cpp` (window may be high-DPI: add
   `SDL_WINDOW_ALLOW_HIGHDPI`, use `SDL_GetRendererOutputSize` not window
   size). End-to-end test: tap all four screen corners, cursor must land
   exactly (their standard verification).
3. **Input mode plumbing**: touch wants absolute positioning, but
   `Settings.Mouse.RawInput` defaults to relative + software cursor. Add a
   touch input mode (auto-detected on iOS) in `settings.cpp`; hide the cursor
   except transiently at tap points, or keep the software cursor as a visible
   "last action" marker — decide by feel on device.
4. **App lifecycle**: `SDL_AddEventWatch` (a watcher, not the poll loop —
   `SDL_APP_WILLENTERBACKGROUND` etc. must be handled immediately and can
   arrive after the event loop stops) → atomic flag → gate **both** simulation
   and `SDL_RenderPresent` in the frame loop (`framelimit.cpp` /
   `Video_End_Frame`). Rationale from Generals: GPU work issued around
   suspension causes drawable-acquire deaths on resume. Wire
   background/foreground to the existing `Focus_Loss`/`Focus_Restore`.
- **Gate: complete a TD and an RA mission using touch only; 10-minute
  app-switch/rotate/background soak without crash or input ghosting.**

### Phase 4 — polish

- **Text input** (savegame names, MP chat): show the iOS keyboard via
  `SDL_StartTextInput` when the game opens an edit field; verify the game's
  `EditClass` gets characters (may need `SDL_TEXTINPUT` → `Put_Key_Message`
  mapping in `wwkeyboard_sdl2.cpp`). Hardware keyboards already work.
- **Display tuning**: default `Boxing` 16:10 letterbox on the 2388×1668 panel;
  pick sharp integer-ish scaling (`Scaler=nearest`) vs `linear` by eye;
  `FrameLimit=120` already matches ProMotion.
- **Ship tuned defaults** (Generals lesson — first-run config seeding): a
  bundled `vanillatd.ini`/`vanillara.ini` copied to `User_Path()` if absent,
  with iOS-appropriate `[Video]`/`[Mouse]` settings.
- **Icons** per game from `resources/vanilla*_icon.svg` (no alpha, loose PNGs
  in bundle root; SpringBoard caches aggressively — a device restart is
  sometimes the only fix).
- **Multiplayer smoke test**: UDP LAN game against a desktop build; confirm the
  local-network permission prompt fires.
- **Quality-of-life backlog** (post-ship): on-screen buttons for common hotkeys
  (guard, formation, team numbers), Apple Pencil as precision pointer,
  Files-app-visible saves, iCloud backup audit.

## Findings log

- **TD crash/black-screen at side-select (Phase 0) — root cause: incomplete
  game data, terrible missing-file handling.** The engine loads missing files
  as junk 1-byte buffers with no error (`Load_Alloc_Data`), then reads font
  metrics from them (`Set_Font`) and feeds garbage to the WSA XOR-delta decoder
  (`Copy_Delta_Buffer`), which loops forever / scribbles over the heap —
  presenting as "black screen" or `BUG IN CLIENT OF LIBMALLOC`. Initially
  misattributed to a macOS 26 QuartzCore bug (crash stacks landed in CA/GL
  code; register x12 held "CHOOSE.W" which led to the real cause). Fix: the TD
  Gold data set REQUIRES the files inside `INSTALL/SETUP.Z` (cclocal, transit,
  speech, update, updatec, *icnh mixes — extract with wfr/unshieldv3) and the
  `gdi/`/`nod/` disc-specific subfolders; the loose `INSTALL/CCLOCAL.MIX` on
  the CD is the German copy and must not be used. Engine robustness fixes
  (validate file loads, bound the WSA decoder) are good upstream candidates and
  matter for iOS, where users hand-assemble the data folder.
- **`DisplayClass::One_Time` reads past `RemapTables`** for multiplayer houses
  (`(hindex+11)*16` into a 256-entry row, `display.cpp:246`) — found by ASan,
  fixed with a bounds check.
- **iOS Phase 2 blockers, in the order they fell** (each found via the iOS
  file logger added to `debugstring.cpp`):
  1. `Data_Path()` must point at the bundle (`Program_Path`), not Documents —
     `CDFileClass` searches User/Raw/Data paths and never Program_Path, so
     bundled data was invisible.
  2. `ini.Load(cfile)` on the config INI is unguarded in both games'
     `startup.cpp`; on a true first run (no INI anywhere) the missing file
     escalates through `CCFileClass::Error` → bogus "CD not found" → deliberate
     fatal crash. Fixed with `Is_Available()` guards (genuine upstream bug).
  3. **`RawFileClass::Set_Name` lowercases the entire path** then relies on
     `Resolve_File` directory scans to case-correct. iOS is case-sensitive AND
     sandbox-denies scanning parents like `/private/var/mobile/Containers`, so
     user-dir paths stayed broken-lowercase and every config/save open failed
     ENOENT. Fixed by skipping the lowercase step on iOS.
- **First-run flow is authentic C&C95**: fresh install → full intro → straight
  to GDI/NOD side-select, no main menu; the game then writes
  `PlayIntro=no` and subsequent launches show the menu. Confirmed working on
  device (CONQUER.INI + RULES.INI + in-game settings persisted in the
  container). Later defaulted `PlayIntro=no` on iOS so first launch also goes
  to the menu (guarded; desktop keeps the faithful default).

## Phase 3/4 features (all confirmed on device)

- **Touch (PeonPad scheme):** deferred gesture state machine in
  `common/wwtouch_sdl2.cpp` — one-finger tap = left click, one-finger drag =
  selection box (anchored at touch-down), two-finger tap = right click
  (deselect, at leftmost finger), three-finger drag = map scroll via the
  gamepad analog-scroll channel. SDL touch→mouse emulation disabled; indirect
  (trackpad) touch devices ignored so pointers still act as mice. Coordinate
  mapping goes through `Video_Touch_To_Game` (letterbox-aware, renderer-pixel
  space) — the earlier "clicks land low" was window-points vs pixels + ignored
  letterbox offset.
- **App lifecycle:** `SDL_AddEventWatch` flips an atomic that gates rendering
  while backgrounded (avoids Metal drawable-acquire death on resume); wired to
  Focus_Loss/Focus_Restore.
- **Cutscene skip:** on iOS a tap (synthesized left click) breaks out of movie
  playback, same seam as Esc on desktop (`VQ_Call_Back` in both games'
  conquer.cpp).
- **On-screen keyboard:** `Show_Virtual_Keyboard` (SDL_StartTextInput/Stop) is
  raised on EditClass focus and around the bespoke `ScoreClass::Input_Name`
  loops; typed characters arrive as `SDL_TEXTINPUT` and are injected via a new
  `Put_Char` + `WWKEY_TEXT_BIT` literal-char path so they bypass the scancode
  keymap (correct case/symbols). Gotchas resolved:
  1. iOS starts with text input active → keyboard showed on menus. Now
     explicitly stopped at startup.
  2. SDL's IsTextInputActive stays true after the user hides the keyboard, so a
     bare StartTextInput won't re-raise it. Track our own intent flag and do a
     stop/start cycle (`Reraise_Virtual_Keyboard`); a tap while a field is
     active re-raises it.
  3. Hardware keyboard on iOS fires both KEYDOWN (scancode) and TEXTINPUT →
     double entry. During text input, only editing/control scancodes are
     forwarded.
  4. SDL's hidden iOS UITextField retained text across sessions, so the
     predictive bar showed stale + current text. Patched SDL
     (`scripts/build/ios/patches/sdl2-ios-textfield-reset.patch`, applied by
     build-deps.sh) to reset the field on every keyboard show.
- **iOS SDK `<endian.h>` pulls MacTypes.h** whose QuickDraw `struct Rect`
  collides with the engine's `Rect`; fixed in `common/endianness.h` by
  preferring `<machine/endian.h>` on Apple platforms.
- **`SDL_MAIN_HANDLED`** was defined in `common/wwkeyboard.h` and
  `redalert/startup.cpp`; both now skip it on iOS so `SDL_main` wrapping works.
- **App icons / names:** generated no-alpha PNGs from the project SVGs
  (`make-icons.sh`), display names "C&C: Tiberian Dawn" / "C&C: Red Alert" set
  via plutil in packaging. SpringBoard caches icons; a device restart may be
  needed for a changed icon to appear.
- **Exit Game hung on a black screen:** on iOS SDL owns the UIApplicationMain
  run loop, so returning from `SDL_main` (after Main_Game clears the screen)
  doesn't terminate the process. Both games now `exit()` explicitly on iOS
  after cleanup; under LiveContainer this returns to the container.
- **Fill-screen scaling — reverted.** Defaulting `Boxing=off` on iOS filled the
  display but stretched the ~4:3 game horizontally; per preference, reverted to
  the aspect-preserving 16:10 letterbox (still adjustable via `[Video] Boxing`).
- **LAN multiplayer — NOT working (open).** iPad↔Mac discovery failed in both
  directions. The Mac host is provably correct (log shows broadcast to
  `10.0.2.255` and `255.255.255.255`). Root cause is iOS blocking UDP
  broadcast/multicast receive for sandboxed apps without the
  `com.apple.developer.networking.multicast` entitlement, which unsigned
  LiveContainer can't supply; the granted "local network" permission only
  covers unicast (Bonjour/direct). Discovery in `common/wspudp.cpp` is
  broadcast-only. Fix path: add a direct-IP **unicast** connect option (UI
  field + unicast connect in wspudp.cpp) to bypass broadcast discovery.
  Single-player is fully working; this is the sole known-incomplete feature.

## Risks / open questions

| Risk | Notes |
|---|---|
| `Settings.Mouse.RawInput`/relative-mode assumptions run deeper than expected | Mitigation: Phase 2 gate uses a real pointer first, isolating touch work. |
| SDL2 static iOS build quirks (framework deps, `SDL_main` link order) | Well-trodden (SDL ships an iOS Xcode project + CMake support); verify early in Phase 1. |
| Sidebar/UI hit targets are small at 640×400 → fat-finger misses | The deferred-tap "down+up at same point" rule helps; if insufficient, consider tap-radius snap to gadgets later — *not* engine UI scaling (huge diff). |
| VQA/WSA playback has its own loops that may not pump lifecycle events | Test backgrounding during intro movie explicitly in the Phase 3 soak. |
| `exit(0)` on `SDL_QUIT` and other desktop-isms | iOS apps don't quit; audit for iOS in Phase 3. |
| Free vs paid Apple ID | Free works for personal installs (7-day expiry, 3-app limit, re-sign weekly); paid ($99/yr) removes both. Plan assumes either. |
| Upstreamability | Keep every change `TARGET_OS_IPHONE`-guarded or in new files; minimal diffs per category (Generals lesson) so pieces can be offered to Vanilla-Conquer upstream. |

## New files summary

```
cmake/ (possible FindSDL2/FindOpenAL tweaks for static iOS libs)
ios/project.yml                      # XcodeGen shell app spec
ios/main.m                           # stub executable for signing shell
ios/Info-defaults + icons            # plist keys, generated PNGs
scripts/build/ios/build-deps.sh      # SDL2 + openal-soft arm64-ios static
scripts/build/ios/package-ios.sh     # assemble, sign, (--install)
common/wwtouch_sdl2.cpp/.h           # deferred-tap state machine
docs/port/IOS_PORT_PLAN.md           # this file; log decisions as they land
```

Modified (all guarded): `CMakeLists.txt` + `common/CMakeLists.txt`,
`CMakePresets.json`, `common/paths_posix.cpp`, `common/video_sdl2.cpp`,
`common/wwkeyboard_sdl2.cpp`, `common/settings.cpp`,
`{tiberiandawn,redalert}/startup.cpp`.
