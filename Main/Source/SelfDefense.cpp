#include "SelfDefense.h"
#include "MissionState.h"
#include "../../Headers/Logger.h"
#include "main.h"
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <array>
#include <cstddef>

namespace
{
    constexpr std::uint64_t playerPedId = 0x096275889B8E0EE0ULL;
    constexpr std::uint64_t playerId = 0x217E9DC48139933DULL;
    constexpr std::uint64_t getPlayerWantedLevel = 0xABC532F9098BFD9DULL;
    constexpr std::uint64_t getWantedScore = 0xDD5FD601481F648BULL;
    constexpr std::uint64_t getEntityHealth = 0x82368787EA73C0F7ULL;
    constexpr std::uint64_t getEntityModel = 0xDA76A9F39210D365ULL;
    constexpr std::uint64_t lastEntityToDamage = 0xAF72EC7E1B54539BULL;
    constexpr std::uint64_t doesEntityExist = 0xD42BD6EB2E0F1677ULL;
    constexpr std::uint64_t isEntityAPed = 0xCF8176912DDA4EA5ULL;
    constexpr std::uint64_t isPedHuman = 0xB980061DA992779DULL;
    constexpr std::uint64_t isPedAPlayer = 0x12534C348C6CB68BULL;
    constexpr std::uint64_t hasDamagedEntity = 0x7B6E7BEC1143AC86ULL;
    constexpr std::uint64_t isPedDeadOrDying = 0x3317DEDB88C95038ULL;
    constexpr std::uint64_t getPedSourceOfDeath = 0x93C8B64DEB84728CULL;
    constexpr std::uint64_t getBounty = 0x54310AAB97B92816ULL;
    constexpr std::uint64_t setBounty = 0x093A9D1F72DF0D19ULL;
    constexpr std::uint64_t lawIncidentActive = 0xAD401C63158ACBAAULL;
    constexpr std::uint64_t witnessesActive = 0x69E181772886F48BULL;
    constexpr std::uint64_t witnessesPending = 0x0BB6DE7D23C60626ULL;
    constexpr std::uint64_t statIdSetInt = 0xA4DDF5DF95E65EEEULL;
    constexpr std::uint64_t clearFeedChannel = 0xDD1232B332CBB9E7ULL;
    constexpr std::uint32_t honorCurrentHash = 0x89587B4E;
    constexpr ULONGLONG updateIntervalMs = 50;
    constexpr ULONGLONG missionGraceMs = 3000;
    constexpr ULONGLONG wantedGraceMs = 3000;
    constexpr ULONGLONG aggressorLifetimeMs = 15000;
    constexpr ULONGLONG honorCorrectionWindowMs = 2000;
    constexpr ULONGLONG bountyCorrectionWindowMs = 12000;
    constexpr ULONGLONG bountyFollowupWindowMs = 2000;
    constexpr std::size_t maxAggressors = 6;

    struct Aggressor
    {
        std::uint32_t ped = 0;
        std::uint32_t model = 0;
        ULONGLONG expiresAt = 0;
    };

    bool honorEnabled = false;
    bool bountyEnabled = false;
    bool healthCaptured = false;
    std::uint32_t playerPed = 0;
    int playerHealth = 0;
    std::array<Aggressor, maxAggressors> aggressors{};
    std::int32_t honorBaseline = 0;
    int bountyBaseline = 0;
    bool honorBaselineValid = false;
    bool bountyBaselineValid = false;
    ULONGLONG honorProtectionUntil = 0;
    ULONGLONG bountyProtectionUntil = 0;
    ULONGLONG bountyFirstCorrectionAt = 0;
    ULONGLONG wantedSuppressionUntil = 0;
    bool wantedSuppressed = false;
    ULONGLONG nextUpdateAt = 0;

    std::uint32_t ReadEntity(std::uint64_t hash, std::uint32_t entity)
    {
        nativeInit(hash);
        nativePush64(entity);
        return static_cast<std::uint32_t>(*nativeCall());
    }

    bool HasDamaged(std::uint32_t victim, std::uint32_t attacker)
    {
        nativeInit(hasDamagedEntity);
        nativePush64(victim);
        nativePush64(attacker);
        nativePush64(1);
        nativePush64(1);
        return static_cast<std::uint32_t>(*nativeCall()) != 0;
    }

    int GetBounty(int player)
    {
        nativeInit(getBounty);
        nativePush64(static_cast<std::uint64_t>(player));
        return static_cast<int>(*nativeCall());
    }

    bool IsPlayerWanted(int player)
    {
        nativeInit(getPlayerWantedLevel);
        nativePush64(static_cast<std::uint64_t>(player));
        if (static_cast<int>(*nativeCall()) > 0)
            return true;
        nativeInit(getWantedScore);
        nativePush64(static_cast<std::uint64_t>(player));
        return static_cast<int>(*nativeCall()) > 0;
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

    bool IsValidHumanAggressor(std::uint32_t candidate)
    {
        return candidate != 0 && candidate != playerPed &&
            ReadEntity(doesEntityExist, candidate) != 0 &&
            ReadEntity(isEntityAPed, candidate) != 0 &&
            ReadEntity(isPedHuman, candidate) != 0 &&
            ReadEntity(isPedAPlayer, candidate) == 0;
    }

    void Reset()
    {
        aggressors = {};
        honorProtectionUntil = 0;
        bountyProtectionUntil = 0;
        bountyFirstCorrectionAt = 0;
        healthCaptured = false;
        honorBaseline = 0;
        bountyBaseline = 0;
        honorBaselineValid = false;
        bountyBaselineValid = false;
    }

    bool HasAggressors()
    {
        for (const Aggressor& item : aggressors)
            if (item.ped != 0)
                return true;
        return false;
    }

    void RecordAggressor(std::uint32_t candidate, ULONGLONG now, int player)
    {
        for (Aggressor& item : aggressors)
        {
            if (item.ped != 0 && now >= item.expiresAt)
                item = {};
        }

        const std::uint32_t model = ReadEntity(getEntityModel, candidate);
        if (model == 0)
            return;

        for (Aggressor& item : aggressors)
        {
            if (item.ped == candidate)
            {
                if (item.model != model)
                    item = {};
                else
                {
                    item.expiresAt = now + aggressorLifetimeMs;
                    return;
                }
            }
        }

        if (HasLawIncident(player) || HasDamaged(candidate, playerPed))
            return;

        Aggressor* slot = nullptr;
        for (Aggressor& item : aggressors)
        {
            if (item.ped == 0)
            {
                slot = &item;
                break;
            }
        }
        if (!slot)
            return;

        if (!HasAggressors() && honorProtectionUntil <= now && bountyProtectionUntil <= now)
        {
            honorBaselineValid = false;
            bountyBaselineValid = false;
            if (honorEnabled)
            {
                if (auto* honor = getGlobalPtr(0x2BA2))
                {
                    const auto current = static_cast<std::int32_t>(*honor);
                    if (current >= -320 && current <= 320)
                    {
                        honorBaseline = current;
                        honorBaselineValid = true;
                    }
                }
            }
            if (bountyEnabled)
            {
                const int current = GetBounty(player);
                if (current >= 0)
                {
                    bountyBaseline = current;
                    bountyBaselineValid = true;
                }
            }
        }

        slot->ped = candidate;
        slot->model = model;
        slot->expiresAt = now + aggressorLifetimeMs;
        if (Logger::IsDebugEnabled())
        {
            char message[160];
            std::snprintf(message, sizeof(message),
                "[DEBUG] SelfDefense: NPC %u confirmed as initial aggressor; honor=%d, bounty=%d.",
                candidate, honorBaseline, bountyBaseline);
            Logger::Log(message);
        }
    }

    bool CheckDefensiveDeaths(ULONGLONG now)
    {
        bool killedAggressor = false;
        for (Aggressor& item : aggressors)
        {
            if (item.ped == 0)
                continue;
            if (now >= item.expiresAt || !ReadEntity(doesEntityExist, item.ped) ||
                !ReadEntity(isEntityAPed, item.ped))
            {
                item = {};
                continue;
            }
            nativeInit(isPedDeadOrDying);
            nativePush64(item.ped);
            nativePush64(1);
            if (static_cast<std::uint32_t>(*nativeCall()) == 0)
                continue;
            if (ReadEntity(getEntityModel, item.ped) != item.model)
            {
                item = {};
                continue;
            }
            const std::uint32_t killer = ReadEntity(getPedSourceOfDeath, item.ped);
            if (killer == 0)
                continue;
            const bool playerKilled = killer == playerPed ||
                (ReadEntity(isEntityAPed, killer) == 0 && HasDamaged(item.ped, playerPed));
            if (playerKilled)
            {
                killedAggressor = true;
                if (Logger::IsDebugEnabled())
                {
                    char message[120];
                    std::snprintf(message, sizeof(message),
                        "[DEBUG] SelfDefense: NPC %u killed by player; corrections armed.", item.ped);
                    Logger::Log(message);
                }
            }
            item = {};
        }
        return killedAggressor;
    }

    void CorrectHonor(ULONGLONG now)
    {
        if (!honorEnabled || !honorBaselineValid || now >= honorProtectionUntil)
            return;
        auto* honor = getGlobalPtr(0x2BA2);
        if (!honor)
            return;
        const auto current = static_cast<std::int32_t>(*honor);
        if (current < -320 || current > 320)
            return;
        if (current > honorBaseline)
        {
            honorBaseline = current;
            return;
        }
        if (current >= honorBaseline)
            return;

        *honor = static_cast<std::uint64_t>(static_cast<std::int64_t>(honorBaseline));
        alignas(8) std::uint32_t statId[2] = { honorCurrentHash, 0 };
        nativeInit(statIdSetInt);
        nativePush64(reinterpret_cast<std::uint64_t>(statId));
        nativePush64(static_cast<std::uint64_t>(static_cast<std::int64_t>(honorBaseline)));
        nativePush64(1);
        const bool statUpdated = static_cast<std::uint32_t>(*nativeCall()) != 0;
        nativeInit(clearFeedChannel);
        nativePush64(7);
        nativePush64(1);
        nativePush64(0);
        nativeCall();
        if (Logger::IsDebugEnabled())
        {
            char message[160];
            std::snprintf(message, sizeof(message),
                "[DEBUG] SelfDefense: honor %d -> %d restored; feed cleared; statUpdated=%d.",
                current, honorBaseline, statUpdated ? 1 : 0);
            Logger::Log(message);
        }
    }

    void CorrectBounty(ULONGLONG now, int player)
    {
        if (!bountyEnabled || !bountyBaselineValid || now >= bountyProtectionUntil)
            return;
        const int current = GetBounty(player);
        if (current < 0)
            return;
        if (current < bountyBaseline)
        {
            bountyBaseline = current;
            return;
        }
        if (current <= bountyBaseline || !HasLawIncident(player))
            return;
        nativeInit(setBounty);
        nativePush64(static_cast<std::uint64_t>(player));
        nativePush64(static_cast<std::uint64_t>(bountyBaseline));
        nativeCall();
        if (auto* stored = getGlobalPtr(0x2EDD))
            *stored = static_cast<std::uint64_t>(bountyBaseline);
        if (auto* regional = getGlobalPtr(0x1D891F))
            *regional = static_cast<std::uint64_t>(bountyBaseline);
        if (bountyFirstCorrectionAt == 0)
        {
            bountyFirstCorrectionAt = now;
            if (bountyProtectionUntil > now + bountyFollowupWindowMs)
                bountyProtectionUntil = now + bountyFollowupWindowMs;
        }
        if (Logger::IsDebugEnabled())
        {
            char message[160];
            std::snprintf(message, sizeof(message),
                "[DEBUG] SelfDefense: bounty %d -> %d restored (defensive incident).",
                current, bountyBaseline);
            Logger::Log(message);
        }
    }
}

namespace Main::SelfDefense
{
    void Initialize(bool fixHonorLoss, bool fixBounties)
    {
        honorEnabled = fixHonorLoss;
        bountyEnabled = fixBounties;
        Reset();
        playerPed = 0;
        wantedSuppressionUntil = 0;
        wantedSuppressed = false;
        nextUpdateAt = 0;
        if (honorEnabled)
            Logger::Log("[INFO] FixSelfDefenseHonorLoss: enabled for defensive kills.");
        if (bountyEnabled)
            Logger::Log("[INFO] FixSelfDefenseBounties: enabled for defensive kills.");
    }

    void Update()
    {
        if (!honorEnabled && !bountyEnabled)
            return;
        const ULONGLONG now = GetTickCount64();
        if (now < nextUpdateAt)
            return;
        nextUpdateAt = now + updateIntervalMs;
        if (MissionState::IsActiveOrRecentlyEnded(missionGraceMs))
        {
            Reset();
            playerPed = 0;
            wantedSuppressionUntil = 0;
            wantedSuppressed = false;
            return;
        }

        nativeInit(playerPedId);
        const std::uint32_t ped = static_cast<std::uint32_t>(*nativeCall());
        if (ped == 0)
        {
            Reset();
            playerPed = 0;
            return;
        }
        if (ped != playerPed)
        {
            Reset();
            playerPed = ped;
        }

        nativeInit(playerId);
        const int player = static_cast<int>(*nativeCall());
        if (IsPlayerWanted(player))
        {
            wantedSuppressionUntil = now + wantedGraceMs;
            if (!wantedSuppressed && Logger::IsDebugEnabled())
                Logger::Log("[DEBUG] SelfDefense: wanted status active; corrections suspended.");
            wantedSuppressed = true;
            Reset();
            return;
        }
        if (now < wantedSuppressionUntil)
        {
            Reset();
            return;
        }
        if (wantedSuppressed && Logger::IsDebugEnabled())
            Logger::Log("[DEBUG] SelfDefense: wanted status cleared; protection resumed.");
        wantedSuppressed = false;

        nativeInit(getEntityHealth);
        nativePush64(playerPed);
        const int health = static_cast<int>(*nativeCall());
        if (health <= 0)
        {
            Reset();
            return;
        }
        if (!healthCaptured)
        {
            playerHealth = health;
            healthCaptured = true;
            return;
        }

        if (health < playerHealth)
        {
            const std::uint32_t attacker = ReadEntity(lastEntityToDamage, playerPed);
            if (IsValidHumanAggressor(attacker) && HasDamaged(playerPed, attacker))
                RecordAggressor(attacker, now, player);
        }
        playerHealth = health;

        const bool defensiveKill = CheckDefensiveDeaths(now);
        if (defensiveKill)
        {
            if (honorEnabled)
                honorProtectionUntil = now + honorCorrectionWindowMs;
            if (bountyEnabled)
            {
                bountyProtectionUntil = now + bountyCorrectionWindowMs;
                bountyFirstCorrectionAt = 0;
            }
        }

        CorrectHonor(now);
        CorrectBounty(now, player);

        if (!defensiveKill && now >= honorProtectionUntil && now >= bountyProtectionUntil && HasAggressors())
        {
            if (honorEnabled)
            {
                if (auto* honor = getGlobalPtr(0x2BA2))
                {
                    const auto current = static_cast<std::int32_t>(*honor);
                    if (current >= -320 && current <= 320)
                    {
                        honorBaseline = current;
                        honorBaselineValid = true;
                    }
                }
            }
            if (bountyEnabled)
            {
                const int current = GetBounty(player);
                if (current >= 0)
                {
                    bountyBaseline = current;
                    bountyBaselineValid = true;
                }
            }
        }
    }

    bool NeedsFastUpdate()
    {
        return honorEnabled || bountyEnabled;
    }
}
