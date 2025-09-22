#pragma once

struct GlobalOptions {
    bool vsync = true;
    float beamSpacing = 0.1f;

    bool kerrDistortionEnabled = true;
    int kerrLutResolution = 64;
    float kerrMaxDistance = 100.0f;

    // Debug: visualize Kerr LUT overlay
    bool kerrDebugLut = false;
    int kerrDebugMode = 0;
};
