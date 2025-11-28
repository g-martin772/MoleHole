//
// Created by leo on 11/28/25.
//

#ifndef MOLEHOLE_SCENEWINDOW_H
#define MOLEHOLE_SCENEWINDOW_H

#include <string>

class Scene;
class UI;

namespace SceneWindow {
    void Render(UI* ui, Scene* scene);
    void LoadScene(Scene* scene, const std::string& path);
}

#endif //MOLEHOLE_SCENEWINDOW_H