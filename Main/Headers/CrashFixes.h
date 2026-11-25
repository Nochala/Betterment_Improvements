#pragma once

namespace Main::CrashFixes
{
    void Initialize(bool textureCrashFix);
    void ExtendHeapLimits(bool enabled);
    void ExtendPackfileLimits(bool enabled);
    void ExtendPedMemoryLimits(bool enabled);
    void ObservePedMemoryLimits();
    void ShutdownPackfileLimits();
    void InitializeCrashDiagnostics(bool enabled);
    void RecordCrashDiagnosticsHeartbeat();
    void ReportCrashDiagnosticsStatus();
    void ShutdownCrashDiagnostics();
    void InitializeTextureStreamingStutterFix(bool enabled);
    void RestoreTextureStreamingStutterFix();
    void InitializeImproveTextureStreaming(bool enabled);
    void ReportImproveTextureStreamingStatus();
    void ShutdownImproveTextureStreaming();
    void ExtendWritableResourceSlots(bool enabled);
    void ReportWritableResourceSlots();
    void ExtendStreamingPools(bool enabled, bool extendAssets, bool optimizeStreamingMemory);
    void ObserveStreamingPoolStartup();
    void ReportPoolExtensionStatus();
    void ShutdownStreamingPoolOverride();
}
