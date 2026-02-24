#pragma once
#include "glm/vec4.hpp"

class RenderCommand {
public:
    static void Init();
    static void Shutdown();

    static bool BeginFrame();
    static void EndFrame();

    static void Resize(glm::vec2 newSize);
    static void SetClearColor(glm::vec4 color);
};
