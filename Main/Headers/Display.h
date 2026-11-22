#pragma once

namespace Main::Display
{
    void UpdateTimecycleEffects(bool vignetteEnabled, bool bloomEnabled, bool chromaticAberrationEnabled);
    void UpdateCompositeDithering(bool enabled);
    void InitializeOverscanFov(bool modEnabled, bool enabled, float scale, int testMode, float cameraFovOffset);
    void ApplyCameraFovOffset(float offset);
    void ReportCameraStartupStatus();
    void UpdateOverscanDiagnostics();
    void ShutdownCamera();
}
