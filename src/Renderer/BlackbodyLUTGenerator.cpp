#include "BlackbodyLUTGenerator.h"
#include <cmath>
#include <algorithm>

namespace MoleHole {

BlackbodyLUTGenerator::RGB BlackbodyLUTGenerator::convertToRGB(float cX, float cY, float cZ, float normalized_cY) {
    float denom = cX + cY + cZ;
    float x = denom > 0.0f ? cX / denom : 0.0f;
    float y = denom > 0.0f ? cY / denom : 0.0f;
    float z = 1.0f - x - y;

    float X = x / std::max(y, 1e-6f);
    float Y = 1.0f;
    float Z = z / std::max(y, 1e-6f);

    // Apply Rec.2020 transform
    float R = rec2020[0][0] * X + rec2020[0][1] * Y + rec2020[0][2] * Z;
    float G = rec2020[1][0] * X + rec2020[1][1] * Y + rec2020[1][2] * Z;
    float B = rec2020[2][0] * X + rec2020[2][1] * Y + rec2020[2][2] * Z;

    // Clamp negatives
    R = std::max(R, 0.0f);
    G = std::max(G, 0.0f);
    B = std::max(B, 0.0f);

    // Normalize to prevent overflow
    float maxVal = std::max({R, G, B});
    if (maxVal > 1.0f) {
        float invMaxVal = 1.0f / maxVal;
        R *= invMaxVal;
        G *= invMaxVal;
        B *= invMaxVal;
    }

    return {R * normalized_cY, G * normalized_cY, B * normalized_cY};
}

BlackbodyLUTGenerator::RGB BlackbodyLUTGenerator::getBlackbodyColor(float temperature, float redshiftFactor) {
    float cX = 0.0f;
    float cY = 0.0f;
    float cZ = 0.0f;
    float wavelength = 380.0f;
    float adjustedTemperature = std::max(1.0f, temperature) / std::max(1e-6f, redshiftFactor);

    const int integrationNum = 80;
    for (int i = 0; i < integrationNum; i++) {
        wavelength += 5.0f;
        float lambda = wavelength * 1e-9f;
        
        // Planck's law
        float exponent = (PlanckConstant * LightSpeed) / (lambda * BoltzmannConstant * adjustedTemperature);
        
        // Prevent overflow for very large exponents
        if (exponent > 100.0f) {
            continue; // Negligible contribution
        }
        
        float intensity = ((2.0f * PlanckConstant * std::pow(LightSpeed, 2.0f)) / std::pow(lambda, 5.0f)) /
                         (std::exp(exponent) - 1.0f);
        
        cX += intensity * matchingFunctionsX[i] * 5.0f;
        cY += intensity * matchingFunctionsY[i] * 5.0f;
        cZ += intensity * matchingFunctionsZ[i] * 5.0f;
    }

    // Guard cY and normalize
    cY = std::max(cY, 1e-12f);
    float log_cY = std::log(cY);
    float normalized_cY = std::clamp((log_cY - LogMin_cY) / (LogMax_cY - LogMin_cY), 0.0f, 1.0f);

    return convertToRGB(cX, cY, cZ, normalized_cY);
}

std::vector<float> BlackbodyLUTGenerator::generateLUT() {
    std::vector<float> lutData;
    lutData.reserve(LUT_WIDTH * LUT_HEIGHT * 3); // RGB

    for (int y = 0; y < LUT_HEIGHT; y++) {
        // Map y to redshift factor
        float t = static_cast<float>(y) / static_cast<float>(LUT_HEIGHT - 1);
        float redshiftFactor = REDSHIFT_MIN + t * (REDSHIFT_MAX - REDSHIFT_MIN);

        for (int x = 0; x < LUT_WIDTH; x++) {
            // Map x to temperature
            float s = static_cast<float>(x) / static_cast<float>(LUT_WIDTH - 1);
            float temperature = TEMP_MIN + s * (TEMP_MAX - TEMP_MIN);

            // Compute blackbody color
            RGB color = getBlackbodyColor(temperature, redshiftFactor);

            // Store RGB
            lutData.push_back(color.r);
            lutData.push_back(color.g);
            lutData.push_back(color.b);
        }
    }

    return lutData;
}

} // namespace MoleHole

