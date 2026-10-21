export module MoleHole:Rendering.DiskPhase;

import std;
import glm;
import GPP;
import :Rendering.Metric;

export namespace MoleHole
{
    constexpr int kDiskPhaseBins = 256;
    constexpr double kDiskPhaseTurnsModulus = 1048576.0;

    // Total rotation angle theta(r) = rate * Omega(r) * t of gas on circular orbits, sampled log-spaced between the
    // disk's inner and outer radius. Computed in double; a bin keeps the whole turns (modulo 2^20), the remainder in
    // [0, 2pi) and the exact unwrapped advance to the next bin, so a float shader never sees the large angle:
    // x = whole turns, y = remainder, z = advance to the next bin, w unused.
    struct DiskPhaseTable
    {
        std::array<glm::vec4, kDiskPhaseBins> Bins{};
        double InnerEdgePhase{0.0}; // theta at the inner edge modulo 2pi
    };

    // `unit` is the metric in units of M (Mass = 1); `gravitationalTime` is G M / c^3 in seconds of the same hole,
    // so Omega [rad/s] = Omega_unit / gravitationalTime. `simTime` is coordinate time in seconds.
    [[nodiscard]] inline DiskPhaseTable BuildDiskPhaseTable(const KerrNewmanParams& unit, const double gravitationalTime,
                                                            const double innerRadius, const double outerRadius,
                                                            const double simTime, const double rate)
    {
        DiskPhaseTable table;
        if (!(gravitationalTime > 0.0) || !(innerRadius > 0.0) || !(outerRadius > innerRadius)) return table;

        constexpr double kTwoPi = 2.0 * std::numbers::pi;
        std::array<double, kDiskPhaseBins> theta{};
        for (int k = 0; k < kDiskPhaseBins; ++k)
        {
            const double r = innerRadius * std::pow(outerRadius / innerRadius, static_cast<double>(k) / (kDiskPhaseBins - 1));
            const double omega = ProgradeOrbitalFrequency(unit, r) / gravitationalTime;
            theta[static_cast<std::size_t>(k)] = std::isfinite(omega) ? rate * omega * simTime : 0.0;
        }

        for (int k = 0; k < kDiskPhaseBins; ++k)
        {
            const auto index = static_cast<std::size_t>(k);
            double remainder = std::fmod(theta[index], kTwoPi);
            if (remainder < 0.0) remainder += kTwoPi;
            const double turns = std::round((theta[index] - remainder) / kTwoPi);
            const double advance = k + 1 < kDiskPhaseBins ? theta[index + 1] - theta[index] : 0.0;
            table.Bins[index] = glm::vec4(static_cast<float>(std::fmod(turns, kDiskPhaseTurnsModulus)),
                                          static_cast<float>(remainder), static_cast<float>(advance), 0.0f);
        }
        table.InnerEdgePhase = std::fmod(theta[0], kTwoPi);
        if (table.InnerEdgePhase < 0.0) table.InnerEdgePhase += kTwoPi;
        return table;
    }

    // Phase seen by the shader at fraction f of bin i (what disk.glsl evaluates), for tests and the CPU reference.
    [[nodiscard]] inline double DiskPhaseAt(const DiskPhaseTable& table, const int bin, const double f)
    {
        const auto& b = table.Bins[static_cast<std::size_t>(bin)];
        return (static_cast<double>(b.x) * 2.0 * std::numbers::pi) + static_cast<double>(b.y) + f * static_cast<double>(b.z);
    }
}
