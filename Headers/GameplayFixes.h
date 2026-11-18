#pragma once

namespace GameplayFixes
{
    struct Settings
    {
        bool enabled = true;
        bool settingsMenu = true;
        bool notification = true;
        int menuKey = 0x77;
        unsigned int menuModifier = 0;
        bool textureCrashFix = true;
        bool extendHeapLimits = true;
        bool extendPackfileLimits = true;
        bool extendPedMemoryLimits = true;
        bool crashDiagnostics = false;
        bool extendStreamingPools = true;
        bool optimizeStreamingMemory = true;
        bool extendAssetPools = true;
        bool textureStreamingStutterFix = false;
        bool improveTextureStreaming = true;
        bool noAutoHorseEquip = true;
        bool noAutoHorseHolster = true;
        bool disableHorseAutoJump = false;
        bool disableHorseWhistleDistance = false;
        bool fixHorseFollowBehavior = false;
        bool noAutoAmmoPickup = false;
        bool disableAutoReload = true;
        bool disableDeadEyeAutoReload = true;
        bool disableEmptyAutoReload = true;
        bool disableArrowTrails = false;
        bool disableReticleSpread = false;
        bool disableReticleSway = false;
        bool disableForcedScope = true;
        bool disableWeaponDegradation = false;
        bool disableHitmarkers = false;
        bool disableReticleHighlighting = false;
        bool disableFirstPersonAutoRun = false;
        bool quickLootBodies = false;
        bool quickLootAnimals = false;
        bool disableHonorLossWhenMasked = true;
        bool disableBountyWhenMasked = true;
        bool disableBounties = false;
        bool fixSelfDefenseHonorLoss = true;
        bool fixSelfDefenseBounties = true;
        bool disableHonorSystem = false;
        bool disableBodySearchHonorLoss = true;
        bool disableAntagonizeHonorLoss = false;
        bool disableOutOfBoundSnipers = false;
        bool skipIntroSplashes = true;
        bool skipLegal = true;
        bool straightToStoryMode = false;
        bool removeBlackBars = false;
        float cameraFovOffset = 0.0f;
        bool expandSsrCoverage = false;
        float ssrOverscanScale = 1.25f;
        int ssrOverscanTestMode = 0;
        bool enableDLSSAutoExposure = false;
        bool disableInteriorLighting = false;
        bool disableDistantLights = false;
        bool disableIdleCamera = false;
        bool fixFullscreenTabOut = false;
        bool minimizeOnTabOut = false;
        bool disableDrunkFilter = false;
        bool disableEagleEyeFilter = false;
        bool disableDeadEyeFilter = false;
        bool disablePauseMenuFilter = false;
        bool disableWeaponWheelFilter = false;
        bool disableLowHealthFilter = false;
        bool disableHeadBob = false;
        bool reduceCameraShake = false;
        bool reduceCameraSway = false;
        bool reduceWeaponRecoil = false;
        float recoilShakeAmplitudeScale = 0.5f;
        bool reduceFirstPersonWeaponRecoil = false;
        float firstPersonRecoilShakeAmplitudeScale = 0.5f;
        bool disableAmbientMusic = false;
        bool disableCombatMusic = false;
        bool disableWantedMusic = false;
    };

    void InitializeEarly();
    void Initialize();
    bool Reload(bool* restartRequired = nullptr);
    void Update();
    unsigned int GetUpdateDelay();
}
