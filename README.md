# MoleHole
Black Hole Simulation using OpenGL and RayTracing

## Features

- Real-time black hole physics simulation with Kerr metric
- Interactive 3D visualization with gravitational lensing
- Multiple rendering modes (simulation, visual, rays 2D)
- Scene editor with black holes, meshes, and spheres
- Animation graph for complex simulations
- **NEW: Image and Video Export** - Render high-quality images and videos with configurable parameters

## Image and Video Export

MoleHole now supports exporting high-resolution images and videos of your simulations:

### Image Export
- Export PNG images at any resolution (up to 8K)
- Uses background rendering for best quality
- Current camera position and settings preserved

### Video Export
- Export MP4 videos with H.264 encoding
- Configurable resolution, length, framerate, and simulation tickrate
- Background rendering at maximum speed
- Progress tracking during export

For detailed documentation, see [EXPORT_FEATURE.md](EXPORT_FEATURE.md)

## Dependencies

The project requires the following system libraries on Linux:
- OpenGL 4.6
- X11, Xrandr, Xi, Xxf86vm, Xcursor, Xinerama
- GTK+ 3.0
- Wayland and xkbcommon
- FFmpeg libraries (libavcodec, libavformat, libavutil, libswscale)

Install on Ubuntu/Debian:
```bash
sudo apt-get install libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev \
    libxi-dev libxxf86vm-dev mesa-common-dev libgtk-3-dev wayland-protocols \
    libwayland-dev libxkbcommon-dev libxkbcommon-x11-dev libavcodec-dev \
    libavformat-dev libavutil-dev libswscale-dev
```

## Building

```bash
git clone --recurse-submodules <repo-url>
cd MoleHole
cmake -B build -S .
make -C build -j$(nproc)
```

## Submodule Management 

This project uses git submodules for dependencies.

To clone the repository with all submodules:

    git clone --recurse-submodules <repo-url>

If you already cloned the repository, initialize and update submodules with:

    git submodule update --init --recursive

To fetch the latest updates for all submodules:

    git submodule update --remote --recursive

To switch a submodule to a different branch:

    git config -f .gitmodules submodule.dependencies/imgui.branch docking
    git submodule sync --recursive
    git submodule update --remote dependencies/imgui

For more information, see the official git submodule documentation: https://git-scm.com/book/en/v2/Git-Tools-Submodules
