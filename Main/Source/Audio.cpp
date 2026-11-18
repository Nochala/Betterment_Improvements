#include "Audio.h"
#include "Helpers/MissionState.h"
#include "../../Headers/Logger.h"
#include "main.h"
#include <windows.h>
#include <cstdint>
#include <cstdio>

namespace Main::Audio
{
    namespace
    {
        constexpr std::uint64_t playerPedId = 0x096275889B8E0EE0ULL;
        constexpr std::uint64_t playerId = 0x217E9DC48139933DULL;
        constexpr std::uint64_t isPlayerControlOn = 0x7964097FCE4C244BULL;
        constexpr std::uint64_t isScreenFadedIn = 0x37F9A426FBCF4AF2ULL;
        constexpr std::uint64_t isPedInCombat = 0x4859F1FC66A6278EULL;
        constexpr std::uint64_t isPedInMeleeCombat = 0x4E209B2C1EAD5159ULL;
        constexpr std::uint64_t getPlayerWantedLevel = 0xABC532F9098BFD9DULL;
        constexpr std::uint64_t getWantedScore = 0xDD5FD601481F648BULL;
        constexpr std::uint64_t isScriptedMusicPlaying = 0x845FFC3A4FEEFA3EULL;
        constexpr std::uint64_t prepareMusicEvent = 0x1E5185B72EF5158AULL;
        constexpr std::uint64_t triggerMusicEvent = 0x706D57B0F50DA710ULL;
        constexpr std::uint64_t setAudioFlag = 0xB9EFD5C25018725AULL;
        constexpr ULONGLONG ambientCheckIntervalMs = 1200;
        constexpr ULONGLONG activeCheckIntervalMs = 400;
        constexpr ULONGLONG musicTailMs = 5000;
        constexpr ULONGLONG musicEventIntervalMs = 6000;
        constexpr ULONGLONG musicEventMinGapMs = 1500;
        constexpr ULONGLONG wantedEventIntervalMs = 6000;
        constexpr ULONGLONG audioFlagRefreshMs = 6000;
        constexpr const char* openWorldMusicFlag = "OpenWorldMusicDisabled";
        constexpr const char* wantedDisableEvent = "ME_WANTED_MUSIC_DISABLED";

        enum class MusicContext : unsigned int
        {
            Ambient,
            Combat,
            Wanted
        };

        bool disableAmbientMusic = false;
        bool disableCombatMusic = false;
        bool disableWantedMusic = false;
        bool musicPrepareReady = false;
        bool wantedEventPrepareReady = false;
        bool openWorldMusicFlagRequested = false;
        bool suppressionWasActive = false;
        MusicContext lastSuppressedContext = MusicContext::Ambient;
        bool wantedEventAttemptLogged = false;
        bool musicAttemptLogged[3] = {};
        std::uint32_t lastPlayerPed = 0;
        ULONGLONG lastCombatAt = 0;
        ULONGLONG lastWantedAt = 0;
        ULONGLONG nextMusicCheckAt = 0;
        ULONGLONG nextMusicPrepareAt = 0;
        ULONGLONG nextMusicEventAt = 0;
        ULONGLONG lastMusicEventAt = 0;
        ULONGLONG nextMusicLogAt = 0;
        ULONGLONG nextWantedEventAt = 0;
        ULONGLONG lastWantedEventAt = 0;
        ULONGLONG nextWantedEventPrepareAt = 0;
        ULONGLONG nextWantedEventLogAt = 0;
        ULONGLONG nextAudioFlagRefreshAt = 0;

        void UpdateOpenWorldMusicFlag(bool requested, ULONGLONG now)
        {
            if (requested)
            {
                if (openWorldMusicFlagRequested && now < nextAudioFlagRefreshAt)
                    return;
            }
            else if (!openWorldMusicFlagRequested)
                return;

            nativeInit(setAudioFlag);
            nativePush64(reinterpret_cast<std::uint64_t>(openWorldMusicFlag));
            nativePush64(requested ? 1 : 0);
            nativeCall();
            openWorldMusicFlagRequested = requested;
            nextAudioFlagRefreshAt = requested ? now + audioFlagRefreshMs : 0;
        }

        std::uint32_t GetPlayerPed()
        {
            nativeInit(playerPedId);
            return static_cast<std::uint32_t>(*nativeCall());
        }

        bool IsWanted()
        {
            nativeInit(playerId);
            const std::uint32_t player = static_cast<std::uint32_t>(*nativeCall());
            nativeInit(getPlayerWantedLevel);
            nativePush64(player);
            if (static_cast<std::int32_t>(*nativeCall()) > 0)
                return true;
            nativeInit(getWantedScore);
            nativePush64(player);
            return static_cast<std::int32_t>(*nativeCall()) > 0;
        }

        bool IsInCombat(std::uint32_t ped)
        {
            nativeInit(isPedInCombat);
            nativePush64(ped);
            nativePush64(0);
            if (static_cast<std::uint32_t>(*nativeCall()) != 0)
                return true;
            nativeInit(isPedInMeleeCombat);
            nativePush64(ped);
            return static_cast<std::uint32_t>(*nativeCall()) != 0;
        }

        const char* ContextName(MusicContext context)
        {
            switch (context)
            {
            case MusicContext::Wanted: return "DisableWantedMusic";
            case MusicContext::Combat: return "DisableCombatMusic";
            default: return "DisableAmbientMusic";
            }
        }
    }

    void Initialize(bool ambient, bool combat, bool wanted)
    {
        UpdateOpenWorldMusicFlag(false, GetTickCount64());
        disableAmbientMusic = ambient;
        disableCombatMusic = combat;
        disableWantedMusic = wanted;
        musicPrepareReady = false;
        wantedEventPrepareReady = false;
        suppressionWasActive = false;
        lastSuppressedContext = MusicContext::Ambient;
        wantedEventAttemptLogged = false;
        lastPlayerPed = 0;
        lastCombatAt = 0;
        lastWantedAt = 0;
        nextMusicCheckAt = 0;
        nextMusicPrepareAt = 0;
        nextMusicEventAt = 0;
        lastMusicEventAt = 0;
        nextMusicLogAt = 0;
        nextWantedEventAt = 0;
        lastWantedEventAt = 0;
        nextWantedEventPrepareAt = 0;
        nextWantedEventLogAt = 0;
        nextAudioFlagRefreshAt = 0;
        for (bool& logged : musicAttemptLogged)
            logged = false;

        if (disableAmbientMusic)
            Logger::Log("[INFO] DisableAmbientMusic: active outside combat and wanted states.");
        if (disableCombatMusic)
            Logger::Log("[INFO] DisableCombatMusic: active during free-roam combat.");
        if (disableWantedMusic)
            Logger::Log("[INFO] DisableWantedMusic: active while wanted in free roam.");
    }

    void Update()
    {
        if (!disableAmbientMusic && !disableCombatMusic && !disableWantedMusic)
            return;

        const ULONGLONG now = GetTickCount64();
        if (now < nextMusicCheckAt)
            return;
        nextMusicCheckAt = now + (disableCombatMusic || disableWantedMusic ? activeCheckIntervalMs : ambientCheckIntervalMs);

        const bool mission = Main::MissionState::IsActiveOrRecentlyEnded(5000);
        const std::uint32_t ped = GetPlayerPed();
        nativeInit(isScreenFadedIn);
        const bool screenReady = static_cast<std::uint32_t>(*nativeCall()) != 0;
        if (ped == 0 || !screenReady || (lastPlayerPed != 0 && ped != lastPlayerPed))
        {
            musicPrepareReady = false;
            nextMusicPrepareAt = 0;
            wantedEventPrepareReady = false;
            nextWantedEventAt = 0;
            nextWantedEventPrepareAt = 0;
            lastCombatAt = 0;
            lastWantedAt = 0;
            suppressionWasActive = false;
            nextMusicEventAt = 0;
        }
        lastPlayerPed = ped;

        bool playerControl = false;
        if (!mission && ped != 0 && screenReady)
        {
            nativeInit(isPlayerControlOn);
            nativePush64(0);
            playerControl = static_cast<std::uint32_t>(*nativeCall()) != 0;
        }
        if (mission || ped == 0 || !screenReady || !playerControl)
        {
            UpdateOpenWorldMusicFlag(false, now);
            suppressionWasActive = false;
            nextMusicEventAt = 0;
            nextWantedEventAt = 0;
            if (Logger::IsDebugEnabled() && now >= nextMusicLogAt)
            {
                char message[192] = {};
                std::snprintf(message, sizeof(message),
                    "[DEBUG] Audio: waiting to stop music (mission=%d, ped=%d, screen=%d, control=%d).",
                    mission ? 1 : 0, ped != 0 ? 1 : 0, screenReady ? 1 : 0, playerControl ? 1 : 0);
                Logger::Log(message);
                nextMusicLogAt = now + 30000;
            }
            return;
        }

        const bool wanted = IsWanted();
        const bool combat = !wanted && IsInCombat(ped);
        if (wanted)
            lastWantedAt = now;
        else if (combat)
            lastCombatAt = now;

        MusicContext context = MusicContext::Ambient;
        if (wanted)
            context = MusicContext::Wanted;
        else if (combat)
            context = MusicContext::Combat;
        else if (lastWantedAt != 0 && now - lastWantedAt < musicTailMs)
            context = MusicContext::Wanted;
        else if (lastCombatAt != 0 && now - lastCombatAt < musicTailMs)
            context = MusicContext::Combat;

        const bool suppressAmbient = context == MusicContext::Ambient && disableAmbientMusic;
        const bool suppressCombat = context == MusicContext::Combat && disableCombatMusic;
        const bool suppressWanted = context == MusicContext::Wanted && disableWantedMusic;
        const bool suppress = suppressAmbient || suppressCombat || suppressWanted;
        UpdateOpenWorldMusicFlag(suppress, now);
        if (!suppress)
        {
            suppressionWasActive = false;
            nextMusicEventAt = 0;
            nextWantedEventAt = 0;
            return;
        }

        if (!suppressionWasActive || context != lastSuppressedContext)
        {
            nextMusicEventAt = lastMusicEventAt != 0 && now - lastMusicEventAt < musicEventMinGapMs
                ? lastMusicEventAt + musicEventMinGapMs : 0;
            if (context == MusicContext::Wanted)
                nextWantedEventAt = lastWantedEventAt != 0 && now - lastWantedEventAt < musicEventMinGapMs
                    ? lastWantedEventAt + musicEventMinGapMs : 0;
        }
        suppressionWasActive = true;
        lastSuppressedContext = context;

        if (suppressWanted && now >= nextWantedEventAt)
        {
            if (!wantedEventPrepareReady && now >= nextWantedEventPrepareAt)
            {
                nativeInit(prepareMusicEvent);
                nativePush64(reinterpret_cast<std::uint64_t>(wantedDisableEvent));
                wantedEventPrepareReady = static_cast<std::uint32_t>(*nativeCall()) != 0;
                nextWantedEventPrepareAt = now + 10000;
            }
            nativeInit(triggerMusicEvent);
            nativePush64(reinterpret_cast<std::uint64_t>(wantedDisableEvent));
            const bool wantedEventAccepted = static_cast<std::uint32_t>(*nativeCall()) != 0;
            lastWantedEventAt = now;
            nextWantedEventAt = now + wantedEventIntervalMs;
            if (!wantedEventAttemptLogged || (Logger::IsDebugEnabled() && now >= nextWantedEventLogAt))
            {
                char message[224] = {};
                std::snprintf(message, sizeof(message),
                    "[%s] DisableWantedMusic: disable event sent (prepared=%d, accepted=%d).",
                    wantedEventAttemptLogged ? "DEBUG" : "INFO",
                    wantedEventPrepareReady ? 1 : 0, wantedEventAccepted ? 1 : 0);
                Logger::Log(message);
                wantedEventAttemptLogged = true;
                nextWantedEventLogAt = now + 30000;
            }
        }
        else if (context != MusicContext::Wanted)
        {
            nextWantedEventAt = 0;
        }

        if (now < nextMusicEventAt)
            return;
        nextMusicEventAt = now + musicEventIntervalMs;

        if (!musicPrepareReady && now >= nextMusicPrepareAt)
        {
            nativeInit(prepareMusicEvent);
            nativePush64(reinterpret_cast<std::uint64_t>("MC_MUSIC_STOP"));
            musicPrepareReady = static_cast<std::uint32_t>(*nativeCall()) != 0;
            nextMusicPrepareAt = now + 10000;
        }

        nativeInit(triggerMusicEvent);
        nativePush64(reinterpret_cast<std::uint64_t>("MC_MUSIC_STOP"));
        const bool triggered = static_cast<std::uint32_t>(*nativeCall()) != 0;
        lastMusicEventAt = now;
        const unsigned int contextIndex = static_cast<unsigned int>(context);
        if (!musicAttemptLogged[contextIndex] || (Logger::IsDebugEnabled() && now >= nextMusicLogAt))
        {
            nativeInit(isScriptedMusicPlaying);
            const bool scripted = static_cast<std::uint32_t>(*nativeCall()) != 0;
            char message[224] = {};
            std::snprintf(message, sizeof(message),
                "[%s] %s: music stop requested (prepared=%d, accepted=%d, scripted=%d, openWorldFlag=%d).",
                musicAttemptLogged[contextIndex] ? "DEBUG" : "INFO", ContextName(context),
                musicPrepareReady ? 1 : 0, triggered ? 1 : 0, scripted ? 1 : 0,
                openWorldMusicFlagRequested ? 1 : 0);
            Logger::Log(message);
            musicAttemptLogged[contextIndex] = true;
            nextMusicLogAt = now + 30000;
        }
    }
}
