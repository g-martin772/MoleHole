#include "Simulation.h"

Simulation::Simulation() {
    scene = std::make_unique<Scene>();
}

void Simulation::Update() {
}

Scene* Simulation::GetScene() {
    return scene.get();
}
