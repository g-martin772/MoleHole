#include "BlackbodyLUTGenerator.h"
#include <cmath>
#include <algorithm>

namespace MoleHole {

BlackbodyLUTGenerator::RGB BlackbodyLUTGenerator::convertToRGB(float cX, float cY, float cZ, float normalized_cY) {
    // Standard XYZ to sRGB transformation matrix (D65 illuminant)
    // This is the correct matrix for converting CIE XYZ to sRGB
    const float xyz_to_srgb[3][3] = {
        { 3.2406f, -1.5372f, -0.4986f},
        {-0.9689f,  1.8758f,  0.0415f},
        { 0.0557f, -0.2040f,  1.0570f}
    };
    
    float R = xyz_to_srgb[0][0] * cX + xyz_to_srgb[0][1] * cY + xyz_to_srgb[0][2] * cZ;
    float G = xyz_to_srgb[1][0] * cX + xyz_to_srgb[1][1] * cY + xyz_to_srgb[1][2] * cZ;
    float B = xyz_to_srgb[2][0] * cX + xyz_to_srgb[2][1] * cY + xyz_to_srgb[2][2] * cZ;

    // Clamp negatives (out of gamut colors)
    R = std::max(R, 0.0f);
    G = std::max(G, 0.0f);
    B = std::max(B, 0.0f);

    // Scale by normalized brightness
    R *= normalized_cY;
    G *= normalized_cY;
    B *= normalized_cY;

    return {R, G, B};
}

BlackbodyLUTGenerator::RGB BlackbodyLUTGenerator::getBlackbodyColor(float temperature, float redshiftFactor) {
    float cX = 0.0f;
    float cY = 0.0f;
    float cZ = 0.0f;
    
    float adjustedTemperature = std::max(1.0f, temperature) / std::max(1e-6f, redshiftFactor);

    // Integrate over visible spectrum (380nm to 780nm in 5nm steps)
    const int integrationNum = 81; // (780-380)/5 + 1 = 81 samples
    for (int i = 0; i < integrationNum; i++) {
        float wavelength = 380.0f + i * 5.0f; // Start at 380nm, increment by 5nm
        float lambda = wavelength * 1e-9f; // Convert to meters
        
        // Planck's law for blackbody radiation
        float exponent = (PlanckConstant * LightSpeed) / (lambda * BoltzmannConstant * adjustedTemperature);
        
        // Prevent overflow for very large exponents
        if (exponent > 100.0f) {
            continue; // Negligible contribution
        }
        
        // Spectral radiance from Planck's law
        float intensity = ((2.0f * PlanckConstant * std::pow(LightSpeed, 2.0f)) / std::pow(lambda, 5.0f)) /
                         (std::exp(exponent) - 1.0f);
        
        // Integrate with CIE color matching functions
        cX += intensity * matchingFunctionsX[i];
        cY += intensity * matchingFunctionsY[i];
        cZ += intensity * matchingFunctionsZ[i];
    }

    // Check if we have any light
    if (cY < 1e-12f) {
        return {0.0f, 0.0f, 0.0f};
    }
    
    // Normalize XYZ values by Y (luminance) to get standard tristimulus values
    // This preserves the color while normalizing brightness
    float maxXYZ = std::max({cX, cY, cZ});
    float scale = 0.0f;
    if (maxXYZ > 0.0f) {
        scale = 1.0f / maxXYZ;
        cX *= scale;
        cY *= scale;
        cZ *= scale;
    }
    
    // Calculate brightness factor from original Y value
    float log_cY = std::log(cY / scale); // Use pre-normalized Y
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
