//
// Created by leo on 11/28/25.
//

#ifndef MOLEHOLE_TOPBAR_H
#define MOLEHOLE_TOPBAR_H

#include <string>

class Scene;
class UI;

namespace TopBar {
    void RenderMainMenuBar(UI* ui, Scene* scene, bool& doSave, bool& doOpen,
                          bool& doTakeScreenshotDialog, bool& doTakeScreenshotViewportDialog,
                          bool& doTakeScreenshot, bool& doTakeScreenshotViewport);
    void HandleFileOperations(UI* ui, Scene* scene, bool doSave, bool doOpen);
    void HandleImageShortcuts(Scene* scene, bool takeViewportScreenshot, bool takeFullScreenshot,
                             bool takeViewportScreenshotWithDialog, bool takeFullScreenshotWithDialog);
    void LoadScene(UI* ui, Scene* scene, const std::string& path);
    void TakeScreenshot();
    void TakeViewportScreenshot();
    void TakeScreenshotWithDialog();
    void TakeViewportScreenshotWithDialog();
}

#endif //MOLEHOLE_TOPBAR_H