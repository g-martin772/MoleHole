module;
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
export module MoleHole:UI.CreateCatalog;

import std;
import GPP;
import :Simulation.Components;
import :UI.FuzzySearch;

export namespace MoleHole
{
    namespace CreateIcon
    {
        constexpr std::uint32_t Folder = 0xf07b;
        constexpr std::uint32_t Star = 0xf005;
        constexpr std::uint32_t Globe = 0xf0ac;
        constexpr std::uint32_t Moon = 0xf186;
        constexpr std::uint32_t Circle = 0xf111;
        constexpr std::uint32_t CircleDot = 0xf192;
        constexpr std::uint32_t Sun = 0xf185;
        constexpr std::uint32_t Bulb = 0xf0eb;
        constexpr std::uint32_t Video = 0xf03d;
        constexpr std::uint32_t Cube = 0xf1b2;
        constexpr std::uint32_t Cubes = 0xf1b3;
        constexpr std::uint32_t Code = 0xf121;
        constexpr std::uint32_t Atom = 0xf5d2;
        constexpr std::uint32_t Bolt = 0xf0e7;
        constexpr std::uint32_t Meteor = 0xf753;
        constexpr std::uint32_t Burst = 0xe4dc;
        constexpr std::uint32_t Bullseye = 0xf140;
        constexpr std::uint32_t Rocket = 0xf135;
        constexpr std::uint32_t Hurricane = 0xf751;
        constexpr std::uint32_t Crosshairs = 0xf05b;
        constexpr std::uint32_t Box = 0xf466;
        constexpr std::uint32_t Gear = 0xf013;
        constexpr std::uint32_t Sitemap = 0xf0e8;
        constexpr std::uint32_t Satellite = 0xf7bf;
        constexpr std::uint32_t Wand = 0xf0d0;
        constexpr std::uint32_t Plus = 0xf067;
        constexpr std::uint32_t File = 0xf15b;
        constexpr std::uint32_t Link = 0xf0c1;
    }

    struct CreatePopupRequest
    {
        bool Open = false;
        bool AtMouse = true;
        glm::vec2 Position{0.0f};

        void RequestAtMouse() noexcept { Open = true; AtMouse = true; }
        void RequestAt(const glm::vec2 position) noexcept { Open = true; AtMouse = false; Position = position; }
    };

    struct CreateContext
    {
        bool HasPrimaryCamera = false;
    };

    using CreateBuild = std::function<void(entt::registry&, entt::entity, const CreateContext&)>;

    struct CreatePart
    {
        std::string TypeTag;
        std::string Name;
        glm::dvec3 OffsetM{0.0};
        CreateBuild Build;
    };

    enum class CreateAction { Entity, MeshFromFile };

    struct CreateEntry
    {
        std::string Id;
        std::string Folder;
        std::string Name;
        std::uint32_t Icon = CreateIcon::Cubes;
        std::string Tooltip;
        std::string Keywords;
        double SizeM = 0.0;
        bool AtView = false;
        bool AlongView = false;
        std::vector<std::string> Requires;
        std::vector<CreatePart> Parts;
        CreateAction Action = CreateAction::Entity;
    };

    struct CreateFolderStyle
    {
        std::string_view Path;
        std::uint32_t Icon;
        std::string_view Tooltip;
    };

    inline constexpr std::array<CreateFolderStyle, 10> kCreateFolderStyles{{
        {"Bodies", CreateIcon::Globe, "Stars, planets, moons and asteroids with real masses and radii"},
        {"Black Holes", CreateIcon::Circle, "Kerr-Newman black holes"},
        {"Lights", CreateIcon::Bulb, "Light sources"},
        {"Cameras", CreateIcon::Video, "Scene cameras"},
        {"Physics", CreateIcon::Atom, "Rigid bodies and colliders"},
        {"Effects", CreateIcon::Burst, "Particle emitters, sinks and colliders"},
        {"Meshes", CreateIcon::Cube, "glTF models from the asset directories"},
        {"Scripts", CreateIcon::Code, "An entity that runs a script"},
        {"Bodies/Moons and Asteroids", CreateIcon::Moon, "Small bodies"},
        {"Black Holes/Real Objects", CreateIcon::Star, "Measured supermassive holes"},
    }};

    [[nodiscard]] inline std::string_view CreateFolderTooltip(const std::string_view path)
    {
        const auto it = std::ranges::find(kCreateFolderStyles, path, &CreateFolderStyle::Path);
        return it == kCreateFolderStyles.end() ? std::string_view{} : it->Tooltip;
    }

    [[nodiscard]] inline std::uint32_t CreateFolderIcon(const std::string_view path)
    {
        const auto it = std::ranges::find(kCreateFolderStyles, path, &CreateFolderStyle::Path);
        return it == kCreateFolderStyles.end() ? CreateIcon::Folder : it->Icon;
    }

    namespace CreateDetail
    {
        inline CreatePart Part(std::string typeTag, CreateBuild build, std::string name = {}, const glm::dvec3& offset = {})
        {
            return CreatePart{std::move(typeTag), std::move(name), offset, std::move(build)};
        }

        inline CreateEntry Entry(std::string folder, std::string name, const std::uint32_t icon, std::string tooltip,
                                 std::vector<CreatePart> parts, const double sizeM = 0.0, std::string keywords = {},
                                 std::vector<std::string> needs = {})
        {
            CreateEntry entry;
            entry.Folder = std::move(folder);
            entry.Name = std::move(name);
            entry.Id = entry.Folder.empty() ? entry.Name : entry.Folder + "/" + entry.Name;
            entry.Icon = icon;
            entry.Tooltip = std::move(tooltip);
            entry.Keywords = std::move(keywords);
            entry.SizeM = sizeM;
            entry.Parts = std::move(parts);
            entry.Requires = std::move(needs);
            return entry;
        }

        inline CreateEntry Single(std::string folder, std::string name, const std::uint32_t icon, std::string tooltip,
                                  std::string typeTag, CreateBuild build, const double sizeM = 0.0, std::string keywords = {},
                                  std::vector<std::string> needs = {})
        {
            std::vector<CreatePart> parts;
            parts.push_back(Part(std::move(typeTag), std::move(build)));
            return Entry(std::move(folder), std::move(name), icon, std::move(tooltip), std::move(parts), sizeM,
                         std::move(keywords), std::move(needs));
        }

        struct BodySpec
        {
            double Radius;
            double GM;
            glm::vec3 Color;
        };

        inline CreateBuild Body(const BodySpec spec)
        {
            return [spec](entt::registry& registry, const entt::entity entity, const CreateContext&)
            {
                registry.emplace<SphereComponent>(entity, SphereComponent{.Radius = spec.Radius, .Color = spec.Color});
                registry.emplace<NBodyComponent>(entity, NBodyComponent{.GM = spec.GM});
            };
        }

        inline CreateBuild Hole(const BlackHoleComponent hole)
        {
            return [hole](entt::registry& registry, const entt::entity entity, const CreateContext&)
            {
                registry.emplace<BlackHoleComponent>(entity, hole);
            };
        }

        inline double HorizonScaleM(const double massKg) { return GPP::Units::SchwarzschildRadius(massKg); }

        template <typename T>
        CreateBuild Plain(T value = {})
        {
            return [value](entt::registry& registry, const entt::entity entity, const CreateContext&)
            {
                registry.emplace<T>(entity, value);
            };
        }

        inline CreateBuild Rigid(GPP::RigidBodyComponent body, GPP::ColliderComponent collider)
        {
            return [body, collider](entt::registry& registry, const entt::entity entity, const CreateContext&)
            {
                registry.emplace<GPP::RigidBodyComponent>(entity, body);
                registry.emplace<GPP::ColliderComponent>(entity, collider);
            };
        }
    }

    [[nodiscard]] inline std::vector<CreateEntry> BuiltinCreateEntries()
    {
        using namespace CreateDetail;
        namespace U = GPP::Units;
        constexpr double kEarthMoonGM = 4.9028e12;
        constexpr double kMoonRadiusM = 1.7374e6;
        constexpr double kVestaGM = 1.7288245e10;
        constexpr double kVestaRadiusM = 2.627e5;

        std::vector<CreateEntry> entries;
        entries.push_back(Single("", "Empty", CreateIcon::Sitemap, "An entity with only a transform: a group or an anchor",
                                 "Empty", [](entt::registry&, entt::entity, const CreateContext&) {}, 0.0, "group node transform"));

        entries.push_back(Single("Bodies", "Star", CreateIcon::Star,
                                 "A solar-type star: 5772 K, one solar radius and mass, lights nearby surfaces", "Star",
                                 [](entt::registry& registry, const entt::entity entity, const CreateContext&)
                                 {
                                     registry.emplace<SphereComponent>(entity, SphereComponent{.Radius = U::kSolarRadius});
                                     registry.emplace<NBodyComponent>(entity, NBodyComponent{.GM = U::kSolarMassParameter});
                                     registry.emplace<StarComponent>(entity, StarComponent{});
                                     registry.emplace<LightComponent>(
                                         entity, LightComponent{.Kind = BodyLightKind::Point, .Color = {1.0f, 0.96f, 0.9f},
                                                                .ReferenceDistance = U::kAstronomicalUnit});
                                 },
                                 U::kSolarRadius, "sun"));
        entries.push_back(Single("Bodies", "Planet", CreateIcon::Globe, "An Earth-sized planet (equatorial radius and GM of the Earth)",
                                 "Sphere", Body({U::kEarthEquatorialRadius, U::kEarthMassParameter, {0.35f, 0.5f, 0.8f}}),
                                 U::kEarthEquatorialRadius, "earth world terrestrial"));
        entries.push_back(Single("Bodies", "Gas Giant", CreateIcon::Hurricane,
                                 "A Jupiter-sized planet (equatorial radius and GM of Jupiter)", "Sphere",
                                 Body({U::kJupiterEquatorialRadius, U::kJupiterMassParameter, {0.8f, 0.65f, 0.5f}}),
                                 U::kJupiterEquatorialRadius, "jupiter"));
        entries.push_back(Single("Bodies/Moons and Asteroids", "Moon", CreateIcon::Moon, "A body with the radius and GM of the Moon",
                                 "Sphere", Body({kMoonRadiusM, kEarthMoonGM, {0.6f, 0.6f, 0.6f}}), kMoonRadiusM, "luna satellite"));
        entries.push_back(Single("Bodies/Moons and Asteroids", "Asteroid", CreateIcon::Meteor,
                                 "A dark rocky body with the radius and GM of Vesta", "Sphere",
                                 Body({kVestaRadiusM, kVestaGM, {0.35f, 0.33f, 0.3f}}), kVestaRadiusM, "vesta rock"));

        const auto hole = [](const double massKg, const double spin) { return BlackHoleComponent{.Mass = massKg, .Spin = spin}; };
        entries.push_back(Single("Black Holes", "Schwarzschild", CreateIcon::Circle,
                                 "A non-rotating, uncharged 10 solar mass black hole", "BlackHole",
                                 Hole(hole(10.0 * U::kSolarMass, 0.0)), HorizonScaleM(10.0 * U::kSolarMass), "static"));
        entries.push_back(Single("Black Holes", "Kerr (spinning)", CreateIcon::CircleDot,
                                 "A 10 solar mass black hole with a* = 0.9", "BlackHole",
                                 Hole(hole(10.0 * U::kSolarMass, 0.9)), HorizonScaleM(10.0 * U::kSolarMass), "rotating spin"));
        entries.push_back(Single("Black Holes", "Kerr-Newman (charged)", CreateIcon::Bolt,
                                 "A 10 solar mass black hole with a* = 0.5 and q* = 0.6", "BlackHole",
                                 Hole(BlackHoleComponent{.Mass = 10.0 * U::kSolarMass, .Spin = 0.5, .Charge = 0.6}),
                                 HorizonScaleM(10.0 * U::kSolarMass), "charge reissner"));
        entries.push_back(Single("Black Holes", "Stellar Mass", CreateIcon::Circle,
                                 "One solar mass, a* = 0.3 (the default black hole)", "BlackHole",
                                 Hole(hole(U::kSolarMass, 0.3)), HorizonScaleM(U::kSolarMass), "solar"));
        entries.push_back(Single("Black Holes/Real Objects", "Sagittarius A*", CreateIcon::Star,
                                 "4.3 million solar masses, a* = 0.9 (mass: GRAVITY 2023)", "BlackHole",
                                 Hole(hole(U::kSagittariusAStarMass, 0.9)), HorizonScaleM(U::kSagittariusAStarMass), "sgr galactic centre"));
        entries.push_back(Single("Black Holes/Real Objects", "M87*", CreateIcon::Star,
                                 "6.5 billion solar masses, a* = 0.9 (mass: EHT 2019)", "BlackHole",
                                 Hole(hole(U::kM87StarMass, 0.9)), HorizonScaleM(U::kM87StarMass), "m87 eht"));
        entries.push_back(Single("Black Holes/Real Objects", "Gargantua", CreateIcon::CircleDot,
                                 "1e8 solar masses, spinning at 1 - 1.3e-14 of the limit", "BlackHole",
                                 Hole(BlackHoleComponent{.Mass = 1.0e8 * U::kSolarMass, .Spin = 0.0,
                                                         .SpinRange = BlackHoleSpinRange::NearExtremal}),
                                 HorizonScaleM(1.0e8 * U::kSolarMass), "interstellar extremal"));
        {
            const double mass = 10.0 * U::kSolarMass;
            const double halfSeparation = 20.0 * U::kSolarGravitationalRadius * 10.0;
            std::vector<CreatePart> parts;
            parts.push_back(Part("BlackHole", Hole(hole(mass, 0.0)), "Binary A", {-halfSeparation, 0.0, 0.0}));
            parts.push_back(Part("BlackHole", Hole(hole(mass, 0.0)), "Binary B", {halfSeparation, 0.0, 0.0}));
            entries.push_back(Entry("Black Holes", "Binary Pair", CreateIcon::Link,
                                    "Two 10 solar mass holes 40 r_g apart; they stay put until given motion", std::move(parts),
                                    HorizonScaleM(mass) + 2.0 * halfSeparation, "two double"));
        }

        const auto light = [](std::string name, const std::uint32_t icon, std::string tooltip, std::string tag,
                              CreateBuild build, std::vector<std::string> needs, std::string keywords = {})
        {
            auto entry = Single("Lights", std::move(name), icon, std::move(tooltip), std::move(tag), std::move(build), 0.0,
                                std::move(keywords), std::move(needs));
            entry.AlongView = true;
            return entry;
        };
        entries.push_back(light("Directional Light", CreateIcon::Sun, "Parallel light of the Sun's irradiance, shining along the view direction",
                                "DirectionalLight", Plain<GPP::DirectionalLightComponent>(), {"DirectionalLight"}, "sun"));
        entries.push_back(light("Point Light", CreateIcon::Bulb, "Light radiating in all directions from a point, 100 W/sr",
                                "PointLight", Plain<GPP::PointLightComponent>(), {"PointLight"}, "bulb omni"));
        entries.push_back(light("Spot Light", CreateIcon::Rocket, "A cone of light along the view direction",
                                "SpotLight", Plain<GPP::SpotLightComponent>(), {"SpotLight"}, "cone"));
        entries.push_back(light("Area Light", CreateIcon::Box, "A 1 m by 1 m emitting rectangle",
                                "AreaLight", Plain<GPP::AreaLightComponent>(), {"AreaLight"}, "rect panel"));
        entries.push_back(light("Body Light", CreateIcon::Wand, "Lights textured body surfaces without lensing (see body lighting)",
                                "Light", Plain<LightComponent>(), {"Light"}, "surface sphere"));

        {
            auto camera = Single("Cameras", "Camera", CreateIcon::Video, "A scene camera placed at, and looking along, the editor view",
                                 "Camera",
                                 [](entt::registry& registry, const entt::entity entity, const CreateContext& context)
                                 {
                                     registry.emplace<GPP::CameraComponent>(
                                         entity, GPP::CameraComponent{.NearPlane = 0.1 * U::kSolarGravitationalRadius,
                                                                      .FarPlane = 1000.0 * U::kSolarGravitationalRadius,
                                                                      .Primary = !context.HasPrimaryCamera});
                                 });
            camera.AtView = true;
            camera.AlongView = true;
            entries.push_back(std::move(camera));
        }

        entries.push_back(Single("Physics", "Rigid Body Sphere", CreateIcon::Circle,
                                 "A dynamic sphere with the mass of the Earth and gravity", "Sphere",
                                 [](entt::registry& registry, const entt::entity entity, const CreateContext&)
                                 {
                                     registry.emplace<SphereComponent>(entity, SphereComponent{.Radius = 0.5 * U::kSolarGravitationalRadius,
                                                                                               .Color = {0.8f, 0.8f, 0.9f}});
                                     registry.emplace<GPP::RigidBodyComponent>(
                                         entity, GPP::RigidBodyComponent{.Type = GPP::RigidBodyType::Dynamic, .Mass = U::kEarthMass,
                                                                         .EnableGravity = true});
                                     registry.emplace<GPP::ColliderComponent>(
                                         entity, GPP::ColliderComponent{.Shape = GPP::ColliderShape::Sphere,
                                                                        .Radius = 0.5 * U::kSolarGravitationalRadius});
                                 },
                                 0.5 * U::kSolarGravitationalRadius, "ball dynamic"));
        entries.push_back(Single("Physics", "Static Box Collider", CreateIcon::Box, "An immovable 1 m box collider", "Collider",
                                 Rigid(GPP::RigidBodyComponent{.Type = GPP::RigidBodyType::Static, .EnableGravity = false},
                                       GPP::ColliderComponent{.Shape = GPP::ColliderShape::Box}),
                                 0.0, "wall floor"));
        entries.push_back(Single("Physics", "Trigger Volume", CreateIcon::Crosshairs, "A 1 m box that reports overlaps without blocking",
                                 "Collider",
                                 Rigid(GPP::RigidBodyComponent{.Type = GPP::RigidBodyType::Static, .EnableGravity = false},
                                       GPP::ColliderComponent{.Shape = GPP::ColliderShape::Box, .IsTrigger = true}),
                                 0.0, "zone sensor"));

        entries.push_back(Single("Effects", "Particle Emitter", CreateIcon::Burst, "Emits particles from a point", "ParticleEmitter",
                                 Plain<GPP::ParticleEmitterComponent>(), 0.0, "spray fountain", {"ParticleEmitter"}));
        entries.push_back(Single("Effects", "Particle Sink", CreateIcon::Bullseye, "A sphere that removes the particles entering it",
                                 "ParticleSink", Plain<GPP::ParticleSinkComponent>(), 0.0, "absorb", {"ParticleSink"}));
        entries.push_back(Single("Effects", "Particle Collider", CreateIcon::Satellite, "A sphere that particles bounce off or are dragged by",
                                 "ParticleCollider", Plain<GPP::ParticleColliderComponent>(), 0.0, "obstacle",
                                 {"ParticleCollider"}));

        {
            CreateEntry file;
            file.Id = "Meshes/From File";
            file.Folder = "Meshes";
            file.Name = "From File...";
            file.Icon = CreateIcon::File;
            file.Tooltip = "Pick any glTF file and choose whether it gets a physics collider";
            file.Keywords = "browse import gltf glb";
            file.Action = CreateAction::MeshFromFile;
            entries.push_back(std::move(file));
        }
        return entries;
    }

    [[nodiscard]] inline std::string MeshEntryFolder(std::string_view path, std::string& name)
    {
        constexpr std::string_view kModelsRoot = "assets/models/";
        const std::filesystem::path file(path);
        const bool external = file.is_absolute();
        std::string relative(path);
        if (relative.starts_with(kModelsRoot)) relative.erase(0, kModelsRoot.size());

        auto parent = std::filesystem::path(relative).parent_path().generic_string();
        name = file.stem().string();
        if (name == "scene" && !parent.empty())
        {
            const auto slash = parent.rfind('/');
            name = slash == std::string::npos ? parent : parent.substr(slash + 1);
            parent = slash == std::string::npos ? std::string{} : parent.substr(0, slash);
        }
        if (external) return "Meshes/External";
        return parent.empty() ? "Meshes" : "Meshes/" + parent;
    }

    [[nodiscard]] inline std::vector<CreateEntry> MeshCreateEntries(const std::span<const std::string> paths)
    {
        std::vector<CreateEntry> entries;
        for (const auto& path : paths)
        {
            std::string name;
            auto folder = MeshEntryFolder(path, name);
            auto entry = CreateDetail::Single(
                std::move(folder), name, CreateIcon::Cube, path, "Mesh",
                [path](entt::registry& registry, const entt::entity entity, const CreateContext&)
                {
                    registry.emplace<GPP::MeshComponent>(entity, GPP::MeshComponent{.AssetPath = path});
                },
                0.0, path);
            entry.Id = "Mesh:" + path;
            entries.push_back(std::move(entry));
        }
        return entries;
    }

    [[nodiscard]] inline std::vector<CreateEntry> ScriptCreateEntries(const std::span<const std::string> names)
    {
        std::vector<CreateEntry> entries;
        for (const auto& name : names)
        {
            auto entry = CreateDetail::Single(
                "Scripts", name, CreateIcon::Code, "An entity running the script '" + name + "'", "Empty",
                [name](entt::registry& registry, const entt::entity entity, const CreateContext&)
                {
                    registry.emplace<GPP::ScriptsComponent>(entity, GPP::ScriptsComponent{{GPP::ScriptEntry{name, {}}}});
                },
                0.0, "script luau", {std::string(GPP::kScriptsComponentName)});
            entry.Id = "Script:" + name;
            entries.push_back(std::move(entry));
        }
        return entries;
    }

    [[nodiscard]] inline std::vector<CreateEntry> SupportedCreateEntries(
        std::vector<CreateEntry> entries, const std::function<bool(const std::string&)>& registered)
    {
        std::erase_if(entries, [&](const CreateEntry& entry)
        {
            return !std::ranges::all_of(entry.Requires, [&](const std::string& name) { return registered(name); });
        });
        return entries;
    }

    struct CreateNode
    {
        std::string Name;
        std::string Path;
        std::optional<std::size_t> Entry;
        std::vector<CreateNode> Children;

        [[nodiscard]] bool IsFolder() const noexcept { return !Entry.has_value(); }
    };

    namespace CreateDetail
    {
        inline CreateNode& FolderNode(CreateNode& root, const std::string_view path)
        {
            CreateNode* node = &root;
            std::string accumulated;
            for (const auto part : std::views::split(path, '/'))
            {
                const std::string_view name(part.begin(), part.end());
                if (name.empty()) continue;
                accumulated += accumulated.empty() ? std::string(name) : "/" + std::string(name);
                auto it = std::ranges::find_if(node->Children, [&](const CreateNode& child)
                                               { return child.IsFolder() && child.Name == name; });
                if (it == node->Children.end())
                {
                    node->Children.push_back(CreateNode{std::string(name), accumulated, std::nullopt, {}});
                    it = node->Children.end() - 1;
                }
                node = &*it;
            }
            return *node;
        }

        inline void FoldersFirst(CreateNode& node)
        {
            std::ranges::stable_partition(node.Children, [](const CreateNode& child) { return child.IsFolder(); });
            for (auto& child : node.Children) FoldersFirst(child);
        }
    }

    [[nodiscard]] inline CreateNode BuildCreateTree(const std::span<const CreateEntry> entries)
    {
        CreateNode root;
        for (std::size_t i = 0; i < entries.size(); ++i)
        {
            auto& folder = CreateDetail::FolderNode(root, entries[i].Folder);
            folder.Children.push_back(CreateNode{entries[i].Name, entries[i].Id, i, {}});
        }
        CreateDetail::FoldersFirst(root);
        return root;
    }

    [[nodiscard]] inline std::vector<std::size_t> FilterCreateEntries(const std::span<const CreateEntry> entries,
                                                                      const std::string_view query)
    {
        std::vector<std::pair<int, std::size_t>> scored;
        for (std::size_t i = 0; i < entries.size(); ++i)
        {
            if (query.find_first_not_of(" \t") == std::string_view::npos)
            {
                scored.emplace_back(0, i);
                continue;
            }
            const auto& entry = entries[i];
            const auto byName = FuzzyScore(query, entry.Name);
            const auto byPath = FuzzyScore(query, entry.Folder + " " + entry.Name + " " + entry.Keywords);
            if (!byName && !byPath) continue;
            scored.emplace_back(std::max(byName ? *byName + 60 : 0, byPath.value_or(0)), i);
        }
        std::ranges::stable_sort(scored, [](const auto& a, const auto& b) { return a.first > b.first; });
        std::vector<std::size_t> result;
        result.reserve(scored.size());
        for (const auto& [score, index] : scored) result.push_back(index);
        return result;
    }

    [[nodiscard]] inline std::string UniqueEntityName(const std::string_view base, const std::unordered_set<std::string>& existing)
    {
        if (!existing.contains(std::string(base))) return std::string(base);
        for (int n = 2;; ++n)
        {
            auto candidate = std::format("{} {}", base, n);
            if (!existing.contains(candidate)) return candidate;
        }
    }

    [[nodiscard]] inline double CreateDistanceM(const double sizeM, const double cameraSpeedMs)
    {
        constexpr double kBodyDiameters = 6.0;
        constexpr double kSecondsOfTravel = 2.0;
        constexpr double kMinimumM = 1.0;
        const double distance = sizeM > 0.0 ? sizeM * kBodyDiameters : cameraSpeedMs * kSecondsOfTravel;
        return std::max(distance, kMinimumM);
    }

    struct CreatePlacement
    {
        glm::dvec3 ViewPositionM{0.0};
        glm::vec3 ViewFront{0.0f, 0.0f, -1.0f};
        glm::vec3 ViewUp{0.0f, 1.0f, 0.0f};
        double CameraSpeedMs = 0.0;
        std::optional<glm::dvec3> At;
    };

    [[nodiscard]] inline glm::dvec3 CreatePosition(const CreateEntry& entry, const CreatePlacement& placement)
    {
        if (placement.At) return *placement.At;
        if (entry.AtView) return placement.ViewPositionM;
        return placement.ViewPositionM + glm::dvec3(placement.ViewFront) * CreateDistanceM(entry.SizeM, placement.CameraSpeedMs);
    }

    struct CreatedEntities
    {
        std::vector<GPP::Command> Commands;
        std::vector<std::uint64_t> Guids;
        std::string Label;
    };

    [[nodiscard]] inline std::optional<CreatedEntities> BuildCreateCommands(const GPP::Scene& reference, const CreateEntry& entry,
                                                                           const CreatePlacement& placement,
                                                                           const std::function<std::uint64_t()>& newGuid)
    {
        if (entry.Action != CreateAction::Entity || entry.Parts.empty()) return std::nullopt;

        std::unordered_set<std::string> names;
        for (auto [entity, metadata] : reference.Registry().view<const GPP::MetadataComponent>().each())
        {
            names.insert(metadata.Name);
        }
        CreateContext context;
        context.HasPrimaryCamera = std::ranges::any_of(
            reference.Registry().view<const GPP::CameraComponent>().each(),
            [](const auto& row) { return std::get<1>(row).Primary; });

        const auto anchor = CreatePosition(entry, placement);
        const std::optional<glm::quat> rotation =
            entry.AlongView ? std::optional<glm::quat>(GPP::LookAtRotation(glm::vec3(0.0f), placement.ViewFront, placement.ViewUp))
                            : std::nullopt;

        CreatedEntities result;
        for (const auto& part : entry.Parts)
        {
            const auto guid = newGuid();
            const auto name = UniqueEntityName(part.Name.empty() ? entry.Name : part.Name, names);
            names.insert(name);

            GPP::Scene scratch;
            const auto entity = scratch.CreateEntityWithGuid(guid, name, part.TypeTag);
            if (entity == entt::entity{entt::null}) return std::nullopt;
            auto& registry = scratch.Registry();
            registry.emplace<GPP::TransformComponent>(entity, GPP::TransformComponent{.Position = anchor + part.OffsetM});
            if (rotation) registry.get<GPP::TransformComponent>(entity).Rotation = *rotation;
            if (part.Build) part.Build(registry, entity, context);
            result.Commands.push_back(GPP::SpawnEntityCommand{guid, scratch.SerializeEntity(entity)});
            result.Guids.push_back(guid);
            if (part.TypeTag == "Camera") context.HasPrimaryCamera = true;
        }
        result.Label = "Create " + entry.Name;
        return result;
    }
}
