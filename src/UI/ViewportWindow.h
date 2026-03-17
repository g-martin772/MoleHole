#pragma once

#include "UI.h"
#include <imgui.h>

class ViewportWindow {
public:
    static void Render(UI* ui, Scene* scene);
};
