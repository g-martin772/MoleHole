#pragma once

#include <vector>
#include <cmath>
#include <algorithm>

namespace MoleHole {

class BlackbodyLUTGenerator {
public:
    // LUT parameters (must match shader constants)
    static constexpr float TEMP_MIN = 1000.0f;
    static constexpr float TEMP_MAX = 40000.0f;
    static constexpr float REDSHIFT_MIN = 0.1f;
    static constexpr float REDSHIFT_MAX = 3.0f;
    static constexpr int LUT_WIDTH = 256;  // Temperature samples
    static constexpr int LUT_HEIGHT = 128; // Redshift samples
    
    // Physical constants
    static constexpr float LightSpeed = 2.99792458e8f;
    static constexpr float BoltzmannConstant = 1.3806504e-23f;
    static constexpr float PlanckConstant = 6.62607015e-34f;
    static constexpr float Min_cY = 3931191.541677483f;
    static constexpr float Max_cY = 9.15738182436502e16f;
    static const float LogMin_cY;
    static const float LogMax_cY;
    
    // CIE color matching functions (same as shader)
    static const std::vector<float> matchingFunctionsX;
    static const std::vector<float> matchingFunctionsY;
    static const std::vector<float> matchingFunctionsZ;
    
    // Rec.2020 transform matrix
    static constexpr float rec2020[3][3] = {
        { 1.7167f, -0.6667f,  0.0176f},
        {-0.3557f,  1.6165f, -0.0428f},
        {-0.2534f,  0.0158f,  0.9421f}
    };
    
    struct RGB {
        float r, g, b;
    };
    
    // Generate the LUT data (returns RGB floats in row-major order)
    static std::vector<float> generateLUT();
    
private:
    static RGB convertToRGB(float cX, float cY, float cZ, float normalized_cY);
    static RGB getBlackbodyColor(float temperature, float redshiftFactor);
};

// Static member initialization
inline const float BlackbodyLUTGenerator::LogMin_cY = std::log(Min_cY);
inline const float BlackbodyLUTGenerator::LogMax_cY = std::log(Max_cY);

// CIE matching functions (81 samples from 380nm to 780nm, 5nm steps)
inline const std::vector<float> BlackbodyLUTGenerator::matchingFunctionsX = {
    0.0014f, 0.0022f, 0.0042f, 0.0076f, 0.0143f, 0.0232f, 0.0435f, 0.0776f, 0.1344f,
    0.2148f, 0.2839f, 0.3285f, 0.3483f, 0.3481f, 0.3362f, 0.3187f, 0.2908f, 0.2511f,
    0.1954f, 0.1421f, 0.0956f, 0.0580f, 0.0320f, 0.0147f, 0.0049f, 0.0024f, 0.0093f,
    0.0291f, 0.0633f, 0.1096f, 0.1655f, 0.2257f, 0.2904f, 0.3597f, 0.4334f, 0.5121f,
    0.5945f, 0.6784f, 0.7621f, 0.8425f, 0.9163f, 0.9786f, 1.0263f, 1.0567f, 1.0622f,
    1.0456f, 1.0026f, 0.9384f, 0.8544f, 0.7514f, 0.6424f, 0.5419f, 0.4479f, 0.3608f,
    0.2835f, 0.2187f, 0.1649f, 0.1212f, 0.0874f, 0.0636f, 0.0468f, 0.0329f, 0.0227f,
    0.0158f, 0.0114f, 0.0081f, 0.0058f, 0.0041f, 0.0029f, 0.0020f, 0.0014f, 0.0010f,
    0.0007f, 0.0005f, 0.0003f, 0.0002f, 0.0002f, 0.0001f, 0.0001f, 0.0001f, 0.0000f
};

inline const std::vector<float> BlackbodyLUTGenerator::matchingFunctionsY = {
    0.0000f, 0.0001f, 0.0001f, 0.0002f, 0.0004f, 0.0006f, 0.0012f, 0.0022f, 0.0040f,
    0.0073f, 0.0116f, 0.0168f, 0.0230f, 0.0298f, 0.0380f, 0.0480f, 0.0600f, 0.0739f,
    0.0910f, 0.1126f, 0.1390f, 0.1693f, 0.2080f, 0.2586f, 0.3230f, 0.4073f, 0.5030f,
    0.6082f, 0.7100f, 0.7932f, 0.8620f, 0.9149f, 0.9540f, 0.9803f, 0.9950f, 1.0000f,
    0.9950f, 0.9786f, 0.9520f, 0.9154f, 0.8700f, 0.8163f, 0.7570f, 0.6949f, 0.6310f,
    0.5668f, 0.5030f, 0.4412f, 0.3810f, 0.3210f, 0.2650f, 0.2170f, 0.1750f, 0.1382f,
    0.1070f, 0.0816f, 0.0610f, 0.0446f, 0.0320f, 0.0232f, 0.0170f, 0.0119f, 0.0082f,
    0.0057f, 0.0041f, 0.0029f, 0.0021f, 0.0015f, 0.0010f, 0.0007f, 0.0005f, 0.0004f,
    0.0002f, 0.0002f, 0.0001f, 0.0001f, 0.0001f, 0.0000f, 0.0000f, 0.0000f, 0.0000f
};

inline const std::vector<float> BlackbodyLUTGenerator::matchingFunctionsZ = {
    0.0065f, 0.0105f, 0.0201f, 0.0362f, 0.0679f, 0.1102f, 0.2074f, 0.3713f, 0.6456f,
    1.0391f, 1.3856f, 1.6230f, 1.7471f, 1.7826f, 1.7721f, 1.7441f, 1.6692f, 1.5281f,
    1.2876f, 1.0419f, 0.8130f, 0.6162f, 0.4652f, 0.3533f, 0.2720f, 0.2123f, 0.1582f,
    0.1117f, 0.0782f, 0.0573f, 0.0422f, 0.0298f, 0.0203f, 0.0134f, 0.0087f, 0.0057f,
    0.0039f, 0.0027f, 0.0021f, 0.0018f, 0.0017f, 0.0014f, 0.0011f, 0.0010f, 0.0008f,
    0.0006f, 0.0003f, 0.0002f, 0.0002f, 0.0001f, 0.0000f, 0.0000f, 0.0000f, 0.0000f,
    0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f,
    0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f,
    0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f
};

} // namespace MoleHole

