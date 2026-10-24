export module MoleHole:Rendering.Accumulation;

import std;
import glm;
import GPP;
import :Rendering.Settings;

export namespace MoleHole
{
    [[nodiscard]] constexpr double Halton(std::uint32_t index, const std::uint32_t base) noexcept
    {
        double scale = 1.0;
        double value = 0.0;
        while (index > 0)
        {
            scale /= static_cast<double>(base);
            value += scale * static_cast<double>(index % base);
            index /= base;
        }
        return value;
    }

    [[nodiscard]] constexpr glm::vec2 AccumulationJitter(const std::uint32_t index) noexcept
    {
        if (index == 0) return {0.0f, 0.0f};
        return {static_cast<float>(Halton(index, 2) - 0.5), static_cast<float>(Halton(index, 3) - 0.5)};
    }

    [[nodiscard]] constexpr float AccumulationWeight(const std::uint32_t samplesBefore) noexcept
    {
        return 1.0f / static_cast<float>(samplesBefore + 1);
    }

    class SignatureHasher
    {
    public:
        void Reset() noexcept { m_State = kSeed; }

        void Bytes(const void* data, std::size_t size) noexcept
        {
            const auto total = size;
            const auto* bytes = static_cast<const unsigned char*>(data);
            while (size >= 8)
            {
                std::uint64_t word;
                std::memcpy(&word, bytes, 8);
                Mix(word);
                bytes += 8;
                size -= 8;
            }
            if (size > 0)
            {
                std::uint64_t word = 0;
                std::memcpy(&word, bytes, size);
                Mix(word);
            }
            Mix(total);
        }

        template <typename T>
            requires std::is_trivially_copyable_v<T>
        void Value(const T& value) noexcept
        {
            Bytes(&value, sizeof(T));
        }

        template <typename T>
            requires std::is_trivially_copyable_v<T>
        void Span(const std::span<const T> values) noexcept
        {
            Bytes(values.data(), values.size_bytes());
        }

        void Pointer(const void* pointer) noexcept { Mix(std::bit_cast<std::uintptr_t>(pointer)); }
        void String(const std::string_view text) noexcept { Bytes(text.data(), text.size()); }

        [[nodiscard]] std::uint64_t Digest() const noexcept { return m_State; }

    private:
        static constexpr std::uint64_t kSeed = 0xCBF29CE484222325ull;

        void Mix(const std::uint64_t word) noexcept
        {
            m_State = (m_State ^ word) * 0x9E3779B97F4A7C15ull;
            m_State ^= m_State >> 29;
        }

        std::uint64_t m_State = kSeed;
    };

    inline void HashSettings(SignatureHasher& hasher, const RenderSettings& settings)
    {
        for (const auto& setting : RenderSettingRegistry().All())
        {
            std::visit([&](const auto& field)
            {
                using V = std::remove_cvref_t<decltype(field)>;
                if constexpr (std::is_same_v<V, std::string>) hasher.String(field);
                else hasher.Value(field);
            }, setting.Get(settings));
        }
    }

    struct RefinementStatus
    {
        std::uint32_t Samples = 0;
        std::uint32_t MaxSamples = 0;
        bool Idle = false;
    };

    struct AccumulationRequest
    {
        std::uint64_t Signature = 0;
        bool Accumulate = false;
        std::uint32_t MaxSamples = 1;
        bool Settled = true;
    };

    struct AccumulationPlan
    {
        bool Render = true;
        bool Accumulate = false;
        glm::vec2 Jitter{0.0f};
        float Weight = 1.0f;
        std::uint32_t Samples = 0;
        bool Converged = false;
    };

    class AccumulationPlanner
    {
    public:
        [[nodiscard]] AccumulationPlan Next(const AccumulationRequest& request) noexcept
        {
            const bool changed = !m_Valid || request.Signature != m_Signature;
            m_Valid = true;
            m_Signature = request.Signature;
            const std::uint32_t maxSamples = std::max<std::uint32_t>(request.MaxSamples, 1);

            if (changed || !request.Settled || !m_DrawnSettled)
            {
                m_Samples = 0;
                m_DrawnSettled = request.Settled;
                return {.Render = true, .Samples = 0};
            }
            if (!request.Accumulate) return {.Render = false};
            if (m_Samples >= maxSamples) return {.Render = false, .Samples = m_Samples, .Converged = true};

            AccumulationPlan plan;
            plan.Accumulate = true;
            plan.Weight = AccumulationWeight(m_Samples);
            plan.Jitter = AccumulationJitter(m_Samples + 1);
            plan.Samples = ++m_Samples;
            plan.Converged = m_Samples >= maxSamples;
            return plan;
        }

        void Reset() noexcept
        {
            m_Valid = false;
            m_Samples = 0;
            m_DrawnSettled = false;
        }

        [[nodiscard]] std::uint32_t Samples() const noexcept { return m_Samples; }

    private:
        bool m_Valid = false;
        std::uint64_t m_Signature = 0;
        std::uint32_t m_Samples = 0;
        bool m_DrawnSettled = false;
    };

    struct LadderRequest
    {
        bool Enabled = false;
        float InteractiveScale = 1.0f;
        double StillMs = 0.0;
        std::uint64_t FramesDone = 0;
        double DebounceMs = 200.0;
    };

    struct LadderStep
    {
        float Scale = 1.0f;
        bool Accumulate = true;
        int Rung = 0;
        bool Climbing = false;
    };

    // Resolutions a still view climbs through, as multipliers of the profile's resolution scale, ending at 1
    [[nodiscard]] inline std::vector<float> LadderRungs(const float interactiveScale)
    {
        std::vector<float> rungs{std::clamp(interactiveScale, 0.05f, 1.0f)};
        for (const float rung : {0.5f, 0.75f})
        {
            if (rung > rungs.front() + 0.02f) rungs.push_back(rung);
        }
        if (rungs.front() < 0.999f) rungs.push_back(1.0f);
        return rungs;
    }

    // While the view moves it stays at the interactive (governor) scale. Once it has been still for the debounce
    // time it renders one frame per rung, each after the previous one finished, and accumulates on the last.
    class RefinementLadder
    {
    public:
        [[nodiscard]] LadderStep Next(const LadderRequest& request)
        {
            const auto rungs = LadderRungs(request.InteractiveScale);
            if (!request.Enabled || rungs.size() == 1)
            {
                Reset();
                return {.Scale = rungs.front(), .Accumulate = true};
            }
            const int last = static_cast<int>(rungs.size()) - 1;
            if (request.StillMs < request.DebounceMs)
            {
                Reset();
                return {.Scale = rungs.front(), .Accumulate = false};
            }
            if (m_Rung == 0 || (request.FramesDone > m_Mark && m_Rung < last))
            {
                m_Rung = std::min(m_Rung + 1, last);
                m_Mark = request.FramesDone;
            }
            m_Rung = std::min(m_Rung, last);
            return {.Scale = rungs[static_cast<std::size_t>(m_Rung)], .Accumulate = m_Rung == last, .Rung = m_Rung,
                    .Climbing = m_Rung < last};
        }

        void Reset() noexcept
        {
            m_Rung = 0;
            m_Mark = 0;
        }

    private:
        int m_Rung = 0;
        std::uint64_t m_Mark = 0;
    };
}
