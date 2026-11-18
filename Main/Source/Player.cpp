#include "Player.h"
#include "MissionState.h"
#include "../../Headers/Logger.h"
#include "main.h"
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <array>
#include <cmath>
#include <string>

namespace
{
    constexpr std::uint64_t playerId = 0x217E9DC48139933DULL;
    constexpr std::uint64_t playerPedId = 0x096275889B8E0EE0ULL;
    constexpr std::uint64_t isPlayerControlOn = 0x7964097FCE4C244BULL;
    constexpr std::uint64_t isFirstPersonCameraActive = 0xA24C1D341C6E0D53ULL;
    constexpr std::uint64_t isPedOnFoot = 0x01FEE67DB37F59B2ULL;
    constexpr std::uint64_t isPedStopped = 0x530944F6F4B8A214ULL;
    constexpr std::uint64_t isPedSwimming = 0x9DE327631295B4C2ULL;
    constexpr std::uint64_t isPedInCover = 0x60DFD0691A170B88ULL;
    constexpr std::uint64_t isPedUsingAnyScenario = 0x57AB4A3080F85143ULL;
    constexpr std::uint64_t isInCinematicMode = 0x74F1D22EFA71FAB8ULL;
    constexpr std::uint64_t isControlPressed = 0xF3A21BCD95725A4AULL;
    constexpr std::uint64_t getHashKey = 0xFD340785ADF8CFB7ULL;
    constexpr std::uint64_t setPedMaxMoveBlendRatio = 0x433083750C5E064AULL;

    constexpr std::uint64_t doesEntityExist = 0xD42BD6EB2E0F1677ULL;
    constexpr std::uint64_t isEntityDead = 0x7D5B1F88E7504BBAULL;
    constexpr std::uint64_t isEntityAPed = 0xCF8176912DDA4EA5ULL;
    constexpr std::uint64_t isPedHuman = 0xB980061DA992779DULL;
    constexpr std::uint64_t getIsAnimal = 0x9A100F1CF4546629ULL;
    constexpr std::uint64_t getEntityModel = 0xDA76A9F39210D365ULL;
    constexpr std::uint64_t getPedModelSizeFromHash = 0xA65AA1ACE81E5A77ULL;
    constexpr std::uint64_t isPedIncapacitated = 0xB655DB7582AEC805ULL;
    constexpr std::uint64_t isPedHogtied = 0x3AA24CCC0D451379ULL;
    constexpr std::uint64_t getLootingPickupTargetEntity = 0x14169FA823679E41ULL;
    constexpr std::uint64_t getEntityCoords = 0xA86D5F069399F44DULL;
    constexpr std::uint64_t getEntitiesNearPoint = 0x59B57C4B06531E1EULL;
    constexpr std::uint64_t createItemset = 0xA1AF16083320065AULL;
    constexpr std::uint64_t destroyItemset = 0x712BC69F10549B92ULL;
    constexpr std::uint64_t getItemsetSize = 0x55F2E375AC6018A9ULL;
    constexpr std::uint64_t getIndexedItemInItemset = 0x275A2E2C0FAB7612ULL;
    constexpr std::uint64_t getPlayerInteractionTargetEntity = 0x3EE1F7A8C32F24E1ULL;
    constexpr std::uint64_t getLootFlag = 0xE4C11F104620DDCEULL;
    constexpr std::uint64_t setLootFlag = 0x6569F31A01B4C097ULL;
    constexpr std::uint32_t lootFlagForceFallback = 13;
    constexpr std::int32_t smallAnimalSize = 0;
    constexpr ULONGLONG animalMissionGraceMs = 3000;
    constexpr ULONGLONG lootScanIntervalMs = 100;
    constexpr ULONGLONG nearbyLootScanIntervalMs = 350;
    constexpr std::int32_t nearbyPedLimit = 64;
    constexpr float nearbyLootRadius = 10.0f;
    constexpr ULONGLONG spatialDiagnosticsIntervalMs = 15000;

    bool quickBodiesEnabled = false;
    bool quickAnimalsEnabled = false;
    std::uint32_t bodyFlagWrites = 0;
    std::uint32_t animalFlagWrites = 0;
    bool nearbyScanLogged = false;
    std::uint32_t spatialScanCount = 0;
    std::uint32_t spatialPedCount = 0;
    std::uint32_t spatialDeadCount = 0;
    ULONGLONG nextSpatialDiagnosticsAt = 0;
    bool interactionTargetLogged = false;
    bool missingTargetLogged = false;
    bool animalMissionLogged = false;
    std::uint32_t lastRejectedTarget = 0;
    ULONGLONG nextLootScanAt = 0;
    ULONGLONG nextNearbyLootScanAt = 0;
    bool enabled = false;
    bool walkingLimitActive = false;
    bool runningRequested = false;
    bool movedWhileRunning = false;
    std::uint32_t limitedPed = 0;
    std::uint32_t sprintControl = 0;

    bool InvokeBoolean(std::uint64_t hash)
    {
        nativeInit(hash);
        return static_cast<std::uint32_t>(*nativeCall()) != 0;
    }

    bool InvokeBoolean(std::uint64_t hash, std::uint32_t value)
    {
        nativeInit(hash);
        nativePush64(value);
        return static_cast<std::uint32_t>(*nativeCall()) != 0;
    }

    void SetMaximumMoveBlendRatio(std::uint32_t ped, float ratio)
    {
        std::uint32_t value = 0;
        std::memcpy(&value, &ratio, sizeof(value));
        nativeInit(setPedMaxMoveBlendRatio);
        nativePush64(ped);
        nativePush64(value);
        nativeCall();
    }

    void ClearLimit()
    {
        if (walkingLimitActive && limitedPed != 0)
            SetMaximumMoveBlendRatio(limitedPed, 3.0f);
        walkingLimitActive = false;
        limitedPed = 0;
    }

    bool GetLootFlag(std::uint32_t ped, std::uint32_t flag)
    {
        nativeInit(getLootFlag);
        nativePush64(ped);
        nativePush64(flag);
        return static_cast<std::uint32_t>(*nativeCall()) != 0;
    }

    void SetLootFlag(std::uint32_t ped, std::uint32_t flag, bool value)
    {
        nativeInit(setLootFlag);
        nativePush64(ped);
        nativePush64(flag);
        nativePush64(value ? 1 : 0);
        nativeCall();
    }

    std::uint32_t GetLootTarget(std::uint32_t ped, std::uint32_t player)
    {
        std::uint32_t target = 0;
        nativeInit(getPlayerInteractionTargetEntity);
        nativePush64(player);
        nativePush64(reinterpret_cast<std::uint64_t>(&target));
        nativePush64(1);
        nativePush64(1);
        if (static_cast<std::uint32_t>(*nativeCall()) != 0 && target != 0 &&
            InvokeBoolean(doesEntityExist, target) && InvokeBoolean(isEntityAPed, target))
            return target;

        nativeInit(getLootingPickupTargetEntity);
        nativePush64(ped);
        target = static_cast<std::uint32_t>(*nativeCall());
        if (target != 0 && InvokeBoolean(doesEntityExist, target) &&
            InvokeBoolean(isEntityAPed, target))
            return target;
        return 0;
    }

    void TryApplyQuickLoot(std::uint32_t target, bool allowAnimals, bool nearby)
    {
        if (target == 0 || !InvokeBoolean(doesEntityExist, target) ||
            !InvokeBoolean(isEntityAPed, target))
            return;

        const bool dead = InvokeBoolean(isEntityDead, target);
        if (nearby && !dead)
            return;

        const bool human = InvokeBoolean(isPedHuman, target);
        const bool eligibleHuman = human && quickBodiesEnabled &&
            (dead || (!nearby && (InvokeBoolean(isPedIncapacitated, target) ||
                InvokeBoolean(isPedHogtied, target))));
        bool eligibleAnimal = false;
        if (!human && allowAnimals && dead && InvokeBoolean(getIsAnimal, target))
        {
            nativeInit(getEntityModel);
            nativePush64(target);
            const std::uint32_t model = static_cast<std::uint32_t>(*nativeCall());
            nativeInit(getPedModelSizeFromHash);
            nativePush64(model);
            eligibleAnimal = static_cast<std::int32_t>(*nativeCall()) > smallAnimalSize;
        }

        if ((!eligibleHuman && !eligibleAnimal) || GetLootFlag(target, lootFlagForceFallback))
            return;

        SetLootFlag(target, lootFlagForceFallback, true);
        if (!GetLootFlag(target, lootFlagForceFallback))
        {
            if (lastRejectedTarget != target && Logger::IsDebugEnabled())
            {
                Logger::Log("[WARNING] QuickLoot: target rejected flag 13.");
                lastRejectedTarget = target;
            }
            return;
        }

        std::uint32_t& writes = eligibleHuman ? bodyFlagWrites : animalFlagWrites;
        ++writes;
        if (writes <= 3 || writes % 10 == 0)
        {
            if (eligibleHuman)
                Logger::Log(nearby
                    ? "[INFO] QuickLootBodies: nearby body flagged for quick looting."
                    : "[INFO] QuickLootBodies: target flagged for quick looting.");
            else
                Logger::Log(nearby
                    ? "[INFO] QuickLootAnimals: nearby animal flagged for quick looting."
                    : "[INFO] QuickLootAnimals: target flagged for quick looting.");
        }
    }

    void PreApplyNearbyQuickLoot(std::uint32_t ped, bool allowAnimals)
    {
        if (!quickBodiesEnabled && !allowAnimals)
            return;

        struct Position { alignas(8) float x; alignas(8) float y; alignas(8) float z; } position = {};
        static_assert(sizeof(Position) == 24, "");
        nativeInit(getEntityCoords);
        nativePush64(ped);
        nativePush64(1);
        nativePush64(1);
        const auto* coordinates = nativeCall();
        if (!coordinates)
            return;
        std::memcpy(&position, coordinates, sizeof(position));
        if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z))
            return;

        nativeInit(createItemset);
        nativePush64(1);
        const std::uint32_t itemset = static_cast<std::uint32_t>(*nativeCall());
        if (!itemset)
            return;

        const auto pushFloat = [](float value)
        {
            std::uint64_t argument = 0;
            std::memcpy(&argument, &value, sizeof(value));
            nativePush64(argument);
        };
        nativeInit(getEntitiesNearPoint);
        pushFloat(position.x);
        pushFloat(position.y);
        pushFloat(position.z);
        pushFloat(nearbyLootRadius);
        nativePush64(itemset);
        nativePush64(1);
        nativeCall();

        nativeInit(getItemsetSize);
        nativePush64(itemset);
        const std::int32_t size = static_cast<std::int32_t>(*nativeCall());
        const std::int32_t count = size > 0 ?
            (size < nearbyPedLimit ? size : nearbyPedLimit) : 0;
        ++spatialScanCount;
        if (count > 0)
        {
            if (!nearbyScanLogged && Logger::IsDebugEnabled())
            {
                Logger::Log("[DEBUG] QuickLoot: spatial itemset enumeration returned entity handles.");
                nearbyScanLogged = true;
            }
            for (std::int32_t i = 0; i < count; ++i)
            {
                nativeInit(getIndexedItemInItemset);
                nativePush64(static_cast<std::uint32_t>(i));
                nativePush64(itemset);
                const std::uint32_t target = static_cast<std::uint32_t>(*nativeCall());
                if (target == 0 || target == ped || !InvokeBoolean(doesEntityExist, target) ||
                    !InvokeBoolean(isEntityAPed, target))
                    continue;
                ++spatialPedCount;
                if (InvokeBoolean(isEntityDead, target))
                    ++spatialDeadCount;
                TryApplyQuickLoot(target, allowAnimals, true);
            }
        }

        nativeInit(destroyItemset);
        nativePush64(itemset);
        nativeCall();

        const ULONGLONG now = GetTickCount64();
        if (Logger::IsDebugEnabled() && now >= nextSpatialDiagnosticsAt)
        {
            nextSpatialDiagnosticsAt = now + spatialDiagnosticsIntervalMs;
            const std::string message = "[DEBUG] QuickLoot: spatial scan totals scans=" + std::to_string(spatialScanCount) +
                ", peds=" + std::to_string(spatialPedCount) +
                ", dead=" + std::to_string(spatialDeadCount) +
                ", bodyFlags=" + std::to_string(bodyFlagWrites) +
                ", animalFlags=" + std::to_string(animalFlagWrites) + ".";
            Logger::Log(message.c_str());
        }
    }

    void UpdateQuickLoot()
    {
        if (!quickBodiesEnabled && !quickAnimalsEnabled)
            return;

        const ULONGLONG now = GetTickCount64();
        if (now < nextLootScanAt)
            return;
        nextLootScanAt = now + lootScanIntervalMs;

        nativeInit(playerPedId);
        const std::uint32_t ped = static_cast<std::uint32_t>(*nativeCall());
        if (ped == 0 || !InvokeBoolean(doesEntityExist, ped) || !InvokeBoolean(isPedOnFoot, ped))
            return;

        nativeInit(playerId);
        const std::uint32_t player = static_cast<std::uint32_t>(*nativeCall());
        if (!InvokeBoolean(isPlayerControlOn, player))
            return;

        const bool allowAnimals = quickAnimalsEnabled &&
            !Main::MissionState::IsActiveOrRecentlyEnded(animalMissionGraceMs);
        if (now >= nextNearbyLootScanAt)
        {
            nextNearbyLootScanAt = now + nearbyLootScanIntervalMs;
            PreApplyNearbyQuickLoot(ped, allowAnimals);
        }

        const std::uint32_t target = GetLootTarget(ped, player);
        if (target == 0)
        {
            if (!missingTargetLogged && Logger::IsDebugEnabled())
            {
                Logger::Log("[DEBUG] QuickLoot: no valid interaction target yet; waiting for a lootable ped.");
                missingTargetLogged = true;
            }
            lastRejectedTarget = 0;
            return;
        }

        if (!interactionTargetLogged && Logger::IsDebugEnabled())
        {
            Logger::Log("[DEBUG] QuickLoot: interaction target detected using the gameplay target natives.");
            interactionTargetLogged = true;
        }

        if (quickAnimalsEnabled && !allowAnimals && !animalMissionLogged && Logger::IsDebugEnabled())
        {
            Logger::Log("[DEBUG] QuickLootAnimals: scripted mission detected; animal fallback left unchanged.");
            animalMissionLogged = true;
        }
        TryApplyQuickLoot(target, allowAnimals, false);
    }

}

namespace Main::Player
{
    void Initialize(bool disableFirstPersonAutoRun, bool quickLootBodies, bool quickLootAnimals)
    {
        quickBodiesEnabled = quickLootBodies;
        quickAnimalsEnabled = quickLootAnimals;
        bodyFlagWrites = 0;
        animalFlagWrites = 0;
        nearbyScanLogged = false;
        spatialScanCount = 0;
        spatialPedCount = 0;
        spatialDeadCount = 0;
        nextSpatialDiagnosticsAt = 0;
        interactionTargetLogged = false;
        missingTargetLogged = false;
        animalMissionLogged = false;
        lastRejectedTarget = 0;
        nextLootScanAt = 0;
        nextNearbyLootScanAt = 0;
        if (quickBodiesEnabled)
            Logger::Log("[INFO] QuickLootBodies: quick looting enabled for corpses.");
        if (quickAnimalsEnabled)
            Logger::Log("[INFO] QuickLootAnimals: quick looting enabled outside missions.");
        enabled = disableFirstPersonAutoRun;
        if (!enabled)
            return;

        nativeInit(getHashKey);
        nativePush64(reinterpret_cast<std::uint64_t>("INPUT_SPRINT"));
        sprintControl = static_cast<std::uint32_t>(*nativeCall());
        Logger::Log("[INFO] DisableFirstPersonAutoRun: walking is the default in first person.");
    }

    void Update()
    {
        UpdateQuickLoot();
        if (!enabled)
            return;

        nativeInit(playerPedId);
        const std::uint32_t ped = static_cast<std::uint32_t>(*nativeCall());
        if (ped == 0 || (limitedPed != 0 && limitedPed != ped))
        {
            walkingLimitActive = false;
            limitedPed = 0;
            runningRequested = false;
            movedWhileRunning = false;
            if (ped == 0)
                return;
        }

        nativeInit(playerId);
        const std::uint32_t player = static_cast<std::uint32_t>(*nativeCall());
        const bool eligible = InvokeBoolean(isFirstPersonCameraActive) &&
            InvokeBoolean(isPlayerControlOn, player) &&
            InvokeBoolean(isPedOnFoot, ped) &&
            !InvokeBoolean(isPedSwimming, ped) &&
            !InvokeBoolean(isPedInCover, ped) &&
            !InvokeBoolean(isPedUsingAnyScenario, ped) &&
            !InvokeBoolean(isInCinematicMode);

        if (!eligible)
        {
            ClearLimit();
            runningRequested = false;
            movedWhileRunning = false;
            return;
        }

        bool sprintPressed = false;
        if (sprintControl != 0)
        {
            nativeInit(isControlPressed);
            nativePush64(0);
            nativePush64(sprintControl);
            sprintPressed = static_cast<std::uint32_t>(*nativeCall()) != 0;
        }

        if (sprintPressed)
            runningRequested = true;

        if (runningRequested)
        {
            if (!InvokeBoolean(isPedStopped, ped))
                movedWhileRunning = true;
            else if (movedWhileRunning && !sprintPressed)
            {
                runningRequested = false;
                movedWhileRunning = false;
            }
        }

        if (runningRequested)
            ClearLimit();
        else
        {
            SetMaximumMoveBlendRatio(ped, 1.0f);
            walkingLimitActive = true;
            limitedPed = ped;
        }
    }

    bool NeedsFastUpdate()
    {
        return enabled && walkingLimitActive;
    }
}
