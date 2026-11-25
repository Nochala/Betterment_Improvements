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

        if (!CreateDirectoryW(directory.c_str(), nullptr))
        {
            const DWORD createError = GetLastError();
            const DWORD attributes = GetFileAttributesW(directory.c_str());
            if (createError != ERROR_ALREADY_EXISTS || attributes == INVALID_FILE_ATTRIBUTES ||
                !(attributes & FILE_ATTRIBUTE_DIRECTORY))
            {
                Logger::FileWriteFailure((directory + L"\\BettermentImprovements.ini").c_str(),
                    "prepare the configuration directory", createError == ERROR_ALREADY_EXISTS ? ERROR_DIRECTORY : createError);
                return rootIni;
            }
        }

        const std::wstring iniPath = directory + L"\\BettermentImprovements.ini";
        const DWORD rootAttributes = GetFileAttributesW(rootIni.c_str());
        if (GetFileAttributesW(iniPath.c_str()) == INVALID_FILE_ATTRIBUTES &&
            rootAttributes != INVALID_FILE_ATTRIBUTES && !(rootAttributes & FILE_ATTRIBUTE_DIRECTORY))
        {
            if (!CopyFileW(rootIni.c_str(), iniPath.c_str(), TRUE))
            {
                Logger::FileWriteFailure(iniPath.c_str(), "copy the game-root configuration", GetLastError());
                return rootIni;
            }
            Logger::Log("[SUCCESS] INI: copied existing config to mod folder.");
        }
        return iniPath;
    }

    bool ParseBoolean(const wchar_t* value, bool defaultValue)
    {
        if (CompareStringOrdinal(value, -1, L"true", -1, TRUE) == CSTR_EQUAL)
            return true;
        if (CompareStringOrdinal(value, -1, L"false", -1, TRUE) == CSTR_EQUAL)
            return false;

        return defaultValue;
    }

    bool ReadIniBoolean(const std::wstring& path, const wchar_t* section, const wchar_t* key, bool defaultValue)
    {
        wchar_t value[16]{};
        GetPrivateProfileStringW(section, key, defaultValue ? L"true" : L"false", value, 16, path.c_str());
        return ParseBoolean(value, defaultValue);
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

    unsigned int ParseMenuModifier(const std::wstring& value)
    {
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

    float ParseCameraFovOffset(const wchar_t* value, float fallback)
    {
        wchar_t* end = nullptr;
        const float parsed = std::wcstof(value, &end);
        if (end == value || *end != L'\0' || !std::isfinite(parsed))
            return fallback;
        if (parsed < -3.0f)
            return -3.0f;
        if (parsed > 3.0f)
            return 3.0f;
        return parsed;
    }

    float ReadCameraFovOffset(const std::wstring& path)
    {
        wchar_t value[64] = {};
        GetPrivateProfileStringW(L"Visual", L"CameraFOVOffset", L"0.0", value, 64, path.c_str());
        return ParseCameraFovOffset(value, 0.0f);
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

    bool WriteIniValue(const std::wstring& path, const wchar_t* section, const wchar_t* key, const wchar_t* value)
    {
        return Logger::WriteProfileValue(section, key, value, path.c_str());
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

            success = WriteIniValue(path, L"General", L"Enabled", L"true") && success;
            if (success)
                Logger::Log("[SUCCESS] INI: default config created.");
        }
        else if (attributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            Logger::FileWriteFailure(path.c_str(), "write configuration", ERROR_DIRECTORY);
            return false;
        }

        wchar_t existing[8] = {};
        GetPrivateProfileStringW(L"General", L"Logging", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"General", L"Logging", L"true") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"General", L"DebugLogging", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"General", L"DebugLogging", L"false") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"General", L"Notification", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"General", L"Notification", L"true") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"General", L"SettingsMenu", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"General", L"SettingsMenu", L"true") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"General", L"MenuKey", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"General", L"MenuKey", L"F8") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"General", L"MenuModifier", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"General", L"MenuModifier", L"none") && success;

        wchar_t oldTextureCrashSetting[8] = {};
        GetPrivateProfileStringW(L"Engine", L"YTDCrashFix", L"", oldTextureCrashSetting, 8, path.c_str());
        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Engine", L"TextureCrashFix", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
        {
            const wchar_t* value = oldTextureCrashSetting[0] == L'\0' ? L"true" :
                ReadIniBoolean(path, L"Engine", L"YTDCrashFix", true) ? L"true" : L"false";
            success = WriteIniValue(path, L"Engine", L"TextureCrashFix", value) && success;
        }
        if (oldTextureCrashSetting[0] != L'\0')
        {
            existing[0] = L'\0';
            GetPrivateProfileStringW(L"Engine", L"TextureCrashFix", L"", existing, 8, path.c_str());
            if (existing[0] != L'\0')
                success = (Logger::WriteProfileValue(L"Engine", L"YTDCrashFix", nullptr, path.c_str()) != FALSE) && success;
        }

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Engine", L"ExtendHeapLimits", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Engine", L"ExtendHeapLimits", L"true") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Engine", L"ExtendPackfileLimits", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Engine", L"ExtendPackfileLimits", L"true") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Engine", L"ExtendPedMemoryLimits", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Engine", L"ExtendPedMemoryLimits", L"true") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Engine", L"ExtendStreamingPools", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Engine", L"ExtendStreamingPools", L"true") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Engine", L"ExtendPopulationStreamingPools", L"", existing, 8, path.c_str());
        if (existing[0] != L'\0' &&
            !Logger::WriteProfileValue(L"Engine", L"ExtendPopulationStreamingPools", nullptr, path.c_str()))
            success = false;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Engine", L"OptimizeStreamingMemory", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Engine", L"OptimizeStreamingMemory", L"true") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Engine", L"ExtendAssetPools", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Engine", L"ExtendAssetPools", L"true") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Engine", L"TextureStreamingStutterFix", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Engine", L"TextureStreamingStutterFix", L"false") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Engine", L"ImproveTextureStreaming", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Engine", L"ImproveTextureStreaming", L"true") && success;

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
                !Logger::WriteProfileValue(option[0], option[1], nullptr, path.c_str()))
                success = false;
        }

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Engine", L"ExpandSSRCoverage", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Engine", L"ExpandSSRCoverage", L"false") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Engine", L"SSROverscanScale", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Engine", L"SSROverscanScale", L"1.25") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Engine", L"SSROverscanTestMode", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Engine", L"SSROverscanTestMode", L"0") && success;

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

            if (Logger::WriteProfileValue(option[0], option[1], nullptr, path.c_str()))
            {
                wchar_t remaining[256] = {};
                if (GetPrivateProfileSectionW(option[0], remaining, 256, path.c_str()) == 0)
                    Logger::WriteProfileValue(option[0], nullptr, nullptr, path.c_str());
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
            success = WriteIniValue(path, L"Player", L"DisableFirstPersonAutoRun", L"false") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Player", L"QuickLootBodies", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Player", L"QuickLootBodies", L"false") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Player", L"QuickLootAnimals", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Player", L"QuickLootAnimals", L"false") && success;

        const auto migrateGameplayOption = [&](const wchar_t* updatedKey, const wchar_t* legacyKey,
            const wchar_t* olderLegacyKey, const wchar_t* defaultValue)
        {
            wchar_t updated[8] = {};
            wchar_t legacy[8] = {};
            wchar_t olderLegacy[8] = {};
            GetPrivateProfileStringW(L"Gameplay", updatedKey, L"", updated, 8, path.c_str());
            GetPrivateProfileStringW(L"Gameplay", legacyKey, L"", legacy, 8, path.c_str());
            if (olderLegacyKey != nullptr)
                GetPrivateProfileStringW(L"Gameplay", olderLegacyKey, L"", olderLegacy, 8, path.c_str());

            if (updated[0] == L'\0' && !WriteIniValue(path, L"Gameplay", updatedKey,
                legacy[0] != L'\0' ? legacy : olderLegacy[0] != L'\0' ? olderLegacy : defaultValue))
                return false;

            bool migrated = true;
            if (legacy[0] != L'\0' && !Logger::WriteProfileValue(L"Gameplay", legacyKey, nullptr, path.c_str()))
            {
                Logger::Log("[WARNING] INI: could not remove old gameplay setting.");
                migrated = false;
            }
            if (olderLegacy[0] != L'\0' && !Logger::WriteProfileValue(L"Gameplay", olderLegacyKey, nullptr, path.c_str()))
            {
                Logger::Log("[WARNING] INI: could not remove old gameplay setting.");
                migrated = false;
            }
            return migrated;
        };
        success = migrateGameplayOption(L"DisableAutoHorseEquip", L"NoAutoHorseEquip", nullptr,
            L"true") && success;
        success = migrateGameplayOption(L"DisableAutoHorseHolster", L"NoAutoHorseHolster", L"NoAutoHorseHolder",
            L"true") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Gameplay", L"DisableHorseAutoJump", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Gameplay", L"DisableHorseAutoJump", L"false") && success;
        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Gameplay", L"DisableHorseWhistleDistance", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Gameplay", L"DisableHorseWhistleDistance", L"false") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Gameplay", L"FixHorseFollowBehavior", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Gameplay", L"FixHorseFollowBehavior", L"false") && success;
        success = migrateGameplayOption(L"DisableAutoAmmoPickup", L"NoAutoAmmoPickup", nullptr,
            L"false") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Gameplay", L"DisableHonorLossWhenMasked", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Gameplay", L"DisableHonorLossWhenMasked", L"true") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Gameplay", L"DisableBountyWhenMasked", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Gameplay", L"DisableBountyWhenMasked", L"true") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Gameplay", L"DisableBounties", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Gameplay", L"DisableBounties", L"false") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Gameplay", L"FixSelfDefenseHonorLoss", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Gameplay", L"FixSelfDefenseHonorLoss", L"true") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Gameplay", L"FixSelfDefenseBounties", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Gameplay", L"FixSelfDefenseBounties", L"true") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Gameplay", L"DisableHonorSystem", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Gameplay", L"DisableHonorSystem", L"false") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Gameplay", L"DisableBodySearchHonorLoss", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Gameplay", L"DisableBodySearchHonorLoss", L"true") && success;

        success = migrateGameplayOption(L"DisableAntagonizeHonorLoss", L"DisableAntagonizehHonorLoss", nullptr,
            L"false") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Gameplay", L"DisableOutOfBoundSnipers", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Gameplay", L"DisableOutOfBoundSnipers", L"false") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Weapons", L"DisableAutoReload", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Weapons", L"DisableAutoReload", L"true") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Weapons", L"DisableDeadEyeAutoReload", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Weapons", L"DisableDeadEyeAutoReload", L"true") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Weapons", L"DisableEmptyAutoReload", L"", existing, 8, path.c_str());
        wchar_t previousEmptyReload[8] = {};
        GetPrivateProfileStringW(L"Weapons", L"DisableEmptyClipAutoReload", L"", previousEmptyReload, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Weapons", L"DisableEmptyAutoReload",
                previousEmptyReload[0] != L'\0' ? previousEmptyReload : L"true") && success;
        if (previousEmptyReload[0] != L'\0' &&
            !Logger::WriteProfileValue(L"Weapons", L"DisableEmptyClipAutoReload", nullptr, path.c_str()))
        {
            Logger::Log("[WARNING] INI: could not remove the old Weapons.DisableEmptyClipAutoReload option.");
            success = false;
        }

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Weapons", L"DisableArrowTrails", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Weapons", L"DisableArrowTrails", L"false") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Weapons", L"DisableReticleSpread", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Weapons", L"DisableReticleSpread", L"false") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Weapons", L"DisableReticleSway", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Weapons", L"DisableReticleSway", L"false") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Weapons", L"DisableForcedScope", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Weapons", L"DisableForcedScope", L"true") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Weapons", L"DisableWeaponDegradation", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Weapons", L"DisableWeaponDegradation", L"false") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Weapons", L"DisableHitmarkers", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Weapons", L"DisableHitmarkers", L"false") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Weapons", L"DisableReticleHighlighting", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Weapons", L"DisableReticleHighlighting", L"false") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Weapons", L"ProbeWeaponSignatures", L"", existing, 8, path.c_str());
        if (existing[0] != L'\0' &&
            !Logger::WriteProfileValue(L"Weapons", L"ProbeWeaponSignatures", nullptr, path.c_str()))
        {
            Logger::Log("[WARNING] INI: could not remove obsolete Weapons.ProbeWeaponSignatures option.");
            success = false;
        }

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Visual", L"CameraFOVOffset", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Visual", L"CameraFOVOffset", L"0.0") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Visual", L"RemoveBlackBars", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Visual", L"RemoveBlackBars", L"false") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Visual", L"DisableVignette", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Visual", L"DisableVignette", L"false") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Visual", L"DisableBloom", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Visual", L"DisableBloom", L"false") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Visual", L"DisableChromaticAberration", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Visual", L"DisableChromaticAberration", L"false") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Visual", L"DisableCompositeDithering", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Visual", L"DisableCompositeDithering", L"false") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Visual", L"EnableDLSSAutoExposure", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Visual", L"EnableDLSSAutoExposure", L"false") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Visual", L"DisableInteriorLighting", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Visual", L"DisableInteriorLighting", L"false") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Visual", L"DisableDistantLights", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Visual", L"DisableDistantLights", L"false") && success;

        const wchar_t* obsoleteSettings[][2] =
        {
            { L"Visual", L"RemoveCameraFade" },
            { L"Visual", L"DisableFilmGrain" },
            { L"Visual", L"ReduceWeaponRecoil" },
            { L"Visual", L"RecoilShakeAmplitudeScale" },
            { L"Visual", L"ReduceFirstPersonWeaponRecoil" },
            { L"Visual", L"FirstPersonRecoilShakeAmplitudeScale" },
            { L"Weapons", L"DisableWeaponRecoil" },
            { L"Weapons", L"DisableFirstPersonWeaponRecoil" },
            { L"Diagnostics", L"DumpNativeHashes" },
            { L"Camera", L"RemoveCameraFade" },
            { L"Diagnostics", L"TraceCameraNatives" }
        };
        for (const auto& option : obsoleteSettings)
        {
            wchar_t previous[16] = {};
            GetPrivateProfileStringW(option[0], option[1], L"__missing__", previous, 16, path.c_str());
            if (CompareStringOrdinal(previous, -1, L"__missing__", -1, TRUE) == CSTR_EQUAL)
                continue;

            if (Logger::WriteProfileValue(option[0], option[1], nullptr, path.c_str()))
            {
                wchar_t remaining[256] = {};
                if (GetPrivateProfileSectionW(option[0], remaining, 256, path.c_str()) == 0)
                    Logger::WriteProfileValue(option[0], nullptr, nullptr, path.c_str());
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
            success = WriteIniValue(path, L"Visual", L"DisableDrunkFilter", L"false") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Visual", L"DisablePauseMenuFilter", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Visual", L"DisablePauseMenuFilter", L"false") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Visual", L"DisableEagleEyeFilter", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Visual", L"DisableEagleEyeFilter", L"false") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Visual", L"DisableDeadEyeFilter", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Visual", L"DisableDeadEyeFilter", L"false") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Visual", L"DisableWeaponWheelFilter", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Visual", L"DisableWeaponWheelFilter", L"false") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Visual", L"DisableLowHealthFilter", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Visual", L"DisableLowHealthFilter", L"false") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Visual", L"DisableIdleCamera", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Visual", L"DisableIdleCamera", L"false") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Visual", L"FixFullscreenTabOut", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Visual", L"FixFullscreenTabOut", L"false") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Visual", L"MinimizeOnTabOut", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Visual", L"MinimizeOnTabOut", L"false") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Visual", L"DisableCameraSmoothing", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Visual", L"DisableCameraSmoothing", L"false") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Visual", L"DisableHeadBob", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Visual", L"DisableHeadBob", L"false") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Visual", L"ReduceCameraShake", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Visual", L"ReduceCameraShake", L"false") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Visual", L"ReduceCameraSway", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Visual", L"ReduceCameraSway", L"false") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Ambience", L"DisableAmbientMusic", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Ambience", L"DisableAmbientMusic", L"false") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Ambience", L"DisableCombatMusic", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Ambience", L"DisableCombatMusic", L"false") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Ambience", L"DisableWantedMusic", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Ambience", L"DisableWantedMusic", L"false") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Diagnostics", L"CrashDiagnostics", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Diagnostics", L"CrashDiagnostics", L"false") && success;

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
            if (!Logger::WriteProfileValue(option[0], option[1], nullptr, path.c_str()))
            {
                Logger::Log("[WARNING] INI: could not remove old graphics setting.");
                success = false;
            }
        }

        wchar_t obsoleteInterval[16] = {};
        GetPrivateProfileStringW(L"General", L"UpdateIntervalMs", L"__missing__", obsoleteInterval, 16, path.c_str());
        if (CompareStringOrdinal(obsoleteInterval, -1, L"__missing__", -1, TRUE) != CSTR_EQUAL)
        {
            if (Logger::WriteProfileValue(L"General", L"UpdateIntervalMs", nullptr, path.c_str()))
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
            if (Logger::WriteProfileValue(L"Debug", L"Enabled", nullptr, path.c_str()))
            {
                wchar_t remaining[256] = {};
                if (GetPrivateProfileSectionW(L"Debug", remaining, 256, path.c_str()) == 0)
                    Logger::WriteProfileValue(L"Debug", nullptr, nullptr, path.c_str());
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
                success = WriteIniValue(path, L"Startup", key, inherited) && success;
        }

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Startup", L"StraightToStoryMode", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Startup", L"StraightToStoryMode", L"false") && success;

        wchar_t obsoleteStartupStatistics[16] = {};
        GetPrivateProfileStringW(L"Startup", L"StartupStatistics", L"__missing__", obsoleteStartupStatistics, 16, path.c_str());
        if (CompareStringOrdinal(obsoleteStartupStatistics, -1, L"__missing__", -1, TRUE) != CSTR_EQUAL)
            success = Logger::WriteProfileValue(L"Startup", L"StartupStatistics", nullptr, path.c_str()) && success;

        GetPrivateProfileStringW(L"General", L"StartupStatDisplay", L"__missing__", obsoleteStartupStatistics, 16, path.c_str());
        if (CompareStringOrdinal(obsoleteStartupStatistics, -1, L"__missing__", -1, TRUE) != CSTR_EQUAL)
            success = Logger::WriteProfileValue(L"General", L"StartupStatDisplay", nullptr, path.c_str()) && success;

        wchar_t obsoleteValue[16] = {};
        GetPrivateProfileStringW(L"Startup", L"FastStoryModeLoading", L"__missing__", obsoleteValue, 16, path.c_str());
        if (CompareStringOrdinal(obsoleteValue, -1, L"__missing__", -1, TRUE) != CSTR_EQUAL)
        {
            if (Logger::WriteProfileValue(L"Startup", L"FastStoryModeLoading", nullptr, path.c_str()))
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
            if (Logger::WriteProfileValue(L"Startup", L"RemoveOnlineTab", nullptr, path.c_str()))
                Logger::Log("[INFO] INI: removed old RemoveOnlineTab setting.");
            else
            {
                Logger::Log("[WARNING] INI: could not remove the obsolete RemoveOnlineTab setting.");
                success = false;
            }
        }

        if (success && legacy[0] != L'\0')
        {
            if (Logger::WriteProfileValue(L"Startup", L"SkipIntroSplashesAndLegal", nullptr, path.c_str()))
                Logger::Log("[INFO] INI: split old SkipIntroSplashesAndLegal setting.");
            else
                Logger::Log("[WARNING] INI: old combined skip option could not be removed; the new options take priority.");
        }

        return success;
    }

    template <typename T>
    struct SettingBinding
    {
        const wchar_t* section;
        const wchar_t* key;
        T GameplayFixes::Settings::* member;
        T defaultValue;
    };

    constexpr SettingBinding<bool> booleanSettings[] = {
        { L"General", L"Enabled", &GameplayFixes::Settings::enabled, true },
        { L"General", L"SettingsMenu", &GameplayFixes::Settings::settingsMenu, true },
        { L"General", L"Notification", &GameplayFixes::Settings::notification, true },
        { L"Engine", L"TextureCrashFix", &GameplayFixes::Settings::textureCrashFix, true },
        { L"Engine", L"ExtendHeapLimits", &GameplayFixes::Settings::extendHeapLimits, true },
        { L"Engine", L"ExtendPackfileLimits", &GameplayFixes::Settings::extendPackfileLimits, true },
        { L"Engine", L"ExtendPedMemoryLimits", &GameplayFixes::Settings::extendPedMemoryLimits, true },
        { L"Engine", L"ExtendStreamingPools", &GameplayFixes::Settings::extendStreamingPools, true },
        { L"Engine", L"OptimizeStreamingMemory", &GameplayFixes::Settings::optimizeStreamingMemory, true },
        { L"Engine", L"ExtendAssetPools", &GameplayFixes::Settings::extendAssetPools, true },
        { L"Engine", L"TextureStreamingStutterFix", &GameplayFixes::Settings::textureStreamingStutterFix, false },
        { L"Engine", L"ImproveTextureStreaming", &GameplayFixes::Settings::improveTextureStreaming, true },
        { L"Engine", L"ExpandSSRCoverage", &GameplayFixes::Settings::expandSsrCoverage, false },
        { L"Gameplay", L"DisableAutoHorseEquip", &GameplayFixes::Settings::noAutoHorseEquip, true },
        { L"Gameplay", L"DisableAutoHorseHolster", &GameplayFixes::Settings::noAutoHorseHolster, true },
        { L"Gameplay", L"DisableHorseAutoJump", &GameplayFixes::Settings::disableHorseAutoJump, false },
        { L"Gameplay", L"DisableHorseWhistleDistance", &GameplayFixes::Settings::disableHorseWhistleDistance, false },
        { L"Gameplay", L"FixHorseFollowBehavior", &GameplayFixes::Settings::fixHorseFollowBehavior, false },
        { L"Gameplay", L"DisableAutoAmmoPickup", &GameplayFixes::Settings::noAutoAmmoPickup, false },
        { L"Weapons", L"DisableAutoReload", &GameplayFixes::Settings::disableAutoReload, true },
        { L"Weapons", L"DisableDeadEyeAutoReload", &GameplayFixes::Settings::disableDeadEyeAutoReload, true },
        { L"Weapons", L"DisableEmptyAutoReload", &GameplayFixes::Settings::disableEmptyAutoReload, true },
        { L"Weapons", L"DisableArrowTrails", &GameplayFixes::Settings::disableArrowTrails, false },
        { L"Weapons", L"DisableReticleSpread", &GameplayFixes::Settings::disableReticleSpread, false },
        { L"Weapons", L"DisableReticleSway", &GameplayFixes::Settings::disableReticleSway, false },
        { L"Weapons", L"DisableForcedScope", &GameplayFixes::Settings::disableForcedScope, true },
        { L"Weapons", L"DisableWeaponDegradation", &GameplayFixes::Settings::disableWeaponDegradation, false },
        { L"Weapons", L"DisableHitmarkers", &GameplayFixes::Settings::disableHitmarkers, false },
        { L"Weapons", L"DisableReticleHighlighting", &GameplayFixes::Settings::disableReticleHighlighting, false },
        { L"Player", L"DisableFirstPersonAutoRun", &GameplayFixes::Settings::disableFirstPersonAutoRun, false },
        { L"Player", L"QuickLootBodies", &GameplayFixes::Settings::quickLootBodies, false },
        { L"Player", L"QuickLootAnimals", &GameplayFixes::Settings::quickLootAnimals, false },
        { L"Gameplay", L"DisableHonorLossWhenMasked", &GameplayFixes::Settings::disableHonorLossWhenMasked, true },
        { L"Gameplay", L"DisableBountyWhenMasked", &GameplayFixes::Settings::disableBountyWhenMasked, true },
        { L"Gameplay", L"DisableBounties", &GameplayFixes::Settings::disableBounties, false },
        { L"Gameplay", L"FixSelfDefenseHonorLoss", &GameplayFixes::Settings::fixSelfDefenseHonorLoss, true },
        { L"Gameplay", L"FixSelfDefenseBounties", &GameplayFixes::Settings::fixSelfDefenseBounties, true },
        { L"Gameplay", L"DisableHonorSystem", &GameplayFixes::Settings::disableHonorSystem, false },
        { L"Gameplay", L"DisableBodySearchHonorLoss", &GameplayFixes::Settings::disableBodySearchHonorLoss, true },
        { L"Gameplay", L"DisableAntagonizeHonorLoss", &GameplayFixes::Settings::disableAntagonizeHonorLoss, false },
        { L"Gameplay", L"DisableOutOfBoundSnipers", &GameplayFixes::Settings::disableOutOfBoundSnipers, false },
        { L"Startup", L"SkipIntroSplashes", &GameplayFixes::Settings::skipIntroSplashes, true },
        { L"Startup", L"SkipLegal", &GameplayFixes::Settings::skipLegal, true },
        { L"Startup", L"StraightToStoryMode", &GameplayFixes::Settings::straightToStoryMode, false },
        { L"Visual", L"RemoveBlackBars", &GameplayFixes::Settings::removeBlackBars, false },
        { L"Visual", L"DisableVignette", &GameplayFixes::Settings::disableVignette, false },
        { L"Visual", L"DisableBloom", &GameplayFixes::Settings::disableBloom, false },
        { L"Visual", L"DisableChromaticAberration", &GameplayFixes::Settings::disableChromaticAberration, false },
        { L"Visual", L"DisableCompositeDithering", &GameplayFixes::Settings::disableCompositeDithering, false },
        { L"Visual", L"EnableDLSSAutoExposure", &GameplayFixes::Settings::enableDLSSAutoExposure, false },
        { L"Visual", L"DisableInteriorLighting", &GameplayFixes::Settings::disableInteriorLighting, false },
        { L"Visual", L"DisableDistantLights", &GameplayFixes::Settings::disableDistantLights, false },
        { L"Visual", L"DisableIdleCamera", &GameplayFixes::Settings::disableIdleCamera, false },
        { L"Visual", L"FixFullscreenTabOut", &GameplayFixes::Settings::fixFullscreenTabOut, false },
        { L"Visual", L"MinimizeOnTabOut", &GameplayFixes::Settings::minimizeOnTabOut, false },
        { L"Visual", L"DisableDrunkFilter", &GameplayFixes::Settings::disableDrunkFilter, false },
        { L"Visual", L"DisablePauseMenuFilter", &GameplayFixes::Settings::disablePauseMenuFilter, false },
        { L"Visual", L"DisableEagleEyeFilter", &GameplayFixes::Settings::disableEagleEyeFilter, false },
        { L"Visual", L"DisableDeadEyeFilter", &GameplayFixes::Settings::disableDeadEyeFilter, false },
        { L"Visual", L"DisableWeaponWheelFilter", &GameplayFixes::Settings::disableWeaponWheelFilter, false },
        { L"Visual", L"DisableLowHealthFilter", &GameplayFixes::Settings::disableLowHealthFilter, false },
        { L"Visual", L"DisableCameraSmoothing", &GameplayFixes::Settings::disableCameraSmoothing, false },
        { L"Visual", L"DisableHeadBob", &GameplayFixes::Settings::disableHeadBob, false },
        { L"Visual", L"ReduceCameraShake", &GameplayFixes::Settings::reduceCameraShake, false },
        { L"Visual", L"ReduceCameraSway", &GameplayFixes::Settings::reduceCameraSway, false },
        { L"Ambience", L"DisableAmbientMusic", &GameplayFixes::Settings::disableAmbientMusic, false },
        { L"Ambience", L"DisableCombatMusic", &GameplayFixes::Settings::disableCombatMusic, false },
        { L"Ambience", L"DisableWantedMusic", &GameplayFixes::Settings::disableWantedMusic, false },
        { L"Diagnostics", L"CrashDiagnostics", &GameplayFixes::Settings::crashDiagnostics, false },
    };

    bool MatchesSetting(const wchar_t* first, const wchar_t* second)
    {
        return CompareStringOrdinal(first, -1, second, -1, TRUE) == CSTR_EQUAL;
    }

    bool ReadMenuSetting(GameplayFixes::Settings& target, const wchar_t* section, const wchar_t* key,
        const std::wstring& value)
    {
        if (Main::ModReload::RequiresGameRestart(section, key))
            return false;

        if (MatchesSetting(section, L"General") && MatchesSetting(key, L"MenuKey"))
        {
            const auto parsed = ParseMenuKey(value);
            target.menuKey = parsed < 0 ? VK_F8 : parsed;
            return true;
        }
        if (MatchesSetting(section, L"General") && MatchesSetting(key, L"MenuModifier"))
        {
            target.menuModifier = ParseMenuModifier(value);
            return true;
        }

        if (MatchesSetting(section, L"Visual") && MatchesSetting(key, L"CameraFOVOffset"))
        {
            target.cameraFovOffset = ParseCameraFovOffset(value.c_str(), target.cameraFovOffset);
            return true;
        }

        for (const auto& option : booleanSettings)
            if (MatchesSetting(section, option.section) && MatchesSetting(key, option.key))
            {
                target.*option.member = ParseBoolean(value.c_str(), target.*option.member);
                return true;
            }
        return false;
    }

    void ReadModSettings(const std::wstring& iniPath, GameplayFixes::Settings& settings)
    {
        for (const auto& option : booleanSettings)
            settings.*option.member = ReadIniBoolean(iniPath, option.section, option.key, option.defaultValue);
        settings.menuKey = ReadMenuKey(iniPath);
        settings.menuModifier = ParseMenuModifier(ReadIniString(iniPath, L"General", L"MenuModifier", L"none"));
        settings.ssrOverscanScale = ReadOverscanScale(iniPath);
        settings.ssrOverscanTestMode = ReadOverscanTestMode(iniPath);
        settings.cameraFovOffset = ReadCameraFovOffset(iniPath);
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
            ", DisableVignette=" + (settings.disableVignette ? "true" : "false") +
            ", DisableBloom=" + (settings.disableBloom ? "true" : "false") +
            ", DisableChromaticAberration=" + (settings.disableChromaticAberration ? "true" : "false") +
            ", DisableCompositeDithering=" + (settings.disableCompositeDithering ? "true" : "false") +
            ", EnableDLSSAutoExposure=" + (settings.enableDLSSAutoExposure ? "true" : "false") +
            ", DisableInteriorLighting=" + (settings.disableInteriorLighting ? "true" : "false") +
            ", DisableDistantLights=" + (settings.disableDistantLights ? "true" : "false") +
            ", DisableIdleCamera=" + (settings.disableIdleCamera ? "true" : "false") +
            ", FixFullscreenTabOut=" + (settings.fixFullscreenTabOut ? "true" : "false") +
            ", MinimizeOnTabOut=" + (settings.minimizeOnTabOut ? "true" : "false") +
            ", DisableDrunkFilter=" + (settings.disableDrunkFilter ? "true" : "false") +
            ", DisablePauseMenuFilter=" + (settings.disablePauseMenuFilter ? "true" : "false") +
            ", DisableEagleEyeFilter=" + (settings.disableEagleEyeFilter ? "true" : "false") +
            ", DisableDeadEyeFilter=" + (settings.disableDeadEyeFilter ? "true" : "false") +
            ", DisableWeaponWheelFilter=" + (settings.disableWeaponWheelFilter ? "true" : "false") +
            ", DisableLowHealthFilter=" + (settings.disableLowHealthFilter ? "true" : "false") +
            ", DisableCameraSmoothing=" + (settings.disableCameraSmoothing ? "true" : "false") +
            ", DisableHeadBob=" + (settings.disableHeadBob ? "true" : "false") +
            ", ReduceCameraShake=" + (settings.reduceCameraShake ? "true" : "false") +
            ", ReduceCameraSway=" + (settings.reduceCameraSway ? "true" : "false") +
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

    void ApplyMenuSetting(const wchar_t* section, const wchar_t* key, const std::wstring& value)
    {
        if (Main::ModReload::RequiresGameRestart(section, key))
            return;

        if (MatchesSetting(section, L"General"))
        {
            if (MatchesSetting(key, L"Logging"))
            {
                Logger::SetEnabled(ParseBoolean(value.c_str(), true));
                return;
            }
            if (MatchesSetting(key, L"DebugLogging"))
            {
                Logger::SetDebugEnabled(ParseBoolean(value.c_str(), false));
                return;
            }
        }

        Settings candidate = settings;
        if (!ReadMenuSetting(candidate, section, key, value))
            return;

        const bool hotkeyChanged = settings.menuKey != candidate.menuKey || settings.menuModifier != candidate.menuModifier;
        const bool notificationChanged = settings.notification != candidate.notification;
        Main::ModReload::Apply(settings, candidate);
        settings = candidate;
        nextOtherUpdateAt = 0;
        if (hotkeyChanged)
            Main::ConfigMenu::SetHotkey(settings.menuKey, settings.menuModifier);
        if (hotkeyChanged || notificationChanged)
            Main::NotifStats::Configure(settings.notification, settings.settingsMenu, settings.menuKey, settings.menuModifier);
    }

    bool Reload(bool* restartNeeded, const MenuSettingChanges* unsaved)
    {
        const std::wstring iniPath = GetIniPath();
        if (iniPath.empty() || GetFileAttributesW(iniPath.c_str()) == INVALID_FILE_ATTRIBUTES)
        {
            Logger::Log("[ERROR] Reload Mod: config file not found.");
            return false;
        }
        Settings candidate = settings;
        ReadModSettings(iniPath, candidate);
        bool logging = ReadIniBoolean(iniPath, L"General", L"Logging", true);
        bool debugLogging = ReadIniBoolean(iniPath, L"General", L"DebugLogging", false);
        if (unsaved)
            for (const auto& [setting, value] : *unsaved)
            {
                const auto* section = setting.first.c_str();
                const auto* key = setting.second.c_str();
                if (MatchesSetting(section, L"General") && MatchesSetting(key, L"Logging"))
                    logging = ParseBoolean(value.c_str(), logging);
                else if (MatchesSetting(section, L"General") && MatchesSetting(key, L"DebugLogging"))
                    debugLogging = ParseBoolean(value.c_str(), debugLogging);
                else
                    ReadMenuSetting(candidate, section, key, value);
            }

        const bool restartRequired = Main::ModReload::Apply(settings, candidate);
        settings = candidate;
        nextOtherUpdateAt = 0;
        if (restartNeeded)
            *restartNeeded = restartRequired;
        Logger::SetEnabled(logging);
        Logger::SetDebugEnabled(debugLogging);
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
        return settings.enabled && (settings.disableEagleEyeFilter ||
            settings.disableDeadEyeFilter || settings.disableWeaponWheelFilter ||
            settings.disableLowHealthFilter || settings.disableIdleCamera || settings.disableHeadBob || settings.reduceCameraShake ||
            settings.reduceCameraSway || settings.disableAutoReload || settings.disableEmptyAutoReload ||
            settings.disableReticleSpread || settings.disableReticleSway ||
            settings.disableArrowTrails ||
            Main::Camera::NeedsFastUpdate() ||
            Main::Player::NeedsFastUpdate()) ? 0U :
            (settings.enabled && (settings.quickLootBodies || settings.quickLootAnimals ||
            settings.disableFirstPersonAutoRun ||
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
