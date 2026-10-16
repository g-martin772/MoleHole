export module MoleHole:Rendering.Frame;

import std;
import glm;
import GPP;
import :Simulation.Components;

export namespace MoleHole
{
    enum class FrameMode : int { SolarMass = 0, PrimaryBlackHole = 1 };

    struct RenderFrame
    {
        glm::dvec3 OriginM{0.0};
        double LengthUnitM{GPP::Units::kSolarGravitationalRadius};

        [[nodiscard]] glm::vec3 ToRender(const glm::dvec3& worldM) const
        {
            return glm::vec3((worldM - OriginM) / LengthUnitM);
        }

        [[nodiscard]] float ToRenderLength(const double lengthM) const { return static_cast<float>(lengthM / LengthUnitM); }

        [[nodiscard]] glm::dvec3 ToWorld(const glm::vec3& render) const { return OriginM + glm::dvec3(render) * LengthUnitM; }

        [[nodiscard]] float GeometricMass(const double massKg) const
        {
            return static_cast<float>(GPP::Units::GravitationalRadius(massKg) / LengthUnitM);
        }

        [[nodiscard]] static RenderFrame ForScene(const GPP::Scene& scene, const FrameMode mode,
                                                  const glm::dvec3& originM)
        {
            RenderFrame frame;
            frame.OriginM = originM;
            if (mode == FrameMode::PrimaryBlackHole)
            {
                double heaviest = 0.0;
                for (auto [entity, blackHole] : scene.Registry().view<const BlackHoleComponent>().each())
                {
                    heaviest = std::max(heaviest, blackHole.Mass);
                }
                if (heaviest > 0.0) frame.LengthUnitM = GPP::Units::GravitationalRadius(heaviest);
            }
            return frame;
        }
    };
}
