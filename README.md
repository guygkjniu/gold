# Open GoldSrc Executable

This project is a clean-room remake of the executable side of the GoldSrc engine.
It does not ship game assets. To run real content, point it at an existing game
folder that contains the loose files or packages for that game.

## Current Status

The first milestone is an executable shell:

- parses GoldSrc-style launch options such as `-game`, `-basedir`, and `+map`
- mounts the base directory and selected game directory
- opens a native OpenGL 3D scene with a rotating colored cube
- validates and summarizes GoldSrc BSP v30 map files
- renders loaded BSP faces as untextured colored world geometry
- supports free-camera movement in loaded maps
- supports a first-person test controller with gravity, jumping, and BSP hull collision
- loads WAD3 texture files referenced by a BSP
- renders BSP faces with WAD textures and UV coordinates
- skips common invisible/tool textures such as `CLIP`, `AAATRIGGER`, `ORIGIN`, and `SKY`
- draws red debug squares at entity origins
- detects Source-engine `VBSP` files and reports that they are not GoldSrc maps
- provides a small console command registry
- runs a basic engine frame loop
- supports `--run-once` for smoke testing without opening a long-running session

Audio, networking, lightmaps, model loading, and game DLL integration are
intentionally still future milestones.

## Build

Verified on this machine with Visual Studio Build Tools:

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" && cmake -S . -B build-vs -G Ninja -DCMAKE_CXX_COMPILER=cl && cmake --build build-vs"
```

If your MinGW install has complete C++ headers, this should also work:

```powershell
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build
```

## Run

```powershell
.\build-vs\goldsrc.exe -basedir "C:\Path\To\Half-Life" -game valve +map c0a0
```

First-person controls in a loaded map:

- click the render window once if keyboard movement is not active
- hold right mouse button and move the mouse to look around
- `W`, `A`, `S`, `D` move horizontally
- `Space` jumps
- `Shift` moves faster
- `N` toggles temporary no-clip diagnostic movement
- `Esc` closes the window

The window title shows a small movement debug readout while a map is loaded:
player position, input movement vector, active hull, trace hit/start-solid state,
and whether no-clip is enabled.

For a no-assets smoke test:

```powershell
.\build-vs\goldsrc.exe --run-once +echo hello
```
