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
    void SetHotkey(int key, unsigned int modifier);
    void Update();
    bool IsOpen();
    bool IsFpsCounterEnabled();
    void Shutdown();
    void OnKeyboard(DWORD key, WORD repeats, BYTE scanCode, BOOL isExtended, BOOL isWithAlt, BOOL wasDownBefore, BOOL isUpNow);
}
