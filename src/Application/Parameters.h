#pragma once

#include "ParameterRegistry.h"

// These use consteval ParameterHandle constructor to hash strings at compile-time ;)

namespace Params {
    // Window Parameters
    inline constexpr ParameterHandle WindowWidth("Window.Width");
    inline constexpr ParameterHandle WindowHeight("Window.Height");
    inline constexpr ParameterHandle WindowPosX("Window.PosX");
    inline constexpr ParameterHandle WindowPosY("Window.PosY");
    inline constexpr ParameterHandle WindowMaximized("Window.Maximized");
    inline constexpr ParameterHandle WindowVSync("Window.VSync");

    // Camera Parameters
    inline constexpr ParameterHandle CameraPosition("Camera.Position");
    inline constexpr ParameterHandle CameraFront("Camera.Front");
    inline constexpr ParameterHandle CameraUp("Camera.Up");
    inline constexpr ParameterHandle CameraPitch("Camera.Pitch");
    inline constexpr ParameterHandle CameraYaw("Camera.Yaw");
    inline constexpr ParameterHandle CameraSpeed("Camera.Speed");
    inline constexpr ParameterHandle CameraMouseSensitivity("Camera.MouseSensitivity");
    inline constexpr ParameterHandle CameraObject("Camera.Object");

    // Rendering Parameters
    inline constexpr ParameterHandle RenderingFOV("Rendering.FOV");
    inline constexpr ParameterHandle RenderingThirdPerson("Rendering.ThirdPerson");
    inline constexpr ParameterHandle ThirdPersonDistance("Rendering.ThirdPersonDistance");
    inline constexpr ParameterHandle ThirdPersonHeight("Rendering.ThirdPersonHeight");
    inline constexpr ParameterHandle RenderingMaxRaySteps("Rendering.MaxRaySteps");
    inline constexpr ParameterHandle RenderingRayStepSize("Rendering.RayStepSize");
    inline constexpr ParameterHandle RenderingAdaptiveStepRate("Rendering.AdaptiveStepRate");
    inline constexpr ParameterHandle RenderingDebugMode("Rendering.DebugMode");
    inline constexpr ParameterHandle RenderingPhysicsDebugEnabled("Rendering.PhysicsDebugEnabled");
    inline constexpr ParameterHandle RenderingPhysicsDebugDepthTest("Rendering.PhysicsDebugDepthTest");
    inline constexpr ParameterHandle RenderingPhysicsDebugScale("Rendering.PhysicsDebugScale");
    inline constexpr ParameterHandle RenderingPhysicsDebugFlags("Rendering.PhysicsDebugFlags");

    inline constexpr ParameterHandle RenderingBlackHolesEnabled("Rendering.BlackHolesEnabled");
    inline constexpr ParameterHandle RenderingAccretionDiskEnabled("Rendering.AccretionDiskEnabled");
    inline constexpr ParameterHandle RenderingAccretionDiskVolumetric("Rendering.AccretionDiskVolumetric");
    inline constexpr ParameterHandle RenderingAccDiskHeight("Rendering.AccDiskHeight");
    inline constexpr ParameterHandle RenderingAccDiskNoiseScale("Rendering.AccDiskNoiseScale");
    inline constexpr ParameterHandle RenderingAccDiskNoiseLOD("Rendering.AccDiskNoiseLOD");
    inline constexpr ParameterHandle RenderingAccDiskSpeed("Rendering.AccDiskSpeed");
    inline constexpr ParameterHandle RenderingDopplerBeamingEnabled("Rendering.DopplerBeamingEnabled");
    inline constexpr ParameterHandle RenderingBloomEnabled("Rendering.BloomEnabled");
    inline constexpr ParameterHandle RenderingBloomThreshold("Rendering.BloomThreshold");
    inline constexpr ParameterHandle RenderingBloomBlurPasses("Rendering.BloomBlurPasses");
    inline constexpr ParameterHandle RenderingBloomIntensity("Rendering.BloomIntensity");
    inline constexpr ParameterHandle RenderingBloomDebug("Rendering.BloomDebug");
    inline constexpr ParameterHandle RenderingLensFlareEnabled("Rendering.LensFlareEnabled");
    inline constexpr ParameterHandle RenderingLensFlareIntensity("Rendering.LensFlareIntensity");
    inline constexpr ParameterHandle RenderingLensFlareThreshold("Rendering.LensFlareThreshold");
    inline constexpr ParameterHandle RenderingAntiAliasingEnabled("Rendering.AntiAliasingEnabled");

    // General Relativity Parameters
    inline constexpr ParameterHandle GRKerrPhysicsEnabled("GeneralRelativity.KerrPhysicsEnabled");
    inline constexpr ParameterHandle GRPhotonRingEnabled("GeneralRelativity.PhotonRingEnabled");
    inline constexpr ParameterHandle GRAngularMomentumEnabled("GeneralRelativity.AngularMomentumEnabled");
    inline constexpr ParameterHandle GRFrameDraggingEnabled("GeneralRelativity.FrameDraggingEnabled");
    inline constexpr ParameterHandle GRGravitationalLensingEnabled("GeneralRelativity.GravitationalLensingEnabled");
    inline constexpr ParameterHandle GRGravitationalRedshiftEnabled("GeneralRelativity.GravitationalRedshiftEnabled");

    // Application Parameters
    inline constexpr ParameterHandle AppLastOpenScene("App.LastOpenScene");
    inline constexpr ParameterHandle AppRecentScenes("App.RecentScenes");
    inline constexpr ParameterHandle AppShowDemoWindow("App.ShowDemoWindow");
    inline constexpr ParameterHandle AppDebugMode("App.DebugMode");
    inline constexpr ParameterHandle AppUseKerrDistortion("App.UseKerrDistortion");
    inline constexpr ParameterHandle AppIntroAnimationEnabled("App.IntroAnimationEnabled");
    inline constexpr ParameterHandle AppBackgroundImage("App.BackgroundImage");
    inline constexpr ParameterHandle AppTutorialCompleted("App.TutorialCompleted");

    // UI Parameters
    inline constexpr ParameterHandle UIFontSize("UI.FontSize");
    inline constexpr ParameterHandle UIMainFont("UI.MainFont");
    inline constexpr ParameterHandle UIDefaultExportPath("UI.DefaultExportPath");
    inline constexpr ParameterHandle UIViewportHUDEnabled("UI.ViewportHUDEnabled");

    // Config
    inline constexpr ParameterHandle AdditionalObjectClassSources("Config.AdditionalObjectClassSources");
    inline constexpr ParameterHandle AdditionalSceneObjectDefinitionSources("Config.AdditionalSceneObjectDefinitionSources");
}

namespace Field {
    namespace Entity {
        inline constexpr ParameterHandle Name("Entity.Name");
        inline constexpr ParameterHandle Position("Entity.Position");
        inline constexpr ParameterHandle Rotation("Entity.Rotation");
        inline constexpr ParameterHandle Scale("Entity.Scale");
    }

    namespace Physics {
        inline constexpr ParameterHandle Enable("Physics.Enable");
        inline constexpr ParameterHandle Mass("Physics.Mass");
        inline constexpr ParameterHandle Velocity("Physics.Velocity");
        inline constexpr ParameterHandle RigidbodyType("Physics.Rigidbody.Type"); // Static/Dynamic/Kinematic
        inline constexpr ParameterHandle RigidbodyUseGravity("Physics.Rigidbody.UseGravity");
        inline constexpr ParameterHandle RigidbodyLinearDamping("Physics.Rigidbody.LinearDamping");
        inline constexpr ParameterHandle RigidbodyAngularDamping("Physics.Rigidbody.AngularDamping");
        inline constexpr ParameterHandle RigidbodyConstraints("Physics.Rigidbody.Constraints"); // bitmask for freeze axes
        inline constexpr ParameterHandle ColliderType("Physics.Collider.Type"); // Box/Sphere/Capsule/Mesh/ConvexHull
        inline constexpr ParameterHandle ColliderIsTrigger("Physics.Collider.IsTrigger");
        inline constexpr ParameterHandle ColliderOffset("Physics.Collider.Offset");
        inline constexpr ParameterHandle ColliderScale("Physics.Collider.Scale");
        inline constexpr ParameterHandle ColliderFriction("Physics.Collider.Friction");
        inline constexpr ParameterHandle ColliderRestitution("Physics.Collider.Restitution");
        inline constexpr ParameterHandle ColliderDensity("Physics.Collider.Density");
        inline constexpr ParameterHandle ColliderCollisionLayer("Physics.Collider.CollisionLayer");
        inline constexpr ParameterHandle ColliderCollisionMask("Physics.Collider.CollisionMask");
        inline constexpr ParameterHandle ColliderBoxHalfExtents("Physics.Collider.Box.HalfExtents");
        inline constexpr ParameterHandle ColliderSphereRadius("Physics.Collider.Sphere.Radius");
        inline constexpr ParameterHandle ColliderCapsuleRadius("Physics.Collider.Capsule.Radius");
        inline constexpr ParameterHandle ColliderCapsuleHeight("Physics.Collider.Capsule.Height");
        inline constexpr ParameterHandle ColliderMeshFilePath("Physics.Collider.Mesh.FilePath");
        inline constexpr ParameterHandle ColliderConvexMeshFilePath("Physics.Collider.ConvexMesh.FilePath");
        inline constexpr ParameterHandle ColliderMeshAutoConvex("Physics.Collider.Mesh.AutoConvex"); // generate convex from mesh
    }

    namespace Mesh {
        inline constexpr ParameterHandle FilePath("Mesh.FilePath");
        inline constexpr ParameterHandle ComOffset("Mesh.ComOffset");
    }

    namespace Sphere {
        inline constexpr ParameterHandle Radius("Sphere.Radius");
        inline constexpr ParameterHandle Color("Sphere.Color");
        inline constexpr ParameterHandle TexturePath("Sphere.TexturePath");
        inline constexpr ParameterHandle Spin("Sphere.Spin");
        inline constexpr ParameterHandle SpinAxis("Sphere.SpinAxis");
        inline constexpr ParameterHandle Mass("Sphere.Mass");
    }

    namespace BlackHole {
        inline constexpr ParameterHandle Mass("Physics.Mass");
        inline constexpr ParameterHandle Spin("BlackHole.Spin");
        inline constexpr ParameterHandle SpinAxis("BlackHole.SpinAxis");
    }
}
