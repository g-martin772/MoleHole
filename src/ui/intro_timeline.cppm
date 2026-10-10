export module MoleHole:UI.IntroTimeline;

import std;

export namespace MoleHole
{
    class IntroTimeline
    {
    public:
        static constexpr float TextLetterDelay = 0.3f;
        static constexpr int TotalLetters = 8; // "MOLEHOLE"
        static constexpr float TextFadeDuration = TextLetterDelay * static_cast<float>(TotalLetters);
        static constexpr float PlanetStartDelay = TextFadeDuration + 0.3f;
        static constexpr float FadeInDuration = 2.0f;
        static constexpr float HoldDuration = 1.5f;
        static constexpr float FadeOutDuration = 1.0f;
        static constexpr float TotalDuration =
            PlanetStartDelay + FadeInDuration + HoldDuration + FadeOutDuration;
        static constexpr float LightDelay = PlanetStartDelay + 0.5f;
        static constexpr float LightDuration = 2.5f;

        void Update(float deltaTime)
        {
            if (!m_Active || m_Complete) return;

            m_Time += deltaTime;

            m_VisibleLetterCount =
                std::clamp(static_cast<int>(m_Time / TextLetterDelay), 0, TotalLetters);

            const float planetTime = m_Time - PlanetStartDelay;
            if (planetTime < 0.0f)
            {
                m_Alpha = 0.0f;
            }
            else if (planetTime < FadeInDuration)
            {
                m_Alpha = planetTime / FadeInDuration;
            }
            else if (planetTime < FadeInDuration + HoldDuration)
            {
                m_Alpha = 1.0f;
            }
            else if (planetTime < FadeInDuration + HoldDuration + FadeOutDuration)
            {
                const float fadeOutProgress = (planetTime - FadeInDuration - HoldDuration) / FadeOutDuration;
                m_Alpha = 1.0f - fadeOutProgress;
            }
            else
            {
                m_Alpha = 0.0f;
                m_Complete = true;
                m_Active = false;
            }

            if (m_Time > LightDelay)
            {
                const float lightProgress = std::clamp((m_Time - LightDelay) / LightDuration, 0.0f, 1.0f);
                m_LightIntensity = lightProgress * lightProgress * (3.0f - 2.0f * lightProgress);
            }
        }

        void Skip()
        {
            m_Complete = true;
            m_Active = false;
            m_Alpha = 0.0f;
        }

        [[nodiscard]] bool IsActive() const noexcept { return m_Active; }
        [[nodiscard]] bool IsComplete() const noexcept { return m_Complete; }
        [[nodiscard]] float Time() const noexcept { return m_Time; }
        [[nodiscard]] float Alpha() const noexcept { return m_Alpha; }
        [[nodiscard]] float LightIntensity() const noexcept { return m_LightIntensity; }
        [[nodiscard]] int VisibleLetterCount() const noexcept { return m_VisibleLetterCount; }

    private:
        bool m_Active = true;
        bool m_Complete = false;
        float m_Time = 0.0f;
        float m_Alpha = 0.0f;
        float m_LightIntensity = 0.0f;
        int m_VisibleLetterCount = 0;
    };
}
