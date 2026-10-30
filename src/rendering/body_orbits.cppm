export module MoleHole:Rendering.BodyOrbits;

import std;
import glm;
import GPP;
import :Simulation.Components;
import :Rendering.OrbitOverlays;

export namespace MoleHole
{
    inline constexpr glm::vec3 kPlanetOrbitColor{0.30f, 0.55f, 0.95f};
    inline constexpr glm::vec3 kMoonOrbitColor{0.95f, 0.65f, 0.25f};
    inline constexpr glm::vec3 kUnboundOrbitColor{0.85f, 0.35f, 0.85f};
    inline constexpr float kTestParticleDimming = 0.45f;

    inline constexpr double kMinOrbitPixels = 10.0;
    inline constexpr double kMarkerBelowPixels = 6.0;
    inline constexpr double kMarkerRadiusPixels = 5.0;
    inline constexpr double kMaxMoonOrbitScreens = 1.5;

    struct ConicElements
    {
        glm::dvec3 P{1.0, 0.0, 0.0}; // towards periapsis
        glm::dvec3 Q{0.0, 1.0, 0.0}; // in the orbit plane, 90 degrees ahead of P
        double Eccentricity{0.0};
        double SemiLatusRectum{0.0};
        double Mu{0.0};
    };

    [[nodiscard]] inline std::optional<ConicElements> ConicFromState(const glm::dvec3& r, const glm::dvec3& v, const double mu)
    {
        const double distance = glm::length(r);
        const glm::dvec3 h = glm::cross(r, v);
        const double h2 = glm::dot(h, h);
        if (!(mu > 0.0) || !(distance > 0.0) || !(h2 > 0.0)) return std::nullopt;
        const glm::dvec3 eccentricityVector = glm::cross(v, h) / mu - r / distance;
        ConicElements out;
        out.Eccentricity = glm::length(eccentricityVector);
        out.SemiLatusRectum = h2 / mu;
        out.Mu = mu;
        const glm::dvec3 normal = h / std::sqrt(h2);
        out.P = out.Eccentricity > 1e-12 ? eccentricityVector / out.Eccentricity : r / distance;
        out.Q = glm::cross(normal, out.P);
        return out;
    }

    [[nodiscard]] inline bool IsBound(const ConicElements& conic) noexcept { return conic.Eccentricity < 1.0 - 1e-9; }

    [[nodiscard]] inline std::vector<glm::dvec3> ConicPoints(const ConicElements& conic, const int samples, const double maxRadius)
    {
        std::vector<glm::dvec3> points;
        points.reserve(static_cast<std::size_t>(samples) + 1);
        const double e = conic.Eccentricity;
        const double p = conic.SemiLatusRectum;
        if (IsBound(conic))
        {
            const double a = p / (1.0 - e * e);
            const double b = a * std::sqrt(1.0 - e * e);
            for (int k = 0; k <= samples; ++k)
            {
                const double eccentric = 2.0 * std::numbers::pi * static_cast<double>(k) / samples;
                points.push_back(conic.P * (a * (std::cos(eccentric) - e)) + conic.Q * (b * std::sin(eccentric)));
            }
            return points;
        }
        const double limit = e > 1e-9 && maxRadius > p / (1.0 + e) ? std::acos(std::clamp((p / maxRadius - 1.0) / e, -1.0, 1.0)) : 0.0;
        const double asymptote = e > 1.0 ? std::acos(-1.0 / e) * 0.999 : std::numbers::pi * 0.999;
        const double nu = std::min(limit, asymptote);
        for (int k = 0; k <= samples; ++k)
        {
            const double anomaly = -nu + 2.0 * nu * static_cast<double>(k) / samples;
            const double radius = p / (1.0 + e * std::cos(anomaly));
            points.push_back((conic.P * std::cos(anomaly) + conic.Q * std::sin(anomaly)) * radius);
        }
        return points;
    }

    [[nodiscard]] inline std::vector<int> OrbitParents(const std::span<const glm::dvec3> position, const std::span<const double> mu)
    {
        const std::size_t count = position.size();
        std::vector<int> parent(count, -1);
        std::vector<std::size_t> order(count);
        std::iota(order.begin(), order.end(), std::size_t{0});
        std::ranges::stable_sort(order, std::greater<>{}, [&](const std::size_t i) { return mu[i]; });
        std::vector<double> hill(count, std::numeric_limits<double>::infinity());
        for (std::size_t rank = 1; rank < count; ++rank)
        {
            const std::size_t i = order[rank];
            double best = std::numeric_limits<double>::infinity();
            int chosen = static_cast<int>(order[0]);
            for (std::size_t other = 0; other < rank; ++other)
            {
                const std::size_t k = order[other];
                if (!(mu[k] > mu[i])) continue;
                const double distance = glm::length(position[i] - position[k]);
                if (distance < hill[k] && distance < best)
                {
                    best = distance;
                    chosen = static_cast<int>(k);
                }
            }
            parent[i] = chosen;
            const auto p = static_cast<std::size_t>(chosen);
            if (mu[p] > 0.0 && mu[i] > 0.0) hill[i] = glm::length(position[i] - position[p]) * std::cbrt(mu[i] / (3.0 * mu[p]));
        }
        return parent;
    }

    struct OrbitOverlayView
    {
        glm::dvec3 Position{0.0};
        glm::dvec3 Front{0.0, 0.0, -1.0};
        glm::dvec3 Right{1.0, 0.0, 0.0};
        glm::dvec3 Up{0.0, 1.0, 0.0};
        double PixelAngle{1e-3}; // radians per pixel
        double ViewHalfAngle{std::numbers::pi}; // half the angle of the view cone; a body outside it is skipped
        double ViewHeightPixels{1.0e9};         // a moon's orbit more than kMaxMoonOrbitScreens of these across is left out
    };

    [[nodiscard]] inline std::vector<OverlaySegment> BodyOrbitsOverlay(const GPP::Scene& scene, const OrbitOverlayView& view,
                                                                       const int samples = 128)
    {
        std::vector<OverlaySegment> out;
        const auto& registry = scene.Registry();
        std::vector<entt::entity> bodies;
        std::vector<glm::dvec3> position;
        std::vector<glm::dvec3> velocity;
        std::vector<double> mu;
        std::vector<double> radius;
        for (auto [entity, body, transform] : registry.view<const NBodyComponent, const GPP::TransformComponent>().each())
        {
            if (IsHidden(registry, entity)) continue;
            bodies.push_back(entity);
            position.push_back(transform.Position);
            velocity.push_back(body.Velocity);
            mu.push_back(body.GM);
            double r = 0.0;
            if (const auto* sphere = registry.try_get<const SphereComponent>(entity)) r = sphere->Radius;
            else if (const auto* star = registry.try_get<const StarComponent>(entity)) r = star->Radius;
            radius.push_back(r);
        }
        if (bodies.size() < 2) return out;

        struct Occluder
        {
            glm::dvec3 Centre;
            double Radius;
        };
        std::vector<Occluder> occluders;
        for (std::size_t i = 0; i < bodies.size(); ++i)
        {
            const double distance = glm::length(position[i] - view.Position);
            if (distance > 0.0 && radius[i] / distance > 2.0 * view.PixelAngle) occluders.push_back({position[i], radius[i]});
        }
        const auto hidden = [&](const glm::dvec3& point)
        {
            const glm::dvec3 ray = point - view.Position;
            const double length = glm::length(ray);
            if (!(length > 0.0)) return false;
            const glm::dvec3 direction = ray / length;
            for (const auto& occluder : occluders)
            {
                const glm::dvec3 toCentre = occluder.Centre - view.Position;
                const double along = glm::dot(toCentre, direction);
                if (along <= 0.0) continue;
                const double miss2 = glm::dot(toCentre, toCentre) - along * along;
                const double depth2 = occluder.Radius * occluder.Radius - miss2;
                if (depth2 > 0.0 && along - std::sqrt(depth2) < length) return true;
            }
            return false;
        };
        const auto addLine = [&](const std::vector<glm::dvec3>& points, const glm::dvec3& origin, const glm::vec3& color)
        {
            for (std::size_t k = 1; k < points.size(); ++k)
            {
                const glm::dvec3 a = origin + points[k - 1];
                const glm::dvec3 b = origin + points[k];
                if (hidden(0.5 * (a + b))) continue;
                out.push_back({a, b, color});
            }
        };

        const auto parent = OrbitParents(position, mu);
        std::vector<int> depth(bodies.size(), 0);
        for (std::size_t i = 0; i < bodies.size(); ++i)
        {
            for (int p = parent[i]; p >= 0; p = parent[static_cast<std::size_t>(p)]) ++depth[i];
        }

        for (std::size_t i = 0; i < bodies.size(); ++i)
        {
            if (parent[i] < 0) continue;
            const auto p = static_cast<std::size_t>(parent[i]);
            const glm::dvec3 relative = position[i] - position[p];
            const auto conic = ConicFromState(relative, velocity[i] - velocity[p], mu[p] + mu[i]);
            if (!conic) continue;

            const double toBody = glm::length(position[i] - view.Position);
            const double toParent = glm::length(position[p] - view.Position);
            if (!(toBody > 0.0) || !(toParent > 0.0)) continue;
            const auto inView = [&](const glm::dvec3& point, const double distance)
            { return glm::dot(point - view.Position, view.Front) > std::cos(view.ViewHalfAngle) * distance; };
            if (!inView(position[i], toBody)) continue;
            const double extent = IsBound(*conic) ? conic->SemiLatusRectum / (1.0 - conic->Eccentricity * conic->Eccentricity)
                                                  : glm::length(relative);
            const double orbitPixels = 2.0 * extent / (std::min(toBody, toParent) * view.PixelAngle);
            if (orbitPixels < kMinOrbitPixels) continue;
            if (depth[i] >= 2 && orbitPixels > kMaxMoonOrbitScreens * view.ViewHeightPixels) continue;

            glm::vec3 color = !IsBound(*conic) ? kUnboundOrbitColor : depth[i] <= 1 ? kPlanetOrbitColor : kMoonOrbitColor;
            if (depth[i] <= 1 && !(mu[i] > 0.0)) color *= kTestParticleDimming;
            addLine(ConicPoints(*conic, samples, 2.0 * glm::length(relative)), position[p], color);

            const double discPixels = radius[i] / (toBody * view.PixelAngle);
            if (discPixels < kMarkerBelowPixels && glm::dot(position[i] - view.Position, view.Front) > 0.0)
            {
                const double size = kMarkerRadiusPixels * view.PixelAngle * toBody;
                constexpr int kSides = 12;
                std::vector<glm::dvec3> ring;
                for (int k = 0; k <= kSides; ++k)
                {
                    const double angle = 2.0 * std::numbers::pi * k / kSides;
                    ring.push_back((view.Right * std::cos(angle) + view.Up * std::sin(angle)) * size);
                }
                addLine(ring, position[i], color);
            }
        }
        return out;
    }
}
