# Lessons Learned: Porting a Classic Windows Game to iOS/iPadOS

A reusable field guide for porting an old SDL2-capable C/C++ game engine to
iOS/iPadOS, written for an LLM (or engineer) doing similar work. It is distilled
from porting Vanilla Conquer (the first-generation Command & Conquer engine:
Tiberian Dawn + Red Alert) to run natively on an iPad via LiveContainer.

The point of this document is not the specific code — it is the **order of
operations, the diagnostic reflexes, and the specific traps** that cost the most
time, so the next port spends its time on new problems instead of these.

---

## 0. The single most important habit: get a log off the device early

The highest-leverage thing we did was add a **file logger that writes to the
app sandbox** on iOS, before trying to debug anything on-device:

```c
#if defined(__APPLE__) && TARGET_OS_IPHONE
    const char* home = getenv("HOME");
    if (home) { snprintf(path, n, "%s/Documents/<app>-debug.log", home); File = fopen(path, "w"); }
#endif
```

iOS has no console you can watch. Every on-device failure before this was a
guess ("black screen, then exit" could be a dozen things). Every failure after
it was a one-shot diagnosis: pull the log over USB, read the last line, fix the
named cause. **Do this in the first hour, not the tenth.**

Corollary: build a **Debug config for device** (keep asserts and verbose
logging), separate from the release build. The log is worthless if the level is
compiled out.

---

## 1. Sequence the work by de-risking, and gate on behavior not builds

The order that worked, each phase with a *behavioral* acceptance gate:

0. **Desktop baseline first.** Get the game fully playable on your dev machine
   (macOS here) with real data. This proves the asset set and gives a reference
   for "correct." Most non-iOS-specific bugs are cheaper to find here.
1. **Cross-compile to a linking binary.** Gate: the arm64-iOS binary exists and
   verifies (`otool -l | grep LC_BUILD_VERSION` shows `platform 2`). Not "runs".
2. **Boots to first screen on device.** Gate: the main menu renders on the
   actual hardware. This is the true halfway point — most of the filesystem and
   packaging pain surfaces here.
3. **Playable with native input.** Gate: complete a level using only touch.
4. **Polish** (text entry, icons, lifecycle, exit) last.

"It compiled" is never a gate. A binary that links and dies on launch taught us
nothing; "the menu rendered on the iPad" told us the whole boot path worked.

---

## 2. iOS-specific traps that cost real time (in the order we hit them)

These are the concrete, repeatable gotchas. Each is small once known and
expensive to discover.

### 2.1 The iOS SDK `<endian.h>` drags in `MacTypes.h`
On the iOS sysroot, `#include <endian.h>` transitively pulls in Carbon
`MacTypes.h`, which defines a legacy QuickDraw `struct Rect`. Any engine with
its own `Rect` type gets a redefinition error. Fix: prefer
`<machine/endian.h>` on Apple, or otherwise avoid the top-level `<endian.h>`.
**Lesson:** old engines collide with old Apple headers on common short names
(`Rect`, `Point`, `Boolean`, `Byte`). Expect a few of these.

### 2.2 `SDL_MAIN_HANDLED` must NOT be set on iOS
On desktop, projects often `#define SDL_MAIN_HANDLED` and call their own
`main`. On iOS, SDL *must* wrap `main` in `UIApplicationMain` via `SDL_main.h`,
or the app never starts (no window, no crash, just nothing). Guard the define
off for iOS and `#include <SDL_main.h>`.

### 2.3 The read-only bundle vs. the writable sandbox
The app bundle is read-only and code-signed. Anything the engine writes
(config, saves) must go to a writable dir (`Documents`, `Library/Application
Support`). Two sub-traps:
- If the engine finds data by scanning `argv[0]`/CWD, that logic won't run
  normally under iOS. We pointed the data path at the bundle and `chdir()`'d
  there at startup.
- **Decide where read-only data lives and make the engine's data-path resolver
  return exactly that.** Our first attempt pointed it at an empty `Documents`
  dir; the bundled data was invisible because the file layer only searched the
  configured data path, never the bundle.

### 2.4 The filesystem is case-sensitive AND the sandbox blocks parent scans
This one was subtle and total. The engine (a DOS/Windows port) lowercased every
path, then "fixed" case by scanning the directory for a matching name. On iOS
that fails twice: the filesystem is case-sensitive, *and* the sandbox forbids
scanning parent directories like `/private/var/mobile/Containers`. Result: every
file in the writable user dir failed to open with ENOENT — but bundle files
(in a scannable dir) worked, which made it look intermittent. Fix: skip the
lowercasing on iOS and rely on exact-case paths.
**Lesson:** any "lowercase then re-resolve by scanning" scheme is a landmine on
case-sensitive sandboxed filesystems. Look for it early.

### 2.5 Ancient error handling turns "file missing" into a fatal crash
On a true first run (no config file anywhere), the engine's file layer treated
*any* failed open as "wrong CD inserted" and deliberately crashed (wrote to a
null pointer). We only avoided it on desktop because a config file already
existed. Fresh installs are the *common* case on mobile. **Lesson:** exercise
the genuinely-fresh-install path (delete all app data) early, and guard the
first-run file loads.

### 2.6 SDL owns the run loop — returning from `main` does NOT quit
On iOS, `SDL_main` returning does not terminate the process; `UIApplicationMain`
keeps running. The game's "Exit" cleared the screen and returned, leaving a
black screen forever. Fix: `exit(0)` explicitly on iOS after cleanup.

### 2.7 iOS starts with text input active
`SDL_IsTextInputActive()` is often true at launch on iOS, so the software
keyboard pops up on menus. Explicitly `SDL_StopTextInput()` at startup; only
enable it when a real text field has focus.

### 2.8 `SDL_IsTextInputActive()` lies after the user hides the keyboard
When the user taps iOS's dismiss-keyboard key, SDL still reports text input
active, so a bare `SDL_StartTextInput()` is a no-op and the keyboard won't come
back. Track your *own* "a field wants input" flag, and do a
`StopTextInput()`+`StartTextInput()` cycle to force a re-raise.

### 2.9 Hardware + software keyboard double-entry
With text input active, an iOS *hardware* keyboard fires both `SDL_KEYDOWN`
(scancode) and `SDL_TEXTINPUT` (character). If you consume both, every letter is
typed twice. During text input, route only editing/control keys (backspace,
enter, arrows) through the scancode path and let printable characters come from
`SDL_TEXTINPUT`.

### 2.10 The predictive-text bar keeps stale context across sessions
SDL's hidden iOS `UITextField` retains its contents between name-entry sessions,
so the suggestion bar shows previous + current text (and tapping a suggestion
inserts a whole word your per-character loop can't handle). Reset the field to
its placeholder every time the keyboard is raised. Since we build SDL from
source, we patched it and kept the patch under version control (see 4.3).

### 2.11 UDP broadcast/multicast receive is blocked for sandboxed apps
LAN discovery by UDP broadcast does not work for a sandboxed iOS app without the
`com.apple.developer.networking.multicast` entitlement (Apple-approved, tied to
a real signed identity — unavailable under unsigned LiveContainer). The "local
network" permission the user grants only covers *unicast* (Bonjour/direct
connection). If the classic game only discovers peers by broadcast, LAN
multiplayer will silently find nothing. The fix is real work: add a direct-IP
unicast connect path. We deferred it.

---

## 3. Input: touch for a mouse-first game

- **Disable SDL's built-in touch→mouse emulation** (`SDL_HINT_TOUCH_MOUSE_EVENTS
  = "0"`) and its inverse; also drop `SDL_TOUCH_MOUSEID` events on the mouse
  path. Then build real gestures. The built-in emulation is unusable for RTS
  (touch-then-drag instantly clicks).
- **Deferred gesture commit is the core idea:** emit nothing on finger-down.
  Decide the gesture as it develops (tap vs. drag vs. multi-finger), then
  synthesize the appropriate mouse events *through the engine's existing mouse
  path* so game logic is untouched. A committed action that gets cancelled must
  never leave a stray click behind.
- A workable scheme (borrowed from another RTS touch port): one-finger tap =
  left click, one-finger drag = selection box (anchored at touch-down), two-
  finger tap = right-click, three-finger drag = camera pan. Reserving finger
  *count* for gesture type is cleaner than time/motion heuristics.
- **Coordinate mapping is a recurring bug source.** Map touches through the
  actual on-screen render rectangle in *renderer-pixel* space, accounting for
  any letterbox offset. Two mistakes we made and fixed: ignoring the letterbox
  bars (clicks landed low) and mixing window *points* with pixel-space scale
  (2× off on Retina).
- Ignore *indirect* touch devices (trackpads report as touch) so external
  pointers keep behaving like mice.

---

## 4. Build, packaging, and dependencies

### 4.1 Static-link the dependencies
Static SDL2 + OpenAL avoids the entire embed-dylibs / `install_name_tool` /
re-sign dance. Prove each dependency builds standalone for `arm64-ios` first
(`otool -l` shows `platform 2`, `lipo -info` shows arm64), then point the game's
CMake at the install prefix. Static SDL2 pulls the needed iOS frameworks
(UIKit/Metal/CoreAudio/etc.) in via its exported CMake config — use
`find_package(SDL2 CONFIG)` so those transitive links come along.

### 4.2 Unsigned IPA + a container is the fastest iteration path
For development, an unsigned `.ipa` run through LiveContainer skips code signing
and provisioning entirely. Package = zip `Payload/<App>.app/` with the binary +
`Info.plist` + icons + data. Push over USB into the container's Documents. This
removed a whole category of friction; reach for real signing only when you need
a standalone install.

### 4.3 Patch vendored dependencies in-tree, not by hand
When you must modify a dependency (we patched SDL's iOS text field), save it as
a `.patch` file in the repo and apply it in the dependency build script. A
hand-edit to an extracted source tree evaporates the next time someone cleans
`build-deps/` — which *will* happen (see 6).

### 4.4 Info.plist details that matter
- Empty `UILaunchScreen` dict → native resolution (else legacy scaled mode).
- `UIRequiresFullScreen`, landscape orientations, `UIStatusBarHidden`.
- Loose-PNG icons in the bundle root (`AppIcon60x60@2x.png`, etc.) for
  container/dev installs; **icons must have no alpha channel** or iOS rejects
  them. SpringBoard caches icons aggressively — a changed icon may need a device
  restart to appear.
- Set display names with special characters (`&`) via `plutil -replace`, not
  `sed` (where `&` is special in the replacement).

---

## 5. App lifecycle

iOS can seize the Metal drawable when the app is backgrounded *without*
backgrounding your process first; issuing GPU work around suspension causes
drawable-acquire failures on resume. Use `SDL_AddEventWatch` (a watcher, not the
poll loop — the events can arrive after your loop stops) to set an atomic flag
on `WILL_ENTER_BACKGROUND` / `DID_ENTER_FOREGROUND`, and gate *both* simulation
and presentation on it. Wire it to whatever focus-lost/gained handling the
engine already has.

---

## 6. Process notes that paid off

- **Keep every platform change guarded or in new files** (`#if
  defined(__APPLE__) && TARGET_OS_IPHONE`, or `*_sdl2.cpp`-style split files).
  Desktop must keep building byte-identically. This also keeps each change small
  enough to offer back upstream.
- **The engine fixes were upstreamable bugs, not iOS hacks.** The `Rect` clash,
  the first-run crash, the case-sensitivity failure, an out-of-bounds read
  (found with AddressSanitizer) — all are real portability bugs that happened to
  surface on iOS. Fixing them at the root (guarded where behavior must differ)
  is better than working around them.
- **Watch disk space.** Bundled game data makes every IPA hundreds of MB;
  repeated builds + dep source trees + staging copies fill a disk fast. We hit a
  full disk that stopped everything. Keep `build/`, `build-deps/`, and `dist/`
  git-ignored, and prune regenerable trees.
- **AddressSanitizer earns its keep.** A "black screen / heap corruption" that
  looked like a renderer bug was an out-of-bounds `Mem_Copy`; ASan named the
  file and line in seconds after ages of guessing.
- **Don't over-trust `git check-ignore` assumptions — verify.** Before the first
  commit, confirm no binaries/game data are staged (`git diff --cached --stat`).

---

## 7. Quick reference: symptom → cause

| Symptom | Likely cause |
|---|---|
| App launches, nothing happens, no crash | `SDL_MAIN_HANDLED` set on iOS (2.2) |
| Black screen then immediate exit on launch | Data not found → engine's fatal file-error path (2.3, 2.5) |
| Works from CLI/console launch, dies from home-screen icon | Watchdog killed a slow init on the main thread |
| Files in user dir fail ENOENT, bundle files work | Path lowercasing + case-sensitive sandbox (2.4) |
| Redefinition of `Rect`/`Point`/`Boolean` | iOS SDK legacy headers via `<endian.h>` (2.1) |
| Exit leaves a black screen forever | Returning from `main` doesn't quit on iOS (2.6) |
| Software keyboard shows on menus | Text input active at startup (2.7) |
| Keyboard won't come back after dismiss | `SDL_IsTextInputActive()` stale; need stop/start cycle (2.8) |
| Every typed letter doubles | Hardware keyboard fires KEYDOWN + TEXTINPUT (2.9) |
| Suggestion bar shows old text | SDL iOS text field retains context (2.10) |
| Taps land offset from finger | Letterbox / points-vs-pixels in coordinate mapping (3) |
| LAN multiplayer finds no games | UDP broadcast blocked for sandboxed apps (2.11) |

---

*Companion document: [IOS_PORT_PLAN.md](IOS_PORT_PLAN.md) contains the concrete
per-phase plan, decisions, and the chronological findings log for this specific
port.*
