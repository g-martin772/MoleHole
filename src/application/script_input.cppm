module;
#include <yaml-cpp/yaml.h>
export module MoleHole:Application.ScriptInput;

import std;
import glm;
import GPP;

export namespace MoleHole
{
    constexpr const char* kSceneInputExtension = "Input";

    struct InputKeyName
    {
        GPP::KeyCode Key;
        std::string_view Name;
    };

    [[nodiscard]] inline const std::vector<InputKeyName>& InputKeyTable()
    {
        using GPP::KeyCode;
        static const std::vector<InputKeyName> table = []
        {
            std::vector<InputKeyName> keys;
            static constexpr std::string_view letters = "abcdefghijklmnopqrstuvwxyz";
            static constexpr std::string_view digits = "0123456789";
            for (std::size_t i = 0; i < letters.size(); ++i) keys.push_back({static_cast<KeyCode>(0x61 + i), letters.substr(i, 1)});
            for (std::size_t i = 0; i < digits.size(); ++i) keys.push_back({static_cast<KeyCode>(0x30 + i), digits.substr(i, 1)});
            keys.insert(keys.end(), {
                {KeyCode::Space, "space"}, {KeyCode::Return, "enter"}, {KeyCode::Tab, "tab"}, {KeyCode::Backspace, "backspace"},
                {KeyCode::Up, "up"}, {KeyCode::Down, "down"}, {KeyCode::Left, "left"}, {KeyCode::Right, "right"},
                {KeyCode::LeftShift, "shift"}, {KeyCode::RightShift, "shift"},
                {KeyCode::LeftControl, "ctrl"}, {KeyCode::RightControl, "ctrl"},
                {KeyCode::LeftAlt, "alt"}, {KeyCode::RightAlt, "alt"},
                {KeyCode::F1, "f1"}, {KeyCode::F2, "f2"}, {KeyCode::F3, "f3"}, {KeyCode::F4, "f4"}, {KeyCode::F5, "f5"},
                {KeyCode::F6, "f6"}, {KeyCode::F7, "f7"}, {KeyCode::F8, "f8"}, {KeyCode::F9, "f9"}, {KeyCode::F10, "f10"},
                {KeyCode::F11, "f11"}, {KeyCode::F12, "f12"},
                {KeyCode::Comma, "comma"}, {KeyCode::Period, "period"}, {KeyCode::Slash, "slash"},
                {KeyCode::Semicolon, "semicolon"}, {KeyCode::Apostrophe, "apostrophe"},
                {KeyCode::LeftBracket, "leftbracket"}, {KeyCode::RightBracket, "rightbracket"},
                {KeyCode::Minus, "minus"}, {KeyCode::Equals, "equals"}, {KeyCode::Grave, "grave"},
                {KeyCode::PageUp, "pageup"}, {KeyCode::PageDown, "pagedown"}, {KeyCode::Home, "home"}, {KeyCode::End, "end"},
                {KeyCode::Insert, "insert"}, {KeyCode::Delete, "delete"},
            });
            return keys;
        }();
        return table;
    }

    [[nodiscard]] inline std::optional<std::string> KeySourceName(const GPP::KeyCode key)
    {
        const auto& table = InputKeyTable();
        const auto it = std::ranges::find(table, key, &InputKeyName::Key);
        if (it == table.end()) return std::nullopt;
        return "key:" + std::string(it->Name);
    }

    [[nodiscard]] inline std::optional<std::string> ButtonSourceName(const GPP::MouseButton button)
    {
        switch (button)
        {
        case GPP::MouseButton::Left: return "mouse:left";
        case GPP::MouseButton::Middle: return "mouse:middle";
        case GPP::MouseButton::Right: return "mouse:right";
        case GPP::MouseButton::X1: return "mouse:x1";
        case GPP::MouseButton::X2: return "mouse:x2";
        }
        return std::nullopt;
    }

    [[nodiscard]] inline std::vector<GPP::KeyCode> KeysOfSource(const std::string_view source)
    {
        std::vector<GPP::KeyCode> keys;
        if (!source.starts_with("key:")) return keys;
        for (const auto& entry : InputKeyTable())
        {
            if (entry.Name == source.substr(4)) keys.push_back(entry.Key);
        }
        return keys;
    }

    [[nodiscard]] inline std::string SourceLabel(const std::string_view source)
    {
        std::string text(source.substr(source.find(':') == std::string_view::npos ? 0 : source.find(':') + 1));
        const bool mouse = source.starts_with("mouse:");
        if (!text.empty()) text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
        return mouse ? "Mouse " + text : text;
    }

    class ScriptInputSettings
    {
    public:
        [[nodiscard]] static ScriptInputSettings& Instance()
        {
            static ScriptInputSettings settings;
            return settings;
        }

        [[nodiscard]] GPP::ActionMap User() const
        {
            std::scoped_lock lock(m_Mutex);
            return m_User;
        }
        [[nodiscard]] std::uint64_t Revision() const noexcept { return m_Revision.load(std::memory_order_relaxed); }

        void SetUser(GPP::ActionMap map)
        {
            std::scoped_lock lock(m_Mutex);
            if (map == m_User) return;
            m_User = std::move(map);
            m_Revision.fetch_add(1, std::memory_order_relaxed);
        }

        void ArmCapture()
        {
            std::scoped_lock lock(m_Mutex);
            m_Captured.reset();
            m_Armed = true;
        }
        void CancelCapture()
        {
            std::scoped_lock lock(m_Mutex);
            m_Armed = false;
            m_Captured.reset();
        }
        [[nodiscard]] bool Capturing() const
        {
            std::scoped_lock lock(m_Mutex);
            return m_Armed;
        }
        [[nodiscard]] std::optional<std::string> TakeCaptured()
        {
            std::scoped_lock lock(m_Mutex);
            return std::exchange(m_Captured, std::nullopt);
        }
        bool Offer(std::string source)
        {
            std::scoped_lock lock(m_Mutex);
            if (!m_Armed) return false;
            m_Armed = false;
            m_Captured = std::move(source);
            return true;
        }

    private:
        mutable std::mutex m_Mutex;
        GPP::ActionMap m_User;
        std::atomic<std::uint64_t> m_Revision{0};
        bool m_Armed{false};
        std::optional<std::string> m_Captured;
    };

    [[nodiscard]] inline GPP::ActionMap EffectiveActionMap(const GPP::Scene* scene, const GPP::ActionMap& user,
                                                           std::string* error = nullptr)
    {
        auto map = GPP::ActionMap::Defaults();
        if (scene)
        {
            if (const auto* node = scene->FindExtension(kSceneInputExtension))
            {
                if (auto shipped = GPP::ActionMap::FromYaml(YAML::Dump(*node))) map.Overlay(*shipped);
                else if (error) *error = shipped.error();
            }
        }
        map.Overlay(user);
        return map;
    }

    struct ScriptInputFrame
    {
        bool Playing{false};
        bool ScriptsUseInput{false};
        bool ViewportFocused{false};
        bool TextInput{false};
        bool ViewportClicked{false};
    };

    class ScriptInputBridge
    {
    public:
        ScriptInputBridge(const std::shared_ptr<GPP::EventDispatcher>& dispatcher, std::shared_ptr<GPP::InputState> input)
            : m_Input(std::move(input))
        {
            m_Subscriptions.push_back(dispatcher->Subscribe<GPP::KeyEvent>([this](const GPP::KeyEvent& event) { OnKey(event); }));
            m_Subscriptions.push_back(
                dispatcher->Subscribe<GPP::MouseButtonEvent>([this](const GPP::MouseButtonEvent& event) { OnButton(event); }));
            m_Subscriptions.push_back(
                dispatcher->Subscribe<GPP::MouseMotionEvent>([this](const GPP::MouseMotionEvent& event) { OnMotion(event); }));
        }

        void SetTarget(std::shared_ptr<GPP::SimulationInput> target) { m_Router.SetTarget(std::move(target)); }
        void SetMap(GPP::ActionMap map) { m_Router.SetMap(std::move(map)); }

        bool Update(const ScriptInputFrame& frame)
        {
            const bool wants = frame.Playing && frame.ScriptsUseInput;
            if (!wants || frame.ViewportClicked) m_EscReleased.store(false, std::memory_order_relaxed);
            m_Wants.store(wants, std::memory_order_relaxed);
            const bool captured = wants && !m_EscReleased.load(std::memory_order_relaxed) && frame.ViewportFocused && !frame.TextInput;
            Set(captured);
            return captured;
        }

        [[nodiscard]] bool Captured() const noexcept { return m_Captured.load(std::memory_order_relaxed); }
        void AddScroll(const float delta)
        {
            if (m_Captured.load(std::memory_order_relaxed)) m_Router.AddScroll(delta);
        }
        [[nodiscard]] bool TakeEscRelease() noexcept { return m_EscEvent.exchange(false, std::memory_order_relaxed); }
        [[nodiscard]] bool WaitingForClick() const noexcept
        {
            return m_Wants.load(std::memory_order_relaxed) && m_EscReleased.load(std::memory_order_relaxed);
        }

    private:
        void Set(const bool captured)
        {
            if (m_Captured.exchange(captured, std::memory_order_relaxed) == captured) return;
            std::scoped_lock lock(m_Mutex);
            m_Down.clear();
            m_Router.Release();
            if (!captured) return;
            for (const auto& entry : InputKeyTable())
            {
                if (!m_Input->IsKeyDown(entry.Key)) continue;
                m_Down.insert(entry.Key);
                m_Router.SetSource("key:" + std::string(entry.Name), 1.0f);
            }
        }

        void OnKey(const GPP::KeyEvent& event)
        {
            auto& settings = ScriptInputSettings::Instance();
            if (event.Down && !event.Repeat && settings.Capturing())
            {
                if (event.Key == GPP::KeyCode::Escape) settings.CancelCapture();
                else if (const auto name = KeySourceName(event.Key)) settings.Offer(*name);
                return;
            }
            if (!m_Captured.load(std::memory_order_relaxed)) return;
            if (event.Key == GPP::KeyCode::Escape)
            {
                if (!event.Down) return;
                m_EscReleased.store(true, std::memory_order_relaxed);
                m_EscEvent.store(true, std::memory_order_relaxed);
                Set(false);
                return;
            }
            if (event.Repeat) return;
            const auto name = KeySourceName(event.Key);
            if (!name) return;
            std::scoped_lock lock(m_Mutex);
            if (event.Down) m_Down.insert(event.Key);
            else m_Down.erase(event.Key);
            const bool any = std::ranges::any_of(KeysOfSource(*name), [&](const GPP::KeyCode key) { return m_Down.contains(key); });
            m_Router.SetSource(*name, any ? 1.0f : 0.0f);
        }

        void OnButton(const GPP::MouseButtonEvent& event)
        {
            const auto name = ButtonSourceName(event.Button);
            if (!name) return;
            if (m_Captured.load(std::memory_order_relaxed)) m_Router.SetSource(*name, event.Down ? 1.0f : 0.0f);
        }

        void OnMotion(const GPP::MouseMotionEvent& event)
        {
            if (m_Captured.load(std::memory_order_relaxed)) m_Router.AddMouse({event.DeltaX, event.DeltaY});
        }

        std::shared_ptr<GPP::InputState> m_Input;
        GPP::ActionRouter m_Router;
        std::mutex m_Mutex;
        std::set<GPP::KeyCode> m_Down;
        std::atomic<bool> m_Captured{false};
        std::atomic<bool> m_Wants{false};
        std::atomic<bool> m_EscReleased{false};
        std::atomic<bool> m_EscEvent{false};
        std::vector<GPP::EventSubscription> m_Subscriptions;
    };
}
