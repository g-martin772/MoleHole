#pragma once

#include "ParameterRegistry.h"

// Compile-time parameter handles for efficient lookup
// These use consteval ParameterHandle constructor to hash strings at compile-time
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

    // Rendering Parameters
    inline constexpr ParameterHandle RenderingFOV("Rendering.FOV");
    inline constexpr ParameterHandle RenderingMaxRaySteps("Rendering.MaxRaySteps");
    inline constexpr ParameterHandle RenderingRayStepSize("Rendering.RayStepSize");
    inline constexpr ParameterHandle RenderingDebugMode("Rendering.DebugMode");
    inline constexpr ParameterHandle RenderingPhysicsDebugEnabled("Rendering.PhysicsDebugEnabled");
    inline constexpr ParameterHandle RenderingPhysicsDebugDepthTest("Rendering.PhysicsDebugDepthTest");
    inline constexpr ParameterHandle RenderingPhysicsDebugScale("Rendering.PhysicsDebugScale");
    inline constexpr ParameterHandle RenderingPhysicsDebugFlags("Rendering.PhysicsDebugFlags");

    inline constexpr ParameterHandle RenderingBlackHolesEnabled("Rendering.BlackHolesEnabled");
    inline constexpr ParameterHandle RenderingGravitationalLensingEnabled("Rendering.GravitationalLensingEnabled");
    inline constexpr ParameterHandle RenderingGravitationalRedshiftEnabled("Rendering.GravitationalRedshiftEnabled");
    inline constexpr ParameterHandle RenderingAccretionDiskEnabled("Rendering.AccretionDiskEnabled");
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

    // Application Parameters
    inline constexpr ParameterHandle AppLastOpenScene("App.LastOpenScene");
    inline constexpr ParameterHandle AppRecentScenes("App.RecentScenes");
    inline constexpr ParameterHandle AppShowDemoWindow("App.ShowDemoWindow");
    inline constexpr ParameterHandle AppDebugMode("App.DebugMode");
    inline constexpr ParameterHandle AppUseKerrDistortion("App.UseKerrDistortion");

    // UI Parameters
    inline constexpr ParameterHandle UIFontSize("UI.FontSize");
    inline constexpr ParameterHandle UIMainFont("UI.MainFont");
    inline constexpr ParameterHandle UIDefaultExportPath("UI.DefaultExportPath");
}

