#include "Renderer/RenderCommand.h"

#include "VulkanApi.h"

static VulkanApi *s_Api;

void RenderCommand::Init() {
    s_Api = new VulkanApi();
    s_Api->Init();
}

void RenderCommand::Shutdown() {
    s_Api->Shutdown();
    delete s_Api;
}

bool RenderCommand::BeginFrame() {
    return s_Api->BeginFrame();
}

void RenderCommand::EndFrame() {
    s_Api->EndFrame();
}

void RenderCommand::Resize(glm::vec2 newSize) {
    s_Api->OnResize(newSize);
}

void RenderCommand::SetClearColor(glm::vec4 color) {
    s_Api->SetClearColor(color);
}
