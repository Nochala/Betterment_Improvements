#pragma once
#include <windows.h>
#include <string>

namespace GameplayFixes
{
    struct Settings;
}

namespace Main::ConfigMenu
{
    void Initialize(const std::wstring& iniPath, const GameplayFixes::Settings& settings);
    void Update();
    bool IsOpen();
    void Shutdown();
    void OnKeyboard(DWORD key, WORD repeats, BYTE scanCode, BOOL isExtended, BOOL isWithAlt, BOOL wasDownBefore, BOOL isUpNow);
}
