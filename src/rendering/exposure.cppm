export module MoleHole:Rendering.Exposure;

import std;

export namespace MoleHole
{
    struct ExposureStep
    {
        float Alpha = 1.0f; // weight of the freshly metered value; 1 restarts from the meter
        bool Commit = false; // the previous render becomes the state the blend starts from
    };

    // Smooths the metered exposure over `time` (film seconds in exports, wall seconds in the viewport). A render at a time
    // already seen blends from the same committed state again, so repeated renders of one film frame give one result.
    class ExposureAdaptation
    {
    public:
        void Reset() noexcept { m_Valid = false; }

        [[nodiscard]] ExposureStep Next(const double time, const double adaptation, const std::uint64_t context) noexcept
        {
            if (!m_Valid || context != m_Context || !(adaptation > 0.0) || time < m_Time)
            {
                m_Valid = true;
                m_Context = context;
                m_Time = time;
                m_Restarted = true;
                return {};
            }
            if (time == m_Time) return m_Restarted ? ExposureStep{} : ExposureStep{Weight(time - m_Previous, adaptation), false};
            m_Previous = m_Time;
            m_Time = time;
            m_Restarted = false;
            return {Weight(time - m_Previous, adaptation), true};
        }

        [[nodiscard]] static float Weight(const double dt, const double adaptation) noexcept
        {
            if (!(adaptation > 0.0)) return 1.0f;
            return static_cast<float>(1.0 - std::exp(-std::max(dt, 0.0) / adaptation));
        }

    private:
        bool m_Valid = false;
        bool m_Restarted = true;
        std::uint64_t m_Context = 0;
        double m_Time = 0.0;
        double m_Previous = 0.0;
    };
}
