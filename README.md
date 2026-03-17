**WIP, currently doesn't do anything useful. Keep an eye out on the releases.**

<p align="center">
  <img width="1037" height="575" alt="image" src="https://github.com/user-attachments/assets/ae2676ae-49b1-498e-9bb5-fe675390fe8a" />
</p>

# Cupuacu -- A minimalist cross-platform audio editor inspired by Syntrillium Cool Edit

Cupuacu started because I sometimes want to do simple things with audio files, like:

1. Open WAV
2. Trim/normalize/change gain
3. Save WAV

I'm not so keen on the workflow for this kind of task with Audacity, so I decided to take a stab at writing an audio editor myself.

Many years ago I used to work with Cool Edit, and it has many of the aspects that I would like to see in an audio editor. Chances are if you like Cool Edit, you might like where Cupuacu is going.

## Name

I love fruit, especially uncommon, wild, tropical ones. Cupuacu, typically spelled "cupuaçu", is such a fruit, and I can recommend everyone to try it. For this project I was looking a name that is reminiscent of Cool Edit, and Cupuacu is a close enough match.

## Development setup

* CMake
* C++

That's it, really. Then you do something like this:

```sh
git clone https://github.com/izzyreal/cupuacu
cd cupuacu
cmake -B build
cmake --build build --target Cupuacu
./build/Cupuacu
```

That will default to the `make` build system on macOS and Linux, which is fine. But in reality I tend to use Ninja, because it gives you an easy way to switch between Debug and Release builds. So we get something like this (after installing Ninja on your system):

```sh
git clone https://github.com/izzyreal/cupuacu
cd cupuacu
cmake -G "Ninja Multi-Config" -B build
cmake --build build --config Debug --target Cupuacu
./build/Cupuacu
```

The `CMakeLists.txt` creates a nice little `compile_commands.json` in the root of the repo, making it play nice with `vim` and `YouCompleteMe`.

## Developer notes

There is now a separate developer-oriented guide in [`DEV.md`](./DEV.md).

It covers:

- codebase structure and architecture
- effect implementation patterns
- realtime preview design
- modal dialog behavior
- sanitizer targets (`RTSan` / `TSan`)
- SDL-backed test behavior and platform-specific test notes, especially on Windows

Typical local test commands:

```sh
cmake --build build-coverage-macos --target cupuacu-tests -j2
./build-coverage-macos/cupuacu-tests "[audio]"
./build-coverage-macos/cupuacu-tests "[actions]"
./build-coverage-macos/cupuacu-tests "[gui]"
./build-coverage-macos/cupuacu-tests "[file]"
```

Integration tests are intentionally separate from the unit suite. They are
meant to run only in a pinned Linux/Xvfb environment:

```sh
cmake -G Ninja -B build-integration-linux -DCUPUACU_BUILD_INTEGRATION_TESTS=ON
cmake --build build-integration-linux --target cupuacu-tests-integration -j2
DISPLAY=:99 SDL_VIDEODRIVER=x11 SDL_AUDIODRIVER=dummy SDL_RENDER_DRIVER=software \
  ./build-integration-linux/cupuacu-tests-integration
```

There is also a helper script for the blessed Linux contract:

```sh
./scripts/run-integration-linux.sh
```

On macOS, you can run that same Linux contract locally through Podman:

```sh
podman machine start
./scripts/run-integration-podman.sh
```

Where supported, sanitizer targets are also available:

```sh
cmake --build build-coverage-macos --target cupuacu-tests-rtsan -j2
./build-coverage-macos/cupuacu-tests-rtsan
```

and:

```sh
cmake --build build-coverage-macos --target cupuacu-tests-tsan -j2
./build-coverage-macos/cupuacu-tests-tsan
```
