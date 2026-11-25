#pragma once

namespace Main::Camera
{
    void InitializeWindowFocus(bool fixFullscreenTabOut, bool minimizeOnTabOut);
    void Initialize(bool disableIdleCamera, bool fixFullscreenTabOut, bool minimizeOnTabOut, bool disableCameraSmoothing, bool disableHeadBob, bool reduceCameraShake, bool reduceCameraSway,
        bool disableDrunkFilter, bool disablePauseMenuFilter, bool disableEagleEyeFilter, bool disableDeadEyeFilter,
        bool disableWeaponWheelFilter, bool disableLowHealthFilter);
    bool NeedsFastUpdate();
    bool IsRefreshRateCappedByVsync(int fps);
    void Shutdown();
    void Update();
}
