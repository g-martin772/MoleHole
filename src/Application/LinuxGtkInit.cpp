#include "LinuxGtkInit.h"

#if defined(__linux__)
#include <dlfcn.h>
#include <cstdlib>
#include <spdlog/spdlog.h>

static void* load_library(const char* name) {
    void* h = dlopen(name, RTLD_LAZY | RTLD_LOCAL);
    if (!h) {
        spdlog::debug("dlopen failed for {}: {}", name, dlerror());
    }
    return h;
}

void TryInitializeGtk() {
    static bool done = false;
    if (done) return;

    const char* candidates[] = {
        "libgtk-3.so.0",
        "libgtk-3.so",
        "libgtk-4.so.1",
        "libgtk-4.so"
    };

    void* handle = nullptr;
    for (const char* c : candidates) {
        handle = load_library(c);
        if (handle) break;
    }

    if (!handle) {
        spdlog::debug("GTK library not found; skipping gtk_init_check");
        done = true;
        return;
    }

    using gtk_init_check_t = int(*)(int*, char***);
    auto sym = dlsym(handle, "gtk_init_check");
    if (!sym) {
        spdlog::debug("gtk_init_check not found in GTK library");
        dlclose(handle);
        done = true;
        return;
    }

    gtk_init_check_t gtk_init_check = reinterpret_cast<gtk_init_check_t>(sym);

    int argc = 0;
    char** argv = nullptr;
    int ok = gtk_init_check(&argc, &argv);
    if (!ok) {
        spdlog::debug("gtk_init_check returned false");
    } else {
        spdlog::debug("GTK initialized via gtk_init_check");
    }

    dlclose(handle);
    done = true;
}
#else
void TryInitializeGtk() {}
#endif

