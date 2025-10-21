# Export System Architecture Diagram

```
┌─────────────────────────────────────────────────────────────────────┐
│                        MoleHole Application                          │
│                                                                      │
│  ┌────────────────────────────────────────────────────────────┐    │
│  │                     Main Window                             │    │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌───────────┐  │    │
│  │  │   View   │  │  System  │  │  Scene   │  │Simulation │  │    │
│  │  │          │  │          │  │          │  │           │  │    │
│  │  │  [Menu]  │  │  [Panel] │  │  [Panel] │  │  [Panel]  │  │    │
│  │  └────┬─────┘  └──────────┘  └──────────┘  └───────────┘  │    │
│  │       │                                                     │    │
│  │       │ "Show Export Window"                               │    │
│  │       ▼                                                     │    │
│  │  ┌──────────────────────────────────────────────┐          │    │
│  │  │         Export Window                        │          │    │
│  │  │  ┌────────────────┬────────────────┐         │          │    │
│  │  │  │ Image Export   │ Video Export   │         │          │    │
│  │  │  ├────────────────┴────────────────┤         │          │    │
│  │  │  │                                 │         │          │    │
│  │  │  │  Resolution: [1920] x [1080]    │         │          │    │
│  │  │  │  [1080p] [4K]                   │         │          │    │
│  │  │  │                                 │         │          │    │
│  │  │  │  Length: [10.0] seconds         │         │          │    │
│  │  │  │  Framerate: [60] fps            │         │          │    │
│  │  │  │  Tickrate: [60.0] tps           │         │          │    │
│  │  │  │                                 │         │          │    │
│  │  │  │  [Export Image/Video...]        │◄────────┼─ User    │    │
│  │  │  │                                 │         │          │    │
│  │  │  ├─────────────────────────────────┤         │          │    │
│  │  │  │ Progress: ████████░░░░░ 80%     │         │          │    │
│  │  │  │ Status: Rendering frame 48/60   │         │          │    │
│  │  │  └─────────────────────────────────┘         │          │    │
│  │  └──────────────────────────────────────────────┘          │    │
│  └─────────────────────────────────────────────────────────────┘    │
│                                                                      │
│  ┌────────────────────────────────────────────────────────────┐    │
│  │                    Application Class                        │    │
│  │                                                             │    │
│  │  ┌──────────┐  ┌────────────┐  ┌────────────────────────┐ │    │
│  │  │ Renderer │  │ Simulation │  │   ExportRenderer       │ │    │
│  │  │          │  │            │  │                        │ │    │
│  │  │ (Main    │  │ (Scene     │  │ (Background Rendering) │ │    │
│  │  │ Viewport)│  │  State)    │  │                        │ │    │
│  │  └────┬─────┘  └─────┬──────┘  └───────┬────────────────┘ │    │
│  └───────┼──────────────┼─────────────────┼──────────────────┘    │
│          │              │                 │                        │
└──────────┼──────────────┼─────────────────┼────────────────────────┘
           │              │                 │
           │              │                 │
           ▼              ▼                 ▼
    ┌────────────┐  ┌──────────┐    ┌──────────────────────┐
    │   OpenGL   │  │  Scene   │    │   Export Thread      │
    │  Context   │  │  Data    │    │                      │
    │            │  │          │    │  ┌────────────────┐  │
    │  - Window  │  │- Cameras │    │  │ Offscreen FBO  │  │
    │  - Main FBO│  │- Objects │    │  │ ┌────────────┐ │  │
    └────────────┘  └──────────┘    │  │ │Color Tex   │ │  │
                                    │  │ │Depth RBO   │ │  │
                                    │  │ └────────────┘ │  │
                                    │  │                │  │
                                    │  │ Render Loop:   │  │
                                    │  │ 1. Clear       │  │
                                    │  │ 2. Update Sim  │  │
                                    │  │ 3. Render      │  │
                                    │  │ 4. Read Pixels │  │
                                    │  │ 5. Encode      │  │
                                    │  └────────────────┘  │
                                    │                      │
                                    │  ┌────────────────┐  │
                                    │  │ FFmpeg Encoder │  │
                                    │  │ ┌────────────┐ │  │
                                    │  │ │ RGB→YUV420 │ │  │
                                    │  │ │ H.264 Codec│ │  │
                                    │  │ │ MP4 Format │ │  │
                                    │  │ └────────────┘ │  │
                                    │  └────────────────┘  │
                                    │          │           │
                                    │          ▼           │
                                    │   ┌────────────┐     │
                                    │   │Output File │     │
                                    │   │ .png/.mp4  │     │
                                    │   └────────────┘     │
                                    └──────────────────────┘

Data Flow:
──────────
1. User configures export parameters in UI
2. UI creates ExportRenderer with config
3. ExportRenderer launches background thread
4. Thread creates offscreen framebuffer
5. For each frame:
   a. Update simulation state
   b. Render scene to offscreen FBO
   c. Read pixels from GPU
   d. Encode frame (video) or save (image)
6. Report progress back to UI
7. Clean up resources
8. Notify completion

Thread Safety:
─────────────
- Export runs in detached std::thread
- Progress updates are atomic (float)
- Scene data is read-only during export
- No mutex needed for progress display
- Main thread continues UI updates

Performance:
───────────
- No viewport dependency
- Full GPU acceleration
- Parallel to main rendering
- Minimal CPU usage
- I/O on separate thread
```

## Component Responsibilities

### UI (UI.cpp)
- Display export configuration controls
- Validate user input
- Launch export with file dialog
- Display progress updates
- Handle user interaction

### ExportRenderer
- Manage offscreen OpenGL framebuffer
- Create independent camera
- Initialize renderers (BlackHole/Visual)
- Coordinate rendering pipeline
- Handle FFmpeg encoding
- Track and report progress
- Clean up resources

### OpenGL FBO
- Color texture (RGB, export resolution)
- Depth renderbuffer (24-bit + 8-bit stencil)
- Independent of main window framebuffer
- Can render at any resolution

### FFmpeg Pipeline
1. **Codec Context**: H.264 encoder configuration
2. **Format Context**: MP4 container
3. **SwsContext**: RGB24 → YUV420P conversion
4. **AVFrame**: Video frame buffer
5. **AVPacket**: Encoded data packets

### Simulation Integration
- Export reads current simulation state
- Video export advances simulation time
- Tickrate controls simulation speed
- Independent of main simulation loop
- State preserved after export

## File Structure

```
src/
├── Application/
│   ├── Application.h     [+3 lines]  - Added ExportRenderer member
│   ├── UI.h             [+21 lines] - Export window interface
│   └── UI.cpp          [+182 lines] - Export UI implementation
│
├── Renderer/
│   ├── ExportRenderer.h  [NEW: 58 lines] - Export renderer interface
│   └── ExportRenderer.cpp[NEW:378 lines] - Export implementation
│
└── [Other existing files unchanged]

Documentation/
├── EXPORT_FEATURE.md     [NEW:130 lines] - Technical docs
├── EXPORT_UI_GUIDE.md    [NEW:136 lines] - User guide
├── IMPLEMENTATION_SUMMARY.md [NEW:199 lines] - This summary
└── README.md            [+52 lines] - Updated main readme
```

## Configuration Examples

### 1080p 60fps Video (Standard)
```
Width: 1920
Height: 1080
Length: 10.0s
Framerate: 60fps
Tickrate: 60.0tps
Result: 600 frames, ~50MB file
```

### 4K Slow Motion
```
Width: 3840
Height: 2160
Length: 5.0s
Framerate: 120fps
Tickrate: 30.0tps
Result: 600 frames, 4x slow motion, ~200MB file
```

### Time Lapse
```
Width: 1920
Height: 1080
Length: 10.0s
Framerate: 30fps
Tickrate: 240.0tps
Result: 300 frames, 8x speed, ~25MB file
```
