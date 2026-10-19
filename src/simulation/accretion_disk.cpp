module;
#include <glm/glm.hpp>
#include <yaml-cpp/yaml.h>
module MoleHole;

import std;
import GPP;
import :Simulation.AccretionDisk;

namespace MoleHole
{
    AccretionBoard& GlobalAccretionBoard()
    {
        static AccretionBoard board;
        return board;
    }

    const std::shared_ptr<GPP::ParticleHub>& GlobalParticleHub()
    {
        static const auto hub = std::make_shared<GPP::ParticleHub>();
        return hub;
    }

    void RegisterAccretionDiskComponent()
    {
        GPP::RegisterComponent<AccretionDiskComponent>("AccretionDisk", GPP::ComponentDescription<AccretionDiskComponent>{
            .DisplayName = "Accretion Disk",
            .Note = "Analytic leaves the shader disk untouched. Particles runs a phenomenological alpha-viscosity model "
                    "(Pringle 1981 drift, Shakura-Sunyaev nu) on a Paczynski-Wiita potential; the hole's spin is ignored "
                    "there and the geodesic integrator is reserved.",
            .Fields = {
                GPP::Field("Model", &AccretionDiskComponent::Model, {.Options = {"Analytic", "Particles"}}),
                GPP::Field("RateMode", &AccretionDiskComponent::RateMode,
                           {.Label = "Rate Mode", .Options = {"Mass Rate", "Eddington"}}),
                GPP::Field("MassRate", &AccretionDiskComponent::MassRate,
                           {.Label = "Mass Rate", .Min = 0.0, .Speed = 1.0e12, .VisibleField = "RateMode",
                            .VisibleMask = 0b01, .Dim = GPP::Units::Dimension::MassRate}),
                GPP::Field("Eddington", &AccretionDiskComponent::Eddington,
                           {.Label = "Eddington Ratio", .Min = 0.0, .Max = 10.0, .Speed = 0.001,
                            .VisibleField = "RateMode", .VisibleMask = 0b10}),
                GPP::Field("Alpha", &AccretionDiskComponent::Alpha,
                           {.Label = "Alpha", .Min = 0.0, .Max = 1.0, .Speed = 0.005}),
                GPP::Field("AspectRatio", &AccretionDiskComponent::AspectRatio,
                           {.Label = "Aspect Ratio H/R", .Min = 0.001, .Max = 0.5, .Speed = 0.002}),
                GPP::Field("InnerRadius", &AccretionDiskComponent::InnerRadius,
                           {.Label = "Inner Radius (r_g, 0 = ISCO)", .Min = 0.0, .Speed = 0.1}),
                GPP::Field("OuterRadius", &AccretionDiskComponent::OuterRadius,
                           {.Label = "Outer Radius (r_g)", .Min = 1.0, .Speed = 1.0}),
                GPP::Field("TruncationRadius", &AccretionDiskComponent::TruncationRadius,
                           {.Label = "Tidal Truncation (r_g, 0 = none)", .Min = 0.0, .Speed = 1.0}),
                GPP::Field("ShockDamping", &AccretionDiskComponent::ShockDamping,
                           {.Label = "Shock Damping (0 = none)", .Min = 0.0, .Speed = 0.5}),
                GPP::Field("ViewRadius", &AccretionDiskComponent::ViewRadius,
                           {.Label = "Grid Half Width (r_g, 0 = 1.6 outer)", .Min = 0.0, .Speed = 1.0}),
                GPP::Field("Opacity", &AccretionDiskComponent::Opacity,
                           {.Label = "Opacity (m^2/kg)", .Min = 0.0, .Speed = 0.001}),
                GPP::Field("MaxParticles", &AccretionDiskComponent::MaxParticles,
                           {.Label = "Max Particles", .Min = 1024.0, .Max = 16777216.0, .Speed = 1024.0}),
                GPP::Field("Integrator", &AccretionDiskComponent::Integrator,
                           {.Options = {"Paczynski-Wiita", "Geodesic (not available)"}}),
                GPP::Field("FeedOuterEdge", &AccretionDiskComponent::FeedOuterEdge, {.Label = "Feed Outer Edge"}),
                GPP::Field("FeedBackToHole", &AccretionDiskComponent::FeedBackToHole, {.Label = "Feed Back To Hole"}),
                GPP::Field("RingRadius", &AccretionDiskComponent::RingRadius,
                           {.Label = "Ring Radius (r_g, 0 = none)", .Min = 0.0, .Speed = 0.5}),
                GPP::Field("RingEccentricity", &AccretionDiskComponent::RingEccentricity,
                           {.Label = "Ring Eccentricity", .Min = 0.0, .Max = 0.95, .Speed = 0.005}),
                GPP::Field("RingMass", &AccretionDiskComponent::RingMass,
                           {.Label = "Ring Mass", .Min = 0.0, .Speed = 1.0e20, .Dim = GPP::Units::Dimension::Mass}),
                GPP::Field("RingParticles", &AccretionDiskComponent::RingParticles,
                           {.Label = "Ring Particles", .Min = 0.0, .Max = 4194304.0, .Speed = 100.0}),
                GPP::Field("SeedMass", &AccretionDiskComponent::SeedMass,
                           {.Label = "Warm Start Mass", .Min = 0.0, .Speed = 1.0e20, .Dim = GPP::Units::Dimension::Mass}),
                GPP::Field("SeedInner", &AccretionDiskComponent::SeedInner,
                           {.Label = "Warm Start Inner (r_g, 0 = 1.15 r_in)", .Min = 0.0, .Speed = 0.5}),
                GPP::Field("SeedOuter", &AccretionDiskComponent::SeedOuter,
                           {.Label = "Warm Start Outer (r_g, 0 = outer radius)", .Min = 0.0, .Speed = 0.5}),
                GPP::Field("SeedParticles", &AccretionDiskComponent::SeedParticles,
                           {.Label = "Warm Start Particles", .Min = 0.0, .Max = 4194304.0, .Speed = 100.0}),
                GPP::Field("Seed", &AccretionDiskComponent::Seed),
                GPP::Computed<AccretionDiskComponent, double>(
                    "ParticleMass",
                    [](const AccretionDiskComponent&) { return GlobalAccretionBoard().Primary().Mass; }, nullptr,
                    {.Label = "Mass In Disk", .Dim = GPP::Units::Dimension::Mass}),
                GPP::Computed<AccretionDiskComponent, double>(
                    "MeasuredMassRate",
                    [](const AccretionDiskComponent&) { return GlobalAccretionBoard().Primary().MassRate; }, nullptr,
                    {.Label = "Measured Mass Rate", .Dim = GPP::Units::Dimension::MassRate}),
                GPP::Computed<AccretionDiskComponent, double>(
                    "MeanTemperature",
                    [](const AccretionDiskComponent&) { return GlobalAccretionBoard().Primary().MeanTemperature; }, nullptr,
                    {.Label = "Mean Temperature", .Dim = GPP::Units::Dimension::Temperature}),
                GPP::Computed<AccretionDiskComponent, double>(
                    "EffectiveInnerRadius",
                    [](const AccretionDiskComponent&) { return GlobalAccretionBoard().Primary().InnerRadius; }, nullptr,
                    {.Label = "Effective Inner Radius", .Dim = GPP::Units::Dimension::Length}),
                GPP::Computed<AccretionDiskComponent, int>(
                    "ParticleCount",
                    [](const AccretionDiskComponent&) { return static_cast<int>(GlobalAccretionBoard().Primary().Particles); },
                    nullptr, {.Label = "Particles"}),
            }});
    }
}
