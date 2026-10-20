#include <catch2/catch_test_macros.hpp>

import GPP;
import imgui;
import MoleHole;
import std;

using namespace MoleHole;

TEST_CASE("The default UI font gets Unicode symbols from the Roboto fallback", "[ui][font]")
{
    ImGuiContext* context = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImFont* font = io.Fonts->AddFontDefault();
    REQUIRE(font != nullptr);

    // ImGui's own default font covers ASCII and Latin-1 only
    const auto has = [&](const unsigned int codepoint)
    {
        return font->GetFontBaked(16.0f)->FindGlyphNoFallback(static_cast<ImWchar>(codepoint)) != nullptr;
    };
    CHECK(has('A'));
    CHECK(has(0x00B2)); // ²
    CHECK_FALSE(has(0x03C4)); // τ

    const std::string roboto = std::string(MOLEHOLE_SOURCE_DIR) + "/font/Roboto-Regular.ttf";
    CHECK(EnsureSymbolFallback(roboto.c_str()) == font);
    CHECK(EnsureSymbolFallback(roboto.c_str()) == nullptr); // once per font

    // The symbols the UI uses: Greek, super/subscripts, math
    for (const unsigned int codepoint : {0x03C4u, 0x03B3u, 0x03B8u, 0x03C6u, 0x03A9u, 0x2074u, 0x208Au, 0x2212u, 0x2248u,
                                         0x221Au, 0x00D7u})
    {
        INFO(std::format("U+{:04X}", codepoint));
        CHECK(has(codepoint));
    }
    CHECK(has('A'));
    ImGui::DestroyContext(context);
}
