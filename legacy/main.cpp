#include <spdlog/spdlog.h>
#include "Application/Application.h"

int main(int argc, char* argv[]) {
    spdlog::set_level(spdlog::level::debug);
    spdlog::set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");

    auto& app = Application::Instance();

    if (!app.Initialize(argc, argv)) {
        spdlog::error("Failed to initialize application");
        return -1;
    }

    if (Application::Args().IsHeadless()) {
        app.RunHeadless();
    } else {
        app.Run();
    }

    app.Shutdown();
    return 0;
}
