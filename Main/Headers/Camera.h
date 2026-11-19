#pragma once

namespace Main::Camera
{
    void InitializeWindowFocus(bool fixFullscreenTabOut, bool minimizeOnTabOut);
    void Initialize(bool disableIdleCamera, bool fixFullscreenTabOut, bool minimizeOnTabOut, bool disableHeadBob, bool reduceCameraShake, bool reduceCameraSway,
        bool disableDrunkFilter, bool disableEagleEyeFilter, bool disableDeadEyeFilter,
        bool disableWeaponWheelFilter, bool disableLowHealthFilter,
        bool reduceWeaponRecoil, float recoilShakeAmplitudeScale,
        bool reduceFirstPersonWeaponRecoil, float firstPersonRecoilShakeAmplitudeScale);
    bool NeedsFastUpdate();
    void Shutdown();
    void Update();
}
