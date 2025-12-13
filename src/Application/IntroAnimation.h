#pragma once

#include <memory>
#include <glm/glm.hpp>

class Shader;

class IntroAnimation {
public:
    IntroAnimation();
    ~IntroAnimation();

    void Init();
    void Update(float deltaTime);
    void Render(int windowWidth, int windowHeight);
    void Shutdown();

    bool IsComplete() const { return m_isComplete; }
    bool IsActive() const { return m_isActive; }
    void Skip();

    float GetAlpha() const { return m_alpha; }

private:
    void InitGeometry();
    void RenderPlanet(int windowWidth, int windowHeight);
    void RenderText(int windowWidth, int windowHeight);

    bool m_initialized = false;
    bool m_isActive = true;
    bool m_isComplete = false;

    float m_time = 0.0f;
    float m_alpha = 0.0f;
    float m_planetLightIntensity = 0.0f;
    int m_visibleLetterCount = 0;

    static constexpr float TEXT_FADE_START = 0.0f;
    static constexpr float TEXT_LETTER_DELAY = 0.3f;
    static constexpr int TOTAL_LETTERS = 8;
    static constexpr float TEXT_FADE_DURATION = TEXT_LETTER_DELAY * TOTAL_LETTERS;
    static constexpr float PLANET_START_DELAY = TEXT_FADE_DURATION + 0.3f;
    static constexpr float FADE_IN_START = 0.0f;
    static constexpr float FADE_IN_DURATION = 2.0f;
    static constexpr float HOLD_DURATION = 1.5f;
    static constexpr float FADE_OUT_DURATION = 1.0f;
    static constexpr float TOTAL_DURATION = PLANET_START_DELAY + FADE_IN_DURATION + HOLD_DURATION + FADE_OUT_DURATION;

    static constexpr float LIGHT_DELAY = PLANET_START_DELAY + 0.5f;
    static constexpr float LIGHT_DURATION = 2.5f;

    // OpenGL resources
    std::unique_ptr<Shader> m_planetShader;
    std::unique_ptr<Shader> m_textShader;
    
    unsigned int m_quadVAO = 0;
    unsigned int m_quadVBO = 0;
    unsigned int m_textVAO = 0;
    unsigned int m_textVBO = 0;
    
    struct ImFont* m_titleFont = nullptr;
};

