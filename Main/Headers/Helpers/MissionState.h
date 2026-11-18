#pragma once
#include <cstdint>

namespace Main::MissionState
{
    bool IsActive();
    bool IsActiveOrRecentlyEnded(std::uint64_t gracePeriodMs);
}
