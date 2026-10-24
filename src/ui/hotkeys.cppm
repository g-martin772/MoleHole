export module MoleHole:UI.Hotkeys;

import std;
import imgui;
import :UI.ActionLogic;

export namespace MoleHole
{
    struct KeyBinding
    {
        std::string_view Name;
        ImGuiKey Key;
    };

    inline const std::vector<KeyBinding>& KeyTable()
    {
        static const std::vector<KeyBinding> table{
            {"A", ImGuiKey_A}, {"B", ImGuiKey_B}, {"C", ImGuiKey_C}, {"D", ImGuiKey_D}, {"E", ImGuiKey_E}, {"F", ImGuiKey_F},
            {"G", ImGuiKey_G}, {"H", ImGuiKey_H}, {"I", ImGuiKey_I}, {"J", ImGuiKey_J}, {"K", ImGuiKey_K}, {"L", ImGuiKey_L},
            {"M", ImGuiKey_M}, {"N", ImGuiKey_N}, {"O", ImGuiKey_O}, {"P", ImGuiKey_P}, {"Q", ImGuiKey_Q}, {"R", ImGuiKey_R},
            {"S", ImGuiKey_S}, {"T", ImGuiKey_T}, {"U", ImGuiKey_U}, {"V", ImGuiKey_V}, {"W", ImGuiKey_W}, {"X", ImGuiKey_X},
            {"Y", ImGuiKey_Y}, {"Z", ImGuiKey_Z}, {"0", ImGuiKey_0}, {"1", ImGuiKey_1}, {"2", ImGuiKey_2}, {"3", ImGuiKey_3},
            {"4", ImGuiKey_4}, {"5", ImGuiKey_5}, {"6", ImGuiKey_6}, {"7", ImGuiKey_7}, {"8", ImGuiKey_8}, {"9", ImGuiKey_9},
            {"F1", ImGuiKey_F1}, {"F2", ImGuiKey_F2}, {"F3", ImGuiKey_F3}, {"F4", ImGuiKey_F4}, {"F5", ImGuiKey_F5},
            {"F6", ImGuiKey_F6}, {"F7", ImGuiKey_F7}, {"F8", ImGuiKey_F8}, {"F9", ImGuiKey_F9}, {"F10", ImGuiKey_F10},
            {"F11", ImGuiKey_F11}, {"F12", ImGuiKey_F12}, {"Del", ImGuiKey_Delete}, {"Enter", ImGuiKey_Enter},
            {"Backspace", ImGuiKey_Backspace}, {"Tab", ImGuiKey_Tab}, {"Space", ImGuiKey_Space}, {".", ImGuiKey_Period},
            {",", ImGuiKey_Comma}, {"[", ImGuiKey_LeftBracket}, {"]", ImGuiKey_RightBracket}, {"-", ImGuiKey_Minus},
            {"=", ImGuiKey_Equal}, {";", ImGuiKey_Semicolon}, {"'", ImGuiKey_Apostrophe}, {"`", ImGuiKey_GraveAccent},
            {"\\", ImGuiKey_Backslash}, {"Up", ImGuiKey_UpArrow}, {"Down", ImGuiKey_DownArrow}, {"Left", ImGuiKey_LeftArrow},
            {"Right", ImGuiKey_RightArrow}, {"Home", ImGuiKey_Home}, {"End", ImGuiKey_End}, {"PageUp", ImGuiKey_PageUp},
            {"PageDown", ImGuiKey_PageDown}, {"Insert", ImGuiKey_Insert}, {"Num +", ImGuiKey_KeypadAdd},
            {"Num -", ImGuiKey_KeypadSubtract},
        };
        return table;
    }

    [[nodiscard]] inline std::optional<ImGuiKey> FindKey(const std::string_view name)
    {
        const auto& table = KeyTable();
        const auto found = std::ranges::find(table, name, &KeyBinding::Name);
        if (found == table.end()) return std::nullopt;
        return found->Key;
    }

    [[nodiscard]] inline std::optional<ImGuiKeyChord> ToImGuiChord(const std::string_view text)
    {
        if (!ChordProblem(text).empty()) return std::nullopt;
        const auto chord = ParseChord(text);
        const auto key = FindKey(chord.Key);
        if (!key) return std::nullopt;
        ImGuiKeyChord result = *key;
        if (chord.Ctrl) result |= ImGuiMod_Ctrl;
        if (chord.Alt) result |= ImGuiMod_Alt;
        if (chord.Shift) result |= ImGuiMod_Shift;
        return result;
    }

    [[nodiscard]] inline std::string ChordText(const bool ctrl, const bool alt, const bool shift, const std::string_view key)
    {
        std::string text;
        if (ctrl) text += "Ctrl+";
        if (alt) text += "Alt+";
        if (shift) text += "Shift+";
        return text + std::string(key);
    }

    [[nodiscard]] inline std::optional<std::string> CapturePressedChord()
    {
        const ImGuiIO& io = ImGui::GetIO();
        for (const auto& [name, key] : KeyTable())
        {
            if (ImGui::IsKeyPressed(key, false)) return ChordText(io.KeyCtrl, io.KeyAlt, io.KeyShift, name);
        }
        return std::nullopt;
    }

    struct HotkeyBinding
    {
        std::string Id;
        ImGuiKeyChord Chord = 0;
        bool Repeat = false;
        bool Command = false;
        bool GraphSafe = false;
    };

    struct HotkeyFrame
    {
        bool TextInput = false;
        bool PopupOpen = false;
        bool AnyItemActive = false;
        bool GraphEditorFocused = false;
    };

    [[nodiscard]] inline bool HotkeyAllowed(const HotkeyBinding& binding, const HotkeyFrame& frame) noexcept
    {
        if (frame.TextInput || frame.PopupOpen) return false;
        if (frame.GraphEditorFocused && !binding.GraphSafe) return false;
        if (frame.AnyItemActive && !binding.Command) return false;
        return true;
    }

    [[nodiscard]] inline std::vector<HotkeyBinding> CollectHotkeys(const std::span<const ActionDescriptor> actions)
    {
        std::vector<HotkeyBinding> bindings;
        for (const auto& action : actions)
        {
            if (!IsRebindable(action)) continue;
            for (const auto& text : SplitShortcuts(action.Shortcut))
            {
                const auto chord = ToImGuiChord(text);
                if (!chord) continue;
                const auto parsed = ParseChord(text);
                bindings.push_back({action.Id, *chord, action.Repeat, parsed.Ctrl || parsed.Alt, action.Category == "File"});
            }
        }
        return bindings;
    }

    [[nodiscard]] inline std::optional<std::string> PollHotkey(const std::span<const HotkeyBinding> bindings, const HotkeyFrame& frame)
    {
        for (const auto& binding : bindings)
        {
            if (!HotkeyAllowed(binding, frame)) continue;
            if (ImGui::IsKeyChordPressed(binding.Chord, binding.Repeat ? ImGuiInputFlags_Repeat : ImGuiInputFlags_None))
                return binding.Id;
        }
        return std::nullopt;
    }
}
