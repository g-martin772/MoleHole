//
// Created by leo on 11/28/25.
//

#ifndef MOLEHOLE_SIMULATIONWINDOW_H
#define MOLEHOLE_SIMULATIONWINDOW_H

class Scene;
class UI;

namespace SimulationWindow {
    void Render(UI* ui, Scene* scene);
    void RenderSimulationControls(UI* ui);
}

#endif //MOLEHOLE_SIMULATIONWINDOW_H