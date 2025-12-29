#pragma once
#include <vector>
#include <glm/glm.hpp>

namespace MoleHole {

/**
 * @brief Generates lookup tables for Kerr metric geodesic calculations
 *
 * Pre-computes photon geodesics in Kerr spacetime to accelerate real-time rendering.
 * Based on the work of Kip Thorne et al. for the Interstellar movie.
 *
 * The Kerr metric describes rotating black holes and includes:
 * - Frame dragging (Lense-Thirring effect)
 * - Ergosphere
 * - Spin-dependent ISCO
 * - Complex photon orbits
 */
class KerrGeodesicLUTGenerator {
public:
    // LUT dimensions
    static constexpr int LUT_IMPACT_PARAM_SAMPLES = 256;  // Impact parameter b
    static constexpr int LUT_SPIN_SAMPLES = 64;            // Spin parameter a (0 to 1)
    static constexpr int LUT_INCLINATION_SAMPLES = 128;    // Observer inclination angle

    // Physical ranges
    static constexpr float SPIN_MIN = 0.0f;     // Schwarzschild
    static constexpr float SPIN_MAX = 0.998f;   // Near-extremal Kerr
    static constexpr float IMPACT_MIN = 0.0f;   // Direct hit
    static constexpr float IMPACT_MAX = 20.0f;  // Far miss (in units of r_s)
    static constexpr float INCLINATION_MIN = 0.0f;      // Equatorial
    static constexpr float INCLINATION_MAX = 3.14159f;  // Polar (π radians)

    struct GeodesicResult {
        float deflectionAngle;      // Total deflection angle (radians)
        float redshiftFactor;       // Gravitational + Doppler redshift
        float properTime;           // Proper time along geodesic
        bool capturedByHorizon;     // Whether photon was captured
        int orbitCount;             // Number of orbits before escaping/capture
        float closestApproach;      // Minimum radius achieved
    };

    /**
     * @brief Generate 3D LUT for geodesic deflection
     * @return Flat array: [spin][inclination][impactParam] -> deflectionAngle
     */
    static std::vector<float> generateDeflectionLUT();

    /**
     * @brief Generate 3D LUT for redshift factors
     * @return Flat array: [spin][inclination][impactParam] -> redshiftFactor
     */
    static std::vector<float> generateRedshiftLUT();

    /**
     * @brief Generate 2D LUT for photon sphere radius
     * @return Flat array: [spin][inclination] -> photonSphereRadius
     */
    static std::vector<float> generatePhotonSphereLUT();

    /**
     * @brief Generate 1D LUT for ISCO radius as function of spin
     * @return Array: [spin] -> iscoRadius
     */
    static std::vector<float> generateISCOLUT();

private:
    /**
     * @brief Integrate photon geodesic in Kerr spacetime using RK4
     * @param spin Black hole spin parameter (0 to 0.998)
     * @param impactParameter Impact parameter in units of r_s
     * @param inclination Observer inclination angle (radians)
     * @param spinAxis Spin axis direction (normalized)
     * @return Geodesic integration result
     */
    static GeodesicResult integrateGeodesic(float spin, float impactParameter,
                                           float inclination, const glm::vec3& spinAxis);

    /**
     * @brief Calculate Kerr metric coefficients at given position
     * @param r Boyer-Lindquist radial coordinate
     * @param theta Boyer-Lindquist polar angle
     * @param spin Spin parameter
     * @param metric Output 4x4 metric tensor components (needed subset)
     */
    static void calculateKerrMetric(float r, float theta, float spin, float metric[10]);

    /**
     * @brief Calculate conserved quantities (energy, angular momentum)
     * @param r Radial coordinate
     * @param theta Polar angle
     * @param spin Spin parameter
     * @param impactParameter Impact parameter
     * @param energy Output: conserved energy
     * @param angularMomentum Output: conserved angular momentum
     */
    static void calculateConservedQuantities(float r, float theta, float spin,
                                            float impactParameter,
                                            float& energy, float& angularMomentum);

    /**
     * @brief Calculate photon sphere radius for given spin
     * @param spin Spin parameter (0 to 0.998)
     * @param inclination Orbital inclination
     * @return Photon sphere radius in units of r_s
     */
    static float calculatePhotonSphereRadius(float spin, float inclination);

    /**
     * @brief Calculate ISCO radius for given spin (Page-Thorne formula)
     * @param spin Spin parameter (0 to 0.998)
     * @return ISCO radius in units of r_s
     */
    static float calculateISCORadius(float spin);

    /**
     * @brief RK4 integrator for geodesic equations
     */
    static void rk4Step(float* state, int stateSize, float spin, float dt,
                       void (*derivatives)(float*, float*, float, float));

    /**
     * @brief Geodesic equations in Boyer-Lindquist coordinates
     */
    static void geodesicDerivatives(float* state, float* derivatives, float spin, float lambda);
};

} // namespace MoleHole

