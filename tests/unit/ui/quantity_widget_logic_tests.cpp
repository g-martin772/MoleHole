#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

import GPP;
import MoleHole;
import std;

using namespace MoleHole;
using namespace GPP::Units;
using Catch::Approx;

TEST_CASE("SymlogDrag is linear near zero and exponential far away", "[ui][units]")
{
    CHECK(SymlogDrag(0.0, 1.0, 10.0, 0.01) == Approx(std::sinh(0.1)));
    const double big = 1.0e30;
    const double dragged = SymlogDrag(big, 1.0, 100.0, 0.01);
    CHECK(dragged / big == Approx(std::exp(1.0)).epsilon(1e-6));
    CHECK(SymlogDrag(big, 1.0, -100.0, 0.01) / big == Approx(std::exp(-1.0)).epsilon(1e-6));
}

TEST_CASE("SymlogDrag crosses zero smoothly and keeps the sign", "[ui][units]")
{
    double value = 5.0;
    double previous = value;
    for (int i = 0; i < 400; ++i)
    {
        value = SymlogDrag(value, 1.0, -1.0, 0.02);
        CHECK(value < previous);
        previous = value;
    }
    CHECK(value < 0.0);
}

TEST_CASE("SymlogDrag respects bounds and survives extreme drags", "[ui][units]")
{
    CHECK(SymlogDrag(10.0, 1.0, -1e6, 0.01, 1.0, 100.0) == 1.0);
    CHECK(SymlogDrag(10.0, 1.0, 1e6, 0.01, 1.0, 100.0) == 100.0);
    CHECK(std::isfinite(SymlogDrag(1.0, 1.0, 1e9, 1.0)));
    CHECK(SymlogDrag(1.0, 0.0, 1.0, 0.01) == Approx(std::sinh(std::asinh(1.0) + 0.01)));
}

TEST_CASE("Quantity labels pick the unit and keep the user's number readable", "[ui][units]")
{
    CHECK(QuantityLabel(1.5 * kAstronomicalUnit, Dimension::Length) == "1.5 AU");
    CHECK(QuantityLabel(0.0, Dimension::Length) == "0 m");
    CHECK(QuantityLabel(10.0 * kSolarMass, Dimension::Mass) == "10 Msun");
    CHECK(QuantityLabel(5972.2e21, Dimension::Mass) == "5.9722e+24 kg");
    CHECK(QuantityLabel(1.0, Dimension::None) == "1");
}

TEST_CASE("Typed text without a unit is read in the shown unit", "[ui][units]")
{
    CHECK(ParseQuantityEdit("2.5", 3.0 * kAstronomicalUnit, Dimension::Length).value() == Approx(2.5 * kAstronomicalUnit));
    CHECK(ParseQuantityEdit("2.5 km", 3.0 * kAstronomicalUnit, Dimension::Length).value() == Approx(2500.0));
    CHECK(ParseQuantityEdit("7", 0.0, Dimension::Mass).value() == Approx(7.0));
    CHECK_FALSE(ParseQuantityEdit("junk", 1.0, Dimension::Length));
    CHECK_FALSE(ParseQuantityEdit("3 kg", 1.0, Dimension::Length));
}

TEST_CASE("Edit text round trips at full precision", "[ui][units]")
{
    for (const double value : {1.2345678901234e11, 2.2e22, 6.5e9 * kSolarMass, 86400.0 * 400})
    {
        for (const auto dimension : {Dimension::Length, Dimension::Mass, Dimension::Time})
        {
            const auto text = QuantityEditText(value, dimension);
            const auto parsed = ParseQuantityEdit(text, value, dimension);
            REQUIRE(parsed);
            CHECK(*parsed == Approx(value).epsilon(1e-10));
        }
    }
}

TEST_CASE("The tooltip lists SI and neighbouring units", "[ui][units]")
{
    const auto text = QuantityTooltip(1.5 * kAstronomicalUnit, Dimension::Length);
    CHECK(text.find("224396806050 m") != std::string::npos);
    CHECK(text.find("Rsun") != std::string::npos);
    CHECK(QuantityTooltip(0.0, Dimension::Length) == "0 m");
}

TEST_CASE("FitQuantityLabel drops digits then the unit as the width shrinks", "[ui][units]")
{
    const auto width = [](const std::string& text) { return static_cast<float>(text.size()); };
    const double value = 25.123456 * kAstronomicalUnit;
    const auto wide = FitQuantityLabel(value, Dimension::Length, 100.0f, width);
    CHECK(wide == QuantityLabel(value, Dimension::Length));
    const auto narrow = FitQuantityLabel(value, Dimension::Length, 7.0f, width);
    CHECK(narrow.size() <= 7);
    CHECK(narrow.find("AU") != std::string::npos);
    const auto tiny = FitQuantityLabel(value, Dimension::Length, 3.0f, width);
    CHECK(tiny.find("AU") == std::string::npos);
    CHECK(FitQuantityLabel(std::numeric_limits<double>::quiet_NaN(), Dimension::Length, 1.0f, width) == "n/a");
}

TEST_CASE("FitFloatFormat reduces decimals only for fixed formats", "[ui]")
{
    const auto width = [](const std::string& text) { return static_cast<float>(text.size()); };
    CHECK(FitFloatFormat(-0.27, "%.3f", 10.0f, width) == "%.3f");
    CHECK(FitFloatFormat(-0.27, "%.3f", 4.0f, width) == "%.1f");
    CHECK(FitFloatFormat(-0.27, "%.3f", 1.0f, width) == "%.0f");
    CHECK(FitFloatFormat(1.0, "%.2fx", 1.0f, width) == "%.2fx");
    CHECK(FitFloatFormat(1.0, "%d", 1.0f, width) == "%d");
}
