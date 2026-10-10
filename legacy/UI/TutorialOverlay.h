#pragma once

class UI;

namespace TutorialOverlay {
    void Render(UI* ui);
    bool IsActive();
    void Start(UI* ui);
    void Stop(UI* ui);
    int GetCurrentStep();
    int GetTotalSteps();
}

