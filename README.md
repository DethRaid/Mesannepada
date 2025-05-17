# 𒊓𒊏

Working title: 𒈩𒀭𒉌𒅆𒊒𒁕 (Mesannepada) (r. 2550 - 2525)

This project is an immersive sim video game set in the ancient city of Ur, during the First Dynasty of Ur (c. 2500 BCE). You're an acolyte at the temple of Nanna when someone breaks in and steals some of the temple treasures. You'll have to track down the thief and retrieve the goods - and uncover a dark decret in the process

# Building

- Install [libKTX](https://github.com/KhronosGroup/KTX-Software). Find the latest release in the Releases tab and install it to `D:/Program Files/KTX-Software`. If your system does not have a D drive, you'll have to modify `src/engine/extern/extern.cmake` to set `KTX_DIR` to the directory you installed libKTX to
- You'll also need to install the Vulkan SDK and add its `bin` directory to your PATH
- Open your favorite terminal
- `mkdir builds`
- `cd build`
- `cmake ..`
- Open `mesannepada.sln`, set `SahWindows` as the startup project, and build
- You'll get some messages about being unable to fine ktx.dll, ffx_backend_x64_vkd.dll, and maybe another file. You'll need to manually build FidelityFX - CACAO and FSR at least, but why not build all. Additionally, copy ktx.dll from the KTX SDK to the output directory
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

- This program definitely works on a Nvidia RTX 4070 Super GPU

## Acknowledgements

This software contains source code provided by NVIDIA Corporation.

## Build issues

- Need to manually build fidelityfx - it has a cmakelists, should be possible to automate it? and copy ffx_backend_vk_x64d
- Need to manaully build KTX and copy the DLL - can we just ship the DLL?

## Future projects

Future projects should be named after other kings of Ur

𒀀𒀭𒉌𒅆𒊒𒁕 (A'annepada) (2525 - 2485)
𒈩𒆠𒉘𒉣 (Meskiagnun) (2485 - 2450)
𒂊𒇻𒇻 (Elulu) (2445)
𒂗𒊮𒊨𒀭𒈾 (Enshakushanna) (2430)

That gives us four kings of the First Dynasty of Ur, wichout having to resort to 𒁀𒇻𒇻!

A'annepada is said to be the "son of Mesannepada", but the dates don't line up. Either the dates are wrong, or he word "son" was used poetically. A'annepada re-built a temple to Ninhursag at Tell-al'Ubaid (Nutur?)

And if we run out of those, the kings of Uruk and Ur:
𒈗𒆠𒉌𒂠𒌌𒌌 (Lugal-kinishe-dudu) (2400)
𒈗𒆦𒋛 (Lugal-kisal-si) (2400)

And if I want to be cheeky, I can bring up 𒁀𒇻𒇻 (Balulu), who is mentioned on the Suemrian Kings List but not anywhere else
