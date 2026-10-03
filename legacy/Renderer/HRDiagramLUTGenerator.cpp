#include "HRDiagramLUTGenerator.h"
#include <cmath>
#include <algorithm>

namespace MoleHole {

float HRDiagramLUTGenerator::massToTemperature(float mass) {
    // Empirical mass-temperature relationship for main sequence stars
    // Based on observed stellar data and physics models
    
    if (mass < 0.43f) {
        // Low mass stars (M-type red dwarfs)
        // Temperature increases slowly with mass
        return 2300.0f + (mass / 0.43f) * 700.0f; // 2300K - 3000K
    } else if (mass < 0.8f) {
        // Lower main sequence (late K to early M)
        return 3000.0f + ((mass - 0.43f) / 0.37f) * 1000.0f; // 3000K - 4000K
    } else if (mass < 1.0f) {
        // Solar-type stars (G to early K)
        return 4000.0f + ((mass - 0.8f) / 0.2f) * 1500.0f; // 4000K - 5500K
    } else if (mass < 1.4f) {
        // Early main sequence (F to late A)
        return 5500.0f + ((mass - 1.0f) / 0.4f) * 1500.0f; // 5500K - 7000K
    } else if (mass < 2.1f) {
        // A-type stars
        return 7000.0f + ((mass - 1.4f) / 0.7f) * 2000.0f; // 7000K - 9000K
    } else if (mass < 16.0f) {
        // B-type stars (blue-white)
        float t = (mass - 2.1f) / 13.9f;
        return 9000.0f + t * 6000.0f; // 9000K - 15000K
    } else {
        // O-type stars (very hot blue stars)
        float t = std::min((mass - 16.0f) / (MASS_MAX - 16.0f), 1.0f);
        return 15000.0f + t * 30000.0f; // 15000K - 45000K
    }
}

float HRDiagramLUTGenerator::massToLuminosity(float mass) {
    // Mass-Luminosity relationship for main sequence stars
    // L/L_sun = (M/M_sun)^α, where α varies with mass
    
    if (mass < 0.43f) {
        // Very low mass: L ∝ M^2.3
        return std::pow(mass, 2.3f);
    } else if (mass < 2.0f) {
        // Lower-middle main sequence: L ∝ M^4
        return std::pow(mass, 4.0f);
    } else if (mass < 20.0f) {
        // Upper main sequence: L ∝ M^3.5
        return std::pow(mass, 3.5f);
    } else {
        // Very massive stars: L ∝ M^3
        return std::pow(mass, 3.0f);
    }
}

float HRDiagramLUTGenerator::massToRadius(float mass) {
    // Mass-Radius relationship for main sequence stars
    // R/R_sun = (M/M_sun)^α, where α varies with mass
    
    if (mass < 1.0f) {
        // Lower main sequence: R ∝ M^0.8
        return std::pow(mass, 0.8f);
    } else {
        // Upper main sequence: R ∝ M^0.57
        return std::pow(mass, 0.57f);
    }
}

HRDiagramLUTGenerator::HRData HRDiagramLUTGenerator::calculateMainSequenceProperties(float mass) {
    HRData data;
    data.temperature = massToTemperature(mass);
    data.luminosity = massToLuminosity(mass);
    data.radius = massToRadius(mass);
    return data;
}

std::vector<float> HRDiagramLUTGenerator::generateLUT() {
    std::vector<float> lutData;
    lutData.reserve(LUT_SIZE * 3); // temperature, luminosity, radius per sample
    
    for (int i = 0; i < LUT_SIZE; i++) {
        // Map index to mass using logarithmic scale for better resolution at low masses
        float t = static_cast<float>(i) / static_cast<float>(LUT_SIZE - 1);
        
        // Logarithmic interpolation between MASS_MIN and MASS_MAX
        float logMassMin = std::log(MASS_MIN);
        float logMassMax = std::log(MASS_MAX);
        float mass = std::exp(logMassMin + t * (logMassMax - logMassMin));
        
        // Calculate HR diagram properties
        HRData properties = calculateMainSequenceProperties(mass);
        
        // Store in LUT: [temperature, luminosity, radius]
        lutData.push_back(properties.temperature);
        lutData.push_back(properties.luminosity);
        lutData.push_back(properties.radius);
    }
    
    return lutData;
}

} // namespace MoleHole

