#pragma once

namespace Main
{
    namespace Gunz
    {
        void Initialize(bool enabled, bool disableAutoReload, bool disableDeadEyeAutoReload, bool disableEmptyAutoReload,
            bool disableArrowTrails, bool disableReticleSpread, bool disableReticleSway, bool disableForcedScope,
            bool disableWeaponDegradation, bool disableHitmarkers, bool disableReticleHighlighting);
        void ApplyForcedScope(bool enabled);
        void ApplyRuntimeToggles(bool disableAutoReload, bool disableDeadEyeAutoReload, bool disableEmptyAutoReload,
            bool disableArrowTrails, bool disableHitmarkers, bool disableReticleHighlighting);
        void ReloadRuntimeFeatures(bool disableReticleSpread, bool disableReticleSway, bool disableWeaponDegradation);
        void Update();
        void Shutdown();
    }
}
