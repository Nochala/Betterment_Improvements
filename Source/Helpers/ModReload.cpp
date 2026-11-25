#include "../../Headers/Helpers/ModReload.h"
#include "../../Headers/Modules.h"
#include "../../Headers/Audio.h"
#include "../../Headers/Camera.h"
#include "../../Headers/Display.h"
#include "../../Headers/Gunz.h"
#include "../../Headers/Horse.h"
#include "../../Headers/Lighting.h"
#include "../../Headers/Looting.h"
#include "../../Headers/Player.h"
#include "../../../Headers/GameplayFixes.h"
#include "../../../Headers/Logger.h"
#include <cwctype>

namespace Main::ModReload
{
    bool RequiresGameRestart(const wchar_t* section, const wchar_t* key)
    {
        if (!key)
            return false;
        const auto matches = [](const wchar_t* left, const wchar_t* right)
        {
            while (*left && *right)
            {
                if (std::towlower(*left) != std::towlower(*right))
                    return false;
                ++left;
                ++right;
            }
            return *left == *right;
        };
        if (section && matches(section, L"Engine"))
            return true;
        static constexpr const wchar_t* keys[] =
        {
            L"Enabled", L"TextureCrashFix", L"ExtendHeapLimits", L"ExtendPackfileLimits",
            L"ExtendPedMemoryLimits", L"ExtendStreamingPools", L"ExtendAssetPools",
            L"OptimizeStreamingMemory", L"TextureStreamingStutterFix", L"ImproveTextureStreaming",
            L"ExpandSSRCoverage", L"SSROverscanScale", L"SSROverscanTestMode",
            L"CrashDiagnostics", L"DumpNativeHashes", L"EnableDLSSAutoExposure",
            L"RemoveBlackBars", L"SkipIntroSplashes", L"SkipLegal", L"StraightToStoryMode"
        };
        for (const wchar_t* restartKey : keys)
            if (matches(key, restartKey))
                return true;
        return false;
    }

    bool Apply(const GameplayFixes::Settings& current, GameplayFixes::Settings& next)
    {
        bool restartRequired = false;
        const auto keepStartup = [&restartRequired](const auto& active, auto& incoming)
        {
            if (incoming != active)
            {
                restartRequired = true;
                incoming = active;
            }
        };
        keepStartup(current.enabled, next.enabled);
        keepStartup(current.textureCrashFix, next.textureCrashFix);
        keepStartup(current.extendHeapLimits, next.extendHeapLimits);
        keepStartup(current.extendPackfileLimits, next.extendPackfileLimits);
        keepStartup(current.extendPedMemoryLimits, next.extendPedMemoryLimits);
        keepStartup(current.crashDiagnostics, next.crashDiagnostics);
        keepStartup(current.extendStreamingPools, next.extendStreamingPools);
        keepStartup(current.optimizeStreamingMemory, next.optimizeStreamingMemory);
        keepStartup(current.extendAssetPools, next.extendAssetPools);
        keepStartup(current.textureStreamingStutterFix, next.textureStreamingStutterFix);
        keepStartup(current.improveTextureStreaming, next.improveTextureStreaming);
        keepStartup(current.expandSsrCoverage, next.expandSsrCoverage);
        keepStartup(current.ssrOverscanScale, next.ssrOverscanScale);
        keepStartup(current.ssrOverscanTestMode, next.ssrOverscanTestMode);
        keepStartup(current.enableDLSSAutoExposure, next.enableDLSSAutoExposure);
        keepStartup(current.skipIntroSplashes, next.skipIntroSplashes);
        keepStartup(current.skipLegal, next.skipLegal);
        keepStartup(current.straightToStoryMode, next.straightToStoryMode);
        keepStartup(current.removeBlackBars, next.removeBlackBars);

        if (current.noAutoAmmoPickup != next.noAutoAmmoPickup)
            Main::Looting::SetNoAutoAmmoPickup(next.enabled && next.noAutoAmmoPickup);

        if (current.disableAmbientMusic != next.disableAmbientMusic ||
            current.disableCombatMusic != next.disableCombatMusic ||
            current.disableWantedMusic != next.disableWantedMusic)
        {
            Main::Audio::Initialize(next.enabled && next.disableAmbientMusic,
                next.enabled && next.disableCombatMusic, next.enabled && next.disableWantedMusic);
        }
        if (current.disableInteriorLighting != next.disableInteriorLighting ||
            current.disableDistantLights != next.disableDistantLights)
        {
            Main::Lighting::Initialize(next.enabled && next.disableInteriorLighting,
                next.enabled && next.disableDistantLights);
        }
        if (current.disableIdleCamera != next.disableIdleCamera ||
            current.fixFullscreenTabOut != next.fixFullscreenTabOut ||
            current.minimizeOnTabOut != next.minimizeOnTabOut ||
            current.disableCameraSmoothing != next.disableCameraSmoothing ||
            current.disableHeadBob != next.disableHeadBob ||
            current.reduceCameraShake != next.reduceCameraShake ||
            current.reduceCameraSway != next.reduceCameraSway ||
            current.disableDrunkFilter != next.disableDrunkFilter ||
            current.disablePauseMenuFilter != next.disablePauseMenuFilter ||
            current.disableEagleEyeFilter != next.disableEagleEyeFilter ||
            current.disableDeadEyeFilter != next.disableDeadEyeFilter ||
            current.disableWeaponWheelFilter != next.disableWeaponWheelFilter ||
            current.disableLowHealthFilter != next.disableLowHealthFilter)
        {
            Main::Camera::Initialize(next.enabled && next.disableIdleCamera,
                next.enabled && next.fixFullscreenTabOut,
                next.enabled && next.minimizeOnTabOut,
                next.enabled && next.disableCameraSmoothing,
                next.enabled && next.disableHeadBob,
                next.enabled && next.reduceCameraShake,
                next.enabled && next.reduceCameraSway,
                next.enabled && next.disableDrunkFilter,
                next.enabled && next.disablePauseMenuFilter,
                next.enabled && next.disableEagleEyeFilter,
                next.enabled && next.disableDeadEyeFilter,
                next.enabled && next.disableWeaponWheelFilter,
                next.enabled && next.disableLowHealthFilter);
        }
        if (current.noAutoHorseEquip != next.noAutoHorseEquip ||
            current.noAutoHorseHolster != next.noAutoHorseHolster ||
            current.disableHorseAutoJump != next.disableHorseAutoJump ||
            current.disableHorseWhistleDistance != next.disableHorseWhistleDistance ||
            current.fixHorseFollowBehavior != next.fixHorseFollowBehavior)
        {
            Main::Horse::Initialize(next.enabled && next.noAutoHorseEquip,
                next.enabled && next.noAutoHorseHolster,
                next.enabled && next.disableHorseAutoJump,
                next.enabled && next.disableHorseWhistleDistance,
                next.enabled && next.fixHorseFollowBehavior);
        }
        if (current.disableFirstPersonAutoRun != next.disableFirstPersonAutoRun ||
            current.quickLootBodies != next.quickLootBodies ||
            current.quickLootAnimals != next.quickLootAnimals)
        {
            Main::Player::Initialize(next.enabled && next.disableFirstPersonAutoRun,
                next.enabled && next.quickLootBodies,
                next.enabled && next.quickLootAnimals);
        }
        if (current.disableReticleSpread != next.disableReticleSpread ||
            current.disableReticleSway != next.disableReticleSway ||
            current.disableWeaponDegradation != next.disableWeaponDegradation)
        {
            Main::Gunz::ReloadRuntimeFeatures(next.enabled && next.disableReticleSpread,
                next.enabled && next.disableReticleSway,
                next.enabled && next.disableWeaponDegradation);
        }
        if (current.enabled != next.enabled ||
            current.disableHonorSystem != next.disableHonorSystem ||
            current.disableBounties != next.disableBounties ||
            current.disableOutOfBoundSnipers != next.disableOutOfBoundSnipers ||
            current.disableHonorLossWhenMasked != next.disableHonorLossWhenMasked ||
            current.disableBountyWhenMasked != next.disableBountyWhenMasked ||
            current.disableBodySearchHonorLoss != next.disableBodySearchHonorLoss ||
            current.disableAntagonizeHonorLoss != next.disableAntagonizeHonorLoss ||
            current.fixSelfDefenseHonorLoss != next.fixSelfDefenseHonorLoss ||
            current.fixSelfDefenseBounties != next.fixSelfDefenseBounties)
            Main::ReloadGameplay(next);
        if (next.enabled && current.cameraFovOffset != next.cameraFovOffset)
            Main::Display::ApplyCameraFovOffset(next.cameraFovOffset);
        if (current.disableAutoReload != next.disableAutoReload ||
            current.disableDeadEyeAutoReload != next.disableDeadEyeAutoReload ||
            current.disableEmptyAutoReload != next.disableEmptyAutoReload ||
            current.disableArrowTrails != next.disableArrowTrails ||
            current.disableHitmarkers != next.disableHitmarkers ||
            current.disableReticleHighlighting != next.disableReticleHighlighting)
        {
            Main::Gunz::ApplyRuntimeToggles(next.enabled && next.disableAutoReload,
                next.enabled && next.disableDeadEyeAutoReload, next.enabled && next.disableEmptyAutoReload,
                next.enabled && next.disableArrowTrails, next.enabled && next.disableHitmarkers,
                next.enabled && next.disableReticleHighlighting);
        }
        if (current.disableForcedScope != next.disableForcedScope)
            Main::Gunz::ApplyForcedScope(next.enabled && next.disableForcedScope);
        Display::UpdateCompositeDithering(next.enabled && next.disableCompositeDithering);
        return restartRequired;
    }
}
