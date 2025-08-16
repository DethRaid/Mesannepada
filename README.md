# 𒈩𒀭𒉌𒅆𒊒𒁕

Working title: 𒈩𒀭𒉌𒅆𒊒𒁕 (Mesannepada) (r. 2550 - 2525)

This project is an immersive sim video game set in the ancient city of Ur, during the First Dynasty of Ur (c. 2500 BCE). You're an acolyte at the temple of Nanna when someone breaks in and steals some of the temple treasures. You'll have to track down the thief and retrieve the goods - and uncover a dark decret in the process

# Building

- Install the Vulkan SDK
- Install [libKTX](https://github.com/KhronosGroup/KTX-Software). Find the latest release in the Releases tab and install it to `D:/Program Files/KTX-Software`. If your system does not have a D drive, you'll have to modify `src/engine/extern/extern.cmake` to set `KTX_DIR` to the directory you installed libKTX to
- Open your favorite terminal
- `mkdir builds`
- `cd build`
- `cmake ..`
- Open `mesannepada.sln`, set `SahWindows` as the startup project, and build
- You'll get some messages about being unable to find ktx.dll, ffx_backend_x64_vkd.dll, and maybe another file. You'll need to manually build FidelityFX - CACAO and FSR at least, but why not build all. Additionally, copy ktx.dll from the KTX SDK to the output directory
- Alright now it probably works

The build outputs all relevant files to `build/out`

# Directory Structure

This project follows the Pitchfork directory structure (mostly)

- build/ - Build output directory
- build/out/ - Final build artifacts, no intermediate files
- build/out/shaders/ - compiled SPIR-V shaders, and dependenciy lists
- build/out/assets/ - assets for the final game to use
- data/ - Data for the project, including game assets
- data/game/ - Data for the game itself. This folder is copied to build/out/assets/game
- docs/ - Documentation on the project
- extras/Saved/ - various files I've saved durind development, including profiling information. Not in source control
- extras/SourceArt/ - source files for the art in the game. .blend files, high-resolution textures, etc
- src/ - source code for the project
- src/engine/ - source code for the game's engine. Compiled as a library, statically linked in
- src/engine/extern/ - external libraries for the engine
- src/game/ - source code for the game itself. It's often unclear what's game-specific and what's general enough to be in the engine, all well
- src/game/extern/ - external libraries for the game
- tools/ - various tools that are used during development, including Tracy and the shader compilation scripts

# How to play

WASD to move, mouse to look around, E to interact. When you're holding an object, use E to place it down gently or left mouse to throw it. When you're not holding an object, use left mouse to attack. CRTL to crouch, SHIFT to sprint. ESC opens the options menu

# Known-good configurations

- This program has been tested on a RTX 4070 Super GPU, on both Windows 11 and Manjaro Linux with proprietary drivers. It does not work on Mesa, because Mesa's shader compiler chokes on one of my shader instructions. It does not support X11-based Linux systems, but _should_ otherwise work on systems that support Vulkan
- This program does not support macOS, iOS, or Android

## Acknowledgements

This software contains source code provided by NVIDIA Corporation.

## Build issues

- Need to manually build fidelityfx - it has a cmakelists, should be possible to automate it? and copy ffx_backend_vk_x64d
- Need to manaully build KTX and copy the DLL - can we just ship the DLL?
