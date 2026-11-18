#include "MissionState.h"
#include "../../Headers/Logger.h"
#include "main.h"
#include <windows.h>
#include <cstdint>

namespace
{
    constexpr std::uint64_t getMissionFlag = 0xB15CD1CF58771DE1ULL;
    constexpr ULONGLONG missionCheckIntervalMs = 50;

    bool missionActive = false;
    bool hasCheckedMission = false;
    ULONGLONG lastMissionActiveAt = 0;
    ULONGLONG nextMissionCheckAt = 0;

    void RefreshMissionState()
    {
        const ULONGLONG now = GetTickCount64();
        if (hasCheckedMission && now < nextMissionCheckAt)
            return;

        nativeInit(getMissionFlag);
        const bool active = static_cast<std::uint32_t>(*nativeCall()) != 0;
        if (active)
            lastMissionActiveAt = now;
        if (hasCheckedMission && active != missionActive && Logger::IsDebugEnabled())
            Logger::Log(active ? "[DEBUG] MissionState: mission started." :
                "[DEBUG] MissionState: mission ended.");

        missionActive = active;
        nextMissionCheckAt = now + missionCheckIntervalMs;
        hasCheckedMission = true;
    }
}

namespace Main::MissionState
{
    bool IsActive()
    {
        RefreshMissionState();
        return missionActive;
    }

    bool IsActiveOrRecentlyEnded(std::uint64_t gracePeriodMs)
    {
        RefreshMissionState();
        return missionActive || (gracePeriodMs != 0 && lastMissionActiveAt != 0 &&
            GetTickCount64() - lastMissionActiveAt < gracePeriodMs);
    }
}
