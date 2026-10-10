#pragma once

#include <vector>
#include <cmath>
#include <algorithm>

namespace MoleHole {

class AccelerationLUTGenerator {
public:
    // LUT parameters
    static constexpr float R_MIN = 0.01f;          // Minimum radius (avoid singularity)
    static constexpr float R_MAX = 50.0f;          // Maximum radius to precompute
    static constexpr float ANG_MOM_MIN = 0.0f;     // Minimum angular momentum squared
    static constexpr float ANG_MOM_MAX = 100.0f;   // Maximum angular momentum squared
    static constexpr int LUT_WIDTH = 512;          // Angular momentum samples
    static constexpr int LUT_HEIGHT = 512;         // Radius samples
    
    static constexpr float EPSILON = 0.01f;
    
    // Generate the LUT data (returns scalar factors in row-major order)
    // The actual acceleration is: factor * relPos, where factor is the LUT value
    static std::vector<float> generateLUT();
    
private:
    static float calculateAccelerationFactor(float angMomentumSqrd, float rSqrd);
};

} // namespace MoleHole

