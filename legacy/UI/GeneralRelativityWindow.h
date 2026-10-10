#pragma once

class UI;

class GeneralRelativityWindow {
public:
    static void Render(bool* p_open, UI* ui);

private:
    static bool s_showAdvancedSettings;
};

