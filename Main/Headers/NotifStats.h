#pragma once

#include <string>

namespace Main::NotifStats
{
    inline constexpr char Version[] = "v1.0.0";

    void Configure(bool enabled, bool settingsMenu = true, int menuKey = 0x77, unsigned int menuModifier = 0);
    void Update();
    void Warning(const std::string& message);
}
