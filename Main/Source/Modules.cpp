#include "Modules.h"
#include "Audio.h"
#include "../../Headers/GameplayFixes.h"
#include "SkipIntro.h"
#include "RemoveBlackBars.h"
#include "Lighting.h"
#include "Camera.h"
#include "CrashFixes.h"
#include "Horse.h"
#include "Player.h"
#include "Looting.h"
#include "Gunz.h"
#include "WaterReflections.h"
#include "../Headers/Display.h"
#include "DLSS.h"
#include "MissionState.h"
#include "SelfDefense.h"
#include "../../Headers/Logger.h"
#include "main.h"
#include <windows.h>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{
    constexpr std::uint64_t playerPedId = 0x096275889B8E0EE0ULL;
    constexpr std::uint64_t playerId = 0x217E9DC48139933DULL;
    constexpr std::uint64_t getShopComponent = 0x77BA37622E22023BULL;
    constexpr std::uint64_t getShopComponentCategory = 0x5FF9A878C3D115B8ULL;
    constexpr std::uint64_t getBounty = 0x54310AAB97B92816ULL;
    constexpr std::uint64_t setBounty = 0x093A9D1F72DF0D19ULL;
    constexpr std::uint64_t lawIncidentActive = 0xAD401C63158ACBAAULL;
    constexpr std::uint64_t witnessesActive = 0x69E181772886F48BULL;
    constexpr std::uint64_t witnessesPending = 0x0BB6DE7D23C60626ULL;
    constexpr std::uint64_t statIdGetInt = 0x767FBC2AC802EF3EULL;
    constexpr std::uint64_t statIdSetInt = 0xA4DDF5DF95E65EEEULL;
    constexpr std::uint64_t clearFeedChannel = 0xDD1232B332CBB9E7ULL;
    constexpr std::uint64_t getLoadingScreenActive = 0x71D4BF5890659B0CULL;
    constexpr std::uint64_t isScreenFadedIn = 0x37F9A426FBCF4AF2ULL;
    constexpr std::uint64_t isPlayerPlaying = 0xBFFB35986CAAE58CULL;
    constexpr std::uint64_t getGameTimer = 0x4F67E8ECA7D3F667ULL;
    constexpr std::uint32_t honorCurrentHash = 0x89587B4E;
    constexpr std::uint32_t neckwearCategory = 0x5FC29285;
    constexpr std::uint32_t masksCategory = 0x7505EF42;
    constexpr std::uint32_t largeMasksCategory = 0x4A73515C;
    constexpr std::uint32_t raisedBandanaState = 0x92F1FC1A;
    constexpr ULONGLONG maskCheckIntervalMs = 125;
    constexpr ULONGLONG recentLawIncidentMs = 30000;
    constexpr ULONGLONG missionBountyGraceMs = 3000;
    constexpr ULONGLONG bountySystemRecentIncidentMs = 30000;
    constexpr ULONGLONG bountySystemLoadGapMs = 3000;
    constexpr ULONGLONG honorStatRecheckMs = 250;
    constexpr ULONGLONG honorStatRecheckWindowMs = 2000;
    constexpr std::uint64_t allHonorEventFlags = 0x7FFFE7ULL;
    constexpr ULONGLONG honorSystemStatCheckMs = 2000;
    constexpr ULONGLONG honorSystemLoadGraceMs = 750;
    constexpr ULONGLONG honorSystemGapMs = 3000;
    constexpr std::uint64_t searchHonorEventFlag = 1ULL;
    constexpr std::uint64_t antagonizeHonorEventFlag = 65536ULL;
    bool disableHonorSystem = false;
    bool honorSystemLoading = false;
    bool honorSystemCaptured = false;
    std::uint32_t honorSystemPlayerPed = 0;
    std::uint32_t honorSystemLastGameTimer = 0;
    std::int32_t honorSystemBaseline = 0;
    ULONGLONG honorSystemLastUpdateAt = 0;
    ULONGLONG honorSystemLoadingEndedAt = 0;
    ULONGLONG honorSystemNextStatCheckAt = 0;
    ULONGLONG honorSystemNextCorrectionLogAt = 0;
    bool disableBodySearchHonorLoss = false;
    bool disableAntagonizeHonorLoss = false;
    std::uint64_t ownedHonorEventFlags = 0;
    std::uint64_t lastLoggedHonorEventTarget = 0;
    bool lastLoggedHonorEventMission = false;
    bool hasLoggedHonorEventState = false;

    bool disableBounties = false;
    constexpr std::uint64_t isPedDeadOrDying = 0x3317DEDB88C95038ULL;
    constexpr std::uint64_t getEntityCoords = 0xA86D5F069399F44DULL;
    constexpr std::uint64_t getMapZoneAtCoords = 0x43AD8FC02B429D33ULL;
    constexpr std::uint64_t isScreenFadingOut = 0x02F39BEFE7B88D00ULL;
    constexpr std::uint64_t isScreenFadedOut = 0xF5472C80DF2FF847ULL;
    constexpr ULONGLONG sniperDeathGraceMs = 3000;
    constexpr ULONGLONG sniperDeathFallbackMs = 8000;
    constexpr ULONGLONG sniperGlobalReadyGraceMs = 5000;
    constexpr ULONGLONG outOfBoundsFlagUpdateIntervalMs = 250;
    constexpr std::int32_t outOfBoundsSniperGlobalIndex = 1572867;
    constexpr std::uint64_t outOfBoundsSniperFlag = 128ULL;
    bool disableOutOfBoundSnipers = false;
    bool sniperGlobalFlagLogged = false;
    bool sniperGlobalDeferredLogged = false;
    bool sniperFlagOwned = false;
    std::uint64_t* sniperOwnedFlagAddress = nullptr;
    ULONGLONG nextOutOfBoundsUpdateAt = 0;
    ULONGLONG storyModeReadySince = 0;
    ULONGLONG sniperDeathBeganAt = 0;
    bool sniperDeathSuspendedLogged = false;
    bool sniperRegionLogged = false;
    bool sniperZoneSampleLogged = false;
    bool sniperRegionProbeFailureLogged = false;
    enum class SniperRegionResult { Unknown, Outside, Inside };
    std::uint32_t GetPlayerPed();
    int GetPlayer();

    bool IsReadableMemory(const void* memory, std::size_t size, bool writable = false)
    {
        if (!memory || size == 0)
            return false;
        MEMORY_BASIC_INFORMATION info = {};
        if (VirtualQuery(memory, &info, sizeof(info)) != sizeof(info) || info.State != MEM_COMMIT ||
            (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
            return false;
        const DWORD protection = info.Protect & 0xFF;
        if (protection != PAGE_READONLY && protection != PAGE_READWRITE && protection != PAGE_WRITECOPY &&
            protection != PAGE_EXECUTE && protection != PAGE_EXECUTE_READ &&
            protection != PAGE_EXECUTE_READWRITE && protection != PAGE_EXECUTE_WRITECOPY)
            return false;
        if (writable && protection != PAGE_READWRITE && protection != PAGE_WRITECOPY &&
            protection != PAGE_EXECUTE_READWRITE && protection != PAGE_EXECUTE_WRITECOPY)
            return false;
        const std::uintptr_t start = reinterpret_cast<std::uintptr_t>(memory);
        const std::uintptr_t regionEnd = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
        return start <= regionEnd && size <= regionEnd - start;
    }

    void RestoreOutOfBoundsSniperFlag()
    {
        if (!sniperFlagOwned)
            return;
        auto* flags = getGlobalPtr(outOfBoundsSniperGlobalIndex);
        if (flags && flags == sniperOwnedFlagAddress && IsReadableMemory(flags, sizeof(*flags), true))
        {
            if ((*flags & outOfBoundsSniperFlag) != 0)
            {
                *flags &= ~outOfBoundsSniperFlag;
                Logger::Debug("DisableOutOfBoundSnipers: owned sniper flag restored for gameplay transition.");
            }
        }
        else
        {
            Logger::Debug("DisableOutOfBoundSnipers: global storage changed or unavailable; no cleanup write attempted.");
        }
        sniperFlagOwned = false;
        sniperOwnedFlagAddress = nullptr;
    }

    SniperRegionResult IsOutOfBoundsSniperRegion(std::uint32_t ped)
    {
        nativeInit(getEntityCoords);
        nativePush64(ped);
        nativePush64(1);
        nativePush64(1);
        struct Position { alignas(8) float x; alignas(8) float y; alignas(8) float z; } position = {};
        static_assert(sizeof(Position) == 24, "");
        const auto* coordinates = nativeCall();
        if (!coordinates)
            return SniperRegionResult::Unknown;
        std::memcpy(&position, coordinates, sizeof(position));
        if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z))
            return SniperRegionResult::Unknown;
        const auto pushFloat = [](float value)
        {
            std::uint64_t argument = 0;
            std::memcpy(&argument, &value, sizeof(value));
            nativePush64(argument);
        };
        nativeInit(getMapZoneAtCoords);
        pushFloat(position.x);
        pushFloat(position.y);
        pushFloat(position.z);
        nativePush64(0);
        const auto* stateResult = nativeCall();
        if (!stateResult)
            return SniperRegionResult::Unknown;
        const std::uint32_t state = static_cast<std::uint32_t>(*stateResult);
        if (state == 0x79E6A26BU || state == 0x7356B8EDU)
            return SniperRegionResult::Inside;
        nativeInit(getMapZoneAtCoords);
        pushFloat(position.x);
        pushFloat(position.y);
        pushFloat(position.z);
        nativePush64(10);
        const auto* districtResult = nativeCall();
        if (!districtResult)
            return SniperRegionResult::Unknown;
        const std::uint32_t district = static_cast<std::uint32_t>(*districtResult);
        if (!sniperZoneSampleLogged && Logger::IsDebugEnabled())
        {
            sniperZoneSampleLogged = true;
            char message[256] = {};
            std::snprintf(message, sizeof(message),
                "[DEBUG] DisableOutOfBoundSnipers: zone probe coords=(%.1f, %.1f, %.1f), state=0x%08X, district=0x%08X.",
                position.x, position.y, position.z, state, district);
            Logger::Log(message);
        }
        const bool restricted = district == 0xF9831C72U || district == 0x84D7AD0EU ||
            district == 0x35390B10U || district == 0x8016C23FU || district == 0xE1736CD7U;
        return restricted ? SniperRegionResult::Inside : SniperRegionResult::Outside;
    }

    void UpdateOutOfBoundsFeatures()
    {
        if (!disableOutOfBoundSnipers)
            return;
        const ULONGLONG now = GetTickCount64();
        if (now < nextOutOfBoundsUpdateAt)
            return;
        nextOutOfBoundsUpdateAt = now + outOfBoundsFlagUpdateIntervalMs;

        nativeInit(getLoadingScreenActive);
        const bool loading = static_cast<std::uint32_t>(*nativeCall()) != 0;
        const std::uint32_t ped = loading ? 0 : GetPlayerPed();
        bool dead = false;
        bool visible = false;
        bool playing = false;
        if (ped != 0)
        {
            nativeInit(isPedDeadOrDying);
            nativePush64(ped);
            nativePush64(1);
            dead = static_cast<std::uint32_t>(*nativeCall()) != 0;
        }
        if (dead)
        {
            if (sniperDeathBeganAt == 0)
            {
                sniperDeathBeganAt = now;
                storyModeReadySince = 0;
            }
            if (!sniperDeathSuspendedLogged && Logger::IsDebugEnabled())
            {
                sniperDeathSuspendedLogged = true;
                Logger::Log("[DEBUG] DisableOutOfBoundSnipers: death started; preserving sniper state until the death transition advances.");
            }
            nativeInit(isScreenFadingOut);
            const bool fadingOut = static_cast<std::uint32_t>(*nativeCall()) != 0;
            nativeInit(isScreenFadedOut);
            const bool fadedOut = static_cast<std::uint32_t>(*nativeCall()) != 0;
            const ULONGLONG deathElapsed = now - sniperDeathBeganAt;
            if (!loading && deathElapsed < sniperDeathFallbackMs &&
                !(deathElapsed >= sniperDeathGraceMs && (fadingOut || fadedOut)))
                return;
        }
        else
        {
            sniperDeathBeganAt = 0;
            sniperDeathSuspendedLogged = false;
        }

        if (!loading && ped != 0 && !dead)
        {
            nativeInit(isScreenFadedIn);
            visible = static_cast<std::uint32_t>(*nativeCall()) != 0;
            const int player = GetPlayer();
            nativeInit(isPlayerPlaying);
            nativePush64(static_cast<std::uint64_t>(player));
            playing = static_cast<std::uint32_t>(*nativeCall()) != 0;
        }
        if (loading || ped == 0 || dead || !visible || !playing)
        {
            if (storyModeReadySince != 0 && Logger::IsDebugEnabled())
            {
                char message[192] = {};
                std::snprintf(message, sizeof(message),
                    "[DEBUG] DisableOutOfBoundSnipers: gameplay suspended (loading=%u, ped=%u, dead=%u, visible=%u, playing=%u).",
                    static_cast<unsigned>(loading), ped, static_cast<unsigned>(dead),
                    static_cast<unsigned>(visible), static_cast<unsigned>(playing));
                Logger::Log(message);
            }
            RestoreOutOfBoundsSniperFlag();
            sniperRegionLogged = false;
            storyModeReadySince = 0;
            return;
        }
        if (storyModeReadySince == 0)
        {
            storyModeReadySince = now;
            if (!sniperGlobalDeferredLogged && Logger::IsDebugEnabled())
            {
                sniperGlobalDeferredLogged = true;
                Logger::Log("[DEBUG] DisableOutOfBoundSnipers: sniper flag deferred until visible, active Story Mode gameplay.");
            }
            return;
        }
        if (now - storyModeReadySince < sniperGlobalReadyGraceMs)
            return;

        const SniperRegionResult region = IsOutOfBoundsSniperRegion(ped);
        if (region == SniperRegionResult::Unknown)
        {
            if (!sniperRegionProbeFailureLogged && Logger::IsDebugEnabled())
            {
                sniperRegionProbeFailureLogged = true;
                Logger::Log("[DEBUG] DisableOutOfBoundSnipers: zone probe unavailable; preserving existing sniper state.");
            }
            return;
        }
        sniperRegionProbeFailureLogged = false;
        if (region == SniperRegionResult::Outside)
        {
            if (sniperRegionLogged && Logger::IsDebugEnabled())
                Logger::Log("[DEBUG] DisableOutOfBoundSnipers: player left a restricted region; restoring sniper flag.");
            sniperRegionLogged = false;
            RestoreOutOfBoundsSniperFlag();
            return;
        }
        if (!sniperRegionLogged && Logger::IsDebugEnabled())
        {
            sniperRegionLogged = true;
            Logger::Log("[DEBUG] DisableOutOfBoundSnipers: player entered New Austin or Guarma; sniper suppression eligible.");
        }
        auto* flags = getGlobalPtr(outOfBoundsSniperGlobalIndex);
        if (flags && IsReadableMemory(flags, sizeof(*flags), true))
        {
            if (sniperFlagOwned && flags != sniperOwnedFlagAddress)
            {
                sniperFlagOwned = false;
                sniperOwnedFlagAddress = nullptr;
                Logger::Debug("DisableOutOfBoundSnipers: global storage changed; previous flag ownership discarded.");
            }
            if ((*flags & outOfBoundsSniperFlag) == 0)
            {
                *flags |= outOfBoundsSniperFlag;
                sniperFlagOwned = (*flags & outOfBoundsSniperFlag) != 0;
                sniperOwnedFlagAddress = sniperFlagOwned ? flags : nullptr;
                if (sniperFlagOwned && Logger::IsDebugEnabled())
                    Logger::Log("[DEBUG] DisableOutOfBoundSnipers: sniper flag acquired by BettermentImprovements.");
            }
            if (!sniperGlobalFlagLogged && (*flags & outOfBoundsSniperFlag) != 0)
            {
                sniperGlobalFlagLogged = true;
                Logger::Log("[INFO] DisableOutOfBoundSnipers: sniper blocked in restricted area.");
            }
        }
    }

    bool bountySystemCaptured = false;
    bool bountySystemSuspended = false;
    bool bountySystemLoading = false;
    std::uint32_t bountySystemPlayerPed = 0;
    std::uint32_t bountySystemLastGameTimer = 0;
    int bountySystemBaseline = 0;
    int bountySystemLastCorrection = -1;
    ULONGLONG bountySystemLastUpdateAt = 0;
    ULONGLONG bountySystemRecentLawIncidentAt = 0;
    ULONGLONG bountySystemLastCorrectionLogAt = 0;

    bool preventHonorLoss = false;
    bool preventBounty = false;
    bool wearingMask = false;
    bool honorCaptured = false;
    bool bountyProtected = false;
    bool bountyMissionSuspended = false;
    std::uint32_t currentPed = 0;
    std::int32_t protectedHonor = 0;
    int protectedBounty = 0;
    int lastBountyCorrection = -1;
    ULONGLONG lastBountyCorrectionLogAt = 0;
    ULONGLONG nextMaskCheckAt = 0;
    ULONGLONG recentLawIncidentAt = 0;
    ULONGLONG nextHonorStatCheckAt = 0;
    ULONGLONG honorStatCheckUntil = 0;

    std::uint32_t GetPlayerPed()
    {
        nativeInit(playerPedId);
        return static_cast<std::uint32_t>(*nativeCall());
    }

    int GetPlayer()
    {
        nativeInit(playerId);
        return static_cast<int>(*nativeCall());
    }

    bool ReadHonorStatistic(std::int32_t& value)
    {
        alignas(8) std::uint32_t statId[2] = { honorCurrentHash, 0 };
        std::int32_t result = 0;
        nativeInit(statIdGetInt);
        nativePush64(reinterpret_cast<std::uint64_t>(statId));
        nativePush64(reinterpret_cast<std::uint64_t>(&result));
        if (static_cast<std::uint32_t>(*nativeCall()) == 0)
            return false;
        value = result;
        return true;
    }

    bool WriteHonorStatistic(std::int32_t value)
    {
        alignas(8) std::uint32_t statId[2] = { honorCurrentHash, 0 };
        nativeInit(statIdSetInt);
        nativePush64(reinterpret_cast<std::uint64_t>(statId));
        nativePush64(static_cast<std::uint64_t>(static_cast<std::int64_t>(value)));
        nativePush64(1);
        return static_cast<std::uint32_t>(*nativeCall()) != 0;
    }

    int GetCurrentBounty(int player)
    {
        nativeInit(getBounty);
        nativePush64(static_cast<std::uint64_t>(player));
        return static_cast<int>(*nativeCall());
    }

    bool HasLawIncident(int player)
    {
        nativeInit(lawIncidentActive);
        nativePush64(static_cast<std::uint64_t>(player));
        if (static_cast<std::uint32_t>(*nativeCall()) != 0)
            return true;

        nativeInit(witnessesActive);
        nativePush64(static_cast<std::uint64_t>(player));
        if (static_cast<std::uint32_t>(*nativeCall()) != 0)
            return true;

        nativeInit(witnessesPending);
        nativePush64(static_cast<std::uint64_t>(player));
        return static_cast<std::uint32_t>(*nativeCall()) != 0;
    }

    bool IsFaceCovered(std::uint32_t ped)
    {
        for (int index = 0; index < 40; ++index)
        {
            std::uint32_t status = 0;
            std::uint32_t wearableState = 0;
            nativeInit(getShopComponent);
            nativePush64(ped);
            nativePush64(index);
            nativePush64(1);
            nativePush64(reinterpret_cast<std::uint64_t>(&status));
            nativePush64(reinterpret_cast<std::uint64_t>(&wearableState));
            const std::uint32_t component = static_cast<std::uint32_t>(*nativeCall());
            if (component == 0)
                continue;

            nativeInit(getShopComponentCategory);
            nativePush64(component);
            nativePush64(0);
            nativePush64(0);
            const std::uint32_t category = static_cast<std::uint32_t>(*nativeCall());
            if (category == masksCategory || category == largeMasksCategory ||
                (category == neckwearCategory && wearableState == raisedBandanaState))
                return true;
        }
        return false;
    }

    void ResetHonorSystemCapture()
    {
        honorSystemCaptured = false;
        honorSystemPlayerPed = 0;
        honorSystemLastGameTimer = 0;
        honorSystemNextStatCheckAt = 0;
        honorSystemNextCorrectionLogAt = 0;
    }

    void UpdateDisabledHonorSystem()
    {
        if (!disableHonorSystem)
            return;

        const ULONGLONG now = GetTickCount64();
        nativeInit(getLoadingScreenActive);
        if (static_cast<std::uint32_t>(*nativeCall()) != 0)
        {
            if (!honorSystemLoading)
                ResetHonorSystemCapture();
            honorSystemLoading = true;
            honorSystemLastUpdateAt = now;
            return;
        }
        if (honorSystemLoading)
        {
            honorSystemLoading = false;
            honorSystemLoadingEndedAt = now;
            ResetHonorSystemCapture();
        }
        if (honorSystemLoadingEndedAt != 0 && now - honorSystemLoadingEndedAt < honorSystemLoadGraceMs)
            return;

        const std::uint32_t ped = GetPlayerPed();
        if (ped == 0)
        {
            ResetHonorSystemCapture();
            honorSystemLastUpdateAt = now;
            return;
        }

        nativeInit(getGameTimer);
        const std::uint32_t gameTime = static_cast<std::uint32_t>(*nativeCall());
        if (ped != honorSystemPlayerPed ||
            (honorSystemLastUpdateAt != 0 && now - honorSystemLastUpdateAt > honorSystemGapMs) ||
            (honorSystemCaptured && gameTime < honorSystemLastGameTimer &&
                honorSystemLastGameTimer - gameTime > 1000))
            ResetHonorSystemCapture();

        honorSystemPlayerPed = ped;
        honorSystemLastGameTimer = gameTime;
        honorSystemLastUpdateAt = now;
        auto* honor = getGlobalPtr(0x2BA2);
        if (!honor)
            return;

        const std::int32_t current = static_cast<std::int32_t>(*honor);
        if (current < -1000 || current > 1000)
            return;

        if (!honorSystemCaptured)
        {
            honorSystemBaseline = current;
            honorSystemCaptured = true;
            honorSystemNextStatCheckAt = now + honorSystemStatCheckMs;
            if (Logger::IsDebugEnabled())
            {
                char message[110];
                std::snprintf(message, sizeof(message),
                    "[DEBUG] DisableHonorSystem: captured honor=%d.", honorSystemBaseline);
                Logger::Log(message);
            }
            return;
        }

        if (current != honorSystemBaseline)
        {
            *honor = static_cast<std::uint64_t>(static_cast<std::int64_t>(honorSystemBaseline));
            nativeInit(clearFeedChannel);
            nativePush64(7);
            nativePush64(1);
            nativePush64(0);
            nativeCall();
            WriteHonorStatistic(honorSystemBaseline);
            honorSystemNextStatCheckAt = now + honorSystemStatCheckMs;
            if (Logger::IsDebugEnabled() && now >= honorSystemNextCorrectionLogAt)
            {
                char message[140];
                std::snprintf(message, sizeof(message),
                    "[DEBUG] DisableHonorSystem: honor change %d -> %d blocked.",
                    current, honorSystemBaseline);
                Logger::Log(message);
                honorSystemNextCorrectionLogAt = now + honorSystemStatCheckMs;
            }
        }
        else if (now >= honorSystemNextStatCheckAt)
        {
            std::int32_t stored = 0;
            if (ReadHonorStatistic(stored) && stored >= -1000 && stored <= 1000 &&
                stored != honorSystemBaseline)
                WriteHonorStatistic(honorSystemBaseline);
            honorSystemNextStatCheckAt = now + honorSystemStatCheckMs;
        }
    }

    void UpdateHonorEventFilters()
    {
        if (!disableHonorSystem && !disableBodySearchHonorLoss && !disableAntagonizeHonorLoss && ownedHonorEventFlags == 0)
            return;

        auto* flags = getGlobalPtr(36616);
        if (!flags)
            return;

        if (disableHonorSystem && honorSystemLoading)
            return;
        const bool mission = !disableHonorSystem && Main::MissionState::IsActiveOrRecentlyEnded(3000);
        const std::uint64_t wanted = disableHonorSystem ? allHonorEventFlags :
            (mission ? 0ULL :
                (disableBodySearchHonorLoss ? searchHonorEventFlag : 0ULL) |
                (disableAntagonizeHonorLoss ? antagonizeHonorEventFlag : 0ULL));
        const std::uint64_t current = *flags;
        ownedHonorEventFlags &= current;
        const std::uint64_t remove = ownedHonorEventFlags & ~wanted;
        const std::uint64_t missing = wanted & ~current;
        std::uint64_t applied = current;
        if (remove != 0 || missing != 0)
        {
            *flags = (current & ~remove) | missing;
            applied = *flags;
        }
        ownedHonorEventFlags = (ownedHonorEventFlags | missing) & applied;

        if ((!hasLoggedHonorEventState || lastLoggedHonorEventTarget != wanted ||
            lastLoggedHonorEventMission != mission) && Logger::IsDebugEnabled())
        {
            char message[160];
            std::snprintf(message, sizeof(message),
                "[DEBUG] HonorEvents: full=%d, search=%d, antagonize=%d, mission=%d, applied=0x%llX.",
                disableHonorSystem ? 1 : 0,
                (wanted & searchHonorEventFlag) != 0 ? 1 : 0,
                (wanted & antagonizeHonorEventFlag) != 0 ? 1 : 0,
                mission ? 1 : 0,
                static_cast<unsigned long long>(applied));
            Logger::Log(message);
        }
        lastLoggedHonorEventTarget = wanted;
        lastLoggedHonorEventMission = mission;
        hasLoggedHonorEventState = true;
    }

    void ResetBountySystem()
    {
        bountySystemCaptured = false;
        bountySystemPlayerPed = 0;
        bountySystemLastGameTimer = 0;
        bountySystemBaseline = 0;
        bountySystemRecentLawIncidentAt = 0;
        bountySystemLastCorrection = -1;
        bountySystemLastCorrectionLogAt = 0;
    }

    void UpdateDisabledBounties()
    {
        if (!disableBounties)
            return;

        const ULONGLONG now = GetTickCount64();
        nativeInit(getLoadingScreenActive);
        const bool loading = static_cast<std::uint32_t>(*nativeCall()) != 0;
        const bool mission = Main::MissionState::IsActiveOrRecentlyEnded(missionBountyGraceMs);
        if (loading || mission)
        {
            if (!bountySystemSuspended || loading != bountySystemLoading)
                ResetBountySystem();
            bountySystemSuspended = true;
            bountySystemLoading = loading;
            bountySystemLastUpdateAt = now;
            return;
        }
        if (bountySystemSuspended)
        {
            bountySystemSuspended = false;
            bountySystemLoading = false;
            ResetBountySystem();
        }

        const std::uint32_t ped = GetPlayerPed();
        if (ped == 0)
        {
            ResetBountySystem();
            bountySystemLastUpdateAt = now;
            return;
        }

        nativeInit(getGameTimer);
        const std::uint32_t gameTime = static_cast<std::uint32_t>(*nativeCall());
        if (ped != bountySystemPlayerPed ||
            (bountySystemLastUpdateAt != 0 && now - bountySystemLastUpdateAt > bountySystemLoadGapMs) ||
            (bountySystemCaptured && gameTime < bountySystemLastGameTimer &&
                bountySystemLastGameTimer - gameTime > 1000))
            ResetBountySystem();

        bountySystemPlayerPed = ped;
        bountySystemLastGameTimer = gameTime;
        bountySystemLastUpdateAt = now;

        const int player = GetPlayer();
        const int bounty = GetCurrentBounty(player);
        if (bounty < 0 || bounty > 100000000)
            return;

        if (!bountySystemCaptured)
        {
            bountySystemBaseline = bounty;
            bountySystemCaptured = true;
            if (Logger::IsDebugEnabled())
            {
                char message[120];
                std::snprintf(message, sizeof(message),
                    "[DEBUG] DisableBounties: captured regional bounty baseline=%d.", bounty);
                Logger::Log(message);
            }
            return;
        }

        const bool lawActive = HasLawIncident(player);
        if (lawActive)
            bountySystemRecentLawIncidentAt = now;

        if (bounty < bountySystemBaseline)
        {
            bountySystemBaseline = bounty;
            bountySystemLastCorrection = -1;
        }
        else if (bounty > bountySystemBaseline)
        {
            const bool recentIncident = bountySystemRecentLawIncidentAt != 0 &&
                now - bountySystemRecentLawIncidentAt <= bountySystemRecentIncidentMs;
            if (!lawActive && !recentIncident)
            {
                bountySystemBaseline = bounty;
                bountySystemLastCorrection = -1;
                return;
            }

            nativeInit(setBounty);
            nativePush64(static_cast<std::uint64_t>(player));
            nativePush64(static_cast<std::uint64_t>(bountySystemBaseline));
            nativeCall();
            if (auto* storedBounty = getGlobalPtr(0x2EDD))
                *storedBounty = static_cast<std::uint64_t>(bountySystemBaseline);
            if (auto* regionalBounty = getGlobalPtr(0x1D891F))
                *regionalBounty = static_cast<std::uint64_t>(bountySystemBaseline);
            if (Logger::IsDebugEnabled() &&
                (bounty != bountySystemLastCorrection || now - bountySystemLastCorrectionLogAt >= 2000))
            {
                const int restored = GetCurrentBounty(player);
                char message[180];
                std::snprintf(message, sizeof(message),
                    "[DEBUG] DisableBounties: bounty %d -> %d corrected; readback=%d, law=%d, recent=%d.",
                    bounty, bountySystemBaseline, restored, lawActive ? 1 : 0, recentIncident ? 1 : 0);
                Logger::Log(message);
                bountySystemLastCorrection = bounty;
                bountySystemLastCorrectionLogAt = now;
            }
        }
    }

    constexpr std::uint64_t isUiBindingValid = 0x1E7130793AAAAB8DULL;
    constexpr std::uint64_t readUiBindingBool = 0x5EEFBD4B6D7CD6EBULL;
    constexpr std::uint64_t readUiBindingString = 0x3D290B5FFA7C5151ULL;
    constexpr std::uint64_t writeUiBindingString = 0xE1BD342F2872AEE9ULL;
    constexpr char unknownSuspectLabel[] = "LAW_UI_UNKNOWN_SUSPECT";
    constexpr char arthurNameLabel[] = "POSTER_PL_ARTHUR_NAME";
    constexpr char johnNameLabel[] = "POSTER_PL_JOHN_NAME";

    struct WantedHudEntry
    {
        int visibleIndex;
        int nameIndex;
        std::uint32_t ownedBinding = 0;
        const char* originalLabel = nullptr;
    };

    // short_update on 1.0.1491.50: investigation and pursuit name fields.
    constexpr int wantedHudBase = 1934266 + 78;
    WantedHudEntry wantedHudEntries[] = {
        { wantedHudBase + 19 + 1, wantedHudBase + 19 + 5 },
        { wantedHudBase + 37 + 1, wantedHudBase + 37 + 4 }
    };

    bool SupportsWantedHudBindings()
    {
        const auto* image = reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(nullptr));
        if (!image)
            return false;
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 4096)
            return false;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(image + dos->e_lfanew);
        return nt->Signature == IMAGE_NT_SIGNATURE && nt->FileHeader.TimeDateStamp == 0x65EEC92E &&
            nt->OptionalHeader.SizeOfImage == 0x7397600;
    }

    bool ReadWantedHudBool(std::uint64_t native, std::uint32_t argument)
    {
        nativeInit(native);
        nativePush64(argument);
        const auto* result = nativeCall();
        return result && static_cast<std::uint32_t>(*result) != 0;
    }

    std::uint32_t GetWantedHudBinding(int index)
    {
        const auto* slot = getGlobalPtr(index);
        const auto binding = slot ? static_cast<std::uint32_t>(*slot) : 0;
        return binding && ReadWantedHudBool(isUiBindingValid, binding) ? binding : 0;
    }

    void WriteWantedHudLabel(std::uint32_t binding, const char* label)
    {
        nativeInit(writeUiBindingString);
        nativePush64(binding);
        nativePush64(reinterpret_cast<std::uint64_t>(label));
        nativeCall();
    }

    void UpdateMaskedWantedHud()
    {
        const bool eligible = preventBounty && wearingMask && bountyProtected && !bountyMissionSuspended;
        if (!eligible && !wantedHudEntries[0].ownedBinding && !wantedHudEntries[1].ownedBinding)
            return;
        static const bool supportedBuild = SupportsWantedHudBindings();
        if (!supportedBuild)
            return;

        const bool showUnknown = eligible && ReadWantedHudBool(lawIncidentActive, GetPlayer());
        for (auto& entry : wantedHudEntries)
        {
            const auto binding = GetWantedHudBinding(entry.nameIndex);
            if (entry.ownedBinding != binding)
            {
                entry.ownedBinding = 0;
                entry.originalLabel = nullptr;
            }
            const auto visible = showUnknown ? GetWantedHudBinding(entry.visibleIndex) : 0;
            const bool apply = visible && ReadWantedHudBool(readUiBindingBool, visible);
            if (!binding || (!apply && !entry.ownedBinding))
                continue;

            nativeInit(readUiBindingString);
            nativePush64(binding);
            const auto* result = nativeCall();
            const auto* label = result ? reinterpret_cast<const char*>(*result) : nullptr;
            if (!label)
                continue;
            if (!apply)
            {
                if (std::strcmp(label, unknownSuspectLabel) == 0)
                    WriteWantedHudLabel(binding, entry.originalLabel);
                entry.ownedBinding = 0;
                entry.originalLabel = nullptr;
                continue;
            }

            if (std::strcmp(label, arthurNameLabel) == 0)
                entry.originalLabel = arthurNameLabel;
            else if (std::strcmp(label, johnNameLabel) == 0)
                entry.originalLabel = johnNameLabel;
            else
                continue;

            WriteWantedHudLabel(binding, unknownSuspectLabel);
            if (entry.ownedBinding != binding && Logger::IsDebugEnabled())
                Logger::Log("[DEBUG] MaskedGameplay: Wanted HUD name replaced with Unknown Suspect.");
            entry.ownedBinding = binding;
        }
    }

    void ResetMaskState()
    {
        wearingMask = false;
        honorCaptured = false;
        bountyProtected = false;
        protectedBounty = 0;
        recentLawIncidentAt = 0;
        lastBountyCorrection = -1;
        lastBountyCorrectionLogAt = 0;
        nextMaskCheckAt = 0;
        nextHonorStatCheckAt = 0;
        honorStatCheckUntil = 0;
    }

    void UpdateMaskedGameplay()
    {
        if (!preventHonorLoss && !preventBounty)
            return;

        const std::uint32_t ped = GetPlayerPed();
        if (ped == 0)
        {
            currentPed = 0;
            ResetMaskState();
            return;
        }
        if (ped != currentPed)
        {
            currentPed = ped;
            ResetMaskState();
        }

        const ULONGLONG now = GetTickCount64();
        if (!wearingMask && now < nextMaskCheckAt)
            return;

        const int player = preventBounty ? GetPlayer() : 0;
        const bool suspendBountyForMission = preventBounty &&
            Main::MissionState::IsActiveOrRecentlyEnded(missionBountyGraceMs);
        if (now >= nextMaskCheckAt)
        {
            nextMaskCheckAt = now + maskCheckIntervalMs;
            const bool masked = IsFaceCovered(ped);
            if (masked != wearingMask)
            {
                wearingMask = masked;
                honorCaptured = false;
                nextHonorStatCheckAt = 0;
                honorStatCheckUntil = 0;
                bountyProtected = false;
                recentLawIncidentAt = 0;
                if (masked && preventBounty && !suspendBountyForMission && !bountyMissionSuspended)
                {
                    protectedBounty = GetCurrentBounty(player);
                    const bool alreadyIdentified = HasLawIncident(player);
                    bountyProtected = !alreadyIdentified;
                    if (Logger::IsDebugEnabled())
                    {
                        char message[180];
                        std::snprintf(message, sizeof(message),
                            "[DEBUG] MaskedGameplay: mask raised; bounty baseline=%d, protection=%d, lawAlreadyActive=%d.",
                            protectedBounty, bountyProtected ? 1 : 0, alreadyIdentified ? 1 : 0);
                        Logger::Log(message);
                    }
                }
                else if (Logger::IsDebugEnabled())
                    Logger::Log(masked ? "[DEBUG] MaskedGameplay: mask raised." :
                        "[DEBUG] MaskedGameplay: mask lowered or removed.");
            }
        }

        if (preventBounty && suspendBountyForMission != bountyMissionSuspended)
        {
            bountyMissionSuspended = suspendBountyForMission;
            bountyProtected = false;
            recentLawIncidentAt = 0;
            lastBountyCorrection = -1;
            lastBountyCorrectionLogAt = 0;
            if (suspendBountyForMission)
            {
                if (Logger::IsDebugEnabled())
                    Logger::Log("[DEBUG] MaskedGameplay: bounty protection suspended during mission and post-mission grace period.");
            }
            else if (wearingMask)
            {
                protectedBounty = GetCurrentBounty(player);
                const bool alreadyIdentified = HasLawIncident(player);
                bountyProtected = !alreadyIdentified;
                if (Logger::IsDebugEnabled())
                {
                    char message[180];
                    std::snprintf(message, sizeof(message),
                        "[DEBUG] MaskedGameplay: mission ended; bounty baseline=%d, protection=%d, lawAlreadyActive=%d.",
                        protectedBounty, bountyProtected ? 1 : 0, alreadyIdentified ? 1 : 0);
                    Logger::Log(message);
                }
            }
        }

        if (!wearingMask)
            return;

        if (preventHonorLoss)
        {
            auto* honor = getGlobalPtr(0x2BA2);
            if (honor)
            {
                const std::int32_t value = static_cast<std::int32_t>(*honor);
                if (value >= -1000 && value <= 1000)
                {
                    if (!honorCaptured)
                    {
                        protectedHonor = value;
                        honorCaptured = true;
                        if (Logger::IsDebugEnabled())
                        {
                            std::int32_t statistic = 0;
                            const bool statAvailable = ReadHonorStatistic(statistic);
                            char message[160];
                            std::snprintf(message, sizeof(message),
                                "[DEBUG] MaskedGameplay: honor baseline=%d, HONOR_CURRENT=%d, statAvailable=%d.",
                                protectedHonor, statistic, statAvailable ? 1 : 0);
                            Logger::Log(message);
                        }
                    }
                    else if (value < protectedHonor)
                    {
                        if (preventBounty && bountyProtected)
                            recentLawIncidentAt = now;
                        *honor = static_cast<std::uint64_t>(static_cast<std::int64_t>(protectedHonor));
                        nativeInit(clearFeedChannel);
                        nativePush64(7);
                        nativePush64(1);
                        nativePush64(0);
                        nativeCall();
                        const bool statisticUpdated = WriteHonorStatistic(protectedHonor);
                        nextHonorStatCheckAt = now + honorStatRecheckMs;
                        honorStatCheckUntil = now + honorStatRecheckWindowMs;
                        if (Logger::IsDebugEnabled())
                        {
                            char message[160];
                            std::snprintf(message, sizeof(message),
                                "[DEBUG] MaskedGameplay: honor loss %d -> %d restored; feed channel 7 cleared; HONOR_CURRENT updated=%d.",
                                value, protectedHonor, statisticUpdated ? 1 : 0);
                            Logger::Log(message);
                        }
                    }
                    else
                    {
                        if (value > protectedHonor)
                            protectedHonor = value;
                        if (honorStatCheckUntil != 0 && now >= nextHonorStatCheckAt)
                        {
                            if (now <= honorStatCheckUntil)
                            {
                                std::int32_t statistic = 0;
                                if (ReadHonorStatistic(statistic) && statistic < protectedHonor)
                                {
                                    const bool updated = WriteHonorStatistic(protectedHonor);
                                    if (Logger::IsDebugEnabled())
                                    {
                                        char message[160];
                                        std::snprintf(message, sizeof(message),
                                            "[DEBUG] MaskedGameplay: delayed honor stat %d -> %d restored, updated=%d.",
                                            statistic, protectedHonor, updated ? 1 : 0);
                                        Logger::Log(message);
                                    }
                                }
                                nextHonorStatCheckAt = now + honorStatRecheckMs;
                            }
                            else
                                honorStatCheckUntil = 0;
                        }
                    }
                }
            }
        }

        if (preventBounty && bountyProtected && !suspendBountyForMission)
        {
            const bool lawActive = HasLawIncident(player);
            if (lawActive)
                recentLawIncidentAt = now;
            const int bounty = GetCurrentBounty(player);
            if (bounty < protectedBounty)
            {
                protectedBounty = bounty;
                lastBountyCorrection = -1;
            }
            else if (bounty > protectedBounty)
            {
                const bool recentCrime = recentLawIncidentAt != 0 &&
                    now - recentLawIncidentAt <= recentLawIncidentMs;
                if (lawActive || recentCrime)
                {
                    nativeInit(setBounty);
                    nativePush64(static_cast<std::uint64_t>(player));
                    nativePush64(static_cast<std::uint64_t>(protectedBounty));
                    nativeCall();
                    auto* storedBounty = getGlobalPtr(0x2EDD);
                    if (storedBounty)
                        *storedBounty = static_cast<std::uint64_t>(protectedBounty);
                    auto* regionalBounty = getGlobalPtr(0x1D891F);
                    if (regionalBounty)
                        *regionalBounty = static_cast<std::uint64_t>(protectedBounty);
                    if (Logger::IsDebugEnabled() &&
                        (lastBountyCorrection != bounty || now - lastBountyCorrectionLogAt >= 2000))
                    {
                        const int restoredBounty = GetCurrentBounty(player);
                        char message[180];
                        std::snprintf(message, sizeof(message),
                            "[DEBUG] MaskedGameplay: bounty %d -> %d, readback=%d, incident=%d, recentCrime=%d, storedSlot=%d, regionalSlot=%d.",
                            bounty, protectedBounty, restoredBounty, lawActive ? 1 : 0, recentCrime ? 1 : 0,
                            storedBounty != nullptr, regionalBounty != nullptr);
                        Logger::Log(message);
                        lastBountyCorrection = bounty;
                        lastBountyCorrectionLogAt = now;
                    }
                }
                else
                {
                    protectedBounty = bounty;
                    lastBountyCorrection = -1;
                    if (Logger::IsDebugEnabled())
                    {
                        char message[160];
                        std::snprintf(message, sizeof(message),
                            "[DEBUG] MaskedGameplay: peaceful regional bounty baseline updated=%d.", protectedBounty);
                        Logger::Log(message);
                    }
                }
            }
        }
    }
}

namespace Main
{
    void InitializeEarly(const GameplayFixes::Settings& settings)
    {
        Camera::InitializeWindowFocus(settings.enabled && settings.fixFullscreenTabOut,
            settings.enabled && settings.minimizeOnTabOut);
        CrashFixes::ExtendHeapLimits(settings.enabled && settings.extendHeapLimits);
        CrashFixes::ExtendPackfileLimits(settings.enabled && settings.extendPackfileLimits);
        CrashFixes::ExtendPedMemoryLimits(settings.enabled && settings.extendPedMemoryLimits);
        CrashFixes::InitializeCrashDiagnostics(settings.enabled && settings.crashDiagnostics);
        Display::InitializeOverscanFov(settings.enabled, settings.enabled && settings.expandSsrCoverage,
            settings.ssrOverscanScale, settings.ssrOverscanTestMode,
            settings.enabled ? settings.cameraFovOffset : 0.0f);
        DLSS::Initialize(settings.enabled && settings.enableDLSSAutoExposure);
        CrashFixes::InitializeTextureStreamingStutterFix(settings.enabled && settings.textureStreamingStutterFix);
        CrashFixes::InitializeImproveTextureStreaming(settings.enabled && settings.improveTextureStreaming);
        CrashFixes::Initialize(settings.enabled && settings.textureCrashFix);
        CrashFixes::ExtendWritableResourceSlots(settings.enabled && settings.extendAssetPools);
        CrashFixes::ExtendStreamingPools(settings.enabled && settings.extendStreamingPools,
            settings.enabled && settings.extendAssetPools,
            settings.enabled && settings.optimizeStreamingMemory);
        SkipIntro::Initialize(settings.enabled && settings.skipIntroSplashes,
            settings.enabled && settings.skipLegal, settings.enabled && settings.straightToStoryMode);
        Looting::InitializeEarly(settings.enabled && settings.noAutoAmmoPickup);
    }

    void Initialize(const GameplayFixes::Settings& settings)
    {
        Display::UpdateTimecycleEffects(settings.enabled && settings.disableVignette,
            settings.enabled && settings.disableBloom,
            settings.enabled && settings.disableChromaticAberration);
        Display::UpdateCompositeDithering(settings.enabled && settings.disableCompositeDithering);
        Audio::Initialize(settings.enabled && settings.disableAmbientMusic,
            settings.enabled && settings.disableCombatMusic,
            settings.enabled && settings.disableWantedMusic);
        CrashFixes::RestoreTextureStreamingStutterFix();
        CrashFixes::ReportImproveTextureStreamingStatus();
        CrashFixes::ReportWritableResourceSlots();
        CrashFixes::ReportPoolExtensionStatus();
        CrashFixes::ReportCrashDiagnosticsStatus();
        SkipIntro::FinishStartup();
        RemoveBlackBars::Initialize(settings.enabled && settings.removeBlackBars);
        Lighting::Initialize(settings.enabled && settings.disableInteriorLighting,
            settings.enabled && settings.disableDistantLights);
        Camera::Initialize(settings.enabled && settings.disableIdleCamera,
            settings.enabled && settings.fixFullscreenTabOut,
            settings.enabled && settings.minimizeOnTabOut,
            settings.enabled && settings.disableCameraSmoothing,
            settings.enabled && settings.disableHeadBob,
            settings.enabled && settings.reduceCameraShake,
            settings.enabled && settings.reduceCameraSway,
            settings.enabled && settings.disableDrunkFilter,
            settings.enabled && settings.disablePauseMenuFilter,
            settings.enabled && settings.disableEagleEyeFilter,
            settings.enabled && settings.disableDeadEyeFilter,
            settings.enabled && settings.disableWeaponWheelFilter,
            settings.enabled && settings.disableLowHealthFilter);
        Horse::Initialize(settings.enabled && settings.noAutoHorseEquip,
            settings.enabled && settings.noAutoHorseHolster,
            settings.enabled && settings.disableHorseAutoJump,
            settings.enabled && settings.disableHorseWhistleDistance,
            settings.enabled && settings.fixHorseFollowBehavior);
        Player::Initialize(settings.enabled && settings.disableFirstPersonAutoRun,
            settings.enabled && settings.quickLootBodies,
            settings.enabled && settings.quickLootAnimals);
        Gunz::Initialize(settings.enabled, settings.enabled && settings.disableAutoReload,
            settings.enabled && settings.disableDeadEyeAutoReload,
            settings.enabled && settings.disableEmptyAutoReload,
            settings.enabled && settings.disableArrowTrails,
            settings.enabled && settings.disableReticleSpread,
            settings.enabled && settings.disableReticleSway,
            settings.enabled && settings.disableForcedScope,
            settings.enabled && settings.disableWeaponDegradation,
            settings.enabled && settings.disableHitmarkers,
            settings.enabled && settings.disableReticleHighlighting);
        disableHonorSystem = settings.enabled && settings.disableHonorSystem;
        disableBounties = settings.enabled && settings.disableBounties;
        disableOutOfBoundSnipers = settings.enabled && settings.disableOutOfBoundSnipers;
        if (disableOutOfBoundSnipers)
            Logger::Log("[INFO] DisableOutOfBoundSnipers: enabled.");
        preventHonorLoss = settings.enabled && settings.disableHonorLossWhenMasked && !disableHonorSystem;
        preventBounty = settings.enabled && settings.disableBountyWhenMasked && !disableBounties;
        if (disableBounties)
            Logger::Log("[INFO] DisableBounties: bounty increases blocked outside missions; wanted response unchanged.");
        if (preventHonorLoss)
            Logger::Log("[INFO] DisableHonorLossWhenMasked: enabled.");
        if (preventBounty)
            Logger::Log("[INFO] DisableBountyWhenMasked: bounty prevented while unidentified and masked.");
        SelfDefense::Initialize(settings.enabled && settings.fixSelfDefenseHonorLoss && !disableHonorSystem,
            settings.enabled && settings.fixSelfDefenseBounties && !disableBounties);
        if (disableHonorSystem)
            Logger::Log("[INFO] DisableHonorSystem: honor changes disabled.");
        disableBodySearchHonorLoss = settings.enabled && settings.disableBodySearchHonorLoss;
        disableAntagonizeHonorLoss = settings.enabled && settings.disableAntagonizeHonorLoss;
        if (disableBodySearchHonorLoss)
            Logger::Log("[INFO] DisableBodySearchHonorLoss: enabled outside missions.");
        if (disableAntagonizeHonorLoss)
            Logger::Log("[INFO] DisableAntagonizeHonorLoss: enabled outside missions.");
        WaterReflections::ReportStartupStatus();
        Display::ReportCameraStartupStatus();
        DLSS::ReportStartupStatus();
    }

    void ReloadGameplay(const GameplayFixes::Settings& settings)
    {
        if (disableOutOfBoundSnipers && (!settings.enabled || !settings.disableOutOfBoundSnipers))
            RestoreOutOfBoundsSniperFlag();
        disableHonorSystem = settings.enabled && settings.disableHonorSystem;
        disableBounties = settings.enabled && settings.disableBounties;
        disableOutOfBoundSnipers = settings.enabled && settings.disableOutOfBoundSnipers;
        preventHonorLoss = settings.enabled && settings.disableHonorLossWhenMasked && !disableHonorSystem;
        preventBounty = settings.enabled && settings.disableBountyWhenMasked && !disableBounties;
        disableBodySearchHonorLoss = settings.enabled && settings.disableBodySearchHonorLoss;
        disableAntagonizeHonorLoss = settings.enabled && settings.disableAntagonizeHonorLoss;
        SelfDefense::Initialize(settings.enabled && settings.fixSelfDefenseHonorLoss && !disableHonorSystem,
            settings.enabled && settings.fixSelfDefenseBounties && !disableBounties);
        UpdateHonorEventFilters();
    }

    void Update(const GameplayFixes::Settings& settings, bool updateOther)
    {
        if (!updateOther)
        {
            Horse::Update();
            Player::Update();
            Gunz::Update();
            return;
        }
        Lighting::Update();
        Camera::Update();
        Display::UpdateOverscanDiagnostics();
        Display::UpdateTimecycleEffects(settings.enabled && settings.disableVignette,
            settings.enabled && settings.disableBloom,
            settings.enabled && settings.disableChromaticAberration);
        Horse::Update();
        Player::Update();
        Gunz::Update();
        Audio::Update();
        UpdateDisabledHonorSystem();
        UpdateHonorEventFilters();
        UpdateDisabledBounties();
        UpdateMaskedGameplay();
        UpdateMaskedWantedHud();
        SelfDefense::Update();
        UpdateOutOfBoundsFeatures();
    }

    bool NeedsMaskedGameplayUpdate()
    {
        return disableHonorSystem || disableBounties || disableBodySearchHonorLoss || disableAntagonizeHonorLoss ||
            ((preventHonorLoss || preventBounty) && wearingMask);
    }
}
