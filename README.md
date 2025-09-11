# MoleHole
Black Hole Simulation using OpenGL and RayTracing

## Submodule Management 

This project uses git submodules for dependencies.

To clone the repository with all submodules:

    git clone --recurse-submodules <repo-url>

If you already cloned the repository, initialize and update submodules with:

    git submodule update --init --recursive

To fetch the latest updates for all submodules:

    git submodule update --remote --recursive

To switch a submodule to a different branch:

    git config -f .gitmodules submodule.dependencies/imgui.branch docking
    git submodule sync --recursive
    git submodule update --remote dependencies/imgui

For more information, see the official git submodule documentation: https://git-scm.com/book/en/v2/Git-Tools-Submodules
