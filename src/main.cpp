#include <GLFW/glfw3.h>
#include <spdlog/spdlog.h>
#include "Renderer/Renderer.h"
#include "Simulation.h"

int main() {
    spdlog::info("Starting MoleHole");

    Renderer renderer;
    renderer.Init();

    Simulation simulation;
    while (!glfwWindowShouldClose(renderer.GetWindow())) {

        simulation.Update();
        renderer.BeginFrame();

        renderer.RenderDockspace();
        renderer.RenderUI();
        renderer.RenderScene(simulation.GetScene());

        renderer.EndFrame();
    }

    renderer.Shutdown();

    spdlog::info("Exiting MoleHole");
    return 0;
}
