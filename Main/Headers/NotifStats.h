#pragma once

#include "../../Headers/Version.h"
#include <string>

namespace Main::NotifStats
{
    inline constexpr auto& Version = ::Version::Mod;

    void Configure(bool enabled, bool settingsMenu = true, int menuKey = 0x77, unsigned int menuModifier = 0);
    void Update();
    void Dismiss();
    void Warning(const std::string& message);
}
