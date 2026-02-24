#include <glad/gl.h>
#include "Renderer.h"
#include "PhysicsDebugRenderer.h"
#include <GLFW/glfw3.h>
#include "ImGuizmo.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "Shader.h"
#include "Camera.h"
#include "Input.h"


#include "Buffer.h"

#include "Application/Application.h"
#include "Application/Parameters.h"
#include "Application/ParameterRegistry.h"
#include "GravityGridRenderer.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static Window* g_gladWindow = nullptr;
static GLADapiproc GladGetProcAddress(const char* name) {
    if (g_gladWindow) {
        return reinterpret_cast<GLADapiproc>(g_gladWindow->GetProcAddress(name));
    }
    return nullptr;
}

#ifdef _WIN32
extern "C" {
    __declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;                    // NVIDIA Optimus
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;                      // AMD PowerXpress
}
#else
extern "C" {
    __attribute__((visibility("default"))) unsigned long NvOptimusEnablement = 0x00000001;   // NVIDIA Optimus
    __attribute__((visibility("default"))) int AmdPowerXpressRequestHighPerformance = 1;     // AMD PowerXpress
}
#endif

void Renderer::Init() {
    Init(false);
}

void Renderer::Init(bool headless) {
    WindowProperties props;
    props.width = 800;
    props.height = 600;
    props.title = "MoleHole Window";
    props.visible = !headless;
    props.vsync = true;
    props.contextVersionMajor = 4;
    props.contextVersionMinor = 6;
    props.coreProfile = true;

    if (headless) {
        spdlog::info("Initializing renderer in headless mode");
    }

    m_window = Window::Create(props);
    if (!m_window) {
        spdlog::error("Failed to create window");
        exit(-1);
    }

    g_gladWindow = m_window;
    int version = gladLoadGL(GladGetProcAddress);
    g_gladWindow = nullptr;

    if (version == 0) {
        spdlog::error("Failed to initialize GLAD");
        exit(-1);
    }

    spdlog::info("Loaded OpenGL {0}.{1}", GLAD_VERSION_MAJOR(version), GLAD_VERSION_MINOR(version));
    spdlog::info("OpenGL Device: {0}", (const char *) glGetString(GL_RENDERER));
    spdlog::info("OpenGL Vendor: {0}", (const char *) glGetString(GL_VENDOR));
    spdlog::info("OpenGL Version: {}", (const char *) glGetString(GL_VERSION));

// #ifndef _WIN32
//     const char* dri_prime = std::getenv("DRI_PRIME");
//     const char* vk_icd = std::getenv("VK_ICD_FILENAMES");
//     if (!dri_prime && !vk_icd) {
//         spdlog::warn("Running on integrated GPU. To use dedicated GPU, launch with:");
//         spdlog::warn("  DRI_PRIME=1 ./MoleHole  (for AMD/Intel)");
//         spdlog::warn("  or use __NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia ./MoleHole  (for NVIDIA)");
//     }
// #endif



    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    (void) io;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(m_window->GetNativeWindow(), true);
    ImGui_ImplOpenGL3_Init("#version 460");

    image = std::make_shared<Image>(last_img_width, last_img_height);

    spdlog::info("Loading shaders");
    quadShader = std::make_unique<Shader>("../shaders/quad.vert", "../shaders/quad.frag");
    circleShader = std::make_unique<Shader>("../shaders/circle.vert", "../shaders/circle.frag");
    sphereShader = std::make_unique<Shader>("../shaders/sphere.vert", "../shaders/sphere.frag");

    blackHoleRenderer = std::make_unique<BlackHoleRenderer>();
    blackHoleRenderer->Init(last_img_width, last_img_height);

    int width, height;
    m_window->GetFramebufferSize(width, height);
    // Set a much larger far plane for the camera to avoid grid clipping
    camera = std::make_unique<Camera>(Application::Params().Get(Params::RenderingFOV, 45.0f), (float) width / (float) height, 0.01f, 10000.0f);

    camera->SetPosition(Application::Params().Get(Params::CameraPosition, glm::vec3(0.0f, 0.0f, 10.0f)));
    camera->SetYawPitch(Application::Params().Get(Params::CameraYaw, -90.0f), Application::Params().Get(Params::CameraPitch, 0.0f));

    input = std::make_unique<Input>(m_window->GetNativeWindow());

    gravityGridRenderer = std::make_unique<GravityGridRenderer>();
    gravityGridRenderer->Init();

    objectPathsRenderer = std::make_unique<ObjectPathsRenderer>();
    objectPathsRenderer->Init();

    m_physicsDebugRenderer = std::make_unique<PhysicsDebugRenderer>();
    m_physicsDebugRenderer->Init();

    InitSphereGeometry();
}

void Renderer::Shutdown() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    if (m_window) {
        delete m_window;
        m_window = nullptr;
    }

    TerminateWindowSystem();
}

void Renderer::BeginFrame() {
    int vsyncVal = Application::Params().Get(Params::WindowVSync, true) ? 1 : 0;
    if (vsyncVal != lastVsync) {
        m_window->SetVSync(vsyncVal == 1);
        lastVsync = vsyncVal;
    }
    m_window->PollEvents();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();
}

void Renderer::EndFrame(bool clearScreen) {
    int display_w, display_h;
    m_window->GetFramebufferSize(display_w, display_h);
    glViewport(0, 0, display_w, display_h);

    if (clearScreen) {
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    m_window->SwapBuffers();
}

void Renderer::RenderScene(Scene *scene) {
    if (scene && camera) {
        scene->camera = camera.get();
    }

    const char *title = nullptr;
    switch (selectedViewport) {
        case ViewportMode::Demo1:
            title = "Viewport - Demo1";
            break;
        case ViewportMode::Rays2D:
            title = "Viewport - 2D Rays";
            break;
        case ViewportMode::Simulation3D:
            title = "Viewport - 3D Simulation";
            break;
        default:
            title = "Viewport";
            break;
    }

    // Push style to remove padding and make viewport flush with window edges
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin(title);
    ImGui::PopStyleVar();

    ImVec2 viewport_pos = ImGui::GetWindowPos();
    ImVec2 viewport_size = ImGui::GetWindowSize();
    ImVec2 content_pos = ImGui::GetCursorScreenPos();
    ImVec2 imgui_size = ImGui::GetContentRegionAvail();

    SetViewportBounds(content_pos.x, content_pos.y, imgui_size.x, imgui_size.y);

    int img_width = std::max(1, static_cast<int>(imgui_size.x));
    int img_height = std::max(1, static_cast<int>(imgui_size.y));

    if (img_width != last_img_width || img_height != last_img_height) {
        image = std::make_shared<Image>(img_width, img_height);
        last_img_width = img_width;
        last_img_height = img_height;
        if (camera) {
            camera->SetYawPitch(camera->GetYaw(), camera->GetPitch());
            camera->SetAspect(static_cast<float>(img_width) / static_cast<float>(img_height));
        }
        if (blackHoleRenderer) {
            blackHoleRenderer->Resize(img_width, img_height);
        }
    }

    bool viewport_focused = ImGui::IsWindowFocused();
    bool viewport_hovered = ImGui::IsWindowHovered();

    bool gizmoUsing = ImGuizmo::IsUsing();
    bool gizmoOver = ImGuizmo::IsOver();

    input->SetViewportFocused(viewport_focused && !gizmoUsing);
    input->SetViewportHovered(viewport_hovered && !gizmoOver);

    static double lastTime = glfwGetTime();
    double currentTime = glfwGetTime();
    float deltaTime = static_cast<float>(currentTime - lastTime);
    lastTime = currentTime;
    input->Update();

    if (viewport_focused && viewport_hovered && !gizmoUsing && !gizmoOver) {
        UpdateCamera(deltaTime);
    } else {
        input->SetCursorEnabled(true);
    }

    unsigned int fbo;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, image->textureID, 0);

    unsigned int depthRBO;
    glGenRenderbuffers(1, &depthRBO);
    glBindRenderbuffer(GL_RENDERBUFFER, depthRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, image->width, image->height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depthRBO);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        spdlog::error("Framebuffer is not complete!");
    }

    glViewport(0, 0, image->width, image->height);

    switch (selectedViewport) {
        case ViewportMode::Demo1:
            RenderDemo1(scene);
            break;
        case ViewportMode::Rays2D:
            Render2DRays(scene);
            break;
        case ViewportMode::Simulation3D:
            Render3DSimulation(scene);
            break;
    }

    glDeleteRenderbuffers(1, &depthRBO);
    glDeleteFramebuffers(1, &fbo);

    ImVec2 cursor_pos = ImGui::GetCursorScreenPos();

    ImGui::Image((void *)(intptr_t)image->textureID,
                 ImVec2((float)image->width, (float)image->height),
                 ImVec2(0, 1), ImVec2(1, 0));

    bool image_hovered = ImGui::IsItemHovered();
    bool image_active = ImGui::IsItemActive();

    if (scene && scene->HasSelection() && camera) {
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());

        ImVec2 image_min = ImGui::GetItemRectMin();
        ImVec2 image_max = ImGui::GetItemRectMax();

        ImGuizmo::SetRect(image_min.x, image_min.y, image_max.x - image_min.x, image_max.y - image_min.y);

        glm::vec3 *position = scene->GetSelectedObjectPosition();
        glm::quat *rotation = scene->GetSelectedObjectRotation();
        glm::vec3 *scale = scene->GetSelectedObjectScale();

        if (position) {
            glm::mat4 view = camera->GetViewMatrix();
            glm::mat4 projection = camera->GetProjectionMatrix();

            glm::mat4 transform = glm::translate(glm::mat4(1.0f), *position);
            if (rotation) {
                transform *= glm::mat4_cast(*rotation);
            }
            if (scale) {
                transform = glm::scale(transform, *scale);
            }

            ImGuizmo::Enable(true);

            ImGui::PushID("ObjectGizmo");

            auto& ui = Application::Instance().GetUI();
            ImGuizmo::OPERATION operation;
            switch (ui.GetCurrentGizmoOperation()) {
                case UI::GizmoOperation::Translate:
                    operation = ImGuizmo::TRANSLATE;
                    break;
                case UI::GizmoOperation::Rotate:
                    operation = ImGuizmo::ROTATE;
                    break;
                case UI::GizmoOperation::Scale:
                    operation = ImGuizmo::SCALE;
                    break;
            }

            float* snap = nullptr;
            auto snapTranslate = ui.GetSnapTranslate();
            auto snapScale = ui.GetSnapScale();
            auto snapRotate = ui.GetSnapRotate();
            if (ui.IsUsingSnap()) {
                switch (ui.GetCurrentGizmoOperation()) {
                    case UI::GizmoOperation::Translate:
                        snap = const_cast<float*>(snapTranslate);
                        break;
                    case UI::GizmoOperation::Rotate:
                        snap = &snapRotate;
                        break;
                    case UI::GizmoOperation::Scale:
                        snap = &snapScale;
                        break;
                }
            }

            bool wasManipulated = ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection),
                                     operation, ImGuizmo::LOCAL, glm::value_ptr(transform),
                                     nullptr, snap);

            if (wasManipulated) {
                glm::vec3 newTranslation, newScale;
                glm::quat newRotation;
                glm::vec3 skew;
                glm::vec4 perspective;
                glm::decompose(transform, newScale, newRotation, newTranslation, skew, perspective);

                *position = newTranslation;
                if (rotation) {
                    *rotation = newRotation;
                }
                if (scale) {
                    *scale = newScale;
                }
            }

            ImGui::PopID();
        }
    }

    ImGui::End();
}

void Renderer::RenderDemo1(Scene *scene) {
    glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    quadShader->Bind();
    glm::mat4 vp = camera->GetViewProjectionMatrix();
    quadShader->SetMat4("uVP", vp);
    for (int x = -2; x <= 2; ++x) {
        for (int y = -2; y <= 2; ++y) {
            float angle = (x + y) * 0.2f;
            DrawQuad(glm::vec3(x, y, 0.0f), angle);
        }
    }
    FlushQuads();
    quadShader->Unbind();
}

void Renderer::DrawCircle(const glm::vec2 &pos, float radius, const glm::vec3 &color) {
    int viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    float screenRadius = radius;
    if (camera) {
        glm::vec4 p0 = camera->GetViewProjectionMatrix() * glm::vec4(pos, 0.0f, 1.0f);
        glm::vec4 p1 = camera->GetViewProjectionMatrix() * glm::vec4(pos + glm::vec2(radius), 0.0f, 1.0f);
        p0 /= p0.w;
        p1 /= p1.w;
        float sx0 = (p0.x * 0.5f + 0.5f) * viewport[2];
        float sx1 = (p1.x * 0.5f + 0.5f) * viewport[2];
        screenRadius = fabs(sx1 - sx0);
    }
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_PROGRAM_POINT_SIZE);
    glPointSize(screenRadius * 2.0f);
    circleShader->Bind();
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, glm::vec3(pos, 0.0f));
    circleShader->SetMat4("uModel", model);
    circleShader->SetMat4("uVP", camera->GetViewProjectionMatrix());
    circleShader->SetVec3("uColor", color);
    float vertices[] = {0.0f, 0.0f, 0.0f};
    unsigned int VAO, VBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void *) 0);
    glDrawArrays(GL_POINTS, 0, 1);
    glBindVertexArray(0);
    glDeleteBuffers(1, &VBO);
    glDeleteVertexArrays(1, &VAO);
    circleShader->Unbind();
    glDisable(GL_PROGRAM_POINT_SIZE);
    glDisable(GL_BLEND);
}

void Renderer::InitSphereGeometry() {
    if (m_SphereVAO != 0) return;
    constexpr int X_SEGMENTS = 32;
    constexpr int Y_SEGMENTS = 32;
    std::vector<float> vertices;
    std::vector<unsigned int> indices;
    for (int y = 0; y <= Y_SEGMENTS; ++y) {
        for (int x = 0; x <= X_SEGMENTS; ++x) {
            float xSegment = (float)x / (float)X_SEGMENTS;
            float ySegment = (float)y / (float)Y_SEGMENTS;
            float xPos = std::cos(xSegment * 2.0f * M_PI) * std::sin(ySegment * M_PI);
            float yPos = std::cos(ySegment * M_PI);
            float zPos = std::sin(xSegment * 2.0f * M_PI) * std::sin(ySegment * M_PI);
            vertices.push_back(xPos);
            vertices.push_back(yPos);
            vertices.push_back(zPos);
        }
    }
    for (int y = 0; y < Y_SEGMENTS; ++y) {
        for (int x = 0; x < X_SEGMENTS; ++x) {
            int i0 = y * (X_SEGMENTS + 1) + x;
            int i1 = i0 + 1;
            int i2 = i0 + (X_SEGMENTS + 1);
            int i3 = i2 + 1;
            // First triangle
            indices.push_back(i0);
            indices.push_back(i2);
            indices.push_back(i1);
            // Second triangle
            indices.push_back(i1);
            indices.push_back(i2);
            indices.push_back(i3);
        }
    }
    m_SphereIndexCount = indices.size();
    glGenVertexArrays(1, &m_SphereVAO);
    glGenBuffers(1, &m_SphereVBO);
    glGenBuffers(1, &m_SphereEBO);
    glBindVertexArray(m_SphereVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_SphereVBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_SphereEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glBindVertexArray(0);
}

void Renderer::Render2DRays(Scene *scene) {
    glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    if (!scene) return;

    float currentTime = static_cast<float>(glfwGetTime());

    blackHoleRenderer->Render(*scene, m_meshCache, *camera, currentTime);
}

void Renderer::Render3DSimulation(Scene *scene) {
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!scene || !blackHoleRenderer) return;

    if (scene->reloadSkybox)
    {
        blackHoleRenderer->LoadSkybox();
        scene->reloadSkybox = false;
    }

    float currentTime = static_cast<float>(glfwGetTime());

        // Pre-load meshes into cache before rendering
    for (const auto& obj : scene->objects) {
        if (obj.HasClass("Mesh")) {
            ParameterHandle pathHandle("Mesh.FilePath");
            auto pathValue = obj.GetParameter(pathHandle);
            if (std::holds_alternative<std::string>(pathValue)) {
                std::string meshPath = std::get<std::string>(pathValue);
                if (!meshPath.empty() && m_meshCache.find(meshPath) == m_meshCache.end()) {
                    GetOrLoadMesh(meshPath);
                }
            }
        }
    }

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    blackHoleRenderer->Render(*scene, m_meshCache, *camera, currentTime);
    blackHoleRenderer->RenderToScreen();

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);

       if (static_cast<DebugMode>(Application::Params().Get(Params::RenderingDebugMode, 0)) == DebugMode::GravityGrid && gravityGridRenderer) {
        gravityGridRenderer->Render(*scene, *camera, currentTime);
    }
        if (static_cast<DebugMode>(Application::Params().Get(Params::RenderingDebugMode, 0)) == DebugMode::ObjectPaths && objectPathsRenderer) {
            objectPathsRenderer->Render(*scene, *camera, currentTime);
        }

    if (m_physicsDebugRenderer && m_physicsDebugRenderer->IsEnabled()) {
        auto& simulation = Application::GetSimulation();
        if (simulation.GetPhysics()) {
            const PxRenderBuffer* renderBuffer = simulation.GetPhysics()->GetDebugRenderBuffer();
            m_physicsDebugRenderer->Render(renderBuffer, camera.get());
        }
    }

    // Render meshes using traditional rasterization on top of raytraced background
    RenderMeshes(scene);
    //RenderSpheres(scene);

    glDisable(GL_DEPTH_TEST);
}

void Renderer::UpdateCamera(float deltaTime) {
    if (ImGuizmo::IsUsing()) {
        return;
    }

    float forward = 0.0f, right = 0.0f, up = 0.0f;
    if (input->IsKeyDown(GLFW_KEY_W)) forward += 1.0f;
    if (input->IsKeyDown(GLFW_KEY_S)) forward -= 1.0f;
    if (input->IsKeyDown(GLFW_KEY_D)) right += 1.0f;
    if (input->IsKeyDown(GLFW_KEY_A)) right -= 1.0f;
    if (input->IsKeyDown(GLFW_KEY_E)) up += 1.0f;
    if (input->IsKeyDown(GLFW_KEY_Q)) up -= 1.0f;

    float cameraSpeed = Application::Params().Get(Params::CameraSpeed, 5.0f);
    camera->ProcessKeyboard(forward, right, up, deltaTime, cameraSpeed);

    bool cameraChanged = false;
    if (input->IsMouseButtonDown(GLFW_MOUSE_BUTTON_RIGHT)) {
        input->SetCursorEnabled(false);
        double dx, dy;
        input->GetMouseDelta(dx, dy);
        float mouseSensitivity = Application::Params().Get(Params::CameraMouseSensitivity, 0.1f);
        if (dx != 0.0 || dy != 0.0) {
            camera->ProcessMouse((float) dx, (float) dy, mouseSensitivity);
            cameraChanged = true;
        }
    } else {
        input->SetCursorEnabled(true);
    }

    if (forward != 0.0f || right != 0.0f || up != 0.0f || cameraChanged) {
        Application::Params().Set(Params::CameraPosition, camera->GetPosition());
        Application::Params().Set(Params::CameraFront, camera->GetFront());
        Application::Params().Set(Params::CameraUp, camera->GetUp());
        Application::Params().Set(Params::CameraPitch, camera->GetPitch());
        Application::Params().Set(Params::CameraYaw, camera->GetYaw());
    }

    if (camera->GetFov() != Application::Params().Get(Params::RenderingFOV, 45.0f)) {
        camera->SetFov(Application::Params().Get(Params::RenderingFOV, 45.0f));
    }
}

void Renderer::DrawQuad(const glm::vec3 &position, float rotationRadians, const glm::vec3 &scale) {
    quadInstances.push_back({position, rotationRadians, scale});
}

void Renderer::FlushQuads() {
    static VertexArray *vao = nullptr;
    static VertexBuffer *vbo = nullptr;
    static IndexBuffer *ebo = nullptr;
    if (!vao) {
        float vertices[] = {
            -0.5f, -0.5f, 0.0f, 0.0f, 0.0f,
            0.5f, -0.5f, 0.0f, 1.0f, 0.0f,
            0.5f, 0.5f, 0.0f, 1.0f, 1.0f,
            -0.5f, 0.5f, 0.0f, 0.0f, 1.0f
        };
        unsigned int indices[] = {0, 1, 2, 2, 3, 0};
        vao = new VertexArray();
        vbo = new VertexBuffer(vertices, sizeof(vertices));
        ebo = new IndexBuffer(indices, 6);
        vao->Bind();
        vbo->Bind();
        ebo->Bind();
        vao->EnableAttrib(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *) 0);
        vao->EnableAttrib(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *) (3 * sizeof(float)));
        vao->Unbind();
    }
    vao->Bind();
    for (const auto &quad: quadInstances) {
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, quad.position);
        model = glm::rotate(model, quad.rotation, glm::vec3(0, 0, 1));
        model = glm::scale(model, quad.scale);
        quadShader->SetMat4("uModel", model);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
    }
    vao->Unbind();
    quadInstances.clear();
}

void Renderer::HandleMousePicking(Scene* scene) {
    if (!scene || !input || !camera) return;

    if (ImGuizmo::IsUsing()) {
        return;
    }

    if (input->IsMouseButtonDown(GLFW_MOUSE_BUTTON_LEFT) && input->IsViewportFocused()) {
        double mouseX, mouseY;
        input->GetMousePos(mouseX, mouseY);

        float relativeX = (float)mouseX - m_viewportX;
        float relativeY = (float)mouseY - m_viewportY;

        if (relativeX >= 0 && relativeX <= m_viewportWidth &&
            relativeY >= 0 && relativeY <= m_viewportHeight) {

            glm::vec3 rayOrigin, rayDirection;
            ScreenToWorldRay(relativeX, relativeY, rayOrigin, rayDirection);

            auto pickedObject = scene->PickObject(rayOrigin, rayDirection);
            if (pickedObject.has_value()) {
                scene->SelectObject(pickedObject->type, pickedObject->index);
            } else {
                scene->ClearSelection();
            }
        }
    }
}

glm::vec3 Renderer::ScreenToWorldRay(float mouseX, float mouseY, glm::vec3& rayOrigin, glm::vec3& rayDirection) {
    if (!camera) {
        rayOrigin = glm::vec3(0.0f);
        rayDirection = glm::vec3(0.0f, 0.0f, -1.0f);
        return rayDirection;
    }

    // Convert screen coordinates to normalized device coordinates (-1 to 1)
    float x = (2.0f * mouseX) / m_viewportWidth - 1.0f;
    float y = 1.0f - (2.0f * mouseY) / m_viewportHeight;

    glm::mat4 projection = camera->GetProjectionMatrix();
    glm::mat4 view = camera->GetViewMatrix();

    glm::vec4 rayClip = glm::vec4(x, y, -1.0f, 1.0f);

    glm::vec4 rayEye = glm::inverse(projection) * rayClip;
    rayEye = glm::vec4(rayEye.x, rayEye.y, -1.0f, 0.0f);

    glm::vec3 rayWorld = glm::vec3(glm::inverse(view) * rayEye);
    rayWorld = glm::normalize(rayWorld);

    rayOrigin = camera->GetPosition();
    rayDirection = rayWorld;

    return rayDirection;
}

void Renderer::SetViewportBounds(float x, float y, float width, float height) {
    m_viewportX = x;
    m_viewportY = y;
    m_viewportWidth = width;
    m_viewportHeight = height;
}

void Renderer::RenderToFramebuffer(unsigned int fbo, int width, int height, Scene* scene, Camera* cam) {
    if (!cam) return;
    
    Camera* savedCamera = camera.get();
    camera.release();
    camera.reset(cam);
    
    GLint oldFBO;
    GLint oldViewport[4];
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFBO);
    glGetIntegerv(GL_VIEWPORT, oldViewport);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, width, height);
    
    if (blackHoleRenderer) {
        blackHoleRenderer->Resize(width, height);
    }

    switch (selectedViewport) {
        case ViewportMode::Demo1:
            RenderDemo1(scene);
            break;
        case ViewportMode::Rays2D:
            Render2DRays(scene);
            break;
        case ViewportMode::Simulation3D:
            Render3DSimulation(scene);
            break;
    }
    
    glFlush();

    if (blackHoleRenderer) {
        blackHoleRenderer->Resize(oldViewport[2], oldViewport[3]);
    }

    camera.release();
    camera.reset(savedCamera);

    glBindFramebuffer(GL_FRAMEBUFFER, oldFBO);
    glViewport(oldViewport[0], oldViewport[1], oldViewport[2], oldViewport[3]);
}

void Renderer::RenderMeshes(Scene* scene) {
    if (!scene || !camera) return;

    if (Application::Params().Get(Params::RenderingThirdPerson, false))
    {
        std::string defaultMeshName = "";
        std::string meshName = Application::Params().Get(Params::CameraObject, defaultMeshName);

        for (auto& obj : scene->objects)
        {
            if (!obj.HasClass("Mesh")) continue;

            ParameterHandle nameHandle("Entity.Name");
            auto nameValue = obj.GetParameter(nameHandle);
            if (!std::holds_alternative<std::string>(nameValue)) continue;

            if (std::get<std::string>(nameValue) == meshName)
            {
                ParameterHandle pathHandle("Mesh.FilePath");
                auto pathValue = obj.GetParameter(pathHandle);
                if (!std::holds_alternative<std::string>(pathValue)) break;

                std::string meshPath = std::get<std::string>(pathValue);
                auto mesh = GetOrLoadMesh(meshPath);
                if (mesh && mesh->IsLoaded()) {
                    float yawRad = glm::radians(camera->GetYaw() - 90);
                    float pitchRad = glm::radians(camera->GetPitch());
                    float rollRad = 0.0f;

                    glm::quat q = glm::quat(glm::vec3(-pitchRad, -yawRad, rollRad));

                    float thirdPersonDistance = Application::Params().Get(Params::ThirdPersonDistance, 10.0f);
                    float thirdPersonHeight = Application::Params().Get(Params::ThirdPersonHeight, 3.0f);

                    glm::vec3 cameraFront = camera->GetFront();
                    glm::vec3 cameraUp = camera->GetUp();
                    glm::vec3 cameraRight = glm::normalize(glm::cross(cameraFront, cameraUp));
                    glm::vec3 actualCameraUp = glm::normalize(glm::cross(cameraRight, cameraFront));

                    glm::vec3 offsetForward = cameraFront * thirdPersonDistance;
                    glm::vec3 offsetDown = -actualCameraUp * thirdPersonHeight;
                    glm::vec3 meshPosition = camera->GetPosition() + offsetForward + offsetDown;

                    obj.SetParameter(ParameterHandle("Entity.Position"), meshPosition);
                    obj.SetParameter(ParameterHandle("Entity.Rotation"), q);

                    mesh->SetPosition(meshPosition);
                    mesh->SetRotation(q);

                    scene->Serialize(scene->currentPath);
                } else {
                    spdlog::warn("Failed to load or render mesh: {}", meshPath);
                }
                break;
            }
        }
    }

    for (const auto& obj : scene->objects) {
        if (!obj.HasClass("Mesh")) continue;

        ParameterHandle pathHandle("Mesh.FilePath");
        ParameterHandle posHandle("Entity.Position");
        ParameterHandle rotHandle("Entity.Rotation");
        ParameterHandle scaleHandle("Entity.Scale");

        auto pathValue = obj.GetParameter(pathHandle);
        auto posValue = obj.GetParameter(posHandle);
        auto rotValue = obj.GetParameter(rotHandle);
        auto scaleValue = obj.GetParameter(scaleHandle);

        if (!std::holds_alternative<std::string>(pathValue)) continue;
        std::string meshPath = std::get<std::string>(pathValue);

        auto mesh = GetOrLoadMesh(meshPath);
        if (mesh && mesh->IsLoaded()) {
            glm::vec3 position = std::holds_alternative<glm::vec3>(posValue) ? std::get<glm::vec3>(posValue) : glm::vec3(0.0f);
            glm::quat rotation = std::holds_alternative<glm::quat>(rotValue) ? std::get<glm::quat>(rotValue) : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            glm::vec3 scale = std::holds_alternative<glm::vec3>(scaleValue) ? std::get<glm::vec3>(scaleValue) : glm::vec3(1.0f);

            mesh->SetPosition(position);
            mesh->SetRotation(rotation);
            mesh->SetScale(scale);
            mesh->Render(camera->GetViewMatrix(), camera->GetProjectionMatrix(), camera->GetPosition());
        } else {
            spdlog::warn("Failed to load or render mesh: {}", meshPath);
        }
    }
}

void Renderer::RenderSpheres(Scene * scene) {
    if (!scene || !camera) return;

    bool hasSpheres = false;
    for (const auto& obj : scene->objects) {
        if (obj.HasClass("Sphere")) {
            hasSpheres = true;
            break;
        }
    }
    if (!hasSpheres) return;

    if (blackHoleRenderer) {
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, blackHoleRenderer->GetBlackbodyLUT());
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, blackHoleRenderer->GetHRDiagramLUT());
    }
    
    for (const auto& obj : scene->objects) {
        if (!obj.HasClass("Sphere")) continue;

        ParameterHandle posHandle("Entity.Position");
        ParameterHandle radiusHandle("Sphere.Radius");
        ParameterHandle colorHandle("Sphere.Color");
        ParameterHandle massHandle("Sphere.Mass");

        auto posValue = obj.GetParameter(posHandle);
        auto radiusValue = obj.GetParameter(radiusHandle);
        auto colorValue = obj.GetParameter(colorHandle);
        auto massValue = obj.GetParameter(massHandle);

        if (!std::holds_alternative<glm::vec3>(posValue) || !std::holds_alternative<float>(radiusValue)) {
            continue;
        }

        glm::vec3 position = std::get<glm::vec3>(posValue);
        float radius = std::get<float>(radiusValue);
        glm::vec3 color = std::holds_alternative<glm::vec3>(colorValue) ? std::get<glm::vec3>(colorValue) : glm::vec3(0.5f);
        float mass = std::holds_alternative<float>(massValue) ? std::get<float>(massValue) : 1.0f;

        glm::mat4 model = glm::translate(glm::mat4(1.0f), position);
        model = glm::scale(model, glm::vec3(radius));
        sphereShader->Bind();
        sphereShader->SetMat4("uModel", model);
        sphereShader->SetMat4("uVP", camera->GetViewProjectionMatrix());
        sphereShader->SetVec3("uColor", color);

        sphereShader->SetFloat("uMass", mass);

        if (blackHoleRenderer) {
            sphereShader->SetInt("u_blackbodyLUT", 2);
            sphereShader->SetInt("u_hrDiagramLUT", 3);
            sphereShader->SetInt("u_useHRDiagramLUT", 1);
            
            sphereShader->SetFloat("u_lutTempMin", 1000.0f);
            sphereShader->SetFloat("u_lutTempMax", 40000.0f);
            sphereShader->SetFloat("u_lutRedshiftMin", 0.1f);
            sphereShader->SetFloat("u_lutRedshiftMax", 3.0f);
        } else {
            sphereShader->SetInt("u_useHRDiagramLUT", 0);
        }
        
        glBindVertexArray(m_SphereVAO);
        glDrawElements(GL_TRIANGLES, m_SphereIndexCount, GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
        sphereShader->Unbind();
    }
}

std::shared_ptr<GLTFMesh> Renderer::GetOrLoadMesh(const std::string& path) {
    auto it = m_meshCache.find(path);
    if (it != m_meshCache.end()) {
        return it->second;
    }

    auto mesh = std::make_shared<GLTFMesh>();
    if (mesh->Load(path)) {
        m_meshCache[path] = mesh;
        return mesh;
    }

    return nullptr;
}

void Renderer::SetPhysicsDebugEnabled(bool enabled) {
    if (m_physicsDebugRenderer) {
        m_physicsDebugRenderer->SetEnabled(enabled);
    }
}

void Renderer::SetPhysicsDebugDepthTestEnabled(bool enabled) {
    if (m_physicsDebugRenderer) {
        m_physicsDebugRenderer->SetDepthTestEnabled(enabled);
    }
}

