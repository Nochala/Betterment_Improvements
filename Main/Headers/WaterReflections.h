#pragma once

namespace Main
{
namespace WaterReflections
{
    void ReportStartupStatus();
    void UpdateOverscanDiagnostics();
    void InitializeOverscanFov(bool enabled, float scale, int testMode, float cameraFovOffset);
    void Shutdown();
}
}
