#include "../Headers/NotifStats.h"
#include "../Headers/ConfigMenu.h"
#include "../../Headers/Logger.h"
#include "main.h"
#include <windows.h>
#include <atomic>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <utility>

namespace
{
    constexpr std::uint64_t createVarString = 0xFA925AC00EB830B9ULL;
    constexpr std::uint64_t getHashKey = 0xFD340785ADF8CFB7ULL;
    constexpr std::uint64_t postSampleToast = 0x26E87218390E6729ULL;
    constexpr std::uint64_t getCurrentFeedMessage = 0xC17F69E1418CD11FULL;
    constexpr std::uint64_t clearFeedChannel = 0xDD1232B332CBB9E7ULL;
    constexpr std::uint64_t clearFeedMessage = 0x2F901291EF177B02ULL;
    constexpr std::uint64_t getFeedMessageState = 0x59FA676177DBE4C9ULL;
    constexpr std::uint64_t toastChannel = 6;
    constexpr std::uint32_t completeFeedState = 6;
    constexpr auto toastDuration = std::chrono::milliseconds(6500);
    constexpr std::uint64_t requestTextureDict = 0xC1BA29DF5631B0F8ULL;
    constexpr std::uint64_t hasTextureDictLoaded = 0x54D6900929CCF162ULL;
    constexpr std::uint64_t releaseTextureDict = 0x4ACA10A91F66F1E2ULL;
    constexpr std::uint64_t isLoadingScreenActive = 0x71D4BF5890659B0CULL;
    constexpr std::uint64_t isPlayerPlaying = 0xBFFB35986CAAE58CULL;
    constexpr std::uint64_t playerId = 0x217E9DC48139933DULL;
    constexpr std::uint64_t playerPedId = 0x096275889B8E0EE0ULL;
    constexpr std::uint64_t doesEntityExist = 0xD42BD6EB2E0F1677ULL;
    constexpr std::uint64_t isScreenFadedIn = 0x37F9A426FBCF4AF2ULL;
    constexpr char textureDict[] = "generic_textures";
    constexpr char successIcon[] = "tick";
    constexpr char warningIcon[] = "cross";
    constexpr char iconColor[] = "COLOR_PURE_WHITE";
    constexpr char successIconColor[] = "COLOR_REDDARK";
    constexpr char notificationTextColor[] = "~COLOR_YELLOWVERYLIGHT~";
    using Clock = std::chrono::steady_clock;

    struct alignas(16) FeedDuration
    {
        std::array<std::uint64_t, 14> fields{};
    };

    struct alignas(8) FeedInfo
    {
        std::array<std::uint64_t, 10> fields{};
    };

    static_assert(sizeof(FeedDuration) >= 104);
    static_assert(sizeof(FeedInfo) == 80);

    std::atomic<bool> enabled{ true };
    std::atomic<bool> warningPending{ false };
    std::mutex warningMutex;
    std::deque<std::string> warningQueue;
    std::string lastWarningShown;
    Clock::time_point lastWarningShownAt{};
    bool initializedNotificationShown = false;
    std::deque<std::uint32_t> toastHandles;
    Clock::time_point toastClearDeadline{};
    std::string startupShortcut;
    bool initialDelayStarted = false;
    bool textureRequested = false;
    bool textureWarningLogged = false;
    int lastReadinessState = -1;
    Clock::time_point initialDelayEnd{};
    Clock::time_point textureRequestTime{};
    Clock::time_point nextReadyCheck{};

    std::string KeyName(int key)
    {
        if (key >= VK_F1 && key <= VK_F24)
            return "F" + std::to_string(key - VK_F1 + 1);
        if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9'))
            return std::string(1, static_cast<char>(key));
        switch (key)
        {
        case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL: return "Ctrl";
        case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT: return "Shift";
        case VK_MENU: case VK_LMENU: case VK_RMENU: return "Alt";
        case VK_INSERT: return "Insert";
        case VK_DELETE: return "Delete";
        case VK_HOME: return "Home";
        case VK_END: return "End";
        case VK_PRIOR: return "PageUp";
        case VK_NEXT: return "PageDown";
        case VK_SPACE: return "Space";
        case VK_TAB: return "Tab";
        case VK_ESCAPE: return "Escape";
        case VK_RETURN: return "Enter";
        case VK_UP: return "Up";
        case VK_DOWN: return "Down";
        case VK_LEFT: return "Left";
        case VK_RIGHT: return "Right";
        default: return "VK" + std::to_string(key);
        }
    }

    std::string ShortcutText(bool settingsMenu, int menuKey, unsigned int modifier)
    {
        if (!settingsMenu || menuKey == 0)
            return {};

        std::string text = "Press ";
        if (modifier & 0x10000U)
        {
            const unsigned int customKey = modifier & 0xFFU;
            if (customKey)
                text += KeyName(static_cast<int>(customKey)) + " + ";
        }
        else
        {
            if (modifier & 1U)
                text += "Ctrl + ";
            if (modifier & 2U)
                text += "Shift + ";
            if (modifier & 4U)
                text += "Alt + ";
        }
        return text + KeyName(menuKey) + " to configure settings";
    }

    std::uint64_t InvokeWithoutArgs(std::uint64_t hash)
    {
        nativeInit(hash);
        const auto result = nativeCall();
        return result ? *result : 0;
    }

    std::uint64_t InvokeWithString(std::uint64_t hash, const char* string)
    {
        nativeInit(hash);
        nativePush64(reinterpret_cast<std::uint64_t>(string));
        const auto result = nativeCall();
        return result ? *result : 0;
    }

    std::uint64_t LiteralString(const char* text)
    {
        nativeInit(createVarString);
        nativePush64(10);
        nativePush64(reinterpret_cast<std::uint64_t>("LITERAL_STRING"));
        nativePush64(reinterpret_cast<std::uint64_t>(text));
        const auto result = nativeCall();
        return result ? *result : 0;
    }

    bool GameplayReady()
    {
        const bool loading = static_cast<std::uint32_t>(InvokeWithoutArgs(isLoadingScreenActive)) != 0;
        const auto player = static_cast<std::uint32_t>(InvokeWithoutArgs(playerId));
        nativeInit(isPlayerPlaying);
        nativePush64(player);
        const auto playing = nativeCall();
        const bool playerPlaying = playing && static_cast<std::uint32_t>(*playing) != 0;
        bool visibleGameplay = false;
        if (loading && playerPlaying)
        {
            const auto ped = static_cast<std::uint32_t>(InvokeWithoutArgs(playerPedId));
            if (ped != 0)
            {
                nativeInit(doesEntityExist);
                nativePush64(ped);
                const auto exists = nativeCall();
                if (exists && static_cast<std::uint32_t>(*exists) != 0)
                    visibleGameplay = static_cast<std::uint32_t>(InvokeWithoutArgs(isScreenFadedIn)) != 0;
            }
        }
        const bool ready = playerPlaying && (!loading || visibleGameplay);
        const int state = ready ? (visibleGameplay ? 3 : 2) : (loading ? 0 : 1);
        if (state != lastReadinessState)
        {
            Logger::Log(("[INFO] Notification: game state loading=" + std::to_string(loading) +
                ", playerId=" + std::to_string(player) + ", playerPlaying=" + std::to_string(playerPlaying) +
                ", visibleGameplay=" + std::to_string(visibleGameplay) + ", ready=" + std::to_string(ready) + ".").c_str());
            lastReadinessState = state;
        }
        return ready;
    }

    bool TextureReady(Clock::time_point now)
    {
        if (!textureRequested)
        {
            nativeInit(requestTextureDict);
            nativePush64(reinterpret_cast<std::uint64_t>(textureDict));
            nativePush64(0);
            nativeCall();
            textureRequested = true;
            textureRequestTime = now;
        }

        if (InvokeWithString(hasTextureDictLoaded, textureDict))
            return true;

        if (!textureWarningLogged && now - textureRequestTime >= std::chrono::seconds(5))
        {
            Logger::Log("[WARNING] Notification: texture dictionary not loaded; waiting.");
            textureWarningLogged = true;
        }
        return false;
    }

    bool IsFeedMessageActive(std::uint32_t handle)
    {
        nativeInit(getFeedMessageState);
        nativePush64(handle);
        const auto result = nativeCall();
        return result && static_cast<std::uint32_t>(*result) < completeFeedState;
    }

    void TrackToast(std::uint64_t handle)
    {
        toastClearDeadline = Clock::now() + toastDuration + std::chrono::seconds(1);
        toastHandles.erase(std::remove_if(toastHandles.begin(), toastHandles.end(), [](std::uint32_t item)
        {
            return !IsFeedMessageActive(item);
        }), toastHandles.end());
        if (static_cast<std::uint32_t>(handle) != 0 && static_cast<std::uint32_t>(handle) != UINT32_MAX)
            toastHandles.push_back(static_cast<std::uint32_t>(handle));
    }

    bool ShowFeedMessage(const std::string& title, const std::string& message, bool warning, std::uint64_t& feedHandle, const std::string& secondary = {})
    {
        const std::string coloredTitle = std::string(notificationTextColor) + title;
        const std::string coloredMessage = std::string(notificationTextColor) + message;
        const std::string coloredSecondary = std::string(notificationTextColor) + secondary;
        const auto titleText = LiteralString(coloredTitle.c_str());
        const auto bodyText = LiteralString(coloredMessage.c_str());
        const auto secondaryText = secondary.empty() ? 0 : LiteralString(coloredSecondary.c_str());
        if (!titleText || !bodyText || (!secondary.empty() && !secondaryText))
            return false;

        FeedDuration duration;
        duration.fields[0] = toastDuration.count();
        duration.fields[6] = secondaryText;
        FeedInfo info;
        info.fields[1] = titleText;
        info.fields[2] = bodyText;
        info.fields[4] = InvokeWithString(getHashKey, textureDict);
        info.fields[5] = InvokeWithString(getHashKey, warning ? warningIcon : successIcon);
        info.fields[6] = InvokeWithString(getHashKey, warning ? iconColor : successIconColor);
        info.fields[7] = 1;

        nativeInit(postSampleToast);
        nativePush64(reinterpret_cast<std::uint64_t>(&duration));
        nativePush64(reinterpret_cast<std::uint64_t>(&info));
        nativePush64(1);
        nativePush64(1);
        const auto result = nativeCall();
        feedHandle = result ? *result : 0;
        return true;
    }
}

namespace Main::NotifStats
{
    void Configure(bool active, bool settingsMenu, int menuKey, unsigned int menuModifier)
    {
        startupShortcut = ShortcutText(settingsMenu, menuKey, menuModifier);
        enabled.store(active, std::memory_order_release);
    }

    void Dismiss()
    {
        initializedNotificationShown = true;
        const bool recentlyPosted = Clock::now() < toastClearDeadline;
        toastClearDeadline = {};
        if (!recentlyPosted && toastHandles.empty())
            return;

        nativeInit(getCurrentFeedMessage);
        nativePush64(toastChannel);
        const auto current = nativeCall();
        const bool ownsCurrent = current && std::find(toastHandles.begin(), toastHandles.end(),
            static_cast<std::uint32_t>(*current)) != toastHandles.end();

        for (auto it = toastHandles.begin(); it != toastHandles.end();)
        {
            if (!IsFeedMessageActive(*it))
            {
                it = toastHandles.erase(it);
                continue;
            }

            nativeInit(clearFeedMessage);
            nativePush64(*it);
            nativePush64(1);
            nativeCall();
            ++it;
        }
        if (recentlyPosted || ownsCurrent)
        {
            nativeInit(clearFeedChannel);
            nativePush64(toastChannel);
            nativePush64(1);
            nativePush64(1);
            nativeCall();
        }
    }

    void Warning(const std::string& message)
    {
        if (message.empty())
            return;

        std::string display = message;
        if (display.size() > 140)
            display = display.substr(0, 137) + "...";

        std::lock_guard<std::mutex> lock(warningMutex);
        if (std::find(warningQueue.begin(), warningQueue.end(), display) != warningQueue.end() ||
            (lastWarningShown == display && Clock::now() - lastWarningShownAt < std::chrono::seconds(30)))
            return;
        if (warningQueue.size() >= 16)
            warningQueue.pop_front();
        warningQueue.push_back(std::move(display));
        warningPending.store(true, std::memory_order_release);
    }

    void Update()
    {
        if (Main::ConfigMenu::IsOpen())
        {
            Dismiss();
            return;
        }
        if (!enabled.load(std::memory_order_acquire) ||
            (initializedNotificationShown && !warningPending.load(std::memory_order_acquire)))
            return;

        const auto now = Clock::now();
        if (now < nextReadyCheck)
            return;

        if (!GameplayReady())
        {
            nextReadyCheck = now + std::chrono::milliseconds(500);
            return;
        }

        if (!initialDelayStarted)
        {
            initialDelayStarted = true;
            initialDelayEnd = now + std::chrono::seconds(2);
        }
        if (now < initialDelayEnd)
            return;
        if (!TextureReady(now))
        {
            nextReadyCheck = now + std::chrono::milliseconds(250);
            return;
        }

        if (!initializedNotificationShown)
        {
            std::uint64_t handle = 0;
            if (!ShowFeedMessage("Betterment Improvements", std::string("Initialized Successfully | ") + Version, false, handle, startupShortcut))
            {
                nextReadyCheck = now + std::chrono::seconds(1);
                Logger::Log("[WARNING] Notification: could not create text; retrying.");
                return;
            }
            initializedNotificationShown = true;
            TrackToast(handle);
            if (warningPending.load(std::memory_order_acquire))
                nextReadyCheck = now + std::chrono::milliseconds(1200);
            Logger::Log(("[INFO] Notification: startup toast sent (handle=" + std::to_string(handle) + "); visibility not verified.").c_str());
            if (warningPending.load(std::memory_order_acquire))
                return;
        }

        if (!warningPending.load(std::memory_order_acquire))
            return;

        std::string message;
        {
            std::lock_guard<std::mutex> lock(warningMutex);
            if (warningQueue.empty())
            {
                warningPending.store(false, std::memory_order_release);
                return;
            }
            message = std::move(warningQueue.front());
            warningQueue.pop_front();
            warningPending.store(!warningQueue.empty(), std::memory_order_release);
        }

        std::uint64_t handle = 0;
        if (ShowFeedMessage("Betterment Improvements Warning", message, true, handle))
        {
            {
                std::lock_guard<std::mutex> lock(warningMutex);
                lastWarningShown = message;
                lastWarningShownAt = now;
            }
            TrackToast(handle);
            Logger::Log(("[INFO] Notification: warning toast sent (handle=" + std::to_string(handle) + "); visibility not verified.").c_str());
            nextReadyCheck = now + std::chrono::milliseconds(1200);
        }
        else
        {
            std::lock_guard<std::mutex> lock(warningMutex);
            if (warningQueue.size() >= 16)
                warningQueue.pop_back();
            warningQueue.push_front(std::move(message));
            warningPending.store(true, std::memory_order_release);
            nextReadyCheck = now + std::chrono::seconds(1);
            Logger::Log("[WARNING] Notification: could not create warning text; retrying.");
        }
    }
}
