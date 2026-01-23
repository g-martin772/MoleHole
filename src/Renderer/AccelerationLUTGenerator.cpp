#include "AccelerationLUTGenerator.h"
#include <cmath>
#include <algorithm>

namespace MoleHole {

float AccelerationLUTGenerator::calculateAccelerationFactor(float angMomentumSqrd, float rSqrd) {
    // Original formula: return (-1.0f * angMomentumSqrd * relPos) * 2.0f / r5;
    // where r5 = pow(rSqrd, 2.5) = rSqrd^2.5
    // So the factor multiplying relPos is: -2.0f * angMomentumSqrd / r5
    
    float r5 = std::pow(rSqrd, 2.5f);
    
    // Avoid division by zero
    if (r5 < EPSILON) {
        return -1.5f * angMomentumSqrd / (1.0f * EPSILON);
    }
    
    return -1.5f * angMomentumSqrd / r5;
}

std::vector<float> AccelerationLUTGenerator::generateLUT() {
    std::vector<float> lutData;
    lutData.reserve(LUT_WIDTH * LUT_HEIGHT);

    for (int y = 0; y < LUT_HEIGHT; y++) {
        // Map y to radius squared (using log scale for better precision near singularity)
        float t = static_cast<float>(y) / static_cast<float>(LUT_HEIGHT - 1);
        // Use log scale: more precision near R_MIN where acceleration changes rapidly
        float logRMin = std::log(R_MIN);
        float logRMax = std::log(R_MAX);
        float r = std::exp(logRMin + t * (logRMax - logRMin));
        float rSqrd = r * r;

        for (int x = 0; x < LUT_WIDTH; x++) {
            // Map x to angular momentum squared
            float s = static_cast<float>(x) / static_cast<float>(LUT_WIDTH - 1);
            float angMomentumSqrd = ANG_MOM_MIN + s * (ANG_MOM_MAX - ANG_MOM_MIN);

            // Compute acceleration factor
            float factor = calculateAccelerationFactor(angMomentumSqrd, rSqrd);

            // Store the factor
            lutData.push_back(factor);
        }
    }

    return lutData;
}

} // namespace MoleHole

