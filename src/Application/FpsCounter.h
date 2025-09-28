#pragma once
#include <chrono>

class FpsCounter {
public:
    FpsCounter() : m_FrameCount(0), m_Fps(0.0f), m_LastTime(std::chrono::high_resolution_clock::now()) {}

    void Frame() {
        ++m_FrameCount;
        auto now = std::chrono::high_resolution_clock::now();
        std::chrono::duration<float> elapsed = now - m_LastTime;
        if (elapsed.count() >= 1.0f) {
            m_Fps = m_FrameCount / elapsed.count();
            m_FrameCount = 0;
            m_LastTime = now;
        }
    }

    float GetFps() const { return m_Fps; }

private:
    int m_FrameCount;
    float m_Fps;
    std::chrono::high_resolution_clock::time_point m_LastTime;
};

