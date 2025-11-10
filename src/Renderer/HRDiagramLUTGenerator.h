#pragma once

#include <vector>
#include <cmath>
#include <algorithm>

namespace MoleHole {

class HRDiagramLUTGenerator {
public:
    // LUT parameters - maps mass (in solar masses) to temperature and color
    static constexpr float MASS_MIN = 0.08f;      // Minimum stellar mass (brown dwarf limit)
    static constexpr float MASS_MAX = 100.0f;     // Maximum stellar mass
    static constexpr int LUT_SIZE = 256;          // Number of mass samples
    
    // Hertzsprung-Russell diagram approximation
    // Temperature ranges for different stellar classes
    static constexpr float TEMP_O_CLASS = 30000.0f;  // O-type (blue)
    static constexpr float TEMP_B_CLASS = 15000.0f;  // B-type (blue-white)
    static constexpr float TEMP_A_CLASS = 9000.0f;   // A-type (white)
    static constexpr float TEMP_F_CLASS = 7000.0f;   // F-type (yellow-white)
    static constexpr float TEMP_G_CLASS = 5500.0f;   // G-type (yellow) - like our Sun
    static constexpr float TEMP_K_CLASS = 4000.0f;   // K-type (orange)
    static constexpr float TEMP_M_CLASS = 3000.0f;   // M-type (red)
    
    struct HRData {
        float temperature;  // Effective temperature in Kelvin
        float luminosity;   // Relative to Sun
        float radius;       // Relative to Sun
    };
    
    // Generate the LUT data (returns temperature, luminosity, radius)
    // Format: [temp0, lum0, rad0, temp1, lum1, rad1, ...]
    static std::vector<float> generateLUT();
    
private:
    // Calculate stellar properties from mass using main sequence relationships
    static HRData calculateMainSequenceProperties(float mass);
    
    // Mass-Temperature relationship (empirical fit for main sequence stars)
    static float massToTemperature(float mass);
    
    // Mass-Luminosity relationship (L ∝ M^3.5 for main sequence)
    static float massToLuminosity(float mass);
    
    // Mass-Radius relationship (R ∝ M^0.8 for main sequence)
    static float massToRadius(float mass);
};

} // namespace MoleHole

