**WIP, currently doesn't do anything useful. Keep an eye out on the releases.**


# Cupuacu -- A minimalist cross-platform audio editor inspired by Syntrillium Cool Edit

I got tired of existing audio editors and how convoluted it is to do things like:

1. Open WAV
2. Trim/normalize/change gain
3. Save WAV

I'm sure Audacity is wonderful for certain things, but it is not for the above. Step 2 is fine, I have no issues with that. But I don't want a project or a project file. I don't want to be confronted with bit-depths and sample rates that are not related to the file that I want to work on. I don't want to be confronted with file browsers when I want to overwrite-save the file I want to work on. I just want to edit an audio file, nothing more, nothing less.

I have no clue where Audition is these days, but I'm quite sure I don't want to know.

I did come across some cool audio editors for macOS, but they were not free, not open source, obsolete, still had weird UI stuff going on, and... well... just for macOS. I find myself working on all 3 popular platforms: Linux, Windows and macOS. Probably quite a few audio developers, audio engineers, audio enthousiasts and others who want to edit some audio have a similar tendency. And even if you don't need the cross-platformness, who doesn't want a free audio editor that lets you pixelate the complete UI in realtime? This is better than a time machine.

So what's the problem with Cool Edit? Actually, if you only work on a Windows machine, I suppose there's no problem at all! I haven't tried it in a while, but as far as I know you can still run Cool Edit there. In fact, you can run it quite well through Wine on macOS (and probably by extension Linux too, but I haven't tried that). But ultimately, for regular use and somewhat nice integration, running things through Wine doesn't really cut it.

Everything added together, it's clear that what this world needs right now is **Cupuacu**!

## Name

I love fruit, especially uncommon, wild, tropical ones. Cupuacu, typically spelled "cupuaçu", is such a fruit, and I can recommend everyone to try it. And when I say "try fruit x", I mean "try fruit x" and I don't mean "try frozen old fruit x" or "try fruit x juice" or anything like that. Hunt for a real cupuaçu, knowing that it has to be very fresh, give off a rich scent, and be suspicious of those that have a pedicel (the stick that links the fruit to the tree), because this fruit is typically only delicious, sweet and rich in pulp around the seeds if the fruit dropped because it's ripe (and wasn't cut off the tree). The seeds are edible too, but that's not really what I'm usually after when it comes to cupuaçu.

"But what happened to the 'ç' in the name of the project?!" you might ask. And you are completely right. But special characters are still surprisingly hellish in 2025, so I went for `Cupuacu` for simplicity.

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
