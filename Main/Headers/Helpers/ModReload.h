#pragma once

namespace GameplayFixes
{
    struct Settings;
}

namespace Main::ModReload
{
    bool RequiresGameRestart(const wchar_t* section, const wchar_t* key);
    bool Apply(const GameplayFixes::Settings& current, GameplayFixes::Settings& next);
}
