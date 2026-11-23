#include "main.h"
#include "script.h"
#include "Logger.h"
#include "GameplayFixes.h"
#include "../Headers/Version.h"
#include "../Main/Headers/CrashFixes.h"
#include "../Main/Headers/WaterReflections.h"
#include "../Main/Headers/Display.h"
#include "../Main/Headers/DLSS.h"
#include "../Main/Headers/Camera.h"
#include "../Main/Headers/Modules.h"
#include "../Main/Headers/Gunz.h"
#include "../Main/Headers/Horse.h"
#include "../Main/Headers/ConfigMenu.h"

namespace
{
    DWORD WINAPI EarlyStartupThread(LPVOID)
    {
        Version::LogStartup();
        GameplayFixes::InitializeEarly();
        Main::CrashFixes::ObserveStreamingPoolStartup();
        return 0;
    }
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
    {
        DisableThreadLibraryCalls(module);
        Logger::LogModuleLoaded();
        scriptRegister(module, ScriptMain);
        keyboardHandlerRegister(Main::ConfigMenu::OnKeyboard);

        HANDLE worker = CreateThread(nullptr, 0, EarlyStartupThread, nullptr, 0, nullptr);
        if (worker != nullptr)
            CloseHandle(worker);
        else
            OutputDebugStringA("[BettermentImprovements] Early startup thread could not be created; configuration will initialize in ScriptMain.\n");
        break;
    }
    case DLL_PROCESS_DETACH:
        keyboardHandlerUnregister(Main::ConfigMenu::OnKeyboard);
        Main::ConfigMenu::Shutdown();
        Main::Gunz::Shutdown();
        Main::Horse::Shutdown();
        Main::Camera::Shutdown();
        Main::CrashFixes::ShutdownCrashDiagnostics();
        Main::CrashFixes::ShutdownPackfileLimits();
        Main::DLSS::Shutdown();
        Main::Display::ShutdownCamera();
        Main::WaterReflections::Shutdown();
        Main::CrashFixes::ShutdownStreamingPoolOverride();
        Main::CrashFixes::ShutdownImproveTextureStreaming();
        scriptUnregister(module);
        break;
    }

    return TRUE;
}
