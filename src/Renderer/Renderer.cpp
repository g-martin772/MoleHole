#include <glad/gl.h>
#include "Renderer.h"
#include <GLFW/glfw3.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "spdlog/spdlog.h"
#include "Shader.h"
#include <memory>
#include "Camera.h"
#include "Input.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "Buffer.h"

void Renderer::Init(GlobalOptions *options) {
    globalOptions = options;

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

    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    camera = std::make_unique<Camera>(45.0f, (float) width / (float) height, 0.1f, 100.0f);
    input = std::make_unique<Input>(window);
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
    if (globalOptions) {
        int vsyncVal = globalOptions->vsync ? 1 : 0;
        if (vsyncVal != lastVsync) {
            glfwSwapInterval(vsyncVal);
            lastVsync = vsyncVal;
        }
    }
    glfwPollEvents();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
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

void Renderer::RenderDockspace(Scene* scene) {
    ImGuiIO& io = ImGui::GetIO();
    bool ctrl = io.KeyCtrl;
    static bool prevCtrlS = false, prevCtrlO = false;
    bool ctrlS = ctrl && ImGui::IsKeyPressed(ImGuiKey_S);
    bool ctrlO = ctrl && ImGui::IsKeyPressed(ImGuiKey_O);
    bool doSave = false, doOpen = false;
    if (ctrlS && !prevCtrlS) doSave = true;
    if (ctrlO && !prevCtrlO) doOpen = true;
    prevCtrlS = ctrlS;
    prevCtrlO = ctrlO;

    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open Scene...", "Ctrl+O")) {
                doOpen = true;
            }
            bool canSave = scene && !scene->currentPath.empty();
            if (ImGui::MenuItem("Save Scene", "Ctrl+S", false, canSave)) {
                doSave = true;
            }
            if (ImGui::MenuItem("Save Scene As...")) {
                if (scene) {
                    auto path = Scene::ShowFileDialog(true);
                    if (!path.empty()) {
                        scene->Serialize(path);
                    }
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            bool selectedDemo1 = selectedViewport == ViewportMode::Demo1;
            bool selectedRays2D = selectedViewport == ViewportMode::Rays2D;
            if (ImGui::MenuItem("Demo1", nullptr, selectedDemo1)) {
                selectedViewport = ViewportMode::Demo1;
            }
            if (ImGui::MenuItem("Rays2D", nullptr, selectedRays2D)) {
                selectedViewport = ViewportMode::Rays2D;
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    if (doOpen && scene) {
        auto path = Scene::ShowFileDialog(false);
        if (!path.empty()) {
            scene->Deserialize(path);
        }
    }
    if (doSave && scene && !scene->currentPath.empty()) {
        scene->Serialize(scene->currentPath);
    }

    ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + ImGui::GetFrameHeight()));
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, viewport->Size.y - ImGui::GetFrameHeight()));
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGuiWindowFlags dockspace_flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
                                       ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                                       |
                                       ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    ImGui::Begin("##DockSpace", nullptr, dockspace_flags);
    ImGui::DockSpace(ImGui::GetID("DockSpace"), ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::End();
    ImGui::PopStyleVar(2);
}

void Renderer::RenderUI(float fps, Scene *scene) {
    ImGui::Begin("Test");
    if (scene) {
        char nameBuffer[128];
        std::strncpy(nameBuffer, scene->name.c_str(), sizeof(nameBuffer));
        nameBuffer[sizeof(nameBuffer)-1] = '\0';
        if (ImGui::InputText("Scene Name", nameBuffer, sizeof(nameBuffer))) {
            scene->name = nameBuffer;
        }
    }
    ImGui::Text("Hello World");
    ImGui::Text("FPS: %.1f", fps);
    bool vsync = globalOptions->vsync;
    if (ImGui::Checkbox("VSync", &vsync)) {
        globalOptions->vsync = vsync;
    }
    ImGui::Text("VSync State: %s", globalOptions->vsync ? "Enabled" : "Disabled");

    if (ImGui::CollapsingHeader("Black Holes", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button("Add Black Hole")) {
            BlackHole newHole;
            newHole.mass = 1.0f;
            newHole.position = glm::vec3(0.0f);
            newHole.showAccretionDisk = true;
            newHole.accretionDiskDensity = 1.0f;
            newHole.accretionDiskSize = 1.0f;
            newHole.accretionDiskColor = glm::vec3(1.0f, 0.8f, 0.2f);
            scene->blackHoles.push_back(newHole);
        }
        int idx = 0;
        for (auto it = scene->blackHoles.begin(); it != scene->blackHoles.end();) {
            ImGui::PushID(idx);
            bool open = ImGui::TreeNode("Black Hole");
            ImGui::SameLine();
            bool remove = ImGui::Button("Remove");
            if (open) {
                BlackHole &bh = *it;
                ImGui::DragFloat("Mass", &bh.mass, 0.01f, 0.0f, 1e10f);
                ImGui::DragFloat3("Position", &bh.position[0], 0.01f);
                ImGui::Checkbox("Show Accretion Disk", &bh.showAccretionDisk);
                ImGui::DragFloat("Accretion Disk Density", &bh.accretionDiskDensity, 0.01f, 0.0f, 1e6f);
                ImGui::DragFloat("Accretion Disk Size", &bh.accretionDiskSize, 0.01f, 0.0f, 1e6f);
                ImGui::ColorEdit3("Accretion Disk Color", &bh.accretionDiskColor[0]);
                ImGui::TreePop();
            }
            ImGui::PopID();
            if (remove) {
                it = scene->blackHoles.erase(it);
            } else {
                ++it;
            }
            idx++;
        }
    }

    ImGui::End();
}

void Renderer::RenderScene(Scene *scene) {
    const char *title = nullptr;
    switch (selectedViewport) {
        case ViewportMode::Demo1: title = "Viewport - Demo1";
            break;
        case ViewportMode::Rays2D: title = "Viewport - 2D Rays";
            break;
        default: title = "Viewport";
            break;
    }
    ImGui::Begin(title);
    ImVec2 imgui_size = ImGui::GetContentRegionAvail();
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
    }
    bool viewport_focused = ImGui::IsWindowFocused();
    bool viewport_hovered = ImGui::IsWindowHovered();
    input->SetViewportFocused(viewport_focused);
    input->SetViewportHovered(viewport_hovered);
    static double lastTime = glfwGetTime();
    double currentTime = glfwGetTime();
    float deltaTime = static_cast<float>(currentTime - lastTime);
    lastTime = currentTime;
    input->Update();
    if (viewport_focused && viewport_hovered) {
        UpdateCamera(deltaTime);
    } else {
        input->SetCursorEnabled(true);
    }

    unsigned int fbo;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, image->textureID, 0);
    glViewport(0, 0, image->width, image->height);

    switch (selectedViewport) {
        case ViewportMode::Demo1:
            glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            RenderDemo1(scene);
            break;
        case ViewportMode::Rays2D:
            glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            Render2DRays(scene);
            break;
        default:
            break;
    }
    glDeleteFramebuffers(1, &fbo);
    ImGui::Image((void *) (intptr_t) image->textureID, ImVec2((float) image->width, (float) image->height),
                 ImVec2(0, 1), ImVec2(1, 0));
    ImGui::End();
}

void Renderer::RenderDemo1(Scene *scene) {
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

void Renderer::Render2DRays(Scene *scene) {
    // TODO
}

void Renderer::UpdateCamera(float deltaTime) {
    float forward = 0.0f, right = 0.0f, up = 0.0f;
    if (input->IsKeyDown(GLFW_KEY_W)) forward += 1.0f;
    if (input->IsKeyDown(GLFW_KEY_S)) forward -= 1.0f;
    if (input->IsKeyDown(GLFW_KEY_D)) right += 1.0f;
    if (input->IsKeyDown(GLFW_KEY_A)) right -= 1.0f;
    if (input->IsKeyDown(GLFW_KEY_E)) up += 1.0f;
    if (input->IsKeyDown(GLFW_KEY_Q)) up -= 1.0f;
    camera->ProcessKeyboard(forward, right, up, deltaTime);
    if (input->IsMouseButtonDown(GLFW_MOUSE_BUTTON_RIGHT)) {
        input->SetCursorEnabled(false);
        double dx, dy;
        input->GetMouseDelta(dx, dy);
        camera->ProcessMouse((float) dx, (float) dy);
    } else {
        input->SetCursorEnabled(true);
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
