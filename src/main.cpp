#include <spdlog/spdlog.h>
#include "Application/Application.h"

int main() {
    spdlog::set_level(spdlog::level::debug);
    spdlog::set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");

    auto& app = Application::Instance();

    if (!app.Initialize()) {
        spdlog::error("Failed to initialize application");
        return -1;
    }

    app.Run();
    app.Shutdown();
    return 0;
}
