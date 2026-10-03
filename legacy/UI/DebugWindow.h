//
// Created by leo on 11/28/25.
//

#ifndef MOLEHOLE_DEBUGWINDOW_H
#define MOLEHOLE_DEBUGWINDOW_H

class UI;

namespace DebugWindow {
    void Render(UI* ui);
    void RenderDebugModeCombo(UI* ui);
    void RenderDebugModeTooltip(int debugMode);
}

#endif //MOLEHOLE_DEBUGWINDOW_H