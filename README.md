**WIP, currently doesn't do anything useful. Keep an eye out on the releases.**


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

Enjoy!
