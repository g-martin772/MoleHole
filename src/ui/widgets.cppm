export module MoleHole:UI.Widgets;

import std;
import glm;
import imgui;
import GPP;
import :UI.State;
import :UI.WidgetLogic;

export namespace MoleHole::UiDetail
{
    inline ImU32 ToU32(const glm::vec4& c)
    {
        return ImGui::ColorConvertFloat4ToU32(ImVec4(c.r, c.g, c.b, c.a));
    }

    inline ImVec4 ToVec4(const glm::vec4& c) { return ImVec4(c.r, c.g, c.b, c.a); }

    inline glm::vec4 FromVec4(const ImVec4& c) { return {c.x, c.y, c.z, c.w}; }

    struct SectionFrame
    {
        bool Child = false;
        float StartY = 0.0f;
        ImGuiID Id = 0;
    };

    inline std::vector<SectionFrame>& SectionStack()
    {
        static thread_local std::vector<SectionFrame> stack;
        return stack;
    }

    inline int StringResizeCallback(ImGuiInputTextCallbackData* data)
    {
        if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
        {
            auto* str = static_cast<std::string*>(data->UserData);
            str->resize(static_cast<std::size_t>(data->BufTextLen));
            data->Buf = str->data();
        }
        return 0;
    }
}

export namespace MoleHole
{
    using namespace UiDetail;

    namespace Palette
    {
        inline const glm::vec4 Accent = HexColor(0xb46428);
        inline const glm::vec4 AccentHover = HexColor(0xc87832);
        inline const glm::vec4 AxisX = HexColor(0xd9534f);
        inline const glm::vec4 AxisY = HexColor(0x5cb85c);
        inline const glm::vec4 AxisZ = HexColor(0x4a90d9);
        inline const glm::vec4 AxisW = HexColor(0x9a9a9a);
        inline const glm::vec4 Panel = HexColor(0x242424);
        inline const glm::vec4 HeaderBg = HexColor(0x2e2e2e);
        inline const glm::vec4 HeaderHover = HexColor(0x383838);
    }

    namespace Icon
    {
        constexpr const char* ChevronRight = "\xef\x81\x94"; // f054
        constexpr const char* Trash = "\xef\x87\xb8"; // f1f8
        constexpr const char* Undo = "\xef\x8b\xaa"; // f2ea
        constexpr const char* Xmark = "\xef\x80\x8d"; // f00d
        constexpr const char* Plus = "\xef\x81\xa7"; // f067
        constexpr const char* Search = "\xef\x80\x82"; // f002
        constexpr const char* Eye = "\xef\x81\xae"; // f06e
        constexpr const char* Copy = "\xef\x83\x85"; // f0c5
        constexpr const char* Pen = "\xef\x8c\x84"; // f304
    }

    // Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y for the scene history; call while the owning window is focused.
    inline bool HandleSceneUndoShortcuts(GPP::SimulationRunner& runner)
    {
        const ImGuiIO& io = ImGui::GetIO();
        if (!io.KeyCtrl || io.WantTextInput || ImGui::IsAnyItemActive() || !runner.IsPaused()) return false;
        if (ImGui::IsKeyPressed(ImGuiKey_Z)) return io.KeyShift ? runner.Redo() : runner.Undo();
        if (ImGui::IsKeyPressed(ImGuiKey_Y)) return runner.Redo();
        return false;
    }

    inline void SectionHeader(const char* title)
    {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ToVec4(Palette::Accent));
        ImGui::TextUnformatted(title);
        ImGui::PopStyleColor();
        ImGui::Separator();
        ImGui::Spacing();
    }

    inline ImFont* EnsureIconFont(UiState& state)
    {
        if (!state.IconFont)
        {
            static const ImWchar iconRanges[] = {0xe000, 0xf8ff, 0};
            ImFontConfig config;
            config.FontDataOwnedByAtlas = true;
            state.IconFont = ImGui::GetIO().Fonts->AddFontFromFileTTF(
                "font/fa-solid-900.ttf", 24.0f, &config, iconRanges);
        }
        return state.IconFont;
    }

    // Smoothly chases a 0..1 target keyed by widget id; frame-rate independent.
    inline float HoverLerp(ImGuiID id, bool target, float rate = 16.0f)
    {
        auto* storage = ImGui::GetStateStorage();
        const float current = storage->GetFloat(id, 0.0f);
        const float next = ApproachExp(current, target ? 1.0f : 0.0f, rate, ImGui::GetIO().DeltaTime);
        storage->SetFloat(id, std::fabs(next - (target ? 1.0f : 0.0f)) < 0.002f ? (target ? 1.0f : 0.0f) : next);
        return next;
    }

    inline void Tooltip(const char* text)
    {
        if (text && *text && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled))
        {
            ImGui::SetTooltip("%s", text);
        }
    }

    inline void DrawIcon(ImDrawList* list, ImFont* font, const char* glyph, const char* fallback, ImVec2 center,
                         float size, ImU32 color, float rotation = 0.0f)
    {
        const char* text = font ? glyph : fallback;
        if (font) ImGui::PushFont(font, size); else ImGui::PushFont(nullptr, size);
        const ImVec2 extent = ImGui::CalcTextSize(text);
        const ImVec2 pos(center.x - extent.x * 0.5f, center.y - extent.y * 0.5f);
        const int start = list->VtxBuffer.Size;
        list->AddText(pos, color, text);
        if (rotation != 0.0f)
        {
            const float c = std::cos(rotation);
            const float s = std::sin(rotation);
            for (int i = start; i < list->VtxBuffer.Size; ++i)
            {
                ImVec2& p = list->VtxBuffer[i].pos;
                const float dx = p.x - center.x;
                const float dy = p.y - center.y;
                p = ImVec2(center.x + dx * c - dy * s, center.y + dx * s + dy * c);
            }
        }
        ImGui::PopFont();
    }

    inline bool IconButton(ImFont* font, const char* glyph, const char* fallback, const char* tooltip,
                           float size = 0.0f, bool danger = false)
    {
        const float s = size > 0.0f ? size : ImGui::GetFrameHeight();
        ImGui::PushID(glyph);
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const bool clicked = ImGui::InvisibleButton("##icon", ImVec2(s, s));
        const float t = HoverLerp(ImGui::GetItemID(), ImGui::IsItemHovered());
        auto* list = ImGui::GetWindowDrawList();
        const glm::vec4 hover = danger ? Palette::AxisX : Palette::Accent;
        list->AddRectFilled(pos, ImVec2(pos.x + s, pos.y + s), ToU32(WithAlpha(hover, 0.35f * t)), 3.0f);
        const glm::vec4 base = HexColor(0xa0a0a0);
        DrawIcon(list, font, glyph, fallback, ImVec2(pos.x + s * 0.5f, pos.y + s * 0.5f), s * 0.5f,
                 ToU32(LerpColor(base, HexColor(0xffffff), t)));
        ImGui::PopID();
        Tooltip(tooltip);
        return clicked;
    }

    // Returns whether the section content should be drawn; always pair with EndSection when true.
    // removeClicked, when provided, shows a trash button on the header.
    inline bool BeginSection(ImFont* icons, const char* label, bool defaultOpen = true, bool* removeClicked = nullptr)
    {
        ImGui::PushID(label);
        auto* storage = ImGui::GetStateStorage();
        const ImGuiID openKey = ImGui::GetID("open");
        const ImGuiID progressKey = ImGui::GetID("progress");
        const ImGuiID heightKey = ImGui::GetID("height");
        const bool open = storage->GetInt(openKey, defaultOpen ? 1 : 0) != 0;
        AnimatedToggle anim{storage->GetFloat(progressKey, open ? 1.0f : 0.0f)};

        const float h = ImGui::GetFrameHeight() + 2.0f;
        const float w = ImGui::GetContentRegionAvail().x;
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        if (removeClicked) ImGui::SetNextItemAllowOverlap();
        const bool toggled = ImGui::InvisibleButton("##header", ImVec2(w, h));
        const float hover = HoverLerp(ImGui::GetItemID(), ImGui::IsItemHovered());
        const bool nowOpen = toggled ? !open : open;
        if (toggled) storage->SetInt(openKey, nowOpen ? 1 : 0);

        anim.Update(nowOpen, ImGui::GetIO().DeltaTime);
        storage->SetFloat(progressKey, anim.Linear);

        auto* list = ImGui::GetWindowDrawList();
        list->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h),
                            ToU32(LerpColor(Palette::HeaderBg, Palette::HeaderHover, hover)), 3.0f);
        list->AddRectFilled(pos, ImVec2(pos.x + 3.0f, pos.y + h), ToU32(Palette::Accent), 2.0f);
        DrawIcon(list, icons, Icon::ChevronRight, ">", ImVec2(pos.x + 16.0f, pos.y + h * 0.5f), 11.0f,
                 ToU32(HexColor(0xcccccc)), anim.Eased() * 1.5707964f);
        const ImVec2 textSize = ImGui::CalcTextSize(label);
        list->AddText(ImVec2(pos.x + 30.0f, pos.y + (h - textSize.y) * 0.5f), ToU32(HexColor(0xf0f0f0)), label);

        if (removeClicked)
        {
            const ImVec2 after = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(ImVec2(pos.x + w - h, pos.y));
            if (IconButton(icons, Icon::Trash, "x", "Remove component", h, true)) *removeClicked = true;
            ImGui::SetCursorScreenPos(after);
        }

        const float contentHeight = storage->GetFloat(heightKey, 0.0f);
        const bool visible = nowOpen || anim.Linear > 0.0f;
        if (!visible)
        {
            ImGui::PopID();
            return false;
        }

        const bool animating = !anim.Settled(nowOpen) && contentHeight > 0.0f;
        SectionFrame frame{animating, ImGui::GetCursorPosY(), ImGui::GetID("body")};
        if (animating)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            ImGui::BeginChild(frame.Id, ImVec2(0.0f, contentHeight * anim.Eased()), ImGuiChildFlags_None,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            ImGui::PopStyleVar();
            frame.StartY = ImGui::GetCursorPosY();
        }
        SectionStack().push_back(frame);
        ImGui::Indent(4.0f);
        return true;
    }

    inline void EndSection()
    {
        auto& stack = SectionStack();
        if (stack.empty()) return;
        const SectionFrame frame = stack.back();
        stack.pop_back();
        ImGui::Unindent(4.0f);
        const float measured = ImGui::GetCursorPosY() - frame.StartY;
        if (frame.Child) ImGui::EndChild();
        else ImGui::GetStateStorage()->SetFloat(ImGui::GetID("height"), measured);
        ImGui::Spacing();
        ImGui::PopID();
    }

    // Label column + value column + reset button. drawValue renders the value widget(s) at the pushed item width.
    // Returns true when the user pressed reset.
    template <typename F>
    bool PropertyRow(ImFont* icons, const char* label, bool modified, F&& drawValue, const char* tooltip = nullptr)
    {
        ImGui::PushID(label);
        const float full = ImGui::GetContentRegionAvail().x;
        const float labelWidth = std::clamp(full * 0.38f, 80.0f, 190.0f);
        const float resetWidth = ImGui::GetFrameHeight();
        const float spacing = ImGui::GetStyle().ItemInnerSpacing.x + 2.0f;
        const float valueWidth = std::max(40.0f, full - labelWidth - resetWidth - spacing);

        const float startX = ImGui::GetCursorPosX();
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float textY = pos.y + ImGui::GetStyle().FramePadding.y;
        auto* list = ImGui::GetWindowDrawList();
        list->PushClipRect(pos, ImVec2(pos.x + labelWidth - 6.0f, pos.y + ImGui::GetFrameHeight()), true);
        list->AddText(ImVec2(pos.x, textY), ToU32(modified ? HexColor(0xffffff) : HexColor(0xb8b8b8)), label);
        list->PopClipRect();
        if (tooltip && ImGui::IsMouseHoveringRect(pos, ImVec2(pos.x + labelWidth, pos.y + ImGui::GetFrameHeight())) &&
            ImGui::IsWindowHovered())
        {
            ImGui::SetTooltip("%s", tooltip);
        }

        ImGui::SetCursorPosX(startX + labelWidth);
        ImGui::PushItemWidth(valueWidth);
        ImGui::BeginGroup();
        drawValue();
        ImGui::EndGroup();
        ImGui::PopItemWidth();

        bool reset = false;
        ImGui::SameLine(0.0f, spacing);
        if (modified) reset = IconButton(icons, Icon::Undo, "r", "Reset to default", resetWidth);
        else ImGui::Dummy(ImVec2(resetWidth, 1.0f));
        ImGui::PopID();
        return reset;
    }

    inline bool DragFloatValue(const char* id, float* v, float speed, float min, float max, const char* format)
    {
        const bool bounded = max > min;
        return ImGui::DragFloat(id, v, speed, bounded ? min : 0.0f, bounded ? max : 0.0f, format,
                                bounded ? ImGuiSliderFlags_AlwaysClamp : ImGuiSliderFlags_None);
    }

    inline bool DragIntValue(const char* id, int* v, float speed, float min, float max)
    {
        const bool bounded = max > min;
        return ImGui::DragInt(id, v, speed, bounded ? static_cast<int>(min) : 0, bounded ? static_cast<int>(max) : 0,
                              "%d", bounded ? ImGuiSliderFlags_AlwaysClamp : ImGuiSliderFlags_None);
    }

    // 2-4 floats each with a coloured axis chip; clicking a chip resets that axis to defaults[i].
    inline bool VecValue(const char* id, float* v, int count, const float* defaults, float speed, float min,
                         float max, const char* format)
    {
        static const char* names[] = {"X", "Y", "Z", "W"};
        const glm::vec4 colors[] = {Palette::AxisX, Palette::AxisY, Palette::AxisZ, Palette::AxisW};
        const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
        const float total = ImGui::CalcItemWidth();
        const float each = std::max(24.0f, (total - spacing * static_cast<float>(count - 1)) / static_cast<float>(count));
        const float chip = ImGui::GetFrameHeight() * 0.8f;
        bool changed = false;
        ImGui::PushID(id);
        for (int i = 0; i < count; ++i)
        {
            if (i > 0) ImGui::SameLine(0.0f, spacing);
            ImGui::PushID(i);
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const float h = ImGui::GetFrameHeight();
            ImGui::SetNextItemAllowOverlap();
            ImGui::SetNextItemWidth(each);
            changed |= DragFloatValue("##v", &v[i], speed, min, max, format);
            const ImVec2 after = ImGui::GetCursorScreenPos();
            auto* list = ImGui::GetWindowDrawList();
            const bool hoverChip = ImGui::IsMouseHoveringRect(pos, ImVec2(pos.x + chip, pos.y + h)) &&
                ImGui::IsWindowHovered();
            list->AddRectFilled(pos, ImVec2(pos.x + chip, pos.y + h),
                                ToU32(hoverChip ? Lighten(colors[i], 0.25f) : colors[i]), 3.0f,
                                ImDrawFlags_RoundCornersLeft);
            const ImVec2 ts = ImGui::CalcTextSize(names[i]);
            list->AddText(ImVec2(pos.x + (chip - ts.x) * 0.5f, pos.y + (h - ts.y) * 0.5f),
                          ToU32(HexColor(0xffffff)), names[i]);
            if (defaults)
            {
                ImGui::SetCursorScreenPos(pos);
                if (ImGui::InvisibleButton("##axis", ImVec2(chip, h)) && v[i] != defaults[i])
                {
                    v[i] = defaults[i];
                    changed = true;
                }
                Tooltip("Reset axis");
                ImGui::SetCursorScreenPos(after);
            }
            ImGui::PopID();
        }
        ImGui::PopID();
        return changed;
    }

    inline bool ColorValue(const char* id, float* rgba, bool alpha)
    {
        ImGui::PushID(id);
        bool changed = false;
        const ImVec4 col(rgba[0], rgba[1], rgba[2], alpha ? rgba[3] : 1.0f);
        if (ImGui::ColorButton("##swatch", col, ImGuiColorEditFlags_AlphaPreviewHalf,
                               ImVec2(ImGui::CalcItemWidth(), ImGui::GetFrameHeight())))
        {
            ImGui::OpenPopup("picker");
        }
        if (ImGui::BeginPopup("picker"))
        {
            changed = alpha ? ImGui::ColorPicker4("##pick", rgba) : ImGui::ColorPicker3("##pick", rgba);
            ImGui::EndPopup();
        }
        ImGui::PopID();
        return changed;
    }

    inline bool ToggleSwitch(const char* id, bool* value)
    {
        const float h = ImGui::GetFrameHeight() * 0.85f;
        const float w = h * 1.8f;
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::PushID(id);
        const bool clicked = ImGui::InvisibleButton("##toggle", ImVec2(w, ImGui::GetFrameHeight()));
        if (clicked) *value = !*value;
        const float t = HoverLerp(ImGui::GetItemID(), *value, 18.0f);
        const float hover = HoverLerp(ImGui::GetItemID() + 1, ImGui::IsItemHovered());
        ImGui::PopID();
        const float top = pos.y + (ImGui::GetFrameHeight() - h) * 0.5f;
        auto* list = ImGui::GetWindowDrawList();
        const glm::vec4 off = LerpColor(HexColor(0x3a3a3a), HexColor(0x4a4a4a), hover);
        list->AddRectFilled(ImVec2(pos.x, top), ImVec2(pos.x + w, top + h),
                            ToU32(LerpColor(off, Palette::Accent, t)), h * 0.5f);
        const float r = h * 0.5f - 3.0f;
        const float cx = pos.x + h * 0.5f + (w - h) * EaseOutCubic(t);
        list->AddCircleFilled(ImVec2(cx, top + h * 0.5f), r, ToU32(HexColor(0xf2f2f2)));
        return clicked;
    }

    inline bool CheckboxValue(const char* id, bool* value) { return ImGui::Checkbox(id, value); }

    inline bool EnumCombo(const char* id, int* index, const std::vector<std::string>& options)
    {
        const bool valid = *index >= 0 && *index < static_cast<int>(options.size());
        const std::string preview = valid ? SpacedName(options[*index]) : std::string{};
        bool changed = false;
        ImGui::SetNextItemWidth(ImGui::CalcItemWidth());
        if (ImGui::BeginCombo(id, preview.c_str()))
        {
            for (int i = 0; i < static_cast<int>(options.size()); ++i)
            {
                if (ImGui::Selectable(SpacedName(options[i]).c_str(), i == *index))
                {
                    *index = i;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    inline bool TextValue(const char* id, std::string& text, const char* hint = "", bool multiline = false)
    {
        constexpr auto flags = ImGuiInputTextFlags_CallbackResize;
        text.reserve(std::max<std::size_t>(text.size() + 1, 64));
        if (multiline)
        {
            return ImGui::InputTextMultiline(id, text.data(), text.capacity() + 1,
                                             ImVec2(ImGui::CalcItemWidth(), ImGui::GetTextLineHeight() * 4.0f),
                                             flags, StringResizeCallback, &text);
        }
        return ImGui::InputTextWithHint(id, hint, text.data(), text.capacity() + 1, flags, StringResizeCallback,
                                        &text);
    }

    struct EntityChoice
    {
        std::uint64_t Guid = 0;
        std::string Name;
    };

    inline bool EntityPicker(const char* id, std::uint64_t* guid, const std::vector<EntityChoice>& choices)
    {
        std::string preview = "None";
        if (*guid != 0)
        {
            preview = "(missing) " + std::to_string(*guid);
            for (const auto& c : choices)
            {
                if (c.Guid == *guid) preview = c.Name;
            }
        }
        bool changed = false;
        if (ImGui::BeginCombo(id, preview.c_str()))
        {
            static thread_local std::string filter;
            if (ImGui::IsWindowAppearing()) filter.clear();
            TextValue("##filter", filter, "Search...");
            if (ImGui::Selectable("None", *guid == 0))
            {
                *guid = 0;
                changed = true;
            }
            for (const auto& c : choices)
            {
                if (!ContainsInsensitive(c.Name, filter)) continue;
                ImGui::PushID(static_cast<int>(c.Guid));
                if (ImGui::Selectable(c.Name.c_str(), c.Guid == *guid))
                {
                    *guid = c.Guid;
                    changed = true;
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    // Text input with a hint and a clear button; returns true when the text changed.
    inline bool SearchBox(ImFont* icons, const char* id, std::string& text, const char* hint = "Search...",
                          float width = -1.0f)
    {
        ImGui::PushID(id);
        const float full = width > 0.0f ? width : ImGui::GetContentRegionAvail().x;
        const float button = ImGui::GetFrameHeight();
        ImGui::SetNextItemWidth(full);
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        bool changed = TextValue("##search", text, hint);
        const ImVec2 after = ImGui::GetCursorScreenPos();
        if (!text.empty())
        {
            ImGui::SetCursorScreenPos(ImVec2(pos.x + full - button, pos.y));
            if (IconButton(icons, Icon::Xmark, "x", "Clear", button))
            {
                text.clear();
                changed = true;
            }
            ImGui::SetCursorScreenPos(after);
        }
        ImGui::PopID();
        return changed;
    }

    // Small rounded tag; returns 1 when clicked, 2 when its close button was clicked.
    inline int Chip(const char* label, const glm::vec4& color, bool closable = false)
    {
        ImGui::PushID(label);
        const ImVec2 ts = ImGui::CalcTextSize(label);
        const float h = ImGui::GetFrameHeight() * 0.85f;
        const float pad = 8.0f;
        const float close = closable ? h : 0.0f;
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float w = ts.x + pad * 2.0f + close;
        const bool clicked = ImGui::InvisibleButton("##chip", ImVec2(w, h));
        const float t = HoverLerp(ImGui::GetItemID(), ImGui::IsItemHovered());
        const bool onClose = closable && ImGui::GetIO().MousePos.x > pos.x + w - close;
        auto* list = ImGui::GetWindowDrawList();
        list->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), ToU32(Lighten(WithAlpha(color, 0.45f), 0.25f * t)),
                            h * 0.5f);
        list->AddText(ImVec2(pos.x + pad, pos.y + (h - ts.y) * 0.5f), ToU32(HexColor(0xf0f0f0)), label);
        if (closable)
        {
            list->AddText(ImVec2(pos.x + w - close + 2.0f, pos.y + (h - ts.y) * 0.5f), ToU32(HexColor(0xcccccc)), "x");
        }
        ImGui::PopID();
        if (!clicked) return 0;
        return onClose ? 2 : 1;
    }
}
