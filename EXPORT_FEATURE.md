# Image and Video Export Feature

This document describes the newly implemented image and video export functionality for the MoleHole black hole simulation application.

## Overview

The export system allows users to render high-quality images and videos of their simulations with configurable parameters, using background rendering that doesn't depend on viewport capture.

## Features Implemented

### 1. ExportRenderer Class (`src/Renderer/ExportRenderer.h/cpp`)
- Background rendering system using offscreen framebuffers
- Independent of main viewport rendering
- Configurable resolution up to 8K (7680x4320)
- Progress tracking with callback support

### 2. Image Export
- **Format**: PNG
- **Configuration Options**:
  - Resolution (width x height)
  - Preset buttons for common resolutions (1080p, 4K)
- **Features**:
  - Renders current scene at specified resolution
  - Uses current camera position and settings
  - Saves to user-selected location via file dialog
  - Progress indication during export

### 3. Video Export  
- **Format**: MP4 (H.264 codec)
- **Configuration Options**:
  - Resolution (width x height)
  - Video length in seconds
  - Framerate (fps)
  - Tickrate (simulation speed in ticks per second)
- **Features**:
  - Renders simulation over time
  - Independent framerate and simulation tickrate
  - Background rendering as fast as possible
  - Progress indication showing current frame
  - Uses ffmpeg libraries for video encoding

### 4. UI Integration
- New "Export" window accessible from View menu
- Two tabs: Image Export and Video Export
- Real-time preview of export settings
- Progress bar and status display
- Non-blocking export (runs in background thread)

## Technical Details

### Dependencies
- **FFmpeg Libraries**: Added via pkg-config
  - libavcodec (video encoding)
  - libavformat (container format)
  - libavutil (utilities)
  - libswscale (color space conversion)
- **STB Image Write**: For PNG export (already present)

### Build System Changes
- Updated CMakeLists.txt to find and link FFmpeg libraries on Unix systems
- Uses pkg-config for dependency resolution
- Properly handles include paths for FFmpeg headers

### Architecture
```
UI (UI.cpp)
  └─> ExportRenderer
       ├─> Offscreen Framebuffers (OpenGL FBO)
       ├─> BlackHoleRenderer (for simulation rendering)
       ├─> VisualRenderer (for visual mode)
       └─> FFmpeg (for video encoding)
```

### Key Implementation Points
1. **Offscreen Rendering**: Uses OpenGL framebuffer objects (FBO) with color texture and depth renderbuffer
2. **Thread Safety**: Export runs in separate thread to avoid blocking UI
3. **Simulation Integration**: Properly advances simulation time during video export
4. **Memory Management**: Properly cleans up OpenGL and FFmpeg resources
5. **Error Handling**: Comprehensive error checking for file I/O and encoding

## Usage Instructions

### Exporting an Image
1. Open "View" -> "Show Export Window"
2. Go to "Image Export" tab
3. Configure resolution (or use preset buttons)
4. Click "Export Image (PNG)..."
5. Choose output location and filename
6. Monitor progress in the progress section

### Exporting a Video
1. Open "View" -> "Show Export Window"
2. Go to "Video Export" tab
3. Configure:
   - Resolution (output video size)
   - Length (duration in seconds)
   - Framerate (video fps)
   - Tickrate (simulation speed)
4. Click "Export Video (MP4)..."
5. Choose output location and filename
6. Monitor progress showing current frame

## Configuration Options Explained

### Image Export
- **Width/Height**: Output image dimensions in pixels
- Common presets available for convenience

### Video Export
- **Width/Height**: Output video dimensions in pixels
- **Length**: Total duration of the exported video in seconds
- **Framerate**: Number of frames per second in the output video (affects smoothness)
- **Tickrate**: Simulation updates per second (affects simulation speed)
  - Higher tickrate = faster simulation
  - Can be different from framerate for slow-motion or time-lapse effects

## Notes
- Export uses the current camera position and orientation
- Export respects the current viewport mode (Simulation3D vs SimulationVisual)
- Do not close the application while an export is in progress
- Larger resolutions and longer videos will take more time to render
- Video encoding uses H.264 with 4Mbps bitrate for good quality/size balance

## Future Enhancements (Not Implemented)
- Custom camera path animation during video export
- Multiple output format options
- Configurable video bitrate and quality settings
- Batch export functionality
- Export queue management
- Real-time preview during export
