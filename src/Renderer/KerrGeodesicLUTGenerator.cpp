#include "KerrGeodesicLUTGenerator.h"
#include <cmath>
#include <algorithm>
#include <spdlog/spdlog.h>

namespace MoleHole {

static inline float sqr(float x) { return x * x; }

std::vector<float> KerrGeodesicLUTGenerator::generateDeflectionLUT() {
    spdlog::info("Generating Kerr geodesic deflection LUT ({}x{}x{} samples)...",
                 LUT_SPIN_SAMPLES, LUT_INCLINATION_SAMPLES, LUT_IMPACT_PARAM_SAMPLES);

    std::vector<float> lutData;
    lutData.reserve(LUT_SPIN_SAMPLES * LUT_INCLINATION_SAMPLES * LUT_IMPACT_PARAM_SAMPLES);

    int totalSamples = LUT_SPIN_SAMPLES * LUT_INCLINATION_SAMPLES * LUT_IMPACT_PARAM_SAMPLES;
    int processedSamples = 0;
    int lastPercent = 0;

    for (int spinIdx = 0; spinIdx < LUT_SPIN_SAMPLES; spinIdx++) {
        float t_spin = static_cast<float>(spinIdx) / static_cast<float>(LUT_SPIN_SAMPLES - 1);
        float spin = SPIN_MIN + t_spin * (SPIN_MAX - SPIN_MIN);

        for (int inclIdx = 0; inclIdx < LUT_INCLINATION_SAMPLES; inclIdx++) {
            float t_incl = static_cast<float>(inclIdx) / static_cast<float>(LUT_INCLINATION_SAMPLES - 1);
            float inclination = INCLINATION_MIN + t_incl * (INCLINATION_MAX - INCLINATION_MIN);

            for (int impactIdx = 0; impactIdx < LUT_IMPACT_PARAM_SAMPLES; impactIdx++) {
                float t_impact = static_cast<float>(impactIdx) / static_cast<float>(LUT_IMPACT_PARAM_SAMPLES - 1);
                float impactParam = IMPACT_MIN + t_impact * (IMPACT_MAX - IMPACT_MIN);

                glm::vec3 spinAxis(0.0f, 1.0f, 0.0f);
                GeodesicResult result = integrateGeodesic(spin, impactParam, inclination, spinAxis);

                lutData.push_back(result.deflectionAngle);

                processedSamples++;
                int percent = (processedSamples * 100) / totalSamples;
                if (percent > lastPercent && percent % 10 == 0) {
                    spdlog::info("  Deflection LUT generation: {}%", percent);
                    lastPercent = percent;
                }
            }
        }
    }

    spdlog::info("Kerr geodesic deflection LUT generated successfully");
    return lutData;
}

std::vector<float> KerrGeodesicLUTGenerator::generateRedshiftLUT() {
    spdlog::info("Generating Kerr redshift LUT ({}x{}x{} samples)...",
                 LUT_SPIN_SAMPLES, LUT_INCLINATION_SAMPLES, LUT_IMPACT_PARAM_SAMPLES);

    std::vector<float> lutData;
    lutData.reserve(LUT_SPIN_SAMPLES * LUT_INCLINATION_SAMPLES * LUT_IMPACT_PARAM_SAMPLES);

    for (int spinIdx = 0; spinIdx < LUT_SPIN_SAMPLES; spinIdx++) {
        float t_spin = static_cast<float>(spinIdx) / static_cast<float>(LUT_SPIN_SAMPLES - 1);
        float spin = SPIN_MIN + t_spin * (SPIN_MAX - SPIN_MIN);

        for (int inclIdx = 0; inclIdx < LUT_INCLINATION_SAMPLES; inclIdx++) {
            float t_incl = static_cast<float>(inclIdx) / static_cast<float>(LUT_INCLINATION_SAMPLES - 1);
            float inclination = INCLINATION_MIN + t_incl * (INCLINATION_MAX - INCLINATION_MIN);

            for (int impactIdx = 0; impactIdx < LUT_IMPACT_PARAM_SAMPLES; impactIdx++) {
                float t_impact = static_cast<float>(impactIdx) / static_cast<float>(LUT_IMPACT_PARAM_SAMPLES - 1);
                float impactParam = IMPACT_MIN + t_impact * (IMPACT_MAX - IMPACT_MIN);

                glm::vec3 spinAxis(0.0f, 1.0f, 0.0f);
                GeodesicResult result = integrateGeodesic(spin, impactParam, inclination, spinAxis);

                lutData.push_back(result.redshiftFactor);
            }
        }
    }

    spdlog::info("Kerr redshift LUT generated successfully");
    return lutData;
}

std::vector<float> KerrGeodesicLUTGenerator::generatePhotonSphereLUT() {
    spdlog::info("Generating photon sphere LUT ({}x{} samples)...",
                 LUT_SPIN_SAMPLES, LUT_INCLINATION_SAMPLES);

    std::vector<float> lutData;
    lutData.reserve(LUT_SPIN_SAMPLES * LUT_INCLINATION_SAMPLES);

    for (int spinIdx = 0; spinIdx < LUT_SPIN_SAMPLES; spinIdx++) {
        float t_spin = static_cast<float>(spinIdx) / static_cast<float>(LUT_SPIN_SAMPLES - 1);
        float spin = SPIN_MIN + t_spin * (SPIN_MAX - SPIN_MIN);

        for (int inclIdx = 0; inclIdx < LUT_INCLINATION_SAMPLES; inclIdx++) {
            float t_incl = static_cast<float>(inclIdx) / static_cast<float>(LUT_INCLINATION_SAMPLES - 1);
            float inclination = INCLINATION_MIN + t_incl * (INCLINATION_MAX - INCLINATION_MIN);

            float photonRadius = calculatePhotonSphereRadius(spin, inclination);
            lutData.push_back(photonRadius);
        }
    }

    spdlog::info("Photon sphere LUT generated successfully");
    return lutData;
}

std::vector<float> KerrGeodesicLUTGenerator::generateISCOLUT() {
    spdlog::info("Generating ISCO LUT ({} samples)...", LUT_SPIN_SAMPLES);

    std::vector<float> lutData;
    lutData.reserve(LUT_SPIN_SAMPLES);

    for (int spinIdx = 0; spinIdx < LUT_SPIN_SAMPLES; spinIdx++) {
        float t_spin = static_cast<float>(spinIdx) / static_cast<float>(LUT_SPIN_SAMPLES - 1);
        float spin = SPIN_MIN + t_spin * (SPIN_MAX - SPIN_MIN);

        float iscoRadius = calculateISCORadius(spin);
        lutData.push_back(iscoRadius);
    }

    spdlog::info("ISCO LUT generated successfully");
    return lutData;
}

KerrGeodesicLUTGenerator::GeodesicResult KerrGeodesicLUTGenerator::integrateGeodesic(
    float spin, float impactParameter, float inclination, const glm::vec3& spinAxis) {

    GeodesicResult result;
    result.deflectionAngle = 0.0f;
    result.redshiftFactor = 1.0f;
    result.properTime = 0.0f;
    result.capturedByHorizon = false;
    result.orbitCount = 0;
    result.closestApproach = 1000.0f;

    // For very large impact parameters, use weak-field approximation
    if (impactParameter > 10.0f) {
        result.deflectionAngle = 4.0f / impactParameter;
        result.redshiftFactor = 1.0f - 1.0f / impactParameter;
        result.closestApproach = impactParameter;
        return result;
    }

    const float r_start = 50.0f;
    const float r_horizon = 1.0f + std::sqrt(1.0f - spin * spin);

    float state[8];
    state[0] = 0.0f;
    state[1] = r_start;
    state[2] = inclination;
    state[3] = 0.0f;

    float energy, angularMomentum;
    calculateConservedQuantities(r_start, inclination, spin, impactParameter, energy, angularMomentum);

    state[4] = energy;
    state[5] = -std::sqrt(std::abs(energy * energy - (1.0f - 2.0f / r_start)));
    state[6] = 0.0f;
    state[7] = angularMomentum;

    const float dlambda = 0.05f;
    const int maxSteps = 10000;
    const float minRadius = r_horizon * 1.01f;
    const float maxRadius = 100.0f;

    float lastPhi = state[3];

    for (int steps = 0; steps < maxSteps; steps++) {
        float r = state[1];
        float theta = state[2];
        float phi = state[3];

        result.closestApproach = std::min(result.closestApproach, r);

        if (r < minRadius) {
            result.capturedByHorizon = true;
            break;
        }

        if (r > maxRadius && state[5] > 0.0f) {
            break;
        }

        float deltaPhi = phi - lastPhi;
        if (std::abs(deltaPhi) > 6.0f) {
            result.orbitCount++;
        }
        lastPhi = phi;

        float rSqr = r * r;
        float a = spin;
        float aSqr = a * a;

        float dr_dlambda = state[5];
        float V_eff_r = -(r - 1.0f) / (rSqr * rSqr) * (energy * energy - 1.0f)
                        + angularMomentum * angularMomentum / (r * r * r);
        float dpr_dlambda = V_eff_r;
        float dphi_dlambda = angularMomentum / rSqr;

        state[1] += dr_dlambda * dlambda;
        state[3] += dphi_dlambda * dlambda;
        state[5] += dpr_dlambda * dlambda;

        result.properTime += dlambda;
    }

    result.deflectionAngle = std::abs(state[3]);

    if (!result.capturedByHorizon && result.closestApproach > minRadius) {
        float r_close = result.closestApproach;
        result.redshiftFactor = std::sqrt(std::max(0.01f, 1.0f - 2.0f / r_close));
    } else {
        result.redshiftFactor = 0.0f;
    }

    return result;
}

void KerrGeodesicLUTGenerator::calculateKerrMetric(float r, float theta, float spin, float metric[10]) {
    float a = spin;
    float a2 = a * a;
    float r2 = r * r;
    float cos_theta = std::cos(theta);
    float sin_theta = std::sin(theta);
    float cos2_theta = cos_theta * cos_theta;
    float sin2_theta = sin_theta * sin_theta;

    float Sigma = r2 + a2 * cos2_theta;
    float Delta = r2 - 2.0f * r + a2;
    float A = (r2 + a2) * (r2 + a2) - a2 * Delta * sin2_theta;

    metric[0] = -(1.0f - 2.0f * r / Sigma);
    metric[1] = Sigma / Delta;
    metric[2] = Sigma;
    metric[3] = A * sin2_theta / Sigma;
    metric[4] = -2.0f * a * r * sin2_theta / Sigma;
}

void KerrGeodesicLUTGenerator::calculateConservedQuantities(
    float r, float theta, float spin, float impactParameter,
    float& energy, float& angularMomentum) {

    energy = 1.0f;
    angularMomentum = impactParameter * energy;
}

float KerrGeodesicLUTGenerator::calculatePhotonSphereRadius(float spin, float inclination) {
    float a = spin;
    bool prograde = (inclination < 0.5f || inclination > 2.64f);

    if (prograde) {
        float term = std::acos(-a) * 2.0f / 3.0f;
        return 2.0f * (1.0f + std::cos(term));
    } else {
        float term = std::acos(a) * 2.0f / 3.0f;
        return 2.0f * (1.0f + std::cos(term));
    }
}

float KerrGeodesicLUTGenerator::calculateISCORadius(float spin) {
    float a = spin;
    float Z1 = 1.0f + std::pow(1.0f - a * a, 1.0f/3.0f) *
               (std::pow(1.0f + a, 1.0f/3.0f) + std::pow(1.0f - a, 1.0f/3.0f));
    float Z2 = std::sqrt(3.0f * a * a + Z1 * Z1);
    float r_isco_M = 3.0f + Z2 - std::sqrt((3.0f - Z1) * (3.0f + Z1 + 2.0f * Z2));
    return r_isco_M;
}

void KerrGeodesicLUTGenerator::rk4Step(float* state, int stateSize, float spin, float dt,
                                       void (*derivatives)(float*, float*, float, float)) {
    float* k1 = new float[stateSize];
    float* k2 = new float[stateSize];
    float* k3 = new float[stateSize];
    float* k4 = new float[stateSize];
    float* temp = new float[stateSize];

    derivatives(state, k1, spin, 0.0f);
    for (int i = 0; i < stateSize; i++) temp[i] = state[i] + 0.5f * dt * k1[i];

    derivatives(temp, k2, spin, 0.5f * dt);
    for (int i = 0; i < stateSize; i++) temp[i] = state[i] + 0.5f * dt * k2[i];

    derivatives(temp, k3, spin, 0.5f * dt);
    for (int i = 0; i < stateSize; i++) temp[i] = state[i] + dt * k3[i];

    derivatives(temp, k4, spin, dt);
    for (int i = 0; i < stateSize; i++) {
        state[i] += (dt / 6.0f) * (k1[i] + 2.0f * k2[i] + 2.0f * k3[i] + k4[i]);
    }

    delete[] k1;
    delete[] k2;
    delete[] k3;
    delete[] k4;
    delete[] temp;
}

void KerrGeodesicLUTGenerator::geodesicDerivatives(float* state, float* derivatives,
                                                   float spin, float lambda) {
    float t = state[0];
    float r = state[1];
    float theta = state[2];
    float phi = state[3];
    float p_t = state[4];
    float p_r = state[5];
    float p_theta = state[6];
    float p_phi = state[7];

    float a = spin;
    float a2 = a * a;
    float r2 = r * r;
    float cos_theta = std::cos(theta);
    float sin_theta = std::sin(theta);
    float cos2_theta = cos_theta * cos_theta;
    float sin2_theta = sin_theta * sin_theta;

    float Sigma = r2 + a2 * cos2_theta;
    float Delta = r2 - 2.0f * r + a2;
    float A_func = (r2 + a2) * (r2 + a2) - a2 * Delta * sin2_theta;

    derivatives[0] = -a * (a * sin2_theta - p_phi) / Delta + A_func / (Sigma * Delta) * p_t;
    derivatives[1] = Delta / Sigma * p_r;
    derivatives[2] = 1.0f / Sigma * p_theta;
    derivatives[3] = -a * (1.0f - A_func / (Sigma * Delta)) * p_t + (r2 + a2) / (Sigma * Delta * sin2_theta) * p_phi;

    float dSigma_dr = 2.0f * r;
    float dSigma_dtheta = -2.0f * a2 * cos_theta * sin_theta;
    float dDelta_dr = 2.0f * r - 2.0f;

    derivatives[4] = 0.0f;
    derivatives[5] = -(dDelta_dr / (2.0f * Sigma) - Delta * dSigma_dr / (2.0f * Sigma * Sigma)) * p_r * p_r
                     -(dSigma_dr / (2.0f * Sigma * Sigma)) * p_theta * p_theta
                     +(r - 1.0f) / (Sigma * Sigma) * p_t * p_t;
    derivatives[6] = -a2 * cos_theta * sin_theta / Sigma * p_r * p_r
                     +a2 * sin_theta * cos_theta / (Sigma * Sigma) * p_theta * p_theta;
    derivatives[7] = 0.0f;
}

} // namespace MoleHole

