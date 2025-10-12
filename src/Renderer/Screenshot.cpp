#include "Screenshot.h"
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <spdlog/spdlog.h>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <filesystem>

// Externe Deklaration der STB-Funktionen (bereits in tinygltf definiert)
extern "C" int stbi_write_png(char const *filename, int w, int h, int comp, const void *data, int stride_in_bytes);

bool Screenshot::CaptureViewport(int x, int y, int width, int height, const std::string& filename) {
    if (width <= 0 || height <= 0) {
        spdlog::error("Invalid viewport dimensions: {}x{}", width, height);
        return false;
    }

    // Buffer für Pixel-Daten (RGB, 3 Bytes pro Pixel)
    std::vector<unsigned char> pixels(width * height * 3);

    // OpenGL-Pixel aus dem Framebuffer lesen
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(x, y, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

    // Prüfen auf OpenGL-Fehler
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        spdlog::error("OpenGL error during screenshot capture: {}", error);
        return false;
    }

    // Bild vertikal spiegeln (OpenGL hat Y-Koordinaten umgekehrt)
    FlipImageVertically(pixels, width, height, 3);

    // Als PNG speichern
    return SavePNG(filename, width, height, pixels);
}

bool Screenshot::CaptureWindow(const std::string& filename) {
    GLFWwindow* window = glfwGetCurrentContext();
    if (!window) {
        spdlog::error("No active OpenGL context found for screenshot");
        return false;
    }

    int width, height;
    glfwGetFramebufferSize(window, &width, &height);

    return CaptureViewport(0, 0, width, height, filename);
}

std::string Screenshot::GenerateTimestampedFilename(const std::string& prefix, const std::string& extension) {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::stringstream ss;
    ss << prefix << "_"
       << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S")
       << "_" << std::setfill('0') << std::setw(3) << ms.count()
       << extension;

    return ss.str();
}

bool Screenshot::SavePNG(const std::string& filename, int width, int height, const std::vector<unsigned char>& pixels) {
    // Erstelle den Verzeichnispfad falls er nicht existiert
    std::filesystem::path filepath(filename);
    std::filesystem::path directory = filepath.parent_path();

    if (!directory.empty()) {
        try {
            std::filesystem::create_directories(directory);
        } catch (const std::exception& e) {
            spdlog::error("Failed to create directory {}: {}", directory.string(), e.what());
            return false;
        }
    }

    // Speichere als PNG mit stb_image_write
    int result = stbi_write_png(filename.c_str(), width, height, 3, pixels.data(), width * 3);

    if (result) {
        spdlog::info("Screenshot saved successfully: {}", filename);
        return true;
    } else {
        spdlog::error("Failed to save screenshot: {}", filename);
        return false;
    }
}

void Screenshot::FlipImageVertically(std::vector<unsigned char>& pixels, int width, int height, int channels) {
    int rowSize = width * channels;
    std::vector<unsigned char> temp(rowSize);

    for (int y = 0; y < height / 2; ++y) {
        int topRow = y * rowSize;
        int bottomRow = (height - 1 - y) * rowSize;

        // Temporär die obere Zeile speichern
        std::copy(pixels.begin() + topRow, pixels.begin() + topRow + rowSize, temp.begin());

        // Untere Zeile nach oben kopieren
        std::copy(pixels.begin() + bottomRow, pixels.begin() + bottomRow + rowSize, pixels.begin() + topRow);

        // Obere Zeile (aus temp) nach unten kopieren
        std::copy(temp.begin(), temp.end(), pixels.begin() + bottomRow);
    }
}
