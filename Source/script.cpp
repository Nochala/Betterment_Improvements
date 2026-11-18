#include "script.h"
#include "main.h"
#include "GameplayFixes.h"
#include "Logger.h"
#include "../Headers/Version.h"
#include "../Main/Headers/CrashFixes.h"
#include "../Main/Headers/NotifStats.h"

void ScriptMain()
{
    Version::LogStartup();
    GameplayFixes::InitializeEarly();
    Logger::LogScriptEntry();
    GameplayFixes::Initialize();
    Logger::Log("[SUCCESS] ScriptMain: initialization complete.");

    bool firstUpdate = true;
    while (true)
    {
        GameplayFixes::Update();
        Main::NotifStats::Update();
        Main::CrashFixes::RecordCrashDiagnosticsHeartbeat();
        if (firstUpdate)
        {
            Logger::Log("[INFO] ScriptMain: first update complete.");
            firstUpdate = false;
        }
        WAIT(GameplayFixes::GetUpdateDelay());
    }
}
