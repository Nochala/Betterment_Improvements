#pragma once

#include <string>

namespace Main::NotifStats
{
    inline constexpr char Version[] = "v1.0.1";

    void Configure(bool enabled, bool settingsMenu = true, int menuKey = 0x77, unsigned int menuModifier = 0);
    void Update();
    void Dismiss();
    void Warning(const std::string& message);
}
