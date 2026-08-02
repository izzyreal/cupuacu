# Development Guide

## Overview

Cupuacu is a cross-platform audio editor with a small custom GUI layer on top of SDL and SDL_ttf, a document-oriented editing model, and a PortAudio-backed realtime audio engine.

The codebase is organized around a few major subsystems:

- `src/main/gui`
  The custom retained-mode UI framework, window/event routing, waveform rendering, menus, and editor view composition.
- `src/main/audio`
  Audio device control, callback processing, transport messaging, recording, and realtime playback/preview processing.
- `src/main/effects`
  Typed effect settings, effect dialogs, target resolution helpers, preview sessions/processors, and offline effect application.
- `src/main/actions`
  User-facing editor commands and undoable operations that mutate the document.
- `src/main/file`
  File IO such as WAV rewrite/overwrite behavior.

## Architectural Patterns

### Document + Undoable model

Committed edits mutate `State::activeDocumentSession.document` through undoables. Undoables remain the authoritative path for destructive edits such as trim, amplify/fade, and dynamics.

Preview is deliberately separate from undoables:

- preview is realtime and non-destructive
- apply is offline and undoable

### Effects subsystem

Effects now live under `src/main/effects` in `cupuacu::effects`.

The core patterns are:

- typed per-effect settings structs in `EffectSettings.hpp`
- shared effect target resolution in `EffectTargeting.hpp`
- a generic shared effect dialog shell in `EffectDialogWindow.hpp`
- per-effect offline apply + preview processor implementations

The dialog layer is shared, but the DSP/application layer stays strongly typed.

### Preview processing

Realtime effect preview uses `cupuacu::audio::AudioProcessor`.

Current model:

- the main/UI thread owns editable effect settings
- a preview session owns a long-lived preview processor
- the audio callback reads a thread-safe settings snapshot per buffer
- changing controls while preview is running updates the preview session, so the next buffer hears the new settings

This keeps UI state off the audio thread while still allowing live parameter tweaking.

### Input monitor processing

Input monitoring does not use `AudioProcessor`. It is a continuously stateful
realtime path owned by `InputMonitorPipeline`:

- recording and input metering branch from raw capture before monitor DSP
- 44.1 kHz callback fragments are adapted to WebRTC AEC3's 48 kHz, 10 ms
  processing frames
- `MonitorCancellationBackend` isolates AEC3 so the backend can be replaced
  without changing PortAudio or transport code
- `FeedbackSuppressionMode` is a persisted audio setting and is delivered to
  the callback as an `AudioMessage`; changing it never reopens the stream
- Standard and Smooth own separately prepared AEC3 instances. This keeps mode
  changes allocation-free on the callback and lets their adaptive state remain
  independent
- Off bypasses AEC3, decorrelation, feedback detection, and emergency
  tripping; it is an explicit user choice rather than an error fallback
- the final speaker signal is fed back as the render reference
- Smooth decorrelation uses an aligned dry/wet ramp and release hysteresis;
  Standard retains the original direct response for comparison and for
  hardware on which it behaves better
- AEC3-specific comfort-noise tail damping is kept in `WebRtcAec3Backend` and
  is armed only by the pipeline's feedback-risk detector; it is slow and
  latched for quiet echo-dominated tails, but releases immediately for
  ordinary input
- the emergency feedback guard is a Cupuacu-owned stage

The pinned WebRTC Audio Processing dependency and update procedure are
documented in `cmake/WebRtcAudioProcessing.md`.

### Modal effect dialogs

Effect dialogs are intentionally modal:

- opening a dialog sets `State::modalWindow`
- the app event dispatcher blocks mouse/keyboard interaction to other windows while the modal dialog is open
- preview transport is owned by the dialog, not by the main transport controls

## Realtime / Threading Notes

Two different synchronization models are used:

- `AtomicStateExchange`
  Used for audio device message ingestion and published device state snapshots.
- lightweight preview parameter snapshots
  Used for live effect preview control updates.

`AtomicStateExchange` is a good fit for transport/device control messages, but is intentionally not used for per-buffer effect parameter tweaking. For preview parameters, latest-value-wins snapshot exchange is simpler and cheaper.

When adding realtime code:

- avoid allocation in the callback where possible
- avoid locks in the callback
- prefer immutable snapshots or atomically replaced shared state

Monitor DSP must additionally preserve the following:

- prepare and destroy backends, resamplers, FFT buffers, and device-format
  state off the callback thread
- pass device timing through the PortAudio-independent
  `AudioCallbackTiming` value
- never fall back silently from Standard or Smooth to unprotected output;
  unprotected monitoring is allowed only when the user explicitly selects Off
- keep raw recording samples independent of all monitor processing

## Test Structure

The main test executable is `cupuacu-tests`.

SDL/window/render integration tests are intentionally separate and should not
be mixed back into the ordinary unit suite. The unit suite is expected to stay
cross-platform and headless-friendly.

Important test slices:

- `[audio]`
  Audio callback core, preview processor behavior, device-adjacent logic.
- `[actions]`
  Undoable/document mutation behavior.
- `[gui]`
  Menus, windows, components, waveform rendering, and effect dialog behavior.
- `[file]`
  WAV rewrite/overwrite behavior.

### SDL-backed tests

Many GUI tests create hidden SDL windows. In tests that do this, initialize SDL_ttf first with `cupuacu::test::ensureSdlTtfInitialized()`.

General guidance:

- if a test creates a `gui::Window`, initialize the SDL/TTF test guard first
- assert `window->isOpen()` when the test depends on successful hidden window creation
- prefer targeted component/window tests over broad end-to-end GUI tests when possible

This matters especially on Windows, where hidden-window creation can fail differently than on macOS/Linux and otherwise turn into misleading crashes later in the test.

## Integration Test Contract

Integration tests should run only in one explicitly supported environment:

- Linux
- Xvfb
- `SDL_VIDEODRIVER=x11`
- `SDL_RENDER_DRIVER=software`
- pinned fonts and X11 packages from the Docker image

The dedicated target is:

- `cupuacu-tests-integration`

Enable it with:

```sh
cmake -G Ninja -B build-integration-linux -DCUPUACU_BUILD_INTEGRATION_TESTS=ON
cmake --build build-integration-linux --target cupuacu-tests-integration -j2
```

Run it under the blessed runtime contract:

```sh
DISPLAY=:99 SDL_VIDEODRIVER=x11 SDL_RENDER_DRIVER=software \
  ./build-integration-linux/cupuacu-tests-integration
```

There is also a helper script:

```sh
./scripts/run-integration-linux.sh
```

It reuses `build-integration-linux` for incremental rebuilds. If you need to
force a fresh configure:

```sh
CUPUACU_FORCE_CMAKE_CONFIGURE=1 ./scripts/run-integration-linux.sh
```

For local iteration on macOS, use Podman to run the same Linux image and
runtime contract:

```sh
podman machine start
./scripts/run-integration-podman.sh
```

For a clean joint coverage run that combines `cupuacu-tests` and
`cupuacu-tests-integration` into one `lcov` report:

```sh
podman machine start
./scripts/run-joint-coverage-podman.sh
```

This rebuilds coverage artifacts in a fresh Linux build directory and writes:

- `dist/coverage.info`
- `dist/coverage-html/index.html`
- `dist/junit-unit.xml`
- `dist/junit-integration.xml`

And a pinned Docker image definition:

- [`docker/integration/linux/Dockerfile`](/Users/izmar/git/cupuacu/docker/integration/linux/Dockerfile)

The intention is:

- `cupuacu-tests`: pure unit/planner/action/audio tests
- `cupuacu-tests-integration`: real SDL/window/event/render integration tests

Do not add integration tests back into `cupuacu-tests`.

## Sanitizer Targets

Sanitizer coverage is platform/compiler dependent.

### RTSan

When supported, CMake creates `cupuacu-tests-rtsan`.

This target is intentionally focused, not the full suite. It currently exercises:

- playback path safety
- recording overwrite/append safety
- realtime effect preview safety, including live parameter updates

Relevant file:

- `src/test/test_rtsan.cpp`

A dedicated allocation-probe target verifies that RTSan itself is active. The
RTSan test executable runs the probe as a child process and expects the
intentional realtime allocation to abort.

### TSan

When supported, CMake creates `cupuacu-tests-tsan`.

Unlike RTSan, this target uses the full test source set plus a verification test. That means TSan coverage includes:

- audio preview tests
- GUI/modal preview tests
- normal action/gui/file tests

There is also a dedicated race-probe target used to verify the sanitizer setup itself.

### Platform notes

- macOS/Clang is the primary environment where these sanitizer targets are expected to exist
- Windows CI usually does not run the RTSan/TSan targets here
- treat sanitizer support as conditional, not universal

## Windows-specific Test Notes

Two patterns are worth remembering:

- file replacement semantics differ from Unix
  Replacing an existing file by rename alone is often not sufficient on Windows. Prefer writing a temp file, closing handles, removing the destination, then renaming into place.
- hidden SDL windows can fail more aggressively
  Tests should initialize SDL/TTF and assert that the hidden window actually opened before using it

## File Association Metadata

The formats exposed by the system `Open With` integration must remain aligned
with `probeAvailableOpenFormats()` in `src/main/file/AudioExport.cpp`. When an
open format or extension changes, update all three packaging declarations:

- `cmake/CupuacuInfo.plist.in` for macOS UTIs
- `cmake/Cupuacu.desktop.in` for Linux MIME types
- `packaging/windows-installer.wxs` for Windows extensions

The application receives Linux and Windows file launches through command-line
arguments. SDL translates macOS Finder open-document requests, as well as
desktop drag-and-drop, to `SDL_EVENT_DROP_FILE`.

## Build / Test Commands

Typical local commands:

```sh
cmake -B build-coverage-macos
cmake --build build-coverage-macos --target cupuacu-tests -j2
./build-coverage-macos/cupuacu-tests "[audio]"
./build-coverage-macos/cupuacu-tests "[actions]"
./build-coverage-macos/cupuacu-tests "[gui]"
./build-coverage-macos/cupuacu-tests "[file]"
```

If sanitizer targets exist in your build:

```sh
cmake --build build-coverage-macos --target cupuacu-tests-rtsan -j2
./build-coverage-macos/cupuacu-tests-rtsan
```

Feedback suppression changes also require a hardware pass on macOS, Windows,
and Linux where available. Cover mono and stereo input, same-device and split
input/output selections, monitoring while recording, playback
suspend/resume, a device change, deliberate low-level feedback, and the
emergency trip. Confirm that the callback does not underrun and that recorded
samples remain raw.

and, where supported:

```sh
cmake --build build-coverage-macos --target cupuacu-tests-tsan -j2
./build-coverage-macos/cupuacu-tests-tsan
```

## Principles for New Work

When adding features, prefer:

- strongly typed settings over stringly typed parameter maps
- shared GUI shells for repeated patterns
- vertical slices for effects
- explicit tests for platform-specific behavior
- preview/offline-apply separation

When in doubt, optimize first for:

- correctness of the editing model
- stability of waveform rendering
- thread-safety of realtime paths
- keeping test failures diagnosable on all supported platforms
