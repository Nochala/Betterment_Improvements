#include "Lighting.h"
#include "../../Headers/Logger.h"
#include "main.h"
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>

namespace
{
    struct alignas(8) NativeVector3
    {
        alignas(8) float x;
        alignas(8) float y;
        alignas(8) float z;
    };
    static_assert(sizeof(NativeVector3) == 24, "NativeVector3 size mismatch");

    constexpr std::uint64_t playerPedId = 0x096275889B8E0EE0ULL;
    constexpr std::uint64_t getEntityMatrix = 0x3A9B1120AF13FBF2ULL;
    constexpr std::uint64_t getInteriorAtCoords = 0xCDD36C9E5C469070ULL;
    constexpr std::uint64_t getInteriorFromEntity = 0xB417689857646F61ULL;
    constexpr std::uint64_t getInteriorFromPrimaryView = 0xBC8A281FF125C655ULL;
    constexpr std::uint64_t isValidInterior = 0x017C1B3159F79F6CULL;
    constexpr std::uint64_t isInteriorReady = 0x941560D2D45DBFC8ULL;
    constexpr std::uint64_t getProxyInteriorIndex = 0x5D1C5D8E62E8EE1CULL;
    constexpr std::uint64_t isProxyArtificialLightsEnabled = 0x113857D66A9CABE6ULL;
    constexpr std::uint64_t setProxyArtificialLightsState = 0xBFCB17895BB99E4EULL;
    constexpr std::uint64_t disableFarArtificialLights = 0xCD284E2F6AC27EE9ULL;
    constexpr ULONGLONG interiorCheckIntervalMs = 500;
    constexpr ULONGLONG interiorRestoreDelayMs = 3000;
    constexpr int maxTrackedInteriors = 16;

    struct TrackedInterior
    {
        std::uint32_t interior = 0;
        int proxy = -1;
        bool originallyEnabled = false;
        ULONGLONG lastSeen = 0;
    };


    bool interiorLightingDisabled = false;
    bool distantLightsDisabled = false;
    bool distantApplied = false;
    TrackedInterior tracked[maxTrackedInteriors];
    ULONGLONG nextInteriorCheck = 0;

    std::uint64_t FloatArg(float value)
    {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        return bits;
    }

    std::uint32_t InvokeInt(std::uint64_t hash, std::uint64_t arg)
    {
        nativeInit(hash);
        nativePush64(arg);
        return static_cast<std::uint32_t>(*nativeCall());
    }

    bool InteriorReady(std::uint32_t interior)
    {
        return interior != 0 && InvokeInt(isValidInterior, interior) != 0 &&
            InvokeInt(isInteriorReady, interior) != 0;
    }

    int ProxyIndex(std::uint32_t interior)
    {
        return static_cast<int>(InvokeInt(getProxyInteriorIndex, interior));
    }

    bool ProxyLightsEnabled(int proxy)
    {
        return InvokeInt(isProxyArtificialLightsEnabled, static_cast<std::uint32_t>(proxy)) != 0;
    }

    void SetProxyLights(int proxy, bool enabled)
    {
        nativeInit(setProxyArtificialLightsState);
        nativePush64(static_cast<std::uint32_t>(proxy));
        nativePush64(enabled ? 1 : 0);
        nativeCall();
    }

    void RestoreInterior(TrackedInterior& entry)
    {
        if (entry.proxy > 0 && entry.originallyEnabled && InteriorReady(entry.interior) &&
            ProxyIndex(entry.interior) == entry.proxy && !ProxyLightsEnabled(entry.proxy))
            SetProxyLights(entry.proxy, true);
        entry = TrackedInterior{};
    }

    void ObserveInterior(std::uint32_t interior, ULONGLONG now)
    {
        if (!InteriorReady(interior))
            return;

        const int proxy = ProxyIndex(interior);
        if (proxy <= 0)
            return;

        for (auto& entry : tracked)
        {
            if (entry.proxy != proxy)
                continue;
            entry.interior = interior;
            entry.lastSeen = now;
            if (ProxyLightsEnabled(proxy))
                SetProxyLights(proxy, false);
            return;
        }

        for (auto& entry : tracked)
        {
            if (entry.proxy > 0)
                continue;
            entry.interior = interior;
            entry.proxy = proxy;
            entry.lastSeen = now;
            entry.originallyEnabled = ProxyLightsEnabled(proxy);
            if (entry.originallyEnabled)
                SetProxyLights(proxy, false);
            if (Logger::IsDebugEnabled())
                Logger::Debug((std::string("DisableInteriorLighting: nearby interior=") +
                    std::to_string(interior) + ", proxy=" + std::to_string(proxy) +
                    ", originallyEnabled=" + (entry.originallyEnabled ? "true" : "false")).c_str());
            return;
        }
    }

    std::uint32_t InteriorAt(float x, float y, float z)
    {
        nativeInit(getInteriorAtCoords);
        nativePush64(FloatArg(x));
        nativePush64(FloatArg(y));
        nativePush64(FloatArg(z));
        return static_cast<std::uint32_t>(*nativeCall());
    }

    void ScanNearby(std::uint32_t ped, ULONGLONG now)
    {
        nativeInit(getInteriorFromPrimaryView);
        const std::uint32_t viewInterior = static_cast<std::uint32_t>(*nativeCall());
        if (viewInterior != 0)
            ObserveInterior(viewInterior, now);
        ObserveInterior(InvokeInt(getInteriorFromEntity, ped), now);

        NativeVector3 right{};
        NativeVector3 forward{};
        NativeVector3 up{};
        NativeVector3 position{};
        nativeInit(getEntityMatrix);
        nativePush64(ped);
        nativePush64(reinterpret_cast<std::uint64_t>(&right));
        nativePush64(reinterpret_cast<std::uint64_t>(&forward));
        nativePush64(reinterpret_cast<std::uint64_t>(&up));
        nativePush64(reinterpret_cast<std::uint64_t>(&position));
        nativeCall();

        constexpr float diagonal = 0.70710678f;
        constexpr float samples[][2] = {
            { 0.0f, 0.0f },
            { 5.0f, 0.0f }, { -5.0f, 0.0f }, { 0.0f, 5.0f }, { 0.0f, -5.0f },
            { 12.0f, 0.0f }, { -12.0f, 0.0f }, { 0.0f, 12.0f }, { 0.0f, -12.0f },
            { 12.0f * diagonal, 12.0f * diagonal }, { -12.0f * diagonal, 12.0f * diagonal },
            { 12.0f * diagonal, -12.0f * diagonal }, { -12.0f * diagonal, -12.0f * diagonal },
            { 22.0f, 0.0f }, { -22.0f, 0.0f }, { 0.0f, 22.0f }, { 0.0f, -22.0f },
            { 22.0f * diagonal, 22.0f * diagonal }, { -22.0f * diagonal, 22.0f * diagonal },
            { 22.0f * diagonal, -22.0f * diagonal }, { -22.0f * diagonal, -22.0f * diagonal },
            { 30.0f, 0.0f }, { -30.0f, 0.0f }, { 0.0f, 30.0f }, { 0.0f, -30.0f }
        };

        std::uint32_t observed[32] = {};
        int observedCount = 0;
        for (const auto& sample : samples)
        {
            const float x = position.x + sample[0];
            const float y = position.y + sample[1];
            for (float height : { 1.0f, 3.0f })
            {
                const std::uint32_t interior = InteriorAt(x, y, position.z + height);
                if (interior == 0)
                    continue;
                bool seen = false;
                for (int i = 0; i < observedCount; ++i)
                    if (observed[i] == interior)
                    {
                        seen = true;
                        break;
                    }
                if (!seen && observedCount < static_cast<int>(sizeof(observed) / sizeof(observed[0])))
                {
                    observed[observedCount++] = interior;
                    ObserveInterior(interior, now);
                }
                break;
            }
        }

        for (auto& entry : tracked)
            if (entry.proxy > 0 && now - entry.lastSeen > interiorRestoreDelayMs)
                RestoreInterior(entry);
    }
}

namespace Main::Lighting
{
    void Initialize(bool disableInteriorLighting, bool disableDistantLights)
    {
        if (interiorLightingDisabled && !disableInteriorLighting)
            for (auto& entry : tracked)
                if (entry.proxy > 0)
                    RestoreInterior(entry);
        if (distantApplied && !disableDistantLights)
        {
            nativeInit(disableFarArtificialLights);
            nativePush64(0);
            nativeCall();
            distantApplied = false;
        }
        interiorLightingDisabled = disableInteriorLighting;
        distantLightsDisabled = disableDistantLights;
        if (interiorLightingDisabled)
            Logger::Log("[INFO] DisableInteriorLighting: watching nearby interiors.");
        if (distantLightsDisabled)
            Logger::Log("[INFO] DisableDistantLights: enabled.");
    }

    void Update()
    {
        if (!interiorLightingDisabled && (!distantLightsDisabled || distantApplied))
            return;

        const ULONGLONG now = GetTickCount64();
        if (distantLightsDisabled && !distantApplied)
        {
            nativeInit(playerPedId);
            if (static_cast<std::uint32_t>(*nativeCall()) != 0)
            {
                nativeInit(disableFarArtificialLights);
                nativePush64(1);
                nativeCall();
                if (!distantApplied)
                {
                    distantApplied = true;
                    Logger::Log("[INFO] DisableDistantLights: distant lights turned off.");
                }
            }
        }

        if (!interiorLightingDisabled || now < nextInteriorCheck)
            return;
        nextInteriorCheck = now + interiorCheckIntervalMs;
        nativeInit(playerPedId);
        const std::uint32_t ped = static_cast<std::uint32_t>(*nativeCall());
        if (ped != 0)
            ScanNearby(ped, now);
    }
}
