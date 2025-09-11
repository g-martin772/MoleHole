#include <memory>

#include "Scene.h"

class Simulation {
public:
    void Update();

    Scene* GetScene();
private:
    std::unique_ptr<Scene> scene;
};
