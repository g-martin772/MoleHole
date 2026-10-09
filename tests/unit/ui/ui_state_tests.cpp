#include <catch2/catch_test_macros.hpp>

import MoleHole;
import std;

using namespace MoleHole;

TEST_CASE("OneShot hands each value to exactly one consumer and restores only into an empty slot",
          "[ui][threading]")
{
    OneShot<int> slot;
    CHECK_FALSE(slot.Take());

    slot.Set(1);
    CHECK(slot.Pending());
    CHECK(slot.Take() == 1);
    CHECK_FALSE(slot.Take());

    slot.Set(2);
    slot.Restore(3);
    CHECK(slot.Take() == 2);
    slot.Restore(4);
    CHECK(slot.Take() == 4);
}

TEST_CASE("OneShot never delivers a value twice under concurrent producers and consumers", "[ui][threading]")
{
    OneShot<int> slot;
    std::atomic<int> taken{0};
    std::atomic<bool> done{false};

    std::thread consumer([&]
    {
        while (!done.load() || slot.Pending())
        {
            if (slot.Take()) ++taken;
        }
    });
    constexpr int kSets = 2000;
    for (int i = 0; i < kSets; ++i) slot.Set(i);
    done = true;
    consumer.join();
    CHECK(taken.load() >= 1);
    CHECK(taken.load() <= kSets);
}
