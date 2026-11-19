#include "GameplayFixes.h"
#include "../Main/Headers/SelfDefense.h"
#include "../Headers/Logger.h"
#include "Main/Headers/Modules.h"
#include "Main/Headers/Horse.h"
#include "Main/Headers/Player.h"
#include "Main/Headers/Camera.h"
#include "Main/Headers/Gunz.h"
#include "Main/Headers/ConfigMenu.h"
#include "Main/Headers/NotifStats.h"
#include "Main/Headers/Helpers/ModReload.h"
#include <windows.h>
#include <string>
#include <cmath>
#include <cwchar>
#include <cwctype>

namespace
{
    GameplayFixes::Settings settings;
    ULONGLONG nextOtherUpdateAt = 0;
    INIT_ONCE earlyInitialization = INIT_ONCE_STATIC_INIT;

    std::wstring GetIniPath()
    {
        wchar_t path[MAX_PATH] = {};
        const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
        if (length == 0 || length >= MAX_PATH)
            return {};

        std::wstring gamePath(path, length);
        const size_t separator = gamePath.find_last_of(L"\\/");
        if (separator == std::wstring::npos)
            return {};

        gamePath.resize(separator + 1);
        const std::wstring directory = gamePath + L"BettermentImprovements";
        const std::wstring rootIni = gamePath + L"BettermentImprovements.ini";
        const std::wstring priorIni = gamePath + L"FusionFixRDR2\\FusionFixRDR2.ini";
        const std::wstring priorRootIni = gamePath + L"FusionFixRDR2.ini";
        const std::wstring legacyIni = gamePath + L"GameplayFixesRDR2.ini";
        const std::wstring* candidates[] = { &rootIni, &priorIni, &priorRootIni, &legacyIni };

        if (!CreateDirectoryW(directory.c_str(), nullptr) &&
            (GetLastError() != ERROR_ALREADY_EXISTS ||
                GetFileAttributesW(directory.c_str()) == INVALID_FILE_ATTRIBUTES ||
                (GetFileAttributesW(directory.c_str()) & FILE_ATTRIBUTE_DIRECTORY) == 0))
        {
            Logger::Log("[WARNING] INI: could not create mod folder; using existing configuration.");
            for (const auto* source : candidates)
            {
                const DWORD attributes = GetFileAttributesW(source->c_str());
                if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY))
                    return *source;
            }
            return rootIni;
        }

        const std::wstring iniPath = directory + L"\\BettermentImprovements.ini";
        if (GetFileAttributesW(iniPath.c_str()) == INVALID_FILE_ATTRIBUTES)
        {
            for (const auto* source : candidates)
            {
                const DWORD attributes = GetFileAttributesW(source->c_str());
                if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY))
                    continue;
                if (CopyFileW(source->c_str(), iniPath.c_str(), TRUE))
                {
                    Logger::Log("[SUCCESS] INI: copied existing config to mod folder.");
                    break;
                }
                Logger::Log("[WARNING] INI: migration failed; using existing config.");
                return *source;
            }
        }
        return iniPath;
    }

    bool ReadIniBoolean(const std::wstring& path, const wchar_t* section, const wchar_t* key, bool defaultValue)
    {
        wchar_t value[16] = {};
        GetPrivateProfileStringW(section, key, defaultValue ? L"true" : L"false", value, 16, path.c_str());

        if (CompareStringOrdinal(value, -1, L"true", -1, TRUE) == CSTR_EQUAL)
            return true;
        if (CompareStringOrdinal(value, -1, L"false", -1, TRUE) == CSTR_EQUAL)
            return false;

        return defaultValue;
    }

    std::wstring ReadIniString(const std::wstring& path, const wchar_t* section, const wchar_t* key, const wchar_t* fallback)
    {
        wchar_t value[64] = {};
        GetPrivateProfileStringW(section, key, fallback, value, 64, path.c_str());
        return value;
    }

    int ParseMenuKey(const std::wstring& name)
    {
        if (CompareStringOrdinal(name.c_str(), -1, L"none", -1, TRUE) == CSTR_EQUAL)
            return 0;
        if (name.size() >= 2 && (name[0] == L'f' || name[0] == L'F'))
        {
            wchar_t* end = nullptr;
            const long number = std::wcstol(name.c_str() + 1, &end, 10);
            if (*end == L'\0' && number >= 1 && number <= 24)
                return VK_F1 + static_cast<int>(number) - 1;
        }
        if (name.size() == 1)
        {
            const wchar_t letter = std::towupper(name[0]);
            if ((letter >= L'A' && letter <= L'Z') || (letter >= L'0' && letter <= L'9'))
                return static_cast<int>(letter);
        }
        if (name.size() >= 3 && (name[0] == L'V' || name[0] == L'v') &&
            (name[1] == L'K' || name[1] == L'k'))
        {
            wchar_t* end = nullptr;
            const long number = std::wcstol(name.c_str() + 2, &end, 10);
            if (*end == L'\0' && number >= 1 && number <= 255)
                return static_cast<int>(number);
        }
        struct NamedKey { const wchar_t* name; int key; };
        static const NamedKey keys[] = {
            { L"Insert", VK_INSERT }, { L"Delete", VK_DELETE },
            { L"Home", VK_HOME }, { L"End", VK_END },
            { L"PageUp", VK_PRIOR }, { L"PageDown", VK_NEXT },
            { L"Space", VK_SPACE }, { L"Tab", VK_TAB },
            { L"Escape", VK_ESCAPE }, { L"Enter", VK_RETURN },
            { L"Up", VK_UP }, { L"Down", VK_DOWN },
            { L"Left", VK_LEFT }, { L"Right", VK_RIGHT },
            { L"Ctrl", VK_CONTROL }, { L"Control", VK_CONTROL },
            { L"Shift", VK_SHIFT }, { L"Alt", VK_MENU }
        };
        for (const auto& item : keys)
            if (CompareStringOrdinal(name.c_str(), -1, item.name, -1, TRUE) == CSTR_EQUAL)
                return item.key;
        return -1;
    }

    int ReadMenuKey(const std::wstring& path)
    {
        const int key = ParseMenuKey(ReadIniString(path, L"General", L"MenuKey", L"F8"));
        return key < 0 ? VK_F8 : key;
    }

    unsigned int ReadMenuModifier(const std::wstring& path)
    {
        std::wstring value = ReadIniString(path, L"General", L"MenuModifier", L"none");
        if (value.find(L'+') == std::wstring::npos &&
            CompareStringOrdinal(value.c_str(), -1, L"ctrl", -1, TRUE) != CSTR_EQUAL &&
            CompareStringOrdinal(value.c_str(), -1, L"control", -1, TRUE) != CSTR_EQUAL &&
            CompareStringOrdinal(value.c_str(), -1, L"shift", -1, TRUE) != CSTR_EQUAL &&
            CompareStringOrdinal(value.c_str(), -1, L"alt", -1, TRUE) != CSTR_EQUAL)
        {
            const int key = ParseMenuKey(value);
            if (key >= 0)
                return key == 0 ? 0U : 0x10000U | static_cast<unsigned int>(key);
        }
        unsigned int mask = 0;
        size_t start = 0;
        while (start < value.size())
        {
            size_t end = value.find(L'+', start);
            if (end == std::wstring::npos)
                end = value.size();
            std::wstring part = value.substr(start, end - start);
            const size_t first = part.find_first_not_of(L" \t");
            const size_t last = part.find_last_not_of(L" \t");
            if (first == std::wstring::npos)
                return 0;
            part = part.substr(first, last - first + 1);
            if (CompareStringOrdinal(part.c_str(), -1, L"none", -1, TRUE) == CSTR_EQUAL)
            {
                if (start != 0 || end != value.size())
                    return 0;
                return 0;
            }
            if (CompareStringOrdinal(part.c_str(), -1, L"ctrl", -1, TRUE) == CSTR_EQUAL ||
                CompareStringOrdinal(part.c_str(), -1, L"control", -1, TRUE) == CSTR_EQUAL)
                mask |= 1U;
            else if (CompareStringOrdinal(part.c_str(), -1, L"shift", -1, TRUE) == CSTR_EQUAL)
                mask |= 2U;
            else if (CompareStringOrdinal(part.c_str(), -1, L"alt", -1, TRUE) == CSTR_EQUAL)
                mask |= 4U;
            else
                return 0;
            start = end + 1;
        }
        return mask;
    }

    float ReadIniFloat(const std::wstring& path, const wchar_t* section, const wchar_t* key, float defaultValue)
    {
        wchar_t value[64] = {};
        const std::wstring fallback = std::to_wstring(defaultValue);
        GetPrivateProfileStringW(section, key, fallback.c_str(), value, 64, path.c_str());
        wchar_t* end = nullptr;
        const float parsed = std::wcstof(value, &end);
        if (end == value || *end != L'\0' || !std::isfinite(parsed))
            return defaultValue;
        if (parsed < 0.0f)
            return 0.0f;
        if (parsed > 1.0f)
            return 1.0f;
        return parsed;
    }

    float ReadOverscanScale(const std::wstring& path)
    {
        wchar_t value[64] = {};
        GetPrivateProfileStringW(L"Engine", L"SSROverscanScale", L"1.25", value, 64, path.c_str());
        wchar_t* end = nullptr;
        const float parsed = std::wcstof(value, &end);
        if (end == value || *end != L'\0' || !std::isfinite(parsed))
            return 1.25f;
        if (parsed < 1.0f)
            return 1.0f;
        if (parsed > 2.0f)
            return 2.0f;
        return parsed;
    }

    float ReadCameraFovOffset(const std::wstring& path)
    {
        wchar_t value[64] = {};
        GetPrivateProfileStringW(L"Visual", L"CameraFOVOffset", L"0.0", value, 64, path.c_str());
        wchar_t* end = nullptr;
        const float parsed = std::wcstof(value, &end);
        if (end == value || *end != L'\0' || !std::isfinite(parsed))
            return 0.0f;
        if (parsed < -3.0f)
            return -3.0f;
        if (parsed > 3.0f)
            return 3.0f;
        return parsed;
    }

    int ReadOverscanTestMode(const std::wstring& path)
    {
        wchar_t value[16] = {};
        GetPrivateProfileStringW(L"Engine", L"SSROverscanTestMode", L"0", value, 16, path.c_str());
        wchar_t* end = nullptr;
        const long parsed = std::wcstol(value, &end, 10);
        if (end == value || *end != L'\0' || parsed < 0 || parsed > 10)
            return 0;
        return static_cast<int>(parsed);
    }

    bool WriteIniValue(const std::wstring& path, const wchar_t* section, const wchar_t* key,
        const wchar_t* value, const char* description)
    {
        if (WritePrivateProfileStringW(section, key, value, path.c_str()))
            return true;

        const DWORD error = GetLastError();
        Logger::Log((std::string("[ERROR] INI: could not write ") + description +
            " (Win32 error " + std::to_string(error) + ").").c_str());
        return false;
    }

    bool EnsureIniExists(const std::wstring& path)
    {
        bool success = true;
        const DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES)
        {
            const DWORD error = GetLastError();
            if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
                Logger::Log((std::string("[WARNING] INI: unable to inspect existing file (Win32 error ") +
                    std::to_string(error) + ").").c_str());

            success = WriteIniValue(path, L"General", L"Enabled", L"true", "General.Enabled") && success;
            if (success)
                Logger::Log("[SUCCESS] INI: default config created.");
        }
        else if (attributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            Logger::Log("[ERROR] INI: config path is a directory, not a file.");
            return false;
        }

        wchar_t existing[8] = {};
        GetPrivateProfileStringW(L"General", L"Logging", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"General", L"Logging", L"true", "General.Logging") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"General", L"DebugLogging", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"General", L"DebugLogging", L"false", "General.DebugLogging") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"General", L"Notification", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"General", L"Notification", L"true", "General.Notification") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"General", L"SettingsMenu", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"General", L"SettingsMenu", L"true", "General.SettingsMenu") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"General", L"MenuKey", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"General", L"MenuKey", L"F8", "General.MenuKey") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"General", L"MenuModifier", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"General", L"MenuModifier", L"none", "General.MenuModifier") && success;

        wchar_t oldTextureCrashSetting[8] = {};
        GetPrivateProfileStringW(L"Engine", L"YTDCrashFix", L"", oldTextureCrashSetting, 8, path.c_str());
        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Engine", L"TextureCrashFix", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
        {
            const wchar_t* value = oldTextureCrashSetting[0] == L'\0' ? L"true" :
                ReadIniBoolean(path, L"Engine", L"YTDCrashFix", true) ? L"true" : L"false";
            success = WriteIniValue(path, L"Engine", L"TextureCrashFix", value, "Engine.TextureCrashFix") && success;
        }
        if (oldTextureCrashSetting[0] != L'\0')
        {
            existing[0] = L'\0';
            GetPrivateProfileStringW(L"Engine", L"TextureCrashFix", L"", existing, 8, path.c_str());
            if (existing[0] != L'\0')
                success = (WritePrivateProfileStringW(L"Engine", L"YTDCrashFix", nullptr, path.c_str()) != FALSE) && success;
        }

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Engine", L"ExtendHeapLimits", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Engine", L"ExtendHeapLimits", L"true", "Engine.ExtendHeapLimits") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Engine", L"ExtendPackfileLimits", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Engine", L"ExtendPackfileLimits", L"true", "Engine.ExtendPackfileLimits") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Engine", L"ExtendPedMemoryLimits", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Engine", L"ExtendPedMemoryLimits", L"true", "Engine.ExtendPedMemoryLimits") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Engine", L"ExtendStreamingPools", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Engine", L"ExtendStreamingPools", L"true", "Engine.ExtendStreamingPools") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Engine", L"ExtendPopulationStreamingPools", L"", existing, 8, path.c_str());
        if (existing[0] != L'\0' &&
            !WritePrivateProfileStringW(L"Engine", L"ExtendPopulationStreamingPools", nullptr, path.c_str()))
            success = false;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Engine", L"OptimizeStreamingMemory", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Engine", L"OptimizeStreamingMemory", L"true", "Engine.OptimizeStreamingMemory") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Engine", L"ExtendAssetPools", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Engine", L"ExtendAssetPools", L"true", "Engine.ExtendAssetPools") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Engine", L"TextureStreamingStutterFix", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Engine", L"TextureStreamingStutterFix", L"false", "Engine.TextureStreamingStutterFix") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Engine", L"ImproveTextureStreaming", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Engine", L"ImproveTextureStreaming", L"true", "Engine.ImproveTextureStreaming") && success;

        const wchar_t* obsoleteWaterSettings[][2] =
        {
            { L"Engine", L"FixWaterReflectionRendering" },
            { L"Visual", L"FixWaterReflections" }
        };
        for (const auto& option : obsoleteWaterSettings)
        {
            wchar_t previous[16] = {};
            GetPrivateProfileStringW(option[0], option[1], L"__missing__", previous, 16, path.c_str());
            if (CompareStringOrdinal(previous, -1, L"__missing__", -1, TRUE) != CSTR_EQUAL &&
                !WritePrivateProfileStringW(option[0], option[1], nullptr, path.c_str()))
                success = false;
        }

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Engine", L"ExpandSSRCoverage", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Engine", L"ExpandSSRCoverage", L"false", "Engine.ExpandSSRCoverage") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Engine", L"SSROverscanScale", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Engine", L"SSROverscanScale", L"1.25", "Engine.SSROverscanScale") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Engine", L"SSROverscanTestMode", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Engine", L"SSROverscanTestMode", L"0", "Engine.SSROverscanTestMode") && success;

        const wchar_t* obsoleteVRAMSettings[][2] =
        {
            { L"Memory", L"FixVRAMLeaks" },
            { L"Engine", L"FixVRAMLeaks" },
            { L"Diagnostics", L"TraceVRAMUsage" }
        };
        for (const auto& option : obsoleteVRAMSettings)
        {
            wchar_t previous[16] = {};
            GetPrivateProfileStringW(option[0], option[1], L"__missing__", previous, 16, path.c_str());
            if (CompareStringOrdinal(previous, -1, L"__missing__", -1, TRUE) == CSTR_EQUAL)
                continue;

            if (WritePrivateProfileStringW(option[0], option[1], nullptr, path.c_str()))
            {
                wchar_t remaining[256] = {};
                if (GetPrivateProfileSectionW(option[0], remaining, 256, path.c_str()) == 0)
                    WritePrivateProfileStringW(option[0], nullptr, nullptr, path.c_str());
            }
            else
            {
                Logger::Log("[WARNING] INI: could not remove old VRAM setting.");
                success = false;
            }
        }

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Player", L"DisableFirstPersonAutoRun", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Player", L"DisableFirstPersonAutoRun", L"false", "Player.DisableFirstPersonAutoRun") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Player", L"QuickLootBodies", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Player", L"QuickLootBodies", L"false", "Player.QuickLootBodies") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Player", L"QuickLootAnimals", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Player", L"QuickLootAnimals", L"false", "Player.QuickLootAnimals") && success;

        const auto migrateGameplayOption = [&](const wchar_t* updatedKey, const wchar_t* legacyKey,
            const wchar_t* olderLegacyKey, const wchar_t* defaultValue, const char* description)
        {
            wchar_t updated[8] = {};
            wchar_t legacy[8] = {};
            wchar_t olderLegacy[8] = {};
            GetPrivateProfileStringW(L"Gameplay", updatedKey, L"", updated, 8, path.c_str());
            GetPrivateProfileStringW(L"Gameplay", legacyKey, L"", legacy, 8, path.c_str());
            if (olderLegacyKey != nullptr)
                GetPrivateProfileStringW(L"Gameplay", olderLegacyKey, L"", olderLegacy, 8, path.c_str());

            if (updated[0] == L'\0' && !WriteIniValue(path, L"Gameplay", updatedKey,
                legacy[0] != L'\0' ? legacy : olderLegacy[0] != L'\0' ? olderLegacy : defaultValue, description))
                return false;

            bool migrated = true;
            if (legacy[0] != L'\0' && !WritePrivateProfileStringW(L"Gameplay", legacyKey, nullptr, path.c_str()))
            {
                Logger::Log("[WARNING] INI: could not remove old gameplay setting.");
                migrated = false;
            }
            if (olderLegacy[0] != L'\0' && !WritePrivateProfileStringW(L"Gameplay", olderLegacyKey, nullptr, path.c_str()))
            {
                Logger::Log("[WARNING] INI: could not remove old gameplay setting.");
                migrated = false;
            }
            return migrated;
        };
        success = migrateGameplayOption(L"DisableAutoHorseEquip", L"NoAutoHorseEquip", nullptr,
            L"true", "Gameplay.DisableAutoHorseEquip") && success;
        success = migrateGameplayOption(L"DisableAutoHorseHolster", L"NoAutoHorseHolster", L"NoAutoHorseHolder",
            L"true", "Gameplay.DisableAutoHorseHolster") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Gameplay", L"DisableHorseAutoJump", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Gameplay", L"DisableHorseAutoJump", L"false", "Gameplay.DisableHorseAutoJump") && success;
        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Gameplay", L"DisableHorseWhistleDistance", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Gameplay", L"DisableHorseWhistleDistance", L"false", "Gameplay.DisableHorseWhistleDistance") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Gameplay", L"FixHorseFollowBehavior", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Gameplay", L"FixHorseFollowBehavior", L"false", "Gameplay.FixHorseFollowBehavior") && success;
        success = migrateGameplayOption(L"DisableAutoAmmoPickup", L"NoAutoAmmoPickup", nullptr,
            L"false", "Gameplay.DisableAutoAmmoPickup") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Gameplay", L"DisableHonorLossWhenMasked", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Gameplay", L"DisableHonorLossWhenMasked", L"true", "Gameplay.DisableHonorLossWhenMasked") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Gameplay", L"DisableBountyWhenMasked", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Gameplay", L"DisableBountyWhenMasked", L"true", "Gameplay.DisableBountyWhenMasked") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Gameplay", L"DisableBounties", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Gameplay", L"DisableBounties", L"false", "Gameplay.DisableBounties") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Gameplay", L"FixSelfDefenseHonorLoss", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Gameplay", L"FixSelfDefenseHonorLoss", L"true", "Gameplay.FixSelfDefenseHonorLoss") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Gameplay", L"FixSelfDefenseBounties", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Gameplay", L"FixSelfDefenseBounties", L"true", "Gameplay.FixSelfDefenseBounties") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Gameplay", L"DisableHonorSystem", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Gameplay", L"DisableHonorSystem", L"false", "Gameplay.DisableHonorSystem") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Gameplay", L"DisableBodySearchHonorLoss", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Gameplay", L"DisableBodySearchHonorLoss", L"true", "Gameplay.DisableBodySearchHonorLoss") && success;

        success = migrateGameplayOption(L"DisableAntagonizeHonorLoss", L"DisableAntagonizehHonorLoss", nullptr,
            L"false", "Gameplay.DisableAntagonizeHonorLoss") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Gameplay", L"DisableOutOfBoundSnipers", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Gameplay", L"DisableOutOfBoundSnipers", L"false", "Gameplay.DisableOutOfBoundSnipers") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Weapons", L"DisableAutoReload", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Weapons", L"DisableAutoReload", L"true", "Weapons.DisableAutoReload") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Weapons", L"DisableDeadEyeAutoReload", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Weapons", L"DisableDeadEyeAutoReload", L"true", "Weapons.DisableDeadEyeAutoReload") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Weapons", L"DisableEmptyAutoReload", L"", existing, 8, path.c_str());
        wchar_t previousEmptyReload[8] = {};
        GetPrivateProfileStringW(L"Weapons", L"DisableEmptyClipAutoReload", L"", previousEmptyReload, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Weapons", L"DisableEmptyAutoReload",
                previousEmptyReload[0] != L'\0' ? previousEmptyReload : L"true", "Weapons.DisableEmptyAutoReload") && success;
        if (previousEmptyReload[0] != L'\0' &&
            !WritePrivateProfileStringW(L"Weapons", L"DisableEmptyClipAutoReload", nullptr, path.c_str()))
        {
            Logger::Log("[WARNING] INI: could not remove the old Weapons.DisableEmptyClipAutoReload option.");
            success = false;
        }

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Weapons", L"DisableArrowTrails", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Weapons", L"DisableArrowTrails", L"false", "Weapons.DisableArrowTrails") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Weapons", L"DisableReticleSpread", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Weapons", L"DisableReticleSpread", L"false", "Weapons.DisableReticleSpread") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Weapons", L"DisableReticleSway", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Weapons", L"DisableReticleSway", L"false", "Weapons.DisableReticleSway") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Weapons", L"DisableForcedScope", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Weapons", L"DisableForcedScope", L"true", "Weapons.DisableForcedScope") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Weapons", L"DisableWeaponDegradation", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Weapons", L"DisableWeaponDegradation", L"false", "Weapons.DisableWeaponDegradation") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Weapons", L"DisableHitmarkers", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Weapons", L"DisableHitmarkers", L"false", "Weapons.DisableHitmarkers") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Weapons", L"DisableReticleHighlighting", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Weapons", L"DisableReticleHighlighting", L"false", "Weapons.DisableReticleHighlighting") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Weapons", L"ProbeWeaponSignatures", L"", existing, 8, path.c_str());
        if (existing[0] != L'\0' &&
            !WritePrivateProfileStringW(L"Weapons", L"ProbeWeaponSignatures", nullptr, path.c_str()))
        {
            Logger::Log("[WARNING] INI: could not remove obsolete Weapons.ProbeWeaponSignatures option.");
            success = false;
        }

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Visual", L"CameraFOVOffset", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Visual", L"CameraFOVOffset", L"0.0", "Visual.CameraFOVOffset") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Visual", L"RemoveBlackBars", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Visual", L"RemoveBlackBars", L"false", "Visual.RemoveBlackBars") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Visual", L"EnableDLSSAutoExposure", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Visual", L"EnableDLSSAutoExposure", L"false", "Visual.EnableDLSSAutoExposure") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Visual", L"DisableInteriorLighting", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Visual", L"DisableInteriorLighting", L"false", "Visual.DisableInteriorLighting") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Visual", L"DisableDistantLights", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Visual", L"DisableDistantLights", L"false", "Visual.DisableDistantLights") && success;

        const wchar_t* obsoleteSettings[][2] =
        {
            { L"Visual", L"RemoveCameraFade" },
            { L"Visual", L"DisableCameraSmoothing" },
            { L"Diagnostics", L"DumpNativeHashes" },
            { L"Camera", L"RemoveCameraFade" },
            { L"Diagnostics", L"TraceCameraNatives" },
            { L"Visual", L"DisableVignette" },
            { L"Visual", L"DisableBloom" }
        };
        for (const auto& option : obsoleteSettings)
        {
            wchar_t previous[16] = {};
            GetPrivateProfileStringW(option[0], option[1], L"__missing__", previous, 16, path.c_str());
            if (CompareStringOrdinal(previous, -1, L"__missing__", -1, TRUE) == CSTR_EQUAL)
                continue;

            if (WritePrivateProfileStringW(option[0], option[1], nullptr, path.c_str()))
            {
                wchar_t remaining[256] = {};
                if (GetPrivateProfileSectionW(option[0], remaining, 256, path.c_str()) == 0)
                    WritePrivateProfileStringW(option[0], nullptr, nullptr, path.c_str());
            }
            else
            {
                Logger::Log("[WARNING] INI: could not remove old setting.");
                success = false;
            }
        }

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Visual", L"DisableDrunkFilter", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Visual", L"DisableDrunkFilter", L"false", "Visual.DisableDrunkFilter") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Visual", L"DisableEagleEyeFilter", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Visual", L"DisableEagleEyeFilter", L"false", "Visual.DisableEagleEyeFilter") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Visual", L"DisableDeadEyeFilter", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Visual", L"DisableDeadEyeFilter", L"false", "Visual.DisableDeadEyeFilter") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Visual", L"DisableWeaponWheelFilter", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Visual", L"DisableWeaponWheelFilter", L"false", "Visual.DisableWeaponWheelFilter") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Visual", L"DisableLowHealthFilter", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Visual", L"DisableLowHealthFilter", L"false", "Visual.DisableLowHealthFilter") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Visual", L"DisableIdleCamera", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Visual", L"DisableIdleCamera", L"false", "Visual.DisableIdleCamera") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Visual", L"FixFullscreenTabOut", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Visual", L"FixFullscreenTabOut", L"false", "Visual.FixFullscreenTabOut") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Visual", L"MinimizeOnTabOut", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Visual", L"MinimizeOnTabOut", L"false", "Visual.MinimizeOnTabOut") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Visual", L"DisableHeadBob", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Visual", L"DisableHeadBob", L"false", "Visual.DisableHeadBob") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Visual", L"ReduceCameraShake", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Visual", L"ReduceCameraShake", L"false", "Visual.ReduceCameraShake") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Visual", L"ReduceCameraSway", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Visual", L"ReduceCameraSway", L"false", "Visual.ReduceCameraSway") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Visual", L"ReduceWeaponRecoil", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Visual", L"ReduceWeaponRecoil", L"false", "Visual.ReduceWeaponRecoil") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Visual", L"RecoilShakeAmplitudeScale", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Visual", L"RecoilShakeAmplitudeScale", L"0.50", "Visual.RecoilShakeAmplitudeScale") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Visual", L"ReduceFirstPersonWeaponRecoil", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Visual", L"ReduceFirstPersonWeaponRecoil", L"false", "Visual.ReduceFirstPersonWeaponRecoil") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Visual", L"FirstPersonRecoilShakeAmplitudeScale", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Visual", L"FirstPersonRecoilShakeAmplitudeScale", L"0.50", "Visual.FirstPersonRecoilShakeAmplitudeScale") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Ambience", L"DisableAmbientMusic", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Ambience", L"DisableAmbientMusic", L"false", "Ambience.DisableAmbientMusic") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Ambience", L"DisableCombatMusic", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Ambience", L"DisableCombatMusic", L"false", "Ambience.DisableCombatMusic") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Ambience", L"DisableWantedMusic", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Ambience", L"DisableWantedMusic", L"false", "Ambience.DisableWantedMusic") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Diagnostics", L"CrashDiagnostics", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Diagnostics", L"CrashDiagnostics", L"false", "Diagnostics.CrashDiagnostics") && success;

        const wchar_t* obsoleteGraphicsSettings[][2] =
        {
            { L"Visual", L"FixShadowFlickering" },
            { L"Visual", L"TestDX12WaterReflection" },
            { L"Engine", L"GFXStateCrashFix" },
            { L"Diagnostics", L"GFXStateCrashFix" }
        };
        for (const auto& option : obsoleteGraphicsSettings)
        {
            wchar_t previous[16] = {};
            GetPrivateProfileStringW(option[0], option[1], L"__missing__", previous, 16, path.c_str());
            if (CompareStringOrdinal(previous, -1, L"__missing__", -1, TRUE) == CSTR_EQUAL)
                continue;
            if (!WritePrivateProfileStringW(option[0], option[1], nullptr, path.c_str()))
            {
                Logger::Log("[WARNING] INI: could not remove old graphics setting.");
                success = false;
            }
        }

        wchar_t obsoleteInterval[16] = {};
        GetPrivateProfileStringW(L"General", L"UpdateIntervalMs", L"__missing__", obsoleteInterval, 16, path.c_str());
        if (CompareStringOrdinal(obsoleteInterval, -1, L"__missing__", -1, TRUE) != CSTR_EQUAL)
        {
            if (WritePrivateProfileStringW(L"General", L"UpdateIntervalMs", nullptr, path.c_str()))
                Logger::Log("[INFO] INI: removed old UpdateIntervalMs setting.");
            else
            {
                Logger::Log("[WARNING] INI: could not remove obsolete UpdateIntervalMs setting.");
                success = false;
            }
        }

        wchar_t obsoleteDebug[16] = {};
        GetPrivateProfileStringW(L"Debug", L"Enabled", L"__missing__", obsoleteDebug, 16, path.c_str());
        if (CompareStringOrdinal(obsoleteDebug, -1, L"__missing__", -1, TRUE) != CSTR_EQUAL)
        {
            if (WritePrivateProfileStringW(L"Debug", L"Enabled", nullptr, path.c_str()))
            {
                wchar_t remaining[256] = {};
                if (GetPrivateProfileSectionW(L"Debug", remaining, 256, path.c_str()) == 0)
                    WritePrivateProfileStringW(L"Debug", nullptr, nullptr, path.c_str());
                Logger::Log("[INFO] INI: removed old Debug.Enabled setting.");
            }
            else
            {
                Logger::Log("[WARNING] INI: could not remove obsolete Debug.Enabled setting.");
                success = false;
            }
        }

        wchar_t legacy[16] = {};
        GetPrivateProfileStringW(L"Startup", L"SkipIntroSplashesAndLegal", L"", legacy, 16, path.c_str());
        const wchar_t* inherited = legacy[0] == L'\0' ? L"true" :
            ReadIniBoolean(path, L"Startup", L"SkipIntroSplashesAndLegal", true) ? L"true" : L"false";
        const wchar_t* keys[] = { L"SkipIntroSplashes", L"SkipLegal" };
        for (const wchar_t* key : keys)
        {
            existing[0] = L'\0';
            GetPrivateProfileStringW(L"Startup", key, L"", existing, 8, path.c_str());
            if (existing[0] == L'\0')
                success = WriteIniValue(path, L"Startup", key, inherited, "Startup skip option") && success;
        }

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Startup", L"StraightToStoryMode", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Startup", L"StraightToStoryMode", L"false", "Startup.StraightToStoryMode") && success;

        wchar_t obsoleteStartupStatistics[16] = {};
        GetPrivateProfileStringW(L"Startup", L"StartupStatistics", L"__missing__", obsoleteStartupStatistics, 16, path.c_str());
        if (CompareStringOrdinal(obsoleteStartupStatistics, -1, L"__missing__", -1, TRUE) != CSTR_EQUAL)
            success = WritePrivateProfileStringW(L"Startup", L"StartupStatistics", nullptr, path.c_str()) && success;

        GetPrivateProfileStringW(L"General", L"StartupStatDisplay", L"__missing__", obsoleteStartupStatistics, 16, path.c_str());
        if (CompareStringOrdinal(obsoleteStartupStatistics, -1, L"__missing__", -1, TRUE) != CSTR_EQUAL)
            success = WritePrivateProfileStringW(L"General", L"StartupStatDisplay", nullptr, path.c_str()) && success;

        wchar_t obsoleteValue[16] = {};
        GetPrivateProfileStringW(L"Startup", L"FastStoryModeLoading", L"__missing__", obsoleteValue, 16, path.c_str());
        if (CompareStringOrdinal(obsoleteValue, -1, L"__missing__", -1, TRUE) != CSTR_EQUAL)
        {
            if (WritePrivateProfileStringW(L"Startup", L"FastStoryModeLoading", nullptr, path.c_str()))
                Logger::Log("[INFO] INI: removed old FastStoryModeLoading setting.");
            else
            {
                Logger::Log("[WARNING] INI: could not remove obsolete FastStoryModeLoading setting.");
                success = false;
            }
        }

        GetPrivateProfileStringW(L"Startup", L"RemoveOnlineTab", L"__missing__", obsoleteValue, 16, path.c_str());
        if (CompareStringOrdinal(obsoleteValue, -1, L"__missing__", -1, TRUE) != CSTR_EQUAL)
        {
            if (WritePrivateProfileStringW(L"Startup", L"RemoveOnlineTab", nullptr, path.c_str()))
                Logger::Log("[INFO] INI: removed old RemoveOnlineTab setting.");
            else
            {
                Logger::Log("[WARNING] INI: could not remove the obsolete RemoveOnlineTab setting.");
                success = false;
            }
        }

        if (success && legacy[0] != L'\0')
        {
            if (WritePrivateProfileStringW(L"Startup", L"SkipIntroSplashesAndLegal", nullptr, path.c_str()))
                Logger::Log("[INFO] INI: split old SkipIntroSplashesAndLegal setting.");
            else
                Logger::Log("[WARNING] INI: old combined skip option could not be removed; the new options take priority.");
        }

        return success;
    }

    void ReadModSettings(const std::wstring& iniPath, GameplayFixes::Settings& settings)
    {
            settings.enabled = ReadIniBoolean(iniPath, L"General", L"Enabled", true);
            settings.settingsMenu = ReadIniBoolean(iniPath, L"General", L"SettingsMenu", true);
            settings.notification = ReadIniBoolean(iniPath, L"General", L"Notification", true);
            settings.menuKey = ReadMenuKey(iniPath);
            settings.menuModifier = ReadMenuModifier(iniPath);
            settings.textureCrashFix = ReadIniBoolean(iniPath, L"Engine", L"TextureCrashFix", true);
            settings.extendHeapLimits = ReadIniBoolean(iniPath, L"Engine", L"ExtendHeapLimits", true);
            settings.extendPackfileLimits = ReadIniBoolean(iniPath, L"Engine", L"ExtendPackfileLimits", true);
            settings.extendPedMemoryLimits = ReadIniBoolean(iniPath, L"Engine", L"ExtendPedMemoryLimits", true);
            settings.extendStreamingPools = ReadIniBoolean(iniPath, L"Engine", L"ExtendStreamingPools", true);
            settings.optimizeStreamingMemory = ReadIniBoolean(iniPath, L"Engine", L"OptimizeStreamingMemory", true);
            settings.extendAssetPools = ReadIniBoolean(iniPath, L"Engine", L"ExtendAssetPools", true);
            settings.textureStreamingStutterFix = ReadIniBoolean(iniPath, L"Engine", L"TextureStreamingStutterFix", false);
            settings.improveTextureStreaming = ReadIniBoolean(iniPath, L"Engine", L"ImproveTextureStreaming", true);
            settings.expandSsrCoverage = ReadIniBoolean(iniPath, L"Engine", L"ExpandSSRCoverage", false);
            settings.ssrOverscanScale = ReadOverscanScale(iniPath);
            settings.ssrOverscanTestMode = ReadOverscanTestMode(iniPath);
            settings.noAutoHorseEquip = ReadIniBoolean(iniPath, L"Gameplay", L"DisableAutoHorseEquip", true);
            settings.noAutoHorseHolster = ReadIniBoolean(iniPath, L"Gameplay", L"DisableAutoHorseHolster", true);
            settings.disableHorseAutoJump = ReadIniBoolean(iniPath, L"Gameplay", L"DisableHorseAutoJump", false);
            settings.disableHorseWhistleDistance = ReadIniBoolean(iniPath, L"Gameplay", L"DisableHorseWhistleDistance", false);
            settings.fixHorseFollowBehavior = ReadIniBoolean(iniPath, L"Gameplay", L"FixHorseFollowBehavior", false);
            settings.noAutoAmmoPickup = ReadIniBoolean(iniPath, L"Gameplay", L"DisableAutoAmmoPickup", false);
            settings.disableAutoReload = ReadIniBoolean(iniPath, L"Weapons", L"DisableAutoReload", true);
            settings.disableDeadEyeAutoReload = ReadIniBoolean(iniPath, L"Weapons", L"DisableDeadEyeAutoReload", true);
            settings.disableEmptyAutoReload = ReadIniBoolean(iniPath, L"Weapons", L"DisableEmptyAutoReload", true);
            settings.disableArrowTrails = ReadIniBoolean(iniPath, L"Weapons", L"DisableArrowTrails", false);
            settings.disableReticleSpread = ReadIniBoolean(iniPath, L"Weapons", L"DisableReticleSpread", false);
            settings.disableReticleSway = ReadIniBoolean(iniPath, L"Weapons", L"DisableReticleSway", false);
            settings.disableForcedScope = ReadIniBoolean(iniPath, L"Weapons", L"DisableForcedScope", true);
            settings.disableWeaponDegradation = ReadIniBoolean(iniPath, L"Weapons", L"DisableWeaponDegradation", false);
            settings.disableHitmarkers = ReadIniBoolean(iniPath, L"Weapons", L"DisableHitmarkers", false);
            settings.disableReticleHighlighting = ReadIniBoolean(iniPath, L"Weapons", L"DisableReticleHighlighting", false);
            settings.disableFirstPersonAutoRun = ReadIniBoolean(iniPath, L"Player", L"DisableFirstPersonAutoRun", false);
            settings.quickLootBodies = ReadIniBoolean(iniPath, L"Player", L"QuickLootBodies", false);
            settings.quickLootAnimals = ReadIniBoolean(iniPath, L"Player", L"QuickLootAnimals", false);
            settings.disableHonorLossWhenMasked = ReadIniBoolean(iniPath, L"Gameplay", L"DisableHonorLossWhenMasked", true);
            settings.disableBountyWhenMasked = ReadIniBoolean(iniPath, L"Gameplay", L"DisableBountyWhenMasked", true);
            settings.disableBounties = ReadIniBoolean(iniPath, L"Gameplay", L"DisableBounties", false);
            settings.fixSelfDefenseHonorLoss = ReadIniBoolean(iniPath, L"Gameplay", L"FixSelfDefenseHonorLoss", true);
            settings.fixSelfDefenseBounties = ReadIniBoolean(iniPath, L"Gameplay", L"FixSelfDefenseBounties", true);
            settings.disableHonorSystem = ReadIniBoolean(iniPath, L"Gameplay", L"DisableHonorSystem", false);
            settings.disableBodySearchHonorLoss = ReadIniBoolean(iniPath, L"Gameplay", L"DisableBodySearchHonorLoss", true);
            settings.disableAntagonizeHonorLoss = ReadIniBoolean(iniPath, L"Gameplay", L"DisableAntagonizeHonorLoss", false);
            settings.disableOutOfBoundSnipers = ReadIniBoolean(iniPath, L"Gameplay", L"DisableOutOfBoundSnipers", false);
            settings.skipIntroSplashes = ReadIniBoolean(iniPath, L"Startup", L"SkipIntroSplashes", true);
            settings.skipLegal = ReadIniBoolean(iniPath, L"Startup", L"SkipLegal", true);
            settings.straightToStoryMode = ReadIniBoolean(iniPath, L"Startup", L"StraightToStoryMode", false);
            settings.cameraFovOffset = ReadCameraFovOffset(iniPath);
            settings.removeBlackBars = ReadIniBoolean(iniPath, L"Visual", L"RemoveBlackBars", false);
            settings.enableDLSSAutoExposure = ReadIniBoolean(iniPath, L"Visual", L"EnableDLSSAutoExposure", false);
            settings.disableInteriorLighting = ReadIniBoolean(iniPath, L"Visual", L"DisableInteriorLighting", false);
            settings.disableDistantLights = ReadIniBoolean(iniPath, L"Visual", L"DisableDistantLights", false);
            settings.disableIdleCamera = ReadIniBoolean(iniPath, L"Visual", L"DisableIdleCamera", false);
            settings.fixFullscreenTabOut = ReadIniBoolean(iniPath, L"Visual", L"FixFullscreenTabOut", false);
            settings.minimizeOnTabOut = ReadIniBoolean(iniPath, L"Visual", L"MinimizeOnTabOut", false);
            settings.disableDrunkFilter = ReadIniBoolean(iniPath, L"Visual", L"DisableDrunkFilter", false);
            settings.disableEagleEyeFilter = ReadIniBoolean(iniPath, L"Visual", L"DisableEagleEyeFilter", false);
            settings.disableDeadEyeFilter = ReadIniBoolean(iniPath, L"Visual", L"DisableDeadEyeFilter", false);
            settings.disableWeaponWheelFilter = ReadIniBoolean(iniPath, L"Visual", L"DisableWeaponWheelFilter", false);
            settings.disableLowHealthFilter = ReadIniBoolean(iniPath, L"Visual", L"DisableLowHealthFilter", false);
            settings.disableHeadBob = ReadIniBoolean(iniPath, L"Visual", L"DisableHeadBob", false);
            settings.reduceCameraShake = ReadIniBoolean(iniPath, L"Visual", L"ReduceCameraShake", false);
            settings.reduceCameraSway = ReadIniBoolean(iniPath, L"Visual", L"ReduceCameraSway", false);
            settings.reduceWeaponRecoil = ReadIniBoolean(iniPath, L"Visual", L"ReduceWeaponRecoil", false);
            settings.recoilShakeAmplitudeScale = ReadIniFloat(iniPath, L"Visual", L"RecoilShakeAmplitudeScale", 0.5f);
            settings.reduceFirstPersonWeaponRecoil = ReadIniBoolean(iniPath, L"Visual", L"ReduceFirstPersonWeaponRecoil", false);
            settings.firstPersonRecoilShakeAmplitudeScale = ReadIniFloat(iniPath, L"Visual", L"FirstPersonRecoilShakeAmplitudeScale", 0.5f);
            settings.disableAmbientMusic = ReadIniBoolean(iniPath, L"Ambience", L"DisableAmbientMusic", false);
            settings.disableCombatMusic = ReadIniBoolean(iniPath, L"Ambience", L"DisableCombatMusic", false);
            settings.disableWantedMusic = ReadIniBoolean(iniPath, L"Ambience", L"DisableWantedMusic", false);
            settings.crashDiagnostics = ReadIniBoolean(iniPath, L"Diagnostics", L"CrashDiagnostics", false);
    }
}

namespace GameplayFixes
{
    BOOL CALLBACK DoEarlyInitialization(PINIT_ONCE, PVOID, PVOID*)
    {
        const std::wstring iniPath = GetIniPath();
        if (!iniPath.empty())
            Logger::SetEnabled(ReadIniBoolean(iniPath, L"General", L"Logging", true));

        Logger::Log("[BOOT] Early startup: loading settings.");
        if (!iniPath.empty())
        {
            Logger::Log("[INFO] INI: loading config.");
            const bool iniReady = EnsureIniExists(iniPath);
            Logger::SetDebugEnabled(ReadIniBoolean(iniPath, L"General", L"DebugLogging", false));
            if (!iniReady)
            {
                Logger::Log("[WARNING] INI: some settings could not be saved; using defaults where needed.");
            }

            ReadModSettings(iniPath, settings);
            Logger::Log(iniReady ? "[SUCCESS] INI: config loaded." :
                "[WARNING] INI: config has errors; some defaults may be in use.");
        }
        else
        {
            Logger::Log("[ERROR] INI: game directory not found; using defaults.");
        }

        if (Logger::IsDebugEnabled())
            Logger::Log((std::string("[DEBUG] Settings: Enabled=") + (settings.enabled ? "true" : "false") +
            ", DebugLogging=" + (Logger::IsDebugEnabled() ? "true" : "false") +
            ", Notification=" + (settings.notification ? "true" : "false") +
            ", TextureCrashFix=" + (settings.textureCrashFix ? "true" : "false") +
            ", ExtendHeapLimits=" + (settings.extendHeapLimits ? "true" : "false") +
            ", ExtendPackfileLimits=" + (settings.extendPackfileLimits ? "true" : "false") +
            ", ExtendPedMemoryLimits=" + (settings.extendPedMemoryLimits ? "true" : "false") +
            ", ExtendStreamingPools=" + (settings.extendStreamingPools ? "true" : "false") +
            ", OptimizeStreamingMemory=" + (settings.optimizeStreamingMemory ? "true" : "false") +
            ", ExtendAssetPools=" + (settings.extendAssetPools ? "true" : "false") +
            ", TextureStreamingStutterFix=" + (settings.textureStreamingStutterFix ? "true" : "false") +
            ", ImproveTextureStreaming=" + (settings.improveTextureStreaming ? "true" : "false") +
            ", ExpandSSRCoverage=" + (settings.expandSsrCoverage ? "true" : "false") +
            ", SSROverscanScale=" + std::to_string(settings.ssrOverscanScale) +
            ", SSROverscanTestMode=" + std::to_string(settings.ssrOverscanTestMode) +
            ", DisableAutoHorseEquip=" + (settings.noAutoHorseEquip ? "true" : "false") +
            ", DisableAutoHorseHolster=" + (settings.noAutoHorseHolster ? "true" : "false") +
            ", DisableHorseAutoJump=" + (settings.disableHorseAutoJump ? "true" : "false") +
            ", DisableHorseWhistleDistance=" + (settings.disableHorseWhistleDistance ? "true" : "false") +
            ", FixHorseFollowBehavior=" + (settings.fixHorseFollowBehavior ? "true" : "false") +
            ", DisableAutoAmmoPickup=" + (settings.noAutoAmmoPickup ? "true" : "false") +
            ", DisableAutoReload=" + (settings.disableAutoReload ? "true" : "false") +
            ", DisableDeadEyeAutoReload=" + (settings.disableDeadEyeAutoReload ? "true" : "false") +
            ", DisableEmptyAutoReload=" + (settings.disableEmptyAutoReload ? "true" : "false") +
            ", DisableArrowTrails=" + (settings.disableArrowTrails ? "true" : "false") +
            ", DisableReticleSpread=" + (settings.disableReticleSpread ? "true" : "false") +
            ", DisableReticleSway=" + (settings.disableReticleSway ? "true" : "false") +
            ", DisableHitmarkers=" + (settings.disableHitmarkers ? "true" : "false") +
            ", DisableReticleHighlighting=" + (settings.disableReticleHighlighting ? "true" : "false") +
            ", DisableForcedScope=" + (settings.disableForcedScope ? "true" : "false") +
            ", DisableFirstPersonAutoRun=" + (settings.disableFirstPersonAutoRun ? "true" : "false") +
            ", QuickLootBodies=" + (settings.quickLootBodies ? "true" : "false") +
            ", QuickLootAnimals=" + (settings.quickLootAnimals ? "true" : "false") +
            ", DisableHonorLossWhenMasked=" + (settings.disableHonorLossWhenMasked ? "true" : "false") +
            ", DisableBountyWhenMasked=" + (settings.disableBountyWhenMasked ? "true" : "false") +
            ", DisableBounties=" + (settings.disableBounties ? "true" : "false") +
            ", FixSelfDefenseHonorLoss=" + (settings.fixSelfDefenseHonorLoss ? "true" : "false") +
            ", FixSelfDefenseBounties=" + (settings.fixSelfDefenseBounties ? "true" : "false") +
            ", DisableHonorSystem=" + (settings.disableHonorSystem ? "true" : "false") +
            ", DisableBodySearchHonorLoss=" + (settings.disableBodySearchHonorLoss ? "true" : "false") +
            ", DisableAntagonizeHonorLoss=" + (settings.disableAntagonizeHonorLoss ? "true" : "false") +
            ", SkipIntroSplashes=" + (settings.skipIntroSplashes ? "true" : "false") +
            ", SkipLegal=" + (settings.skipLegal ? "true" : "false") +
            ", StraightToStoryMode=" + (settings.straightToStoryMode ? "true" : "false") +
            ", CameraFOVOffset=" + std::to_string(settings.cameraFovOffset) +
            ", RemoveBlackBars=" + (settings.removeBlackBars ? "true" : "false") +
            ", EnableDLSSAutoExposure=" + (settings.enableDLSSAutoExposure ? "true" : "false") +
            ", DisableInteriorLighting=" + (settings.disableInteriorLighting ? "true" : "false") +
            ", DisableDistantLights=" + (settings.disableDistantLights ? "true" : "false") +
            ", DisableIdleCamera=" + (settings.disableIdleCamera ? "true" : "false") +
            ", FixFullscreenTabOut=" + (settings.fixFullscreenTabOut ? "true" : "false") +
            ", MinimizeOnTabOut=" + (settings.minimizeOnTabOut ? "true" : "false") +
            ", DisableDrunkFilter=" + (settings.disableDrunkFilter ? "true" : "false") +
            ", DisableEagleEyeFilter=" + (settings.disableEagleEyeFilter ? "true" : "false") +
            ", DisableDeadEyeFilter=" + (settings.disableDeadEyeFilter ? "true" : "false") +
            ", DisableWeaponWheelFilter=" + (settings.disableWeaponWheelFilter ? "true" : "false") +
            ", DisableLowHealthFilter=" + (settings.disableLowHealthFilter ? "true" : "false") +
            ", DisableHeadBob=" + (settings.disableHeadBob ? "true" : "false") +
            ", ReduceCameraShake=" + (settings.reduceCameraShake ? "true" : "false") +
            ", ReduceCameraSway=" + (settings.reduceCameraSway ? "true" : "false") +
            ", ReduceWeaponRecoil=" + (settings.reduceWeaponRecoil ? "true" : "false") +
            ", RecoilShakeAmplitudeScale=" + std::to_string(settings.recoilShakeAmplitudeScale) +
            ", ReduceFirstPersonWeaponRecoil=" + (settings.reduceFirstPersonWeaponRecoil ? "true" : "false") +
            ", FirstPersonRecoilShakeAmplitudeScale=" + std::to_string(settings.firstPersonRecoilShakeAmplitudeScale) +
            ", DisableAmbientMusic=" + (settings.disableAmbientMusic ? "true" : "false") +
            ", DisableCombatMusic=" + (settings.disableCombatMusic ? "true" : "false") +
            ", DisableWantedMusic=" + (settings.disableWantedMusic ? "true" : "false") +
            ", CrashDiagnostics=" + (settings.crashDiagnostics ? "true" : "false") +
            ".").c_str());
        Main::InitializeEarly(settings);
        Logger::Log("[SUCCESS] Early startup: complete.");
        return TRUE;
    }

    void InitializeEarly()
    {
        if (!InitOnceExecuteOnce(&earlyInitialization, DoEarlyInitialization, nullptr, nullptr))
            Logger::Log("[ERROR] Early startup: initialization failed.");
    }

    void Initialize()
    {
        InitializeEarly();
        Logger::LogStartup();
        Main::Initialize(settings);
        nextOtherUpdateAt = 0;
        Main::ConfigMenu::Initialize(GetIniPath(), settings);
        Main::NotifStats::Configure(settings.notification, settings.settingsMenu, settings.menuKey, settings.menuModifier);
        Logger::Log("[SUCCESS] Gameplay features: initialized.");
    }

    bool Reload(bool* restartNeeded)
    {
        const std::wstring iniPath = GetIniPath();
        if (iniPath.empty() || GetFileAttributesW(iniPath.c_str()) == INVALID_FILE_ATTRIBUTES)
        {
            Logger::Log("[ERROR] Reload Mod: config file not found.");
            return false;
        }
        Settings candidate = settings;
        ReadModSettings(iniPath, candidate);
        const bool restartRequired = Main::ModReload::Apply(settings, candidate);
        settings = candidate;
        nextOtherUpdateAt = 0;
        if (restartNeeded)
            *restartNeeded = restartRequired;
        Logger::SetEnabled(ReadIniBoolean(iniPath, L"General", L"Logging", true));
        Logger::SetDebugEnabled(ReadIniBoolean(iniPath, L"General", L"DebugLogging", false));
        Main::ConfigMenu::Initialize(iniPath, settings);
        Main::NotifStats::Configure(settings.notification, settings.settingsMenu, settings.menuKey, settings.menuModifier);
        Logger::Log(restartRequired ?
            "[INFO] Reload Mod: settings reloaded; restart needed for startup changes." :
            "[SUCCESS] Reload Mod: settings reloaded.");
        return true;
    }

    static unsigned int GetNonHorseUpdateDelay()
    {
        if (Main::ConfigMenu::IsOpen())
            return 0U;
        return settings.enabled && (settings.disableDrunkFilter || settings.disableEagleEyeFilter ||
            settings.disableDeadEyeFilter || settings.disableWeaponWheelFilter ||
            settings.disableLowHealthFilter || settings.disableIdleCamera || settings.disableHeadBob || settings.reduceCameraShake ||
            settings.reduceCameraSway || settings.reduceWeaponRecoil ||
            settings.reduceFirstPersonWeaponRecoil || settings.disableReticleSpread || settings.disableReticleSway ||
            settings.disableArrowTrails ||
            Main::Camera::NeedsFastUpdate() ||
            Main::Player::NeedsFastUpdate()) ? 0U :
            (settings.enabled && (settings.quickLootBodies || settings.quickLootAnimals ||
            settings.disableFirstPersonAutoRun || settings.disableEmptyAutoReload ||
            Main::Horse::NeedsFastUpdate() ||
            Main::NeedsMaskedGameplayUpdate() || Main::SelfDefense::NeedsFastUpdate()) ? 50U : 250U);
    }

    void Update()
    {
        Main::ConfigMenu::Update();
        if (settings.enabled)
        {
            bool updateOther = true;
            if (Main::Horse::NeedsAutoJumpFrameUpdate())
            {
                const unsigned int otherDelay = GetNonHorseUpdateDelay();
                const ULONGLONG now = GetTickCount64();
                updateOther = otherDelay == 0U || now >= nextOtherUpdateAt;
                if (updateOther)
                    nextOtherUpdateAt = now + otherDelay;
            }
            Main::Update(settings, updateOther);
        }
    }

    unsigned int GetUpdateDelay()
    {
        if (settings.enabled && Main::Horse::NeedsPerFrameUpdate())
            return 0U;
        return GetNonHorseUpdateDelay();
    }

}
