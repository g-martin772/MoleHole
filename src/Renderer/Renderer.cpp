#include <glad/gl.h>
#include "Renderer.h"
#include <GLFW/glfw3.h>
#include "imgui.h"
#include "ImGuizmo.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "spdlog/spdlog.h"
#include "Shader.h"
#include <memory>
#include "Camera.h"
#include "Input.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>

#include "Buffer.h"
#include <cmath>

#include "Application/Application.h"
#include "GravityGridRenderer.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void Renderer::Init() {
    if (!glfwInit()) {
        spdlog::error("Failed to initialize GLFW");
        exit(-1);
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    window = glfwCreateWindow(800, 600, "MoleHole Window", nullptr, nullptr);
    if (!window) {
        spdlog::error("Failed to create GLFW window");
        glfwTerminate();
        exit(-1);
    }
    glfwMakeContextCurrent(window);

    int version = gladLoadGL(glfwGetProcAddress);
    if (version == 0) {
        spdlog::error("Failed to initialize GLAD");
        exit(-1);
    }

    spdlog::info("Loaded OpenGL {0}.{1}", GLAD_VERSION_MAJOR(version), GLAD_VERSION_MINOR(version));

    spdlog::info("OpenGL version: {}", (const char *) glGetString(GL_VERSION));

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    (void) io;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 460");

    image = std::make_shared<Image>(last_img_width, last_img_height);

    spdlog::info("Loading shaders");
    quadShader = std::make_unique<Shader>("../shaders/quad.vert", "../shaders/quad.frag");
    circleShader = std::make_unique<Shader>("../shaders/circle.vert", "../shaders/circle.frag");
    sphereShader = std::make_unique<Shader>("../shaders/sphere.vert", "../shaders/sphere.frag");

    blackHoleRenderer = std::make_unique<BlackHoleRenderer>();
    blackHoleRenderer->Init(last_img_width, last_img_height);
    visualRenderer = std::make_unique<VisualRenderer>();
    visualRenderer->Init(last_img_width, last_img_height);

    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    // Set a much larger far plane for the camera to avoid grid clipping
    camera = std::make_unique<Camera>(Application::State().rendering.fov, (float) width / (float) height, 0.01f, 10000.0f);

    camera->SetPosition(Application::State().camera.position);
    camera->SetYawPitch(Application::State().camera.yaw, Application::State().camera.pitch);

    input = std::make_unique<Input>(window);

    gravityGridRenderer = std::make_unique<GravityGridRenderer>();
    gravityGridRenderer->Init();
}

void Renderer::Shutdown() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    window = nullptr;
}

void Renderer::BeginFrame() {
    int vsyncVal = Application::State().window.vsync ? 1 : 0;
    if (vsyncVal != lastVsync) {
        glfwSwapInterval(vsyncVal);
        lastVsync = vsyncVal;
    }
    glfwPollEvents();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();
}

void Renderer::EndFrame() {
    int display_w, display_h;
    glfwGetFramebufferSize(window, &display_w, &display_h);
    glViewport(0, 0, display_w, display_h);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window);
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
        case ViewportMode::SimulationVisual:
            title = "Viewport - Visual Simulation";
            break;
        default:
            title = "Viewport";
            break;
    }

    ImGui::Begin(title);

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
        case ViewportMode::SimulationVisual:
            RenderVisualSimulation(scene);
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

void Renderer::DrawSphere(const glm::vec3 &pos, float radius, const glm::vec3 &color) {
    static unsigned int sphereVAO = 0, sphereVBO = 0, sphereEBO = 0;
    static int indexCount = 0;
    if (sphereVAO == 0) {
        const int X_SEGMENTS = 16;
        const int Y_SEGMENTS = 16;
        std::vector<float> vertices;
        std::vector<unsigned int> indices;
        for (int y = 0; y <= Y_SEGMENTS; ++y) {
            for (int x = 0; x <= X_SEGMENTS; ++x) {
                float xSegment = (float) x / (float) X_SEGMENTS;
                float ySegment = (float) y / (float) Y_SEGMENTS;
                float xPos = std::cos(xSegment * 2.0f * M_PI) * std::sin(ySegment * M_PI);
                float yPos = std::cos(ySegment * M_PI);
                float zPos = std::sin(xSegment * 2.0f * M_PI) * std::sin(ySegment * M_PI);
                vertices.push_back(xPos);
                vertices.push_back(yPos);
                vertices.push_back(zPos);
            }
        }
        for (int y = 0; y < Y_SEGMENTS; ++y) {
            for (int x = 0; x <= X_SEGMENTS; ++x) {
                indices.push_back(y * (X_SEGMENTS + 1) + x);
                indices.push_back((y + 1) * (X_SEGMENTS + 1) + x);
            }
        }
        indexCount = indices.size();
        glGenVertexArrays(1, &sphereVAO);
        glGenBuffers(1, &sphereVBO);
        glGenBuffers(1, &sphereEBO);
        glBindVertexArray(sphereVAO);
        glBindBuffer(GL_ARRAY_BUFFER, sphereVBO);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sphereEBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void *) 0);
        glBindVertexArray(0);
    }
    sphereShader->Bind();
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, pos);
    model = glm::scale(model, glm::vec3(radius));
    sphereShader->SetMat4("uModel", model);
    sphereShader->SetMat4("uVP", camera->GetViewProjectionMatrix());
    sphereShader->SetVec3("uColor", color);
    glBindVertexArray(sphereVAO);
    for (int y = 0; y < 16; ++y) {
        glDrawElements(GL_TRIANGLE_STRIP, (16 + 1) * 2, GL_UNSIGNED_INT,
                       (void *) (sizeof(unsigned int) * (y * (16 + 1) * 2)));
    }
    glBindVertexArray(0);
    sphereShader->Unbind();
}

void Renderer::Render2DRays(Scene *scene) {
    glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    if (!scene) return;

    float currentTime = static_cast<float>(glfwGetTime());

    for (const auto &bh: scene->blackHoles) {
        DrawCircle(glm::vec2(bh.position.x, bh.position.y), 0.1f * std::cbrt(bh.mass), bh.accretionDiskColor);
    }
    blackHoleRenderer->Render(scene->blackHoles, *camera, currentTime);
}

void Renderer::Render3DSimulation(Scene *scene) {
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!scene || !blackHoleRenderer) return;

    float currentTime = static_cast<float>(glfwGetTime());

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    blackHoleRenderer->Render(scene->blackHoles, *camera, currentTime);
    blackHoleRenderer->RenderToScreen();

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);

    if (Application::State().rendering.debugMode == DebugMode::GravityGrid && gravityGridRenderer) {
        gravityGridRenderer->Render(scene->blackHoles, *camera, currentTime);
    }

    RenderMeshes(scene);

    glDisable(GL_DEPTH_TEST);
}

void Renderer::RenderVisualSimulation(Scene *scene) {

    if (!scene || !visualRenderer) return;

    float currentTime = static_cast<float>(glfwGetTime());
    visualRenderer->Render(*camera, currentTime);
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

    float cameraSpeed = Application::State().app.cameraSpeed;
    camera->ProcessKeyboard(forward, right, up, deltaTime, cameraSpeed);

    bool cameraChanged = false;
    if (input->IsMouseButtonDown(GLFW_MOUSE_BUTTON_RIGHT)) {
        input->SetCursorEnabled(false);
        double dx, dy;
        input->GetMouseDelta(dx, dy);
        float mouseSensitivity = Application::State().app.mouseSensitivity;
        if (dx != 0.0 || dy != 0.0) {
            camera->ProcessMouse((float) dx, (float) dy, mouseSensitivity);
            cameraChanged = true;
        }
    } else {
        input->SetCursorEnabled(true);
    }

    if (forward != 0.0f || right != 0.0f || up != 0.0f || cameraChanged) {
        Application::State().UpdateCameraState(
            camera->GetPosition(),
            camera->GetFront(),
            camera->GetUp(),
            camera->GetPitch(),
            camera->GetYaw()
        );
    }

    if (camera->GetFov() != Application::State().rendering.fov) {
        camera->SetFov(Application::State().rendering.fov);
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

void Renderer::RenderMeshes(Scene* scene) {
    if (!scene || !camera) return;

    if (scene->meshes.empty()) {
        return;
    }

    for (const auto& meshObj : scene->meshes) {
        auto mesh = GetOrLoadMesh(meshObj.path);
        if (mesh && mesh->IsLoaded()) {
            mesh->SetPosition(meshObj.position);
            mesh->SetRotation(meshObj.rotation);
            mesh->SetScale(meshObj.scale);
            mesh->Render(camera->GetViewMatrix(), camera->GetProjectionMatrix(), camera->GetPosition());
        } else {
            spdlog::warn("Failed to load or render mesh: {}", meshObj.path);
        }
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
