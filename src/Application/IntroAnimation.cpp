#include "IntroAnimation.h"
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <spdlog/spdlog.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
#include <cstring>
#include "imgui.h"
#include "Renderer/Shader.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

IntroAnimation::IntroAnimation() = default;
IntroAnimation::~IntroAnimation() {
    if (m_initialized) {
        Shutdown();
    }
}

void IntroAnimation::Init() {
    if (m_initialized) {
        spdlog::warn("IntroAnimation already initialized");
        return;
    }

    spdlog::info("Initializing intro animation");

    InitGeometry();

    // Load shaders for intro animation
    m_planetShader = std::make_unique<Shader>("../shaders/intro_planet.vert", "../shaders/intro_planet.frag");
    m_textShader = std::make_unique<Shader>("../shaders/intro_text.vert", "../shaders/intro_text.frag");

    // Load a large title font for "MoleHole"
    // Note: Font atlas needs to be rebuilt after adding new fonts
    ImGuiIO& io = ImGui::GetIO();
    
    // Check if we already loaded this font
    bool fontAlreadyLoaded = false;
    for (int i = 0; i < io.Fonts->Fonts.Size; i++) {
        if (io.Fonts->Fonts[i]->LegacySize == 120.0f) {
            m_titleFont = io.Fonts->Fonts[i];
            fontAlreadyLoaded = true;
            break;
        }
    }
    
    if (!fontAlreadyLoaded) {
        m_titleFont = io.Fonts->AddFontFromFileTTF("../font/DidotLTPro-Bold.ttf", 120.0f);
        if (m_titleFont) {
            // Rebuild font atlas
            unsigned char* pixels;
            int width, height;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
            spdlog::info("Loaded title font and rebuilt font atlas");
        } else {
            spdlog::warn("Failed to load title font, using default");
            m_titleFont = io.FontDefault;
        }
    }

    m_initialized = true;
    m_isActive = true;
    m_isComplete = false;
    m_time = 0.0f;
    m_alpha = 0.0f;
    m_planetLightIntensity = 0.0f;

    spdlog::info("Intro animation initialized successfully");
}

void IntroAnimation::Shutdown() {
    if (!m_initialized) {
        return;
    }

    if (m_quadVAO) {
        glDeleteVertexArrays(1, &m_quadVAO);
        glDeleteBuffers(1, &m_quadVBO);
        m_quadVAO = 0;
        m_quadVBO = 0;
    }

    if (m_textVAO) {
        glDeleteVertexArrays(1, &m_textVAO);
        glDeleteBuffers(1, &m_textVBO);
        m_textVAO = 0;
        m_textVBO = 0;
    }

    m_planetShader.reset();
    m_textShader.reset();

    m_initialized = false;
}

void IntroAnimation::InitGeometry() {
    // Create fullscreen quad for planet rendering
    float quadVertices[] = {
        // positions   // texCoords
        -1.0f,  1.0f,  0.0f, 1.0f,
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 0.0f,

        -1.0f,  1.0f,  0.0f, 1.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f
    };

    glGenVertexArrays(1, &m_quadVAO);
    glGenBuffers(1, &m_quadVBO);

    glBindVertexArray(m_quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    glBindVertexArray(0);

    // Create text quad VAO
    glGenVertexArrays(1, &m_textVAO);
    glGenBuffers(1, &m_textVBO);

    glBindVertexArray(m_textVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_textVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4, nullptr, GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    glBindVertexArray(0);
}

void IntroAnimation::Update(float deltaTime) {
    if (!m_isActive || m_isComplete) {
        return;
    }

    m_time += deltaTime;

    m_visibleLetterCount = glm::clamp(static_cast<int>(m_time / TEXT_LETTER_DELAY), 0, TOTAL_LETTERS);

    float planetTime = m_time - PLANET_START_DELAY;
    
    if (planetTime < 0.0f) {
        m_alpha = 0.0f;
    } else if (planetTime < FADE_IN_DURATION) {
        m_alpha = planetTime / FADE_IN_DURATION;
    } else if (planetTime < FADE_IN_DURATION + HOLD_DURATION) {
        m_alpha = 1.0f;
    } else if (planetTime < FADE_IN_DURATION + HOLD_DURATION + FADE_OUT_DURATION) {
        float fadeOutProgress = (planetTime - FADE_IN_DURATION - HOLD_DURATION) / FADE_OUT_DURATION;
        m_alpha = 1.0f - fadeOutProgress;
    } else {
        m_alpha = 0.0f;
        m_isComplete = true;
        m_isActive = false;
    }

    if (m_time > LIGHT_DELAY) {
        float lightProgress = (m_time - LIGHT_DELAY) / LIGHT_DURATION;
        lightProgress = glm::clamp(lightProgress, 0.0f, 1.0f);
        m_planetLightIntensity = lightProgress * lightProgress * (3.0f - 2.0f * lightProgress);
    }
}

void IntroAnimation::Render(int windowWidth, int windowHeight) {
    if (!m_isActive || !m_initialized) {
        return;
    }

    // Save current OpenGL state
    GLboolean blendEnabled = glIsEnabled(GL_BLEND);
    GLboolean depthTestEnabled = glIsEnabled(GL_DEPTH_TEST);
    GLint blendSrc, blendDst;
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &blendSrc);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &blendDst);

    // Setup for intro rendering
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Set viewport
    glViewport(0, 0, windowWidth, windowHeight);

    // Clear to black
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Always render planet background (even if alpha is 0, it will be black)
    RenderPlanet(windowWidth, windowHeight);

    // Render text on top
    RenderText(windowWidth, windowHeight);

    // Restore OpenGL state
    if (blendEnabled) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (depthTestEnabled) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    glBlendFunc(blendSrc, blendDst);
}

void IntroAnimation::RenderPlanet(int windowWidth, int windowHeight) {
    if (!m_planetShader || m_quadVAO == 0) {
        return;
    }

    m_planetShader->Bind();
    
    // Set uniforms
    m_planetShader->SetFloat("u_time", m_time);
    m_planetShader->SetFloat("u_alpha", m_alpha);
    m_planetShader->SetFloat("u_lightIntensity", m_planetLightIntensity);
    m_planetShader->SetVec2("u_resolution", glm::vec2(windowWidth, windowHeight));

    // Render fullscreen quad
    glBindVertexArray(m_quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    m_planetShader->Unbind();
}

void IntroAnimation::RenderText(int windowWidth, int windowHeight) {
    if (!m_textShader || m_textVAO == 0 || m_visibleLetterCount <= 0) {
        return;
    }

    ImGui::PushFont(m_titleFont);
    
    const char* fullText = "MOLEHOLE";
    ImVec2 fullTextSize = ImGui::CalcTextSize(fullText);
    
    float textX = (windowWidth - fullTextSize.x) * 0.5f;
    float textY = (windowHeight - fullTextSize.y) * 0.5f;

    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    
    char visibleText[9] = {0};
    int charsToShow = glm::min(m_visibleLetterCount, TOTAL_LETTERS);
    strncpy(visibleText, fullText, charsToShow);
    
    // Global alpha that increases as more letters appear
    // Maps from 0 letters (alpha=0) to all letters (alpha=1)
    float globalAlpha = static_cast<float>(charsToShow) / static_cast<float>(TOTAL_LETTERS);
    // Use smoothstep for smoother fade
    globalAlpha = globalAlpha * globalAlpha * (3.0f - 2.0f * globalAlpha);
    
    float currentX = textX;
    
    for (int i = 0; i < charsToShow; i++) {
        char singleChar[2] = {fullText[i], '\0'};
        ImVec2 charSize = ImGui::CalcTextSize(singleChar);
        
        bool isMole = (i < 4);
        ImVec4 charColorVec = isMole ? ImVec4(1.0f, 1.0f, 1.0f, globalAlpha) : ImVec4(0.0f, 0.0f, 0.0f, globalAlpha);
        ImU32 charColor = ImGui::ColorConvertFloat4ToU32(charColorVec);
        
        if (isMole) {
            float glowAlpha = 0.3f * globalAlpha;
            for (int j = 1; j <= 3; j++) {
                ImU32 glowColor = ImGui::ColorConvertFloat4ToU32(ImVec4(0.7f, 0.9f, 1.0f, glowAlpha / j));
                drawList->AddText(m_titleFont, m_titleFont->LegacySize, 
                                 ImVec2(currentX + j * 2, textY + j * 2), glowColor, singleChar);
            }
        }
        else {
            float glowAlpha = 0.3f * globalAlpha;
            for (int j = 1; j <= 3; j++) {
                ImU32 glowColor = ImGui::ColorConvertFloat4ToU32(ImVec4(0.7f, 0.9f, 1.0f, glowAlpha / j));
                drawList->AddText(m_titleFont, m_titleFont->LegacySize, 
                                 ImVec2(currentX + j * 2, textY + j * 2), glowColor, singleChar);
            }
        }
        
        drawList->AddText(m_titleFont, m_titleFont->LegacySize, ImVec2(currentX, textY), charColor, singleChar);
        
        currentX += charSize.x;
    }
    
    ImGui::PopFont();
}

void IntroAnimation::Skip() {
    m_isComplete = true;
    m_isActive = false;
    m_alpha = 0.0f;
}

