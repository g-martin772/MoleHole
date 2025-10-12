#pragma once

#include <string>
#include <vector>

class Screenshot {
public:
    // Nimmt einen Screenshot des aktuellen Viewports auf
    static bool CaptureViewport(int x, int y, int width, int height, const std::string& filename);

    // Nimmt einen Screenshot des gesamten Fensters auf
    static bool CaptureWindow(const std::string& filename);

    // Generiert einen automatischen Dateinamen mit Zeitstempel
    static std::string GenerateTimestampedFilename(const std::string& prefix = "screenshot", const std::string& extension = ".png");

private:
    // Speichert Pixel-Daten als PNG-Datei
    static bool SavePNG(const std::string& filename, int width, int height, const std::vector<unsigned char>& pixels);

    // Flippt das Bild vertikal (OpenGL-Koordinaten korrigieren)
    static void FlipImageVertically(std::vector<unsigned char>& pixels, int width, int height, int channels);
};
