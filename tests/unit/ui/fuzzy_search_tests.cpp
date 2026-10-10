#include <catch2/catch_test_macros.hpp>

import MoleHole;
import std;

using namespace MoleHole;

TEST_CASE("FuzzyScore rejects non-matches and accepts subsequences", "[ui][fuzzy]")
{
    CHECK_FALSE(FuzzyScore("xyz", "Add").has_value());
    CHECK(FuzzyScore("ad", "Add").has_value());
    CHECK(FuzzyScore("gtrns", "Get Transform").has_value());
    CHECK_FALSE(FuzzyScore("transformz", "Get Transform").has_value());
}

TEST_CASE("FuzzyScore is case-insensitive and treats an empty query as a match", "[ui][fuzzy]")
{
    CHECK(FuzzyScore("TICK", "tick").has_value());
    CHECK(FuzzyScore("", "anything") == 0);
}

TEST_CASE("FuzzyScore ranks prefix over substring over scattered subsequence", "[ui][fuzzy]")
{
    const int prefix = *FuzzyScore("sin", "Sin");
    const int word = *FuzzyScore("sin", "Cos Sin");
    const int inner = *FuzzyScore("sin", "Using");
    const int scattered = *FuzzyScore("sin", "Set Item Number");
    CHECK(prefix > word);
    CHECK(word > inner);
    CHECK(inner > scattered);
}

TEST_CASE("FuzzyScore requires every query token to match", "[ui][fuzzy]")
{
    CHECK(FuzzyScore("get trans", "Get Transform").has_value());
    CHECK_FALSE(FuzzyScore("set trans", "Get Transform").has_value());
}
