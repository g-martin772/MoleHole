#include <catch2/catch_test_macros.hpp>

import MoleHole;
import GPP;
import std;

using namespace MoleHole;

namespace
{
    RenderSettings Make(const Transport transport, const GPP::Units::Precision precision = GPP::Units::Precision::F32)
    {
        RenderSettings settings;
        settings.TransportMode = transport;
        settings.GpuPrecision = precision;
        settings.RenderMeshes = true;
        settings.MeshDrawMode = MeshMode::Traced;
        return settings;
    }

    TraceDemand Caps(const bool f64 = true, const bool meshes = true, const bool sceneMeshes = false, const bool probes = false)
    {
        return {.F64Supported = f64, .MeshTraceSupported = meshes, .SceneHasMeshes = sceneMeshes, .ProbesWanted = probes};
    }
}

TEST_CASE("Straight rays need only the flat kernel", "[rendering][pipeline][variants]")
{
    const auto variants = RequiredTraceVariants(Make(Transport::Straight, GPP::Units::Precision::F64), Caps(true, true, true, true));
    CHECK(variants == TraceVariants{.Flat = true});
}

TEST_CASE("The marcher variant follows precision and device support", "[rendering][pipeline][variants]")
{
    CHECK(RequiredTraceVariants(Make(Transport::AnalyticLens), Caps()) == TraceVariants{.Marcher = true});
    CHECK(RequiredTraceVariants(Make(Transport::Integrated), Caps()) == TraceVariants{.Marcher = true});
    CHECK(RequiredTraceVariants(Make(Transport::Integrated, GPP::Units::Precision::F64), Caps()) == TraceVariants{.MarcherF64 = true});
    CHECK(RequiredTraceVariants(Make(Transport::Integrated, GPP::Units::Precision::F64), Caps(false)) == TraceVariants{.Marcher = true});
    CHECK(RequiredTraceVariants(Make(Transport::AnalyticLens, GPP::Units::Precision::F64), Caps()) == TraceVariants{.Marcher = true});
}

TEST_CASE("Mesh kernels are required only when the scene has meshes and the device can trace them", "[rendering][pipeline][variants]")
{
    const auto settings = Make(Transport::Integrated);
    CHECK(RequiredTraceVariants(settings, Caps(true, true, false)) == TraceVariants{.Marcher = true});
    CHECK(RequiredTraceVariants(settings, Caps(true, false, true)) == TraceVariants{.Marcher = true});
    CHECK(RequiredTraceVariants(settings, Caps(true, true, true)) == TraceVariants{.Marcher = true, .Mesh = true});

    auto f64 = Make(Transport::Integrated, GPP::Units::Precision::F64);
    CHECK(RequiredTraceVariants(f64, Caps(true, true, true)) == TraceVariants{.MarcherF64 = true, .MeshF64 = true});

    auto rasterised = settings;
    rasterised.MeshDrawMode = MeshMode::Raster;
    CHECK(RequiredTraceVariants(rasterised, Caps(true, true, true)) == TraceVariants{.Marcher = true});
    auto hidden = settings;
    hidden.RenderMeshes = false;
    CHECK(RequiredTraceVariants(hidden, Caps(true, true, true)) == TraceVariants{.Marcher = true});
}

TEST_CASE("Probe lighting adds the matching probe variant and the reduction", "[rendering][pipeline][variants]")
{
    CHECK(RequiredTraceVariants(Make(Transport::Integrated), Caps(true, true, false, true))
          == TraceVariants{.Marcher = true, .Probe = true, .Reduce = true});
    CHECK(RequiredTraceVariants(Make(Transport::Integrated, GPP::Units::Precision::F64), Caps(true, true, false, true))
          == TraceVariants{.MarcherF64 = true, .ProbeF64 = true, .Reduce = true});
    CHECK(RequiredTraceVariants(Make(Transport::Straight), Caps(true, true, false, true)) == TraceVariants{.Flat = true});
}
