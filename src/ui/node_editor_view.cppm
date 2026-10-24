export module MoleHole:UI.NodeEditorView;

import std;

export namespace MoleHole
{
    constexpr double kMinNodeEditorZoom = 0.1;
    constexpr double kMaxNodeEditorMagnitude = 1.0e6;

    [[nodiscard]] inline bool NodeEditorViewSane(const std::string_view json)
    {
        static constexpr std::string_view kZoomKey = "\"zoom\":";
        const std::size_t zoomAt = json.find(kZoomKey);
        if (zoomAt == std::string_view::npos) return false;
        for (std::size_t i = 0; i < json.size();)
        {
            const char c = json[i];
            if (c != '-' && c != '.' && !(c >= '0' && c <= '9')) { ++i; continue; }
            double value = 0.0;
            const auto [end, ec] = std::from_chars(json.data() + i, json.data() + json.size(), value);
            if (ec != std::errc{} || !std::isfinite(value) || std::abs(value) > kMaxNodeEditorMagnitude) return false;
            if (i == zoomAt + kZoomKey.size() && value < kMinNodeEditorZoom) return false;
            i = static_cast<std::size_t>(end - json.data());
        }
        return true;
    }

    // The library refits a saved visible_rect to whatever canvas exists at load, which changes the zoom between sessions.
    [[nodiscard]] inline std::string StripVisibleRect(std::string json)
    {
        static constexpr std::string_view kKey = "\"visible_rect\":";
        const std::size_t keyAt = json.find(kKey);
        if (keyAt == std::string::npos) return json;
        const std::size_t open = keyAt + kKey.size();
        if (open >= json.size() || json[open] != '{') return json;
        int depth = 0;
        std::size_t close = open;
        for (; close < json.size(); ++close)
        {
            if (json[close] == '{') ++depth;
            else if (json[close] == '}' && --depth == 0) break;
        }
        if (close >= json.size()) return json;
        std::size_t begin = keyAt;
        std::size_t end = close + 1;
        if (end < json.size() && json[end] == ',') ++end;
        else if (begin > 0 && json[begin - 1] == ',') --begin;
        json.erase(begin, end - begin);
        return json;
    }
}
