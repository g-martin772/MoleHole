#pragma once

#include <string>
#include <vector>

class Screenshot {
public:
    static bool CaptureViewport(int x, int y, int width, int height, const std::string& filename);
    static bool CaptureWindow(const std::string& filename);
    static std::string GenerateTimestampedFilename(const std::string& prefix = "screenshot", const std::string& extension = ".png");

private:
    static bool SavePNG(const std::string& filename, int width, int height, const std::vector<unsigned char>& pixels);
    static void FlipImageVertically(std::vector<unsigned char>& pixels, int width, int height, int channels);
};
