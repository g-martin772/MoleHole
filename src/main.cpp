#include <spdlog/spdlog.h>
#include "Application/Application.h"

int main() {
    auto& app = Application::Instance();

    if (!app.Initialize()) {
        spdlog::error("Failed to initialize application");
        return -1;
    }

    app.Run();
    app.Shutdown();
    return 0;
}
