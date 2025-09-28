#include <memory>

#include "Scene.h"

class Simulation {
public:
    Simulation();
    void Update();

    Scene* GetScene();
private:
    std::unique_ptr<Scene> scene;
};
