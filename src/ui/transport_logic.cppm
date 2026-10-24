export module MoleHole:UI.TransportLogic;

import std;
import GPP;
import :UI.StatusBar;

export namespace MoleHole
{
    inline constexpr std::array<double, 4> kSpeedPresets{0.1, 1.0, 10.0, 100.0};
    inline constexpr std::uint64_t kMaxStepTicks = 10000;
    inline constexpr std::uint64_t kDefaultStepTicks = 10;
    inline constexpr std::size_t kMaxBookmarks = 8;

    [[nodiscard]] inline std::optional<std::size_t> MatchSpeedPreset(const double timeScale)
    {
        for (std::size_t i = 0; i < kSpeedPresets.size(); ++i)
        {
            if (std::abs(timeScale - kSpeedPresets[i]) <= kSpeedPresets[i] * 1e-9) return i;
        }
        return std::nullopt;
    }

    [[nodiscard]] inline double NextSpeedPreset(const double timeScale, const int direction)
    {
        if (!std::isfinite(timeScale) || direction == 0) return timeScale;
        if (const auto match = MatchSpeedPreset(timeScale))
        {
            const auto index = static_cast<std::ptrdiff_t>(*match) + (direction > 0 ? 1 : -1);
            return kSpeedPresets[static_cast<std::size_t>(std::clamp<std::ptrdiff_t>(index, 0, kSpeedPresets.size() - 1))];
        }
        if (direction > 0)
        {
            for (const double preset : kSpeedPresets) if (preset > timeScale) return preset;
            return kSpeedPresets.back();
        }
        for (const double preset : kSpeedPresets | std::views::reverse) if (preset < timeScale) return preset;
        return kSpeedPresets.front();
    }

    [[nodiscard]] inline std::string FormatSpeed(const double timeScale)
    {
        if (!std::isfinite(timeScale)) return "x?";
        const double magnitude = std::abs(timeScale);
        if (magnitude != 0.0 && (magnitude >= 1e6 || magnitude < 1e-3)) return std::format("x{:.3g}", timeScale);
        return std::format("x{}", timeScale);
    }

    [[nodiscard]] inline std::optional<double> ParseJumpTime(const std::string_view text)
    {
        const auto base = GPP::Units::BaseUnit(GPP::Units::Dimension::Time);
        const auto parsed = GPP::Units::ParseQuantity(text, GPP::Units::Dimension::Time, base);
        if (!parsed || !std::isfinite(parsed->ValueSi)) return std::nullopt;
        return parsed->ValueSi;
    }

    [[nodiscard]] inline std::uint64_t ClampStepCount(const long long requested)
    {
        return static_cast<std::uint64_t>(std::clamp<long long>(requested, 1, static_cast<long long>(kMaxStepTicks)));
    }

    [[nodiscard]] inline std::uint64_t StepTicks(const bool multi, const long long configured)
    {
        return multi ? ClampStepCount(configured) : 1;
    }

    [[nodiscard]] inline std::size_t PickTransportTier(const float available, const std::span<const float> widths)
    {
        for (std::size_t i = 0; i < widths.size(); ++i)
        {
            if (widths[i] <= available) return i;
        }
        return widths.empty() ? 0 : widths.size() - 1;
    }

    [[nodiscard]] inline std::string BookmarkLabel(const double simTime) { return FormatSimTime(simTime); }

    template <typename T>
    class BookmarkRing
    {
    public:
        explicit BookmarkRing(const std::size_t capacity = kMaxBookmarks) : m_Capacity(std::max<std::size_t>(capacity, 1)) {}

        void Push(T item)
        {
            if (m_Items.size() == m_Capacity) m_Items.pop_front();
            m_Items.push_back(std::move(item));
        }

        [[nodiscard]] const T* At(const std::size_t index) const
        {
            return index < m_Items.size() ? &m_Items[index] : nullptr;
        }

        void Clear() { m_Items.clear(); }
        [[nodiscard]] std::size_t Size() const noexcept { return m_Items.size(); }
        [[nodiscard]] std::size_t Capacity() const noexcept { return m_Capacity; }
        [[nodiscard]] const std::deque<T>& Items() const noexcept { return m_Items; }

    private:
        std::size_t m_Capacity;
        std::deque<T> m_Items;
    };

    struct BookmarkInfo
    {
        std::string Name;
        double SimTime = 0.0;
        std::uint64_t Tick = 0;
    };

    class BookmarkView
    {
    public:
        void Set(std::vector<BookmarkInfo> items)
        {
            std::scoped_lock lock(m_Mutex);
            m_Items = std::move(items);
        }

        [[nodiscard]] std::vector<BookmarkInfo> Get() const
        {
            std::scoped_lock lock(m_Mutex);
            return m_Items;
        }

    private:
        mutable std::mutex m_Mutex;
        std::vector<BookmarkInfo> m_Items;
    };

    struct TransportRequest
    {
        enum class Kind { Step, Snapshot, Restore } What = Kind::Step;
        std::uint64_t Ticks = 1;
        std::size_t Index = 0;
    };

    class TransportRequests
    {
    public:
        void Push(const TransportRequest request)
        {
            std::scoped_lock lock(m_Mutex);
            m_Pending.push_back(request);
        }

        [[nodiscard]] std::vector<TransportRequest> Take()
        {
            std::scoped_lock lock(m_Mutex);
            return std::exchange(m_Pending, {});
        }

    private:
        std::mutex m_Mutex;
        std::vector<TransportRequest> m_Pending;
    };
}
