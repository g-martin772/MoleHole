module;
#include <cstddef>
export module MoleHole:Rendering.Bodies;

import std;
import glm;
import GPP;
import :Simulation.Components;
import :Rendering.Data;
import :Rendering.Frame;
import :Simulation.ObserverReadout;

// Surface data of the traced spheres: orientation, material and texture slots. Entry i belongs to u_spheres[i]
// (FillSceneData), so both walk the same registry view. GLSL mirror: shaders/body.glsl.
// Lighting: each body's sky probe (shaders/black_hole_rendering.comp with GPP_BODY_PROBE) traces the light arriving
// at its centre; shaders/body_probe_reduce.comp turns it into SH irradiance plus the brightest source.
// See docs/notes/textured_bodies.md and docs/notes/body_lighting.md
export namespace MoleHole
{
    constexpr std::size_t kBodyTextureSlots = 64;
    constexpr std::uint32_t kProbeWidth = 64; // equirectangular probe per body, world-aligned
    constexpr std::uint32_t kProbeHeight = 32;
    constexpr std::size_t kMaxLights = 8;

    // Texture lookup used while filling: path and role -> slot, or -1 (empty path, loading, failed, no slot left).
    using BodyTextureSlotFn = std::function<int(const std::string&, GPP::TextureRole)>;

    // std430: BodyData in shaders/body.glsl
    struct BodyGpu
    {
        glm::vec4 Orientation{0.0f, 0.0f, 0.0f, 1.0f}; // unit quaternion (x, y, z, w), body frame -> world
        glm::ivec4 Maps{-1, -1, -1, -1};               // texture slots: albedo, normal, roughness, emissive
        glm::vec4 Tint{1.0f};                          // rgb tint, emissive strength
        glm::vec4 Material{0.9f, 1.0f, 0.0f, 0.45f};   // roughness, normal strength, roughness map is specular, glossy roughness
        // Velocity relative to the ZAMO of the first hole along (e_r, e_theta, e_phi) in c; w = 1 when it moves. The
        // body's probe sees the sky aberrated and Doppler-shifted by it, like a moving camera.
        glm::vec4 Velocity{0.0f};
        // Atmosphere in render units (docs/notes/atmospheres.md); AtmosphereShape.x = 0 when there is none
        glm::vec4 AtmosphereShape{0.0f}; // top radius, Rayleigh scale height, Mie scale height, Mie g
        glm::vec4 Rayleigh{0.0f};        // rgb scattering, w: Mie scattering
        glm::vec4 Absorption{0.0f};      // rgb ozone absorption, w: Mie absorption
        glm::vec4 Ozone{0.0f};           // peak altitude, half width, unused, unused
        // Cloud shell; Cloud.x = 0 when there is none
        glm::vec4 CloudOrientation{0.0f, 0.0f, 0.0f, 1.0f}; // quaternion, cloud frame -> world
        glm::vec4 Cloud{0.0f};      // shell radius, opacity, coverage slot (as float, -1 = uniform), unused
        glm::vec4 CloudColor{1.0f};
        glm::ivec4 Flags{0, -1, 0, 0}; // x: 1 = lit Surface; y: height map slot or -1; z: ocean; w: detail octaves
        glm::vec4 Terrain{0.0f};       // render units: height of black, height of white, detail amplitude, detail scale
        glm::vec4 TerrainSeed{0.0f};   // xyz: noise offset from the seed; w: 1 = the body has a Terrain
        glm::vec4 Ring{0.0f};          // inner radius, outer radius (render units; 0 = none), strip slot, opacity
    };
    static_assert(sizeof(BodyGpu) == 256);

    inline void FillTerrainGpu(BodyGpu& gpu, const TerrainComponent& terrain, const BodyTextureSlotFn& slotFor,
                               const double lengthUnitM)
    {
        const auto length = [lengthUnitM](const double metres) { return static_cast<float>(metres / lengthUnitM); };
        gpu.Flags.y = slotFor(terrain.Height, GPP::TextureRole::Height);
        gpu.Flags.z = terrain.Ocean ? 1 : 0;
        gpu.Flags.w = std::clamp(terrain.DetailOctaves, 1, 10);
        gpu.Terrain = {length(terrain.HeightMin), length(terrain.HeightMax), length(terrain.DetailAmplitude),
                       length(std::max(terrain.DetailScale, 1e-3))};
        // A seed picks a far-apart region of the noise field
        const auto seed = static_cast<float>(terrain.Seed);
        gpu.TerrainSeed = {std::fmod(seed * 127.1f, 1000.0f), std::fmod(seed * 311.7f, 1000.0f),
                           std::fmod(seed * 74.7f, 1000.0f), 1.0f};
    }
    static_assert(offsetof(BodyGpu, Maps) == 16);
    static_assert(offsetof(BodyGpu, Material) == 48);

    enum BodyMap : int { kAlbedoMap = 0, kNormalMap = 1, kRoughnessMap = 2, kEmissiveMap = 3 };

    // Written by body_probe_reduce.comp, read by shadeBody: irradiance as 9 RGB spherical-harmonic coefficients
    // (Ramamoorthi & Hanrahan 2001, without the brightest source) and that source as a directional light.
    struct BodyLightingGpu
    {
        std::array<glm::vec4, 9> Sh{};
        glm::vec4 Direction{0.0f, 1.0f, 0.0f, 0.0f}; // towards the source
        glm::vec4 Irradiance{0.0f};                  // rgb at normal incidence
    };
    static_assert(sizeof(BodyLightingGpu) == 176);

    enum class BodyLightingMode : std::int32_t { Fixed = 0, Probe = 1, LightsOnly = 2 };

    // std430: LightData in shaders/body.glsl
    struct BodyLightGpu
    {
        glm::vec4 Position{0.0f}; // xyz: direction towards the light (w = 0) or position in render units (w = 1)
        glm::vec4 Color{0.0f};    // rgb irradiance, w: reference distance in render units (point lights)
    };

    struct BodyLightBlockGpu
    {
        glm::ivec4 Header{0, 0, -1, 0}; // x: light count, y: BodyLightingMode, z: local-frame body (sphere index) or -1
        glm::vec4 PatchCamera{0.0f};     // camera relative to that body, render units along the world axes
        std::array<BodyLightGpu, kMaxLights> Lights{};
    };
    static_assert(sizeof(BodyLightBlockGpu) == 32 + kMaxLights * 32);

    // The camera renders a body in its local frame while it is closer than this many body radii to its centre.
    constexpr double kLocalFrameRadii = 20.0;

    struct LocalFrameChoice
    {
        int Sphere = -1;           // index in the uploaded spheres
        glm::vec3 CameraOffset{0.0f}; // render units
    };

    // The nearest sphere whose local frame contains the camera, in the order FillSceneData uploads them. Offsets are
    // taken in double, so a camera metres above a planet far from the origin keeps its precision.
    [[nodiscard]] inline LocalFrameChoice ChooseLocalFrame(const GPP::Scene& scene, const RenderFrame& frame,
                                                           const glm::dvec3& cameraM)
    {
        LocalFrameChoice choice;
        double best = std::numeric_limits<double>::infinity();
        int index = 0;
        for (auto [entity, sphere, transform] : scene.Registry().view<const SphereComponent, const GPP::TransformComponent>().each())
        {
            if (index >= kMaxSpheres) break;
            const glm::dvec3 offset = cameraM - transform.Position;
            const double distance = glm::length(offset);
            if (distance < kLocalFrameRadii * sphere.Radius && distance / sphere.Radius < best)
            {
                best = distance / sphere.Radius;
                choice.Sphere = index;
                choice.CameraOffset = glm::vec3(offset / frame.LengthUnitM);
            }
            ++index;
        }
        return choice;
    }

    // Quaternion helpers on glm::vec4 (x, y, z, w), matching quatRotate in shaders/body.glsl.
    [[nodiscard]] inline glm::vec4 QuatMultiply(const glm::vec4& a, const glm::vec4& b)
    {
        const glm::vec3 av(a), bv(b);
        return {a.w * bv + b.w * av + glm::cross(av, bv), a.w * b.w - glm::dot(av, bv)};
    }

    [[nodiscard]] inline glm::vec3 QuatRotate(const glm::vec4& q, const glm::vec3& v)
    {
        const glm::vec3 u(q);
        return v + 2.0f * glm::cross(u, glm::cross(u, v) + q.w * v);
    }

    [[nodiscard]] inline glm::vec4 QuatConjugate(const glm::vec4& q) { return {-q.x, -q.y, -q.z, q.w}; }

    // Shortest rotation taking +Y to `axis`.
    [[nodiscard]] inline glm::vec4 QuatFromYTo(const glm::vec3& axis)
    {
        const float length = glm::length(axis);
        if (!(length > 0.0f)) return {0.0f, 0.0f, 0.0f, 1.0f};
        const glm::vec3 to = axis / length;
        const float d = to.y; // dot(+Y, to)
        if (d < -0.999999f) return {1.0f, 0.0f, 0.0f, 0.0f}; // half turn about X
        const glm::vec3 c(to.z, 0.0f, -to.x);             // cross(+Y, to)
        return glm::normalize(glm::vec4(c, 1.0f + d));
    }

    // Body frame: +Y along the spin axis (north), +X through longitude 0 at rotation angle 0. Turning by `angle`
    // about +Y is eastward rotation: a point at longitude λ moves towards λ + angle in world space.
    [[nodiscard]] inline glm::vec4 BodyOrientation(const glm::vec3& spinAxis, const double angleRad)
    {
        const float half = static_cast<float>(0.5 * angleRad);
        const glm::vec4 spin{0.0f, std::sin(half), 0.0f, std::cos(half)};
        return glm::normalize(QuatMultiply(QuatFromYTo(spinAxis), spin));
    }

    // Equirectangular coordinates of a body-frame direction: u = 0 at longitude -180°, 0.5 at 0°, growing east;
    // v = 0 at the north pole. East longitude λ = atan2(-z, x), so +X is λ = 0 and -Z is λ = +90°.
    [[nodiscard]] inline glm::vec2 EquirectUv(const glm::vec3& bodyDirection)
    {
        const glm::vec3 n = glm::normalize(bodyDirection);
        const float longitude = std::atan2(-n.z, n.x);
        const float latitude = std::asin(std::clamp(n.y, -1.0f, 1.0f));
        return {0.5f + longitude / (2.0f * std::numbers::pi_v<float>), 0.5f - latitude / std::numbers::pi_v<float>};
    }

    // Assigns up to N distinct images to descriptor slots, in order of first use.
    template <std::size_t N>
    class TextureSlotTable
    {
    public:
        int Assign(const GPP::VulkanImage* image)
        {
            if (!image) return -1;
            for (std::size_t i = 0; i < m_Count; ++i)
            {
                if (m_Images[i] == image) return static_cast<int>(i);
            }
            if (m_Count == N)
            {
                m_Overflowed = true;
                return -1;
            }
            m_Images[m_Count] = image;
            return static_cast<int>(m_Count++);
        }

        void Clear()
        {
            m_Images.fill(nullptr);
            m_Count = 0;
            m_Overflowed = false;
        }

        [[nodiscard]] bool Overflowed() const noexcept { return m_Overflowed; }
        [[nodiscard]] const std::array<const GPP::VulkanImage*, N>& Images() const noexcept { return m_Images; }

    private:
        std::array<const GPP::VulkanImage*, N> m_Images{};
        std::size_t m_Count = 0;
        bool m_Overflowed = false;
    };


    // Scattering and absorption coefficients are per metre; the shader works in render units of LengthUnitM metres.
    inline void FillAtmosphereGpu(BodyGpu& gpu, const SphereComponent& sphere, const AtmosphereComponent& atmosphere,
                                  const double lengthUnitM)
    {
        const auto length = [lengthUnitM](const double metres) { return static_cast<float>(metres / lengthUnitM); };
        const auto perLength = [lengthUnitM](const double perMetre) { return static_cast<float>(perMetre * lengthUnitM); };
        gpu.AtmosphereShape = {length(sphere.Radius + atmosphere.Height), length(atmosphere.RayleighScaleHeight),
                               length(atmosphere.MieScaleHeight), atmosphere.MieAnisotropy};
        gpu.Rayleigh = {perLength(atmosphere.RayleighScattering.x), perLength(atmosphere.RayleighScattering.y),
                        perLength(atmosphere.RayleighScattering.z), perLength(atmosphere.MieScattering)};
        gpu.Absorption = {perLength(atmosphere.OzoneAbsorption.x), perLength(atmosphere.OzoneAbsorption.y),
                          perLength(atmosphere.OzoneAbsorption.z), perLength(atmosphere.MieAbsorption)};
        gpu.Ozone = {length(atmosphere.OzoneCenter), length(0.5 * atmosphere.OzoneWidth), 0.0f, 0.0f};
    }

    [[nodiscard]] inline BodyGpu MakeBodyGpu(const SphereComponent& sphere, const SurfaceComponent* surface,
                                             const BodyTextureSlotFn& slotFor,
                                             const std::optional<Vector3>& velocity = std::nullopt)
    {
        BodyGpu gpu;
        gpu.Velocity = velocity ? glm::vec4(static_cast<float>((*velocity)[0]), static_cast<float>((*velocity)[1]),
                                            static_cast<float>((*velocity)[2]), 1.0f)
                                : glm::vec4(0.0f);
        if (!surface)
        {
            gpu.Orientation = BodyOrientation(sphere.SpinAxis, 0.0);
            gpu.Maps[kAlbedoMap] = slotFor(sphere.TexturePath, GPP::TextureRole::Color);
            return gpu;
        }
        const double prime = static_cast<double>(surface->PrimeMeridian) * std::numbers::pi / 180.0;
        gpu.Orientation = BodyOrientation(sphere.SpinAxis, surface->RotationAngle + prime);
        gpu.Flags.x = 1;
        gpu.Maps = {slotFor(surface->Albedo, GPP::TextureRole::Color), slotFor(surface->Normal, GPP::TextureRole::Data),
                    slotFor(surface->Roughness, GPP::TextureRole::Data),
                    slotFor(surface->Emissive, GPP::TextureRole::Color)};
        gpu.Tint = glm::vec4(surface->Tint, surface->EmissiveStrength);
        gpu.Material = {surface->RoughnessFactor, surface->NormalStrength, surface->RoughnessIsSpecular ? 1.0f : 0.0f,
                        surface->GlossyRoughness};
        return gpu;
    }

    // One entry per sphere, in the order FillSceneData uploads them.
    [[nodiscard]] inline std::array<BodyGpu, kMaxSpheres> FillBodyData(const GPP::Scene& scene,
                                                                       const BodyTextureSlotFn& slotFor,
                                                                       const double lengthUnitM = 1.0)
    {
        std::array<BodyGpu, kMaxSpheres> bodies{};
        std::size_t count = 0;
        const auto& registry = scene.Registry();
        for (auto [entity, sphere, transform] : registry.view<const SphereComponent, const GPP::TransformComponent>().each())
        {
            if (count >= bodies.size()) break;
            const auto* surface = registry.try_get<const SurfaceComponent>(entity);
            auto& gpu = bodies[count++];
            gpu = MakeBodyGpu(sphere, surface, slotFor, BodyObserverVelocity(scene, scene.GuidOf(entity)));
            if (const auto* atmosphere = registry.try_get<const AtmosphereComponent>(entity))
            {
                FillAtmosphereGpu(gpu, sphere, *atmosphere, lengthUnitM);
            }
            if (const auto* terrain = registry.try_get<const TerrainComponent>(entity))
            {
                FillTerrainGpu(gpu, *terrain, slotFor, lengthUnitM);
            }
            if (const auto* ring = registry.try_get<const RingComponent>(entity); ring && ring->OuterRadius > ring->InnerRadius)
            {
                gpu.Ring = {static_cast<float>(ring->InnerRadius / lengthUnitM), static_cast<float>(ring->OuterRadius / lengthUnitM),
                            static_cast<float>(slotFor(ring->Texture, GPP::TextureRole::Color)), ring->Opacity};
            }
            if (const auto* clouds = registry.try_get<const CloudLayerComponent>(entity))
            {
                const double surfaceAngle = surface ? surface->RotationAngle
                                                          + static_cast<double>(surface->PrimeMeridian) * std::numbers::pi / 180.0
                                                    : 0.0;
                gpu.CloudOrientation = BodyOrientation(sphere.SpinAxis, surfaceAngle + clouds->RotationAngle);
                gpu.Cloud = {static_cast<float>((sphere.Radius + clouds->Altitude) / lengthUnitM), clouds->Opacity,
                             static_cast<float>(slotFor(clouds->Coverage, GPP::TextureRole::Data)), 0.0f};
                gpu.CloudColor = glm::vec4(clouds->Color, 1.0f);
            }
        }
        return bodies;
    }

    [[nodiscard]] inline BodyLightBlockGpu FillLightData(const GPP::Scene& scene, const RenderFrame& frame,
                                                     const BodyLightingMode mode)
    {
        BodyLightBlockGpu block;
        block.Header.y = static_cast<std::int32_t>(mode);
        std::size_t count = 0;
        for (auto [entity, light, transform] : scene.Registry().view<const LightComponent, const GPP::TransformComponent>().each())
        {
            if (count >= kMaxLights) break;
            auto& gpu = block.Lights[count++];
            gpu.Color = glm::vec4(light.Color * light.Intensity, 0.0f);
            if (light.Kind == BodyLightKind::Directional)
            {
                const float length = glm::length(light.Direction);
                gpu.Position = glm::vec4(length > 0.0f ? light.Direction / length : glm::vec3(0.0f, 1.0f, 0.0f), 0.0f);
            }
            else
            {
                gpu.Position = glm::vec4(frame.ToRender(transform.Position), 1.0f);
                gpu.Color.w = frame.ToRenderLength(light.ReferenceDistance);
            }
        }
        block.Header.x = static_cast<std::int32_t>(count);
        return block;
    }

    // CPU mirror of the probe directions and the SH projection/evaluation in shaders/body_probe_reduce.comp and
    // shaders/body.glsl, for tests. Directions: v = 0 at +Y, u = 0.5 towards +X, east (u growing) towards -Z.
    [[nodiscard]] inline glm::vec3 ProbeDirection(const float u, const float v)
    {
        const float longitude = (u - 0.5f) * 2.0f * std::numbers::pi_v<float>;
        const float latitude = (0.5f - v) * std::numbers::pi_v<float>;
        return {std::cos(latitude) * std::cos(longitude), std::sin(latitude), -std::cos(latitude) * std::sin(longitude)};
    }

    [[nodiscard]] inline std::array<float, 9> ShBasis(const glm::vec3& d)
    {
        return {0.282095f, 0.488603f * d.y, 0.488603f * d.z, 0.488603f * d.x, 1.092548f * d.x * d.y,
                1.092548f * d.y * d.z, 0.315392f * (3.0f * d.z * d.z - 1.0f), 1.092548f * d.x * d.z,
                0.546274f * (d.x * d.x - d.y * d.y)};
    }

    // Projection of radiance(direction) over a kProbeWidth x kProbeHeight grid with its solid angles.
    [[nodiscard]] inline std::array<glm::vec3, 9> ProjectRadianceSh(const std::function<glm::vec3(const glm::vec3&)>& radiance)
    {
        std::array<glm::vec3, 9> sh{};
        const float cellArea = (2.0f * std::numbers::pi_v<float> / kProbeWidth) * (std::numbers::pi_v<float> / kProbeHeight);
        for (std::uint32_t y = 0; y < kProbeHeight; ++y)
        {
            for (std::uint32_t x = 0; x < kProbeWidth; ++x)
            {
                const float u = (static_cast<float>(x) + 0.5f) / kProbeWidth;
                const float v = (static_cast<float>(y) + 0.5f) / kProbeHeight;
                const auto d = ProbeDirection(u, v);
                const float solidAngle = cellArea * std::cos((0.5f - v) * std::numbers::pi_v<float>);
                const auto basis = ShBasis(d);
                const auto value = radiance(d) * solidAngle;
                for (std::size_t i = 0; i < 9; ++i) sh[i] += value * basis[i];
            }
        }
        return sh;
    }

    // Irradiance E(n) from radiance coefficients, R&H 2001 eq. 13 (c1..c5), in the basis order of ShBasis.
    [[nodiscard]] inline glm::vec3 EvaluateIrradianceSh(const std::array<glm::vec3, 9>& L, const glm::vec3& n)
    {
        constexpr float c1 = 0.429043f, c2 = 0.511664f, c3 = 0.743125f, c4 = 0.886227f, c5 = 0.247708f;
        return c1 * L[8] * (n.x * n.x - n.y * n.y) + c3 * L[6] * n.z * n.z + c4 * L[0] - c5 * L[6]
               + 2.0f * c1 * (L[4] * n.x * n.y + L[7] * n.x * n.z + L[5] * n.y * n.z)
               + 2.0f * c2 * (L[3] * n.x + L[1] * n.y + L[2] * n.z);
    }
}
