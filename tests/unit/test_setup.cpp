#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

import MoleHole;

namespace
{
    // The app registers its components at startup, before any graph node is built. Do the same before the first
    // test: graph Getter/Setter/Decompose nodes take their pins from the registered components, so a test that
    // happens to run first in the random order would otherwise build pin-less nodes.
    class RegisterComponentsListener final : public Catch::EventListenerBase
    {
    public:
        using Catch::EventListenerBase::EventListenerBase;

        void testRunStarting(const Catch::TestRunInfo&) override { MoleHole::RegisterComponents(); }
    };
}

CATCH_REGISTER_LISTENER(RegisterComponentsListener)
