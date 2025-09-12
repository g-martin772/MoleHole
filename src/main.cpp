#include <GLFW/glfw3.h>
#include <spdlog/spdlog.h>
#include "Renderer/Renderer.h"
#include "Simulation.h"
#include "FpsCounter.h"
#include "GlobalOptions.h"

int main() {
    spdlog::info("Starting MoleHole");

    GlobalOptions globalOptions;
    Renderer renderer;
    renderer.Init(&globalOptions);

    Simulation simulation;

    {
        auto scene = simulation.GetScene();
        if (scene && std::filesystem::exists("scene.yaml")) {
            scene->Deserialize("scene.yaml");
        }
    }

    FpsCounter fpsCounter;
    while (!glfwWindowShouldClose(renderer.GetWindow())) {
        fpsCounter.Frame();
        simulation.Update();
        renderer.BeginFrame();

        auto scene = simulation.GetScene();
        renderer.RenderDockspace(scene);
        renderer.RenderUI(fpsCounter.GetFps(), scene);
        renderer.RenderScene(scene);

        renderer.EndFrame();
    }

    renderer.Shutdown();

    spdlog::info("Exiting MoleHole");
    return 0;
}
