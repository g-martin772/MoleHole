//
// Created by leo on 11/28/25.
//

#ifndef MOLEHOLE_SETTINGSPOPUP_H
#define MOLEHOLE_SETTINGSPOPUP_H
#include "Simulation/Scene.h"

class UI;

namespace SettingsPopUp {
    void Render(UI* ui, Scene* scene, bool* showSettingsWindow);
}

#endif //MOLEHOLE_SETTINGSPOPUP_H