# Export UI Panel Guide

## Accessing the Export Panel

1. Launch MoleHole application
2. Click on the "View" menu in the top menu bar
3. Select "Show Export Window"
4. The Export window will appear with two tabs: "Image Export" and "Video Export"

## Image Export Tab

### Controls:
- **Width**: Set the output image width in pixels (default: 1920)
- **Height**: Set the output image height in pixels (default: 1080)
- **Preset Buttons**:
  - "Set 1080p (1920x1080)": Quick set to Full HD resolution
  - "Set 4K (3840x2160)": Quick set to 4K UHD resolution

### Preview Section:
- **Resolution**: Shows the configured output resolution
- **Aspect Ratio**: Displays the aspect ratio of the output

### Export Button:
- **Export Image (PNG)...**: Opens file dialog to choose save location
  - Renders the current scene at the specified resolution
  - Saves as PNG format with full quality
  - Progress shown in the Progress section below

## Video Export Tab

### Controls:
- **Width**: Set the output video width in pixels (default: 1920)
- **Height**: Set the output video height in pixels (default: 1080)
- **Preset Buttons**:
  - "Set 1080p (1920x1080)": Quick set to Full HD resolution
  - "Set 4K (3840x2160)": Quick set to 4K UHD resolution
- **Length (seconds)**: Duration of the output video (default: 10.0)
- **Framerate (fps)**: Video frames per second (default: 60)
  - Higher values = smoother video
  - Common values: 24, 30, 60, 120
- **Tickrate (tps)**: Simulation ticks per second (default: 60.0)
  - Controls how fast the simulation advances
  - Independent of framerate for creative control
  - Tickrate > Framerate = fast motion effect
  - Tickrate < Framerate = slow motion effect

### Preview Section:
- **Resolution**: Shows the configured output resolution
- **Duration**: Shows the video length in seconds
- **Total Frames**: Calculated total number of frames to render
- **Aspect Ratio**: Displays the aspect ratio of the output

### Export Button:
- **Export Video (MP4)...**: Opens file dialog to choose save location
  - Renders the simulation from current state
  - Encodes as H.264 MP4 video
  - Progress shown with current frame number

## Progress Section

Located at the bottom of the Export window:

### While Idle:
- Shows "No export in progress"

### During Export:
- **Status**: "Export in Progress" in green text
- **Progress Bar**: Shows completion percentage (0-100%)
- **Status Text**: Shows current operation:
  - Image: "Initializing...", "Rendering frame...", "Saving PNG...", etc.
  - Video: "Rendering frame X/Y" where X is current frame and Y is total
- **Warning**: "Do not close the application while exporting"

### After Completion:
- Status changes to "Complete"
- Progress bar shows 100%
- Can start new export

## Tips for Best Results

### Image Export:
- Set up your desired camera angle before exporting
- Higher resolutions take longer but produce better quality
- 4K (3840x2160) is excellent for high-quality screenshots
- 8K (7680x4320) is available for extreme detail (very slow)

### Video Export:
- Longer videos take significantly more time to render
- Frame count = Length × Framerate (e.g., 10s × 60fps = 600 frames)
- Each frame is fully rendered, so be patient with long/high-res videos
- Use tickrate to control simulation speed:
  - Tickrate = Framerate: Real-time simulation
  - Tickrate > Framerate: Fast-forward effect
  - Tickrate < Framerate: Slow-motion effect

### Performance Considerations:
- Higher resolutions require more VRAM and take longer
- 1080p is a good balance of quality and performance
- Video export runs in background thread, but may impact overall performance
- Monitor console/log for any errors during export

## Example Workflows

### Quick Screenshot for Social Media:
1. Open Export window
2. Image Export tab
3. Click "Set 1080p" preset
4. Click "Export Image (PNG)..."
5. Save to desired location
6. Wait for completion (~5-10 seconds)

### High-Quality 4K Video:
1. Set up perfect camera angle
2. Open Export window  
3. Video Export tab
4. Click "Set 4K" preset
5. Set Length to desired duration (e.g., 15 seconds)
6. Set Framerate to 60 fps
7. Set Tickrate to 60.0 tps
8. Click "Export Video (MP4)..."
9. Save to desired location
10. Wait for completion (may take several minutes)

### Slow-Motion Effect:
1. Video Export tab
2. Set Framerate to 60 fps
3. Set Tickrate to 15.0 tps (4x slower than real-time)
4. Length will be 4x the simulation time
5. Export normally

### Time-Lapse Effect:
1. Video Export tab
2. Set Framerate to 30 fps
3. Set Tickrate to 120.0 tps (4x faster)
4. Simulation runs 4x faster than video playback
5. Export normally
