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
    FpsCounter fpsCounter;
    while (!glfwWindowShouldClose(renderer.GetWindow())) {
        fpsCounter.Frame();
        simulation.Update();
        renderer.BeginFrame();

        renderer.RenderDockspace();
        renderer.RenderUI(fpsCounter.GetFps());
        renderer.RenderScene(simulation.GetScene());

        renderer.EndFrame();
    }

    renderer.Shutdown();

    spdlog::info("Exiting MoleHole");
    return 0;
}
