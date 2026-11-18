#pragma once

namespace Main
{
    namespace Gunz
    {
        void Initialize(bool disableAutoReload, bool disableDeadEyeAutoReload, bool disableEmptyAutoReload,
            bool disableArrowTrails, bool disableReticleSpread, bool disableReticleSway, bool disableForcedScope,
            bool disableWeaponDegradation, bool disableHitmarkers, bool disableReticleHighlighting);
        void ReloadRuntimeFeatures(bool disableReticleSpread, bool disableReticleSway, bool disableWeaponDegradation);
        void Update();
        void Shutdown();
    }
}
