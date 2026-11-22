#include "ConfigMenu.h"
#include "../../Headers/GameplayFixes.h"
#include "../../Headers/Logger.h"
#include "../Headers/Helpers/ModReload.h"
#include "../Headers/NotifStats.h"
#include "main.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <cwchar>
#include <cwctype>
#include <string>
#include <vector>
#include <utility>

namespace
{
    constexpr std::uint64_t drawRect = 0x405224591DF02025ULL;
    constexpr std::uint64_t setScriptGfxDrawOrder = 0xCFCC78391C8B3814ULL;
    constexpr std::uint64_t createVarString = 0xFA925AC00EB830B9ULL;
    constexpr std::uint64_t bgDisplayText = 0x16794E044C9EFB58ULL;
    constexpr std::uint64_t bgSetTextScale = 0xA1253A3C870B6843ULL;
    constexpr std::uint64_t bgSetTextColor = 0x16FA5CE47F184F1EULL;
    constexpr std::uint64_t drawSprite = 0xC9884ECADE94CB34ULL;
    constexpr std::uint64_t requestStreamedTextureDict = 0xC1BA29DF5631B0F8ULL;
    constexpr std::uint64_t hasStreamedTextureDictLoaded = 0x54D6900929CCF162ULL;
    constexpr std::uint64_t isPauseMenuActive = 0x535384D6067BA42EULL;
    constexpr std::uint64_t isPlayerPlaying = 0xBFFB35986CAAE58CULL;
    constexpr std::uint64_t playerId = 0x217E9DC48139933DULL;
    constexpr std::uint64_t isLoadingScreenActive = 0x71D4BF5890659B0CULL;
    constexpr std::uint64_t playerPedId = 0x096275889B8E0EE0ULL;
    constexpr std::uint64_t doesEntityExist = 0xD42BD6EB2E0F1677ULL;
    constexpr std::uint64_t isScreenFadedIn = 0x37F9A426FBCF4AF2ULL;
    constexpr std::uint64_t playSoundFrontend = 0x67C540AA08E4A6F5ULL;

    enum Input : unsigned
    {
        Toggle = 1,
        Up = 2,
        Down = 4,
        Left = 8,
        Right = 16,
        Select = 32,
        Back = 64
    };

    enum class Page
    {
        Root,
        MenuSettings,
        GeneralSettings,
        IniSections,
        IniEntries
    };

    enum class ValueType
    {
        Boolean,
        Number,
        MenuKey,
        Modifier,
        Text
    };

    struct Entry
    {
        std::wstring key;
        std::wstring value;
        std::wstring savedValue;
        ValueType type = ValueType::Text;
        float minValue = 0.0f;
        float maxValue = 0.0f;
        float increment = 0.0f;
    };

    struct MenuRow
    {
        int entryIndex = -1;
        const char* heading = nullptr;
    };

    struct Section
    {
        std::wstring name;
        std::vector<Entry> entries;
        std::vector<MenuRow> menuRows;
        std::vector<int> entryRows;
    };

    std::wstring configPath;
    std::wstring cachePath;
    std::vector<Section> sections;
    Section generalSection;
    Page page = Page::Root;
    int menuPositionX = 3;
    int menuPositionY = 5;
    int menuSize = 100;
    int menuOpacity = 100;
    bool menuAudioEnabled = true;
    int savedMenuPositionX = 3;
    int savedMenuPositionY = 5;
    int savedMenuSize = 100;
    int savedMenuOpacity = 100;
    bool savedMenuAudioEnabled = true;
    bool menuPreferencesDirty = false;
    GameplayFixes::MenuSettingChanges pendingChanges;
    std::map<std::pair<std::wstring, std::wstring>, std::wstring> originalRestartValues;
    bool restartPendingFromDisk = false;
    std::string actionStatus;
    std::atomic<unsigned> pendingInput{ 0 };
    std::atomic<int> captureMode{ 0 };
    std::atomic<int> capturedKey{ 0 };
    std::atomic<bool> configured{ false };
    std::atomic<bool> menuOpen{ false };
    std::atomic<int> menuKey{ VK_F8 };
    std::atomic<unsigned> menuModifier{ 0 };
    int currentSection = -1;
    int selected = 0;
    ULONGLONG lastNavigationSoundAt = 0;
    float menuReveal = 0.0f;
    float menuRevealStart = 0.0f;
    float menuRevealTarget = 0.0f;
    ULONGLONG menuTransitionStartedAt = 0;
    bool menuTransitionActive = false;

    std::uint64_t FloatArgument(float value)
    {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        return bits;
    }

    bool Same(const std::wstring& left, const wchar_t* right)
    {
        return CompareStringOrdinal(left.c_str(), -1, right, -1, TRUE) == CSTR_EQUAL;
    }

    std::string Narrow(const std::wstring& value)
    {
        if (value.empty())
            return {};
        const int required = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
        if (required <= 0)
            return {};
        std::string result(required, '\0');
        WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), result.data(), required, nullptr, nullptr);
        for (char& character : result)
            if (character == '<' || character == '>' || character == '&')
                character = ' ';
        return result;
    }

    bool ParseNumber(const std::wstring& value, float& result)
    {
        wchar_t* end = nullptr;
        const float parsed = std::wcstof(value.c_str(), &end);
        if (end == value.c_str() || *end != L'\0' || !std::isfinite(parsed))
            return false;
        result = parsed;
        return true;
    }

    std::wstring CacheFilePath(const std::wstring& iniPath)
    {
        const size_t separator = iniPath.find_last_of(L"\\/");
        if (separator == std::wstring::npos)
            return {};
        std::wstring directory = iniPath.substr(0, separator);
        const size_t folderStart = directory.find_last_of(L"\\/");
        if (!Same(directory.substr(folderStart == std::wstring::npos ? 0 : folderStart + 1), L"BettermentImprovements"))
        {
            directory += L"\\BettermentImprovements";
            if (!CreateDirectoryW(directory.c_str(), nullptr))
            {
                const DWORD attributes = GetFileAttributesW(directory.c_str());
                if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY))
                    return {};
            }
        }
        const std::wstring result = directory + L"\\MenuSettings.cache";
        if (GetFileAttributesW(result.c_str()) == INVALID_FILE_ATTRIBUTES)
        {
            const size_t parent = directory.find_last_of(L"\\/");
            if (parent != std::wstring::npos &&
                Same(directory.substr(parent + 1), L"BettermentImprovements"))
            {
                const std::wstring prior = directory.substr(0, parent + 1) +
                    L"FusionFixRDR2\\MenuSettings.cache";
                CopyFileW(prior.c_str(), result.c_str(), TRUE);
            }
        }
        return result;
    }

    int ReadCacheNumber(const wchar_t* key, int fallback, int minimum, int maximum, int step = 1)
    {
        wchar_t buffer[32] = {};
        const std::wstring defaultValue = std::to_wstring(fallback);
        GetPrivateProfileStringW(L"Menu", key, defaultValue.c_str(), buffer, 32, cachePath.c_str());
        wchar_t* end = nullptr;
        const long value = std::wcstol(buffer, &end, 10);
        if (end == buffer || *end != L'\0' || value < minimum || value > maximum || value % step != 0)
            return fallback;
        return static_cast<int>(value);
    }

    void RecordSavedMenuPreferences()
    {
        savedMenuPositionX = menuPositionX;
        savedMenuPositionY = menuPositionY;
        savedMenuSize = menuSize;
        savedMenuOpacity = menuOpacity;
        savedMenuAudioEnabled = menuAudioEnabled;
        menuPreferencesDirty = false;
    }

    void RefreshMenuPreferencesDirty()
    {
        menuPreferencesDirty = menuPositionX != savedMenuPositionX || menuPositionY != savedMenuPositionY ||
            menuSize != savedMenuSize || menuOpacity != savedMenuOpacity ||
            menuAudioEnabled != savedMenuAudioEnabled;
    }

    bool SaveMenuPreferences()
    {
        if (cachePath.empty())
            return false;
        const std::wstring xValue = std::to_wstring(menuPositionX);
        const std::wstring yValue = std::to_wstring(menuPositionY);
        const std::wstring sizeValue = std::to_wstring(menuSize);
        const std::wstring opacityValue = std::to_wstring(menuOpacity);
        const wchar_t* audioValue = menuAudioEnabled ? L"true" : L"false";
        return WritePrivateProfileStringW(L"Menu", L"PositionX", xValue.c_str(), cachePath.c_str()) &&
            WritePrivateProfileStringW(L"Menu", L"PositionY", yValue.c_str(), cachePath.c_str()) &&
            WritePrivateProfileStringW(L"Menu", L"Size", sizeValue.c_str(), cachePath.c_str()) &&
            WritePrivateProfileStringW(L"Menu", L"Opacity", opacityValue.c_str(), cachePath.c_str()) &&
            WritePrivateProfileStringW(L"Menu", L"MenuAudio", audioValue, cachePath.c_str()) &&
            WritePrivateProfileStringW(L"Menu", L"Orientation", nullptr, cachePath.c_str());
    }

    void LoadMenuPreferences()
    {
        menuPositionX = 3;
        menuPositionY = 5;
        menuSize = 100;
        menuOpacity = 100;
        menuAudioEnabled = true;
        if (cachePath.empty())
        {
            RecordSavedMenuPreferences();
            return;
        }
        menuPositionX = ReadCacheNumber(L"PositionX", -1, 0, 100);
        const bool migratePosition = menuPositionX < 0;
        if (migratePosition)
        {
            wchar_t position[32] = {};
            GetPrivateProfileStringW(L"Menu", L"Orientation", L"", position, 32, cachePath.c_str());
            menuPositionX = Same(position, L"Center") ? 50 : Same(position, L"Right") ? 96 : Same(position, L"Left") ? 4 : 3;
        }
        menuPositionY = ReadCacheNumber(L"PositionY", 5, 0, 100);
        menuSize = ReadCacheNumber(L"Size", 100, 70, 130, 10);
        menuOpacity = ReadCacheNumber(L"Opacity", 100, 30, 100, 10);
        wchar_t audio[16] = {};
        GetPrivateProfileStringW(L"Menu", L"MenuAudio", L"true", audio, 16, cachePath.c_str());
        menuAudioEnabled = !Same(audio, L"false") && !Same(audio, L"0");
        RecordSavedMenuPreferences();
    }

    void UpdateMenuTransitions(ULONGLONG now)
    {
        if (menuTransitionActive)
        {
            const float t = (std::min)(1.0f,
                static_cast<float>(now - menuTransitionStartedAt) / 175.0f);
            const float eased = t * t * (3.0f - 2.0f * t);
            menuReveal = menuRevealStart + (menuRevealTarget - menuRevealStart) * eased;
            if (t >= 1.0f)
            {
                menuReveal = menuRevealTarget;
                menuTransitionActive = false;
            }
        }
    }

    void BeginMenuTransition(bool opening, ULONGLONG now)
    {
        UpdateMenuTransitions(now);
        menuRevealStart = menuReveal;
        menuRevealTarget = opening ? 1.0f : 0.0f;
        menuTransitionStartedAt = now;
        menuTransitionActive = menuRevealStart != menuRevealTarget;
    }

    float MenuScale()
    {
        return static_cast<float>(menuSize) / 100.0f;
    }

    float MenuX(float x)
    {
        const float scale = MenuScale();
        const float left = static_cast<float>(menuPositionX) / 100.0f * (1.0f - 0.312f * scale);
        return left + (x - 0.026f) * scale;
    }

    float MenuY(float y)
    {
        const float scale = MenuScale();
        const float top = static_cast<float>(menuPositionY) / 100.0f * (1.0f - 0.513f * scale);
        return top + (y - 0.100f + (1.0f - menuReveal) * 0.010f) * scale;
    }

    int MenuAlpha(int alpha)
    {
        return static_cast<int>((static_cast<float>(alpha * menuOpacity) * menuReveal + 50.0f) / 100.0f);
    }

    Entry MakeEntry(const std::wstring& key, const std::wstring& value)
    {
        Entry result;
        result.key = key;
        result.value = value;
        result.savedValue = value;
        if (Same(key, L"MenuKey"))
            result.type = ValueType::MenuKey;
        else if (Same(key, L"MenuModifier"))
            result.type = ValueType::Modifier;
        else if (Same(value, L"true") || Same(value, L"false"))
            result.type = ValueType::Boolean;
        else
        {
            float number = 0.0f;
            if (ParseNumber(value, number))
            {
                if (Same(key, L"SSROverscanScale"))
                {
                    result.type = ValueType::Number;
                    result.minValue = 1.0f;
                    result.maxValue = 2.0f;
                    result.increment = 0.05f;
                }
                else if (Same(key, L"SSROverscanTestMode"))
                {
                    result.type = ValueType::Number;
                    result.minValue = 0.0f;
                    result.maxValue = 10.0f;
                    result.increment = 1.0f;
                }
                else if (Same(key, L"CameraFOVOffset"))
                {
                    result.type = ValueType::Number;
                    result.minValue = -3.0f;
                    result.maxValue = 3.0f;
                    result.increment = 0.25f;
                }
                else if (Same(key, L"RecoilShakeAmplitudeScale") || Same(key, L"FirstPersonRecoilShakeAmplitudeScale"))
                {
                    result.type = ValueType::Number;
                    result.minValue = 0.0f;
                    result.maxValue = 1.0f;
                    result.increment = 0.05f;
                }
            }
        }
        return result;
    }

    bool IsExperimentalMenuOption(const std::wstring& key)
    {
        static constexpr const wchar_t* keys[] =
        {
            L"ExpandSSRCoverage", L"SSROverscanScale", L"SSROverscanTestMode",
            L"ReduceWeaponRecoil", L"RecoilShakeAmplitudeScale",
            L"ReduceFirstPersonWeaponRecoil", L"FirstPersonRecoilShakeAmplitudeScale"
        };
        for (const wchar_t* experimentalKey : keys)
            if (Same(key, experimentalKey))
                return true;
        return false;
    }

    const wchar_t* DefaultSettingValue(const std::wstring& section, const std::wstring& key)
    {
        struct SettingDefault { const wchar_t* section; const wchar_t* key; const wchar_t* value; };
        static constexpr SettingDefault defaults[] =
        {
            { L"General", L"Logging", L"true" },
            { L"General", L"DebugLogging", L"true" },
            { L"General", L"Notification", L"true" },
            { L"General", L"MenuKey", L"F8" },
            { L"General", L"MenuModifier", L"none" },
            { L"Startup", L"SkipIntroSplashes", L"false" },
            { L"Startup", L"SkipLegal", L"false" },
            { L"Startup", L"StraightToStoryMode", L"false" },
            { L"Engine", L"TextureCrashFix", L"true" },
            { L"Engine", L"ExtendHeapLimits", L"true" },
            { L"Engine", L"ExtendPackfileLimits", L"true" },
            { L"Engine", L"ExtendStreamingPools", L"true" },
            { L"Engine", L"ExtendAssetPools", L"true" },
            { L"Engine", L"OptimizeStreamingMemory", L"true" },
            { L"Engine", L"TextureStreamingStutterFix", L"true" },
            { L"Engine", L"ImproveTextureStreaming", L"true" },
            { L"Engine", L"ExtendPedMemoryLimits", L"true" },
            { L"Gameplay", L"DisableAutoAmmoPickup", L"true" },
            { L"Gameplay", L"DisableAutoHorseEquip", L"true" },
            { L"Gameplay", L"DisableAutoHorseHolster", L"true" },
            { L"Gameplay", L"DisableOutOfBoundSnipers", L"false" },
            { L"Gameplay", L"DisableHonorLossWhenMasked", L"true" },
            { L"Gameplay", L"DisableBodySearchHonorLoss", L"false" },
            { L"Gameplay", L"DisableAntagonizeHonorLoss", L"false" },
            { L"Gameplay", L"FixSelfDefenseHonorLoss", L"true" },
            { L"Gameplay", L"DisableHonorSystem", L"false" },
            { L"Gameplay", L"DisableBountyWhenMasked", L"true" },
            { L"Gameplay", L"FixSelfDefenseBounties", L"true" },
            { L"Gameplay", L"DisableBounties", L"false" },
            { L"Gameplay", L"FixHorseFollowBehavior", L"true" },
            { L"Gameplay", L"DisableHorseAutoJump", L"true" },
            { L"Gameplay", L"DisableHorseWhistleDistance", L"false" },
            { L"Player", L"DisableFirstPersonAutoRun", L"true" },
            { L"Player", L"QuickLootBodies", L"false" },
            { L"Player", L"QuickLootAnimals", L"false" },
            { L"Weapons", L"DisableEmptyAutoReload", L"false" },
            { L"Weapons", L"DisableAutoReload", L"true" },
            { L"Weapons", L"DisableDeadEyeAutoReload", L"true" },
            { L"Weapons", L"DisableForcedScope", L"true" },
            { L"Weapons", L"DisableHitmarkers", L"false" },
            { L"Weapons", L"DisableReticleHighlighting", L"false" },
            { L"Weapons", L"DisableArrowTrails", L"false" },
            { L"Weapons", L"DisableReticleSpread", L"false" },
            { L"Weapons", L"DisableReticleSway", L"false" },
            { L"Weapons", L"DisableWeaponDegradation", L"false" },
            { L"Visual", L"RemoveBlackBars", L"true" },
            { L"Visual", L"DisableVignette", L"false" },
            { L"Visual", L"DisableBloom", L"false" },
            { L"Visual", L"DisableChromaticAberration", L"false" },
            { L"Visual", L"DisableCompositeDithering", L"false" },
            { L"Visual", L"EnableDLSSAutoExposure", L"false" },
            { L"Visual", L"FixFullscreenTabOut", L"true" },
            { L"Visual", L"MinimizeOnTabOut", L"true" },
            { L"Visual", L"DisableIdleCamera", L"true" },
            { L"Visual", L"ReduceCameraShake", L"false" },
            { L"Visual", L"ReduceCameraSway", L"false" },
            { L"Visual", L"DisableHeadBob", L"false" },
            { L"Visual", L"CameraFOVOffset", L"0.0" },
            { L"Visual", L"DisableInteriorLighting", L"false" },
            { L"Visual", L"DisableDistantLights", L"false" },
            { L"Visual", L"DisableDrunkFilter", L"false" },
            { L"Visual", L"DisableEagleEyeFilter", L"false" },
            { L"Visual", L"DisableDeadEyeFilter", L"false" },
            { L"Visual", L"DisableWeaponWheelFilter", L"false" },
            { L"Visual", L"DisableLowHealthFilter", L"false" },
            { L"Ambience", L"DisableAmbientMusic", L"false" },
            { L"Ambience", L"DisableCombatMusic", L"false" },
            { L"Ambience", L"DisableWantedMusic", L"false" },
            { L"Diagnostics", L"CrashDiagnostics", L"false" },
            { L"Weapons", L"DisableEmptyClipAutoReload", L"false" },
        };
        for (const auto& item : defaults)
            if (Same(section, item.section) && Same(key, item.key))
                return item.value;
        return nullptr;
    }

    const char* SubcategoryName(const std::wstring& section, const std::wstring& key)
    {
        if (Same(section, L"Engine"))
            return "MEMORY";
        struct GroupSetting { const wchar_t* section; const wchar_t* key; const char* heading; };
        static constexpr GroupSetting groups[] =
        {
            { L"Gameplay", L"DisableAutoAmmoPickup", "GENERAL" },
            { L"Gameplay", L"DisableAutoHorseEquip", "GENERAL" },
            { L"Gameplay", L"DisableAutoHorseHolster", "GENERAL" },
            { L"Gameplay", L"DisableOutOfBoundSnipers", "GENERAL" },
            { L"Gameplay", L"DisableHonorLossWhenMasked", "HONOR" },
            { L"Gameplay", L"DisableBodySearchHonorLoss", "HONOR" },
            { L"Gameplay", L"DisableAntagonizeHonorLoss", "HONOR" },
            { L"Gameplay", L"DisableAntagonizehHonorLoss", "HONOR" },
            { L"Gameplay", L"FixSelfDefenseHonorLoss", "HONOR" },
            { L"Gameplay", L"DisableHonorSystem", "HONOR" },
            { L"Gameplay", L"DisableBountyWhenMasked", "BOUNTY" },
            { L"Gameplay", L"FixSelfDefenseBounties", "BOUNTY" },
            { L"Gameplay", L"DisableBounties", "BOUNTY" },
            { L"Gameplay", L"FixHorseFollowBehavior", "HORSE" },
            { L"Gameplay", L"DisableHorseAutoJump", "HORSE" },
            { L"Gameplay", L"DisableHorseWhistleDistance", "HORSE" },
            { L"Player", L"DisableFirstPersonAutoRun", "MOVEMENT" },
            { L"Player", L"QuickLootBodies", "LOOTING" },
            { L"Player", L"QuickLootAnimals", "LOOTING" },
            { L"Weapons", L"DisableEmptyAutoReload", "RELOADING" },
            { L"Weapons", L"DisableEmptyClipAutoReload", "RELOADING" },
            { L"Weapons", L"DisableAutoReload", "RELOADING" },
            { L"Weapons", L"DisableDeadEyeAutoReload", "RELOADING" },
            { L"Weapons", L"DisableForcedScope", "AIMING & EFFECTS" },
            { L"Weapons", L"DisableHitmarkers", "AIMING & EFFECTS" },
            { L"Weapons", L"DisableReticleHighlighting", "AIMING & EFFECTS" },
            { L"Weapons", L"DisableArrowTrails", "AIMING & EFFECTS" },
            { L"Weapons", L"DisableReticleSpread", "AIMING & EFFECTS" },
            { L"Weapons", L"DisableReticleSway", "AIMING & EFFECTS" },
            { L"Weapons", L"DisableWeaponDegradation", "MAINTENANCE" },
            { L"Visual", L"RemoveBlackBars", "DISPLAY" },
            { L"Visual", L"DisableVignette", "DISPLAY" },
            { L"Visual", L"DisableBloom", "DISPLAY" },
            { L"Visual", L"DisableChromaticAberration", "DISPLAY" },
            { L"Visual", L"DisableCompositeDithering", "DISPLAY" },
            { L"Visual", L"EnableDLSSAutoExposure", "DISPLAY" },
            { L"Visual", L"FixFullscreenTabOut", "DISPLAY" },
            { L"Visual", L"MinimizeOnTabOut", "DISPLAY" },
            { L"Visual", L"DisableIdleCamera", "CAMERA" },
            { L"Visual", L"ReduceCameraShake", "CAMERA" },
            { L"Visual", L"ReduceCameraSway", "CAMERA" },
            { L"Visual", L"DisableHeadBob", "CAMERA" },
            { L"Visual", L"CameraFOVOffset", "CAMERA" },
            { L"Visual", L"DisableInteriorLighting", "WORLD" },
            { L"Visual", L"DisableDistantLights", "WORLD" },
            { L"Visual", L"DisableDrunkFilter", "FILTERS" },
            { L"Visual", L"DisableEagleEyeFilter", "FILTERS" },
            { L"Visual", L"DisableDeadEyeFilter", "FILTERS" },
            { L"Visual", L"DisableWeaponWheelFilter", "FILTERS" },
            { L"Visual", L"DisableLowHealthFilter", "FILTERS" },
        };
        for (const auto& group : groups)
            if (Same(section, group.section) && Same(key, group.key))
                return group.heading;
        return nullptr;
    }

    void ReloadSections()
    {
        sections.clear();
        generalSection = Section{};
        generalSection.name = L"General";
        if (configPath.empty())
            return;
        std::vector<wchar_t> names(32768, L'\0');
        const DWORD nameLength = GetPrivateProfileSectionNamesW(names.data(), static_cast<DWORD>(names.size()), configPath.c_str());
        if (!nameLength || nameLength >= names.size() - 2)
            return;
        for (const wchar_t* name = names.data(); *name; name += std::wcslen(name) + 1)
        {
            if (Same(name, L"Experimental") || Same(name, L"Experimental Features"))
                continue;
            Section section;
            section.name = name;
            std::vector<wchar_t> data(65536, L'\0');
            const DWORD sectionLength = GetPrivateProfileSectionW(name, data.data(), static_cast<DWORD>(data.size()), configPath.c_str());
            if (sectionLength && sectionLength < data.size() - 2)
            {
                for (const wchar_t* line = data.data(); *line; line += std::wcslen(line) + 1)
                {
                    const std::wstring setting = line;
                    const size_t separator = setting.find(L'=');
                    if (separator == std::wstring::npos || separator == 0)
                        continue;
                    const std::wstring key = setting.substr(0, separator);
                    if (!IsExperimentalMenuOption(key) &&
                        (!Same(section.name, L"Visual") || DefaultSettingValue(section.name, key) != nullptr) &&
                        !(Same(section.name, L"General") && (Same(key, L"Enabled") || Same(key, L"SettingsMenu"))))
                        section.entries.push_back(MakeEntry(key, setting.substr(separator + 1)));
                }
            }
            if (Same(section.name, L"General"))
                generalSection = std::move(section);
            else if (!section.entries.empty())
                sections.push_back(std::move(section));
        }
        for (auto& section : sections)
            for (auto& entry : section.entries)
            {
                const auto found = pendingChanges.find({ section.name, entry.key });
                if (found != pendingChanges.end())
                    entry.value = found->second;
            }
        for (auto& entry : generalSection.entries)
        {
            const auto found = pendingChanges.find({ generalSection.name, entry.key });
            if (found != pendingChanges.end())
                entry.value = found->second;
        }
        for (auto& section : sections)
        {
            section.menuRows.reserve(section.entries.size() + 5);
            section.entryRows.reserve(section.entries.size());
            const char* previousHeading = nullptr;
            for (size_t index = 0; index < section.entries.size(); ++index)
            {
                const char* heading = SubcategoryName(section.name, section.entries[index].key);
                if (heading && (!previousHeading || std::strcmp(previousHeading, heading) != 0))
                    section.menuRows.push_back({ -1, heading });
                if (heading)
                    previousHeading = heading;
                section.entryRows.push_back(static_cast<int>(section.menuRows.size()));
                section.menuRows.push_back({ static_cast<int>(index), nullptr });
            }
            section.menuRows.push_back({ -2, nullptr });
        }
    }

    Section* SelectedSection()
    {
        if (page == Page::GeneralSettings)
            return &generalSection;
        return page == Page::IniEntries && currentSection >= 0 && currentSection < static_cast<int>(sections.size()) ?
            &sections[currentSection] : nullptr;
    }

    bool EngineReady()
    {
        nativeInit(isPauseMenuActive);
        const auto pause = nativeCall();
        if (!pause || static_cast<std::uint32_t>(*pause) != 0)
            return false;
        nativeInit(playerId);
        const auto player = nativeCall();
        if (!player)
            return false;
        const auto playerHandle = static_cast<std::uint32_t>(*player);
        nativeInit(isPlayerPlaying);
        nativePush64(playerHandle);
        const auto playing = nativeCall();
        if (!playing || static_cast<std::uint32_t>(*playing) == 0)
            return false;
        nativeInit(isLoadingScreenActive);
        const auto loading = nativeCall();
        if (!loading)
            return false;
        if (static_cast<std::uint32_t>(*loading) == 0)
            return true;
        nativeInit(playerPedId);
        const auto pedResult = nativeCall();
        if (!pedResult)
            return false;
        const auto ped = static_cast<std::uint32_t>(*pedResult);
        if (ped == 0)
            return false;
        nativeInit(doesEntityExist);
        nativePush64(ped);
        const auto exists = nativeCall();
        if (!exists || static_cast<std::uint32_t>(*exists) == 0)
            return false;
        nativeInit(isScreenFadedIn);
        const auto visible = nativeCall();
        const bool ready = visible && static_cast<std::uint32_t>(*visible) != 0;
        if (ready)
        {
            static bool fallbackLogged = false;
            if (!fallbackLogged)
            {
                Logger::Debug("Settings menu: loading state remains active during visible gameplay; using validated player state.");
                fallbackLogged = true;
            }
        }
        return ready;
    }

    void PlayMenuSound(const char* name, bool navigation = false)
    {
        if (!menuAudioEnabled)
            return;
        if (navigation)
        {
            const ULONGLONG now = GetTickCount64();
            if (lastNavigationSoundAt && now - lastNavigationSoundAt < 65)
                return;
            lastNavigationSoundAt = now;
        }
        static constexpr const char* soundset = "HUD_PLAYER_MENU";
        nativeInit(playSoundFrontend);
        nativePush64(reinterpret_cast<std::uint64_t>(name));
        nativePush64(reinterpret_cast<std::uint64_t>(soundset));
        nativePush64(1);
        nativePush64(0);
        nativeCall();
    }

    void NativeRect(float x, float y, float width, float height, int red, int green, int blue, int alpha)
    {
        nativeInit(drawRect);
        nativePush64(FloatArgument(MenuX(x)));
        nativePush64(FloatArgument(MenuY(y)));
        nativePush64(FloatArgument(width * MenuScale()));
        nativePush64(FloatArgument(height * MenuScale()));
        nativePush64(red);
        nativePush64(green);
        nativePush64(blue);
        nativePush64(MenuAlpha(alpha));
        nativePush64(1);
        nativePush64(0);
        nativeCall();
    }

    bool HasTextureDictionary(const char* dictionary)
    {
        nativeInit(hasStreamedTextureDictLoaded);
        nativePush64(reinterpret_cast<std::uint64_t>(dictionary));
        const auto result = nativeCall();
        return result && *result != 0;
    }

    void RequestTextureDictionary(const char* dictionary)
    {
        nativeInit(requestStreamedTextureDict);
        nativePush64(reinterpret_cast<std::uint64_t>(dictionary));
        nativePush64(0);
        nativeCall();
    }

    void NativeSprite(const char* dictionary, const char* texture, float x, float y, float width, float height, int red, int green, int blue, int alpha)
    {
        nativeInit(drawSprite);
        nativePush64(reinterpret_cast<std::uint64_t>(dictionary));
        nativePush64(reinterpret_cast<std::uint64_t>(texture));
        nativePush64(FloatArgument(MenuX(x)));
        nativePush64(FloatArgument(MenuY(y)));
        nativePush64(FloatArgument(width * MenuScale()));
        nativePush64(FloatArgument(height * MenuScale()));
        nativePush64(FloatArgument(0.0f));
        nativePush64(red);
        nativePush64(green);
        nativePush64(blue);
        nativePush64(MenuAlpha(alpha));
        nativePush64(1);
        nativeCall();
    }

    void DrawFormattedText(const std::string& text, float x, float y, bool titleFont)
    {
        const std::string formatted = std::string(titleFont ? "<FONT FACE='$title'>" : "<FONT FACE='$body'>") + text + "</FONT>";
        nativeInit(createVarString);
        nativePush64(10);
        nativePush64(reinterpret_cast<std::uint64_t>("LITERAL_STRING"));
        nativePush64(reinterpret_cast<std::uint64_t>(formatted.c_str()));
        const auto innerResult = nativeCall();
        std::uint64_t scriptText = 0;
        if (innerResult && *innerResult)
        {
            const std::uint64_t innerText = *innerResult;
            nativeInit(createVarString);
            nativePush64(42);
            nativePush64(reinterpret_cast<std::uint64_t>("COLOR_STRING"));
            nativePush64(0);
            nativePush64(innerText);
            const auto outerResult = nativeCall();
            if (outerResult)
                scriptText = *outerResult;
        }
        if (!scriptText)
        {
            nativeInit(createVarString);
            nativePush64(10);
            nativePush64(reinterpret_cast<std::uint64_t>("LITERAL_STRING"));
            nativePush64(reinterpret_cast<std::uint64_t>(text.c_str()));
            const auto fallbackResult = nativeCall();
            if (!fallbackResult || !*fallbackResult)
                return;
            scriptText = *fallbackResult;
        }
        nativeInit(bgDisplayText);
        nativePush64(scriptText);
        nativePush64(FloatArgument(x));
        nativePush64(FloatArgument(y));
        nativeCall();
    }

    void NativeText(const std::string& text, float x, float y, int size, int red, int green, int blue, bool titleFont = false)
    {
        const float scale = size >= 30 ? 0.44f : size >= 22 ? 0.32f : size >= 20 ? 0.30f : size >= 19 ? 0.28f : 0.26f;
        nativeInit(bgSetTextScale);
        nativePush64(FloatArgument(scale * MenuScale()));
        nativePush64(FloatArgument(scale * MenuScale()));
        nativeCall();
        nativeInit(bgSetTextColor);
        nativePush64(red);
        nativePush64(green);
        nativePush64(blue);
        nativePush64(MenuAlpha(255));
        nativeCall();
        DrawFormattedText(text, MenuX(x), MenuY(y), titleFont);
    }

    void ScreenRect(float x, float y, float width, float height, int red, int green, int blue, int alpha)
    {
        nativeInit(drawRect);
        nativePush64(FloatArgument(x));
        nativePush64(FloatArgument(y));
        nativePush64(FloatArgument(width));
        nativePush64(FloatArgument(height));
        nativePush64(red);
        nativePush64(green);
        nativePush64(blue);
        nativePush64(static_cast<std::uint64_t>(static_cast<float>(alpha) * menuReveal + 0.5f));
        nativePush64(1);
        nativePush64(0);
        nativeCall();
    }

    void ScreenText(const std::string& text, float x, float y, float scale, int red, int green, int blue, bool titleFont = false)
    {
        nativeInit(bgSetTextScale);
        nativePush64(FloatArgument(scale));
        nativePush64(FloatArgument(scale));
        nativeCall();
        nativeInit(bgSetTextColor);
        nativePush64(red);
        nativePush64(green);
        nativePush64(blue);
        nativePush64(static_cast<std::uint64_t>(255.0f * menuReveal + 0.5f));
        nativeCall();
        DrawFormattedText(text, x, y, titleFont);
    }

    std::string Abbreviate(const std::wstring& value, size_t maxLength)
    {
        std::string result = Narrow(value);
        if (result.size() > maxLength)
            result = result.substr(0, maxLength - 3) + "...";
        return result;
    }

    std::wstring SettingsLabel(const std::wstring& key)
    {
        std::wstring label;
        label.reserve(key.size() + 8);
        for (size_t index = 0; index < key.size(); ++index)
        {
            const wchar_t character = key[index];
            if (index > 0 && std::iswupper(character) &&
                (std::iswlower(key[index - 1]) || std::iswdigit(key[index - 1]) ||
                (std::iswupper(key[index - 1]) && index + 1 < key.size() && std::iswlower(key[index + 1]))))
                label += L' ';
            label += character;
        }
        return label;
    }

    std::wstring KeyName(DWORD key)
    {
        if (key >= VK_F1 && key <= VK_F24)
            return L"F" + std::to_wstring(key - VK_F1 + 1);
        if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9'))
            return std::wstring(1, static_cast<wchar_t>(key));
        switch (key)
        {
        case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL: return L"Ctrl";
        case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT: return L"Shift";
        case VK_MENU: case VK_LMENU: case VK_RMENU: return L"Alt";
        case VK_INSERT: return L"Insert";
        case VK_DELETE: return L"Delete";
        case VK_HOME: return L"Home";
        case VK_END: return L"End";
        case VK_PRIOR: return L"PageUp";
        case VK_NEXT: return L"PageDown";
        case VK_SPACE: return L"Space";
        case VK_TAB: return L"Tab";
        case VK_ESCAPE: return L"Escape";
        case VK_RETURN: return L"Enter";
        case VK_UP: return L"Up";
        case VK_DOWN: return L"Down";
        case VK_LEFT: return L"Left";
        case VK_RIGHT: return L"Right";
        default: return L"VK" + std::to_wstring(key);
        }
    }

    std::string DisplayValue(const Entry& entry)
    {
        if (entry.type == ValueType::Boolean)
            return Same(entry.value, L"true") ? "On" : "Off";
        return Abbreviate(entry.value, 17);
    }

    std::string HoverDescription()
    {
        if (page == Page::Root)
        {
            if (!actionStatus.empty() && selected >= 4)
                return actionStatus;
            switch (selected)
            {
            case 0: return "Adjust the menu position, size, and opacity.";
            case 1: return "Configure general options and menu hotkeys.";
            case 2: return "Browse the mod features and edit their INI settings.";
            case 3: return "Save your changes to the INI and menu cache.";
            case 4: return "Reload saved settings and supported fixes.";
            default: return {};
            }
        }
        if (page == Page::MenuSettings)
        {
            switch (selected)
            {
            case 0: return "Move the menu left or right. Save from the main menu.";
            case 1: return "Move the menu up or down. Save from the main menu.";
            case 2: return "Adjust the menu scale. Save from the main menu.";
            case 3: return "Adjust the menu transparency. Save from the main menu.";
            case 4: return "Enable or disable native menu sounds. Save to keep this preference.";
            case 5: return !actionStatus.empty() ? actionStatus :
                "Restore the menu position, size, opacity, and audio defaults. Save from the main menu.";
            default: return {};
            }
        }
        if (page == Page::IniSections)
        {
            if (selected < 0 || selected >= static_cast<int>(sections.size()))
                return {};
            const auto& name = sections[selected].name;
            if (Same(name, L"General")) return "General preferences, logging, and menu hotkeys.";
            if (Same(name, L"Engine")) return "Engine limits, memory, streaming, and rendering fixes.";
            if (Same(name, L"Visual")) return "Camera, lighting, and screen-effect settings.";
            if (Same(name, L"Player")) return "Player movement and interaction settings.";
            if (Same(name, L"Horse")) return "Horse behavior and riding options.";
            if (Same(name, L"Gameplay")) return "Gameplay behavior, Honor, and bounty settings.";
            if (Same(name, L"Weapons")) return "Weapon behavior, recoil, and reticle settings.";
            if (Same(name, L"Ambient") || Same(name, L"Ambience")) return "Ambient, combat, and wanted music options.";
            if (Same(name, L"Startup")) return "Startup and loading-screen options.";
            if (Same(name, L"Diagnostics")) return "Diagnostic options used to investigate problems.";
            if (Same(name, L"Debug")) return "Debugging and troubleshooting settings.";
            return "Open this category to configure its features.";
        }
        const Section* section = SelectedSection();
        if (!section)
            return {};
        const auto& options = section->entries;
        if (selected == static_cast<int>(options.size()))
            return !actionStatus.empty() ? actionStatus :
                "Restore this category's defaults. Save from the main menu to keep them.";
        if (selected < 0 || selected >= static_cast<int>(options.size()))
            return {};
        const auto& setting = options[selected];
        struct HelpText { const wchar_t* key; const char* description; };
        static constexpr HelpText help[] =
        {
            { L"Logging", "Writes general mod activity to the log file." },
            { L"DebugLogging", "Adds detailed debug information to the log." },
            { L"Notification", "Shows a native notification when Betterment Improvements initializes and warns about errors." },
            { L"SettingsMenu", "Enables the Betterment Improvements in-game configuration menu." },
            { L"MenuKey", "Choose the menu hotkey. Delete clears it; Backspace cancels." },
            { L"MenuModifier", "Choose a modifier key. Delete removes it; Backspace cancels." },
            { L"UpdateIntervalMs", "Sets the update interval for features that do not require per-frame work." },
            { L"Enabled", "Enables or disables the main mod features." },
            { L"TextureCrashFix", "Applies a fix for crashes involving streamed texture dictionaries." },
            { L"ExtendHeapLimits", "Raises engine heap limits to help prevent allocation failures." },
            { L"ExtendPackfileLimits", "Raises limits associated with loaded packfiles." },
            { L"ExtendPedMemoryLimits", "Expands memory limits used for pedestrian data." },
            { L"ExtendStreamingPools", "Expands streaming request pools to reduce limit exhaustion." },
            { L"OptimizeStreamingMemory", "Enables streaming-memory optimizations." },
            { L"ExtendAssetPools", "Raises asset pool limits to help avoid exhaustion." },
            { L"TextureStreamingStutterFix", "Applies a fix targeting texture-streaming stutter." },
            { L"ImproveTextureStreaming", "Improves how textures are requested and streamed." },
            { L"ExpandSSRCoverage", "Expands screen-space reflection coverage where supported." },
            { L"SSROverscanScale", "Adjusts the amount of extra screen-space reflection coverage." },
            { L"SSROverscanTestMode", "Selects a diagnostic mode for screen-space reflection overscan." },
            { L"SkipIntroSplashes", "Skips the introductory splash screens at startup." },
            { L"SkipLegal", "Skips the legal splash screen at startup." },
            { L"SkipIntroSplashesAndLegal", "Skips introductory splash and legal screens." },
            { L"StraightToStoryMode", "Attempts to continue directly into Story Mode on launch." },
            { L"FastStoryModeLoading", "Enables changes intended to shorten Story Mode loading." },
            { L"RemoveOnlineTab", "Hides the Online tab from the front end." },
            { L"RemoveBlackBars", "Removes cinematic black bars where supported." },
            { L"DisableVignette", "Disables vignette from weather and timecycle modifiers." },
            { L"DisableBloom", "Disables bloom from visual settings, weather, and timecycle modifiers." },
            { L"DisableChromaticAberration", "Disables color fringing from weather and timecycle modifiers." },
            { L"DisableCompositeDithering", "Disables blue-noise dithering in the final composite. This may make color banding visible." },
            { L"CameraFOVOffset", "Adjusts the camera field-of-view offset." },
            { L"EnableDLSSAutoExposure", "Enables automatic exposure handling for DLSS." },
            { L"DisableInteriorLighting", "Disables interior lighting effects." },
            { L"DisableDistantLights", "Disables distant light rendering." },
            { L"DisableDrunkFilter", "Removes the screen filter used while the player is drunk." },
            { L"DisableEagleEyeFilter", "Removes the Eagle Eye screen color filter." },
            { L"DisableDeadEyeFilter", "Removes the Dead Eye screen color filter." },
            { L"DisableWeaponWheelFilter", "Removes the filter shown when opening the weapon wheel." },
            { L"DisableLowHealthFilter", "Removes the low-health screen filter." },
            { L"DisableIdleCamera", "Prevents the automatic idle camera from activating." },
            { L"FixFullscreenTabOut", "Fixes fullscreen behavior when switching out of the game." },
            { L"MinimizeOnTabOut", "Minimizes the game when tabbing out." },
            { L"DisableHeadBob", "Removes camera bobbing from movement." },
            { L"ReduceCameraShake", "Reduces shake applied to the gameplay camera." },
            { L"ReduceCameraSway", "Reduces swaying motion applied to the camera." },
            { L"ReduceWeaponRecoil", "Reduces camera recoil from firing weapons." },
            { L"RecoilShakeAmplitudeScale", "Controls how strongly weapon recoil shakes the camera." },
            { L"ReduceFirstPersonWeaponRecoil", "Reduces camera recoil in first-person view." },
            { L"FirstPersonRecoilShakeAmplitudeScale", "Controls first-person recoil camera shake." },
            { L"DisableFirstPersonAutoRun", "Disables automatic running in first-person view." },
            { L"QuickLootBodies", "Shortens looting interactions with bodies." },
            { L"QuickLootAnimals", "Shortens looting interactions with animals." },
            { L"DisableAutoHorseEquip", "Prevents the game from automatically equipping horse weapons." },
            { L"DisableAutoHorseHolster", "Prevents automatic weapon holstering while riding." },
            { L"DisableAutoAmmoPickup", "Prevents automatically picking up nearby ammunition." },
            { L"DisableHorseAutoJump", "Disables automatic jumps when riding a horse." },
            { L"DisableHorseWhistleDistance", "Removes the usual distance restriction on horse whistles." },
            { L"FixHorseFollowBehavior", "Improves horse follow behavior." },
            { L"DisableHonorLossWhenMasked", "Prevents masked actions from lowering Honor." },
            { L"DisableBountyWhenMasked", "Prevents certain bounty penalties when masked." },
            { L"DisableBounties", "Disables the bounty system." },
            { L"FixSelfDefenseHonorLoss", "Prevents unfair Honor penalties for self-defense." },
            { L"FixSelfDefenseBounties", "Prevents unfair bounty penalties for self-defense." },
            { L"DisableHonorSystem", "Disables the Honor system." },
            { L"DisableBodySearchHonorLoss", "Prevents Honor loss when searching bodies." },
            { L"DisableAntagonizeHonorLoss", "Prevents Honor loss from antagonizing characters." },
            { L"DisableOutOfBoundSnipers", "Disables snipers used to enforce restricted map areas." },
            { L"DisableAutoReload", "Stops weapons from automatically reloading." },
            { L"DisableDeadEyeAutoReload", "Stops Dead Eye from automatically reloading weapons." },
            { L"DisableEmptyAutoReload", "Stops automatic reloads when ammunition runs out." },
            { L"DisableEmptyClipAutoReload", "Stops automatic reloads when a weapon is empty." },
            { L"DisableArrowTrails", "Removes visible projectile trails from arrows." },
            { L"DisableReticleSpread", "Removes reticle expansion from weapon handling." },
            { L"DisableReticleSway", "Disables reticle sway." },
            { L"DisableForcedScope", "Prevents forced scope view for supported weapons." },
            { L"DisableWeaponDegradation", "Prevents weapons from degrading through use." },
            { L"DisableHitmarkers", "Hides the hitmarker feedback when shots connect." },
            { L"DisableReticleHighlighting", "Stops the reticle from highlighting targets." },
            { L"ProbeWeaponSignatures", "Enables diagnostic probing of weapon-related signatures." },
            { L"DisableAmbientMusic", "Turns off ambient background music." },
            { L"DisableCombatMusic", "Turns off music that plays during combat." },
            { L"DisableWantedMusic", "Turns off music played during wanted situations." },
            { L"CrashDiagnostics", "Enables extra diagnostic output for troubleshooting crashes." },
            { L"DumpNativeHashes", "Outputs native-hash data for debugging." }
        };
        for (const auto& item : help)
            if (Same(setting.key, item.key))
                return item.description;
        return setting.type == ValueType::Text ?
            "This option is displayed as text and cannot be edited here." :
            "Changes apply immediately when supported. Save from the main menu to keep them.";
    }

    bool RequiresGameRestart(const Entry& entry)
    {
        const Section* section = SelectedSection();
        return Main::ModReload::RequiresGameRestart(section ? section->name.c_str() : L"", entry.key.c_str());
    }

    const std::vector<std::string>& DescriptionLines(const std::string& description)
    {
        constexpr size_t maxLine = 43;
        static std::string cachedDescription;
        static std::vector<std::string> lines;
        if (cachedDescription == description && !lines.empty())
            return lines;
        cachedDescription = description;
        lines.clear();
        size_t start = 0;
        while (start < description.size())
        {
            size_t end = (std::min)(start + maxLine, description.size());
            if (end < description.size())
            {
                const size_t split = description.rfind(' ', end);
                if (split != std::string::npos && split > start)
                    end = split;
            }
            lines.emplace_back(description.substr(start, end - start));
            start = end;
            while (start < description.size() && description[start] == ' ')
                ++start;
        }
        if (lines.empty())
            lines.emplace_back();
        return lines;
    }

    void DrawMenu()
    {
        nativeInit(setScriptGfxDrawOrder);
        nativePush64(3);
        nativeCall();

        constexpr float centreX = 0.182f;
        constexpr float leftX = 0.044f;
        constexpr float rightX = 0.320f;
        constexpr float labelX = leftX + 0.002f;
        constexpr float contentRightX = rightX - 0.008f;
        constexpr float innerWidth = 0.276f;
        constexpr float firstY = 0.231f;
        constexpr float rowStep = 0.040f;
        constexpr float rowHeight = 0.035f;
        constexpr float actionGap = 0.013f;
        constexpr float resetGap = 0.010f;
        constexpr int visibleRows = 7;

        const bool genericLoaded = HasTextureDictionary("generic_textures");
        if (!genericLoaded)
            RequestTextureDictionary("generic_textures");
        const bool menuLoaded = HasTextureDictionary("menu_textures");
        if (!menuLoaded)
            RequestTextureDictionary("menu_textures");

        const Section* section = SelectedSection();
        const bool folders = page == Page::IniSections;
        const bool grouped = page == Page::IniEntries && section && !section->menuRows.empty();
        const int count = page == Page::Root ? 5 : page == Page::MenuSettings ? 6 :
            page == Page::IniSections ? static_cast<int>(sections.size()) :
            section ? static_cast<int>(grouped ? section->menuRows.size() : section->entries.size() + 1) : 0;
        const int selectedRow = grouped && selected >= 0 ?
            (selected < static_cast<int>(section->entryRows.size()) ? section->entryRows[selected] :
                static_cast<int>(section->menuRows.size()) - 1) : selected;
        const int shownRows = (std::max)(2, (std::min)(visibleRows, count));
        const float listBottomY = firstY + static_cast<float>(shownRows) * rowStep +
            (page == Page::Root ? actionGap : 0.0f) + 0.018f;
        const auto& description = DescriptionLines(HoverDescription());
        const bool saved = page == Page::Root && selected == 3 && actionStatus == "Settings saved.";
        const bool reloadSelected = page == Page::Root && selected == 4;
        const bool reloadSuccess = reloadSelected && actionStatus == "Runtime settings reloaded from INI.";
        const bool restart = section && page != Page::IniSections &&
            selected >= 0 && selected < static_cast<int>(section->entries.size()) &&
            RequiresGameRestart(section->entries[selected]);
        const bool restartPending = restartPendingFromDisk || !originalRestartValues.empty();
        const bool restartWarning = restartPending || restart;
        const int footerLines = static_cast<int>(description.size()) + (restartWarning ? 1 : 0);
        const bool unsaved = !pendingChanges.empty() || menuPreferencesDirty;
        const float unsavedY = 0.058f + static_cast<float>((std::max)(0, footerLines - 2)) * 0.023f;
        const float footerEndY = 0.014f + static_cast<float>(footerLines - 1) * 0.023f;
        const float bottomY = listBottomY + (std::max)(0.077f, unsaved ? unsavedY + 0.019f : footerEndY + 0.040f);
        constexpr float topY = 0.100f;

        if (genericLoaded)
        {
            NativeSprite("generic_textures", "inkroller_1a", centreX, (topY + bottomY) * 0.5f,
                0.344f, bottomY - topY + 0.026f, 0, 0, 0, 245);
        }
        else
            NativeRect(centreX, (topY + bottomY) * 0.5f, 0.304f, bottomY - topY - 0.018f, 0, 0, 0, 240);

        NativeText("BETTERMENT IMPROVEMENTS", 0.084f, 0.119f, 30, 250, 241, 221, true);
        NativeText(Main::NotifStats::Version, 0.167f, 0.151f, 18, 223, 211, 192);

        const std::string subtitle = page == Page::Root ? "MAIN MENU" :
            page == Page::MenuSettings ? "MENU SETTINGS" :
            page == Page::GeneralSettings ? "GENERAL SETTINGS" :
            page == Page::IniSections ? "FEATURE SETTINGS" :
            section ? Abbreviate(section->name, 26) : "FEATURE SETTINGS";
        constexpr float categoryHeadingY = 0.1934f;
        NativeText(subtitle, labelX, categoryHeadingY, 19, 238, 226, 205, true);
        if (count > 0)
        {
            const bool restoring = (page == Page::MenuSettings && selected == 5) ||
                ((page == Page::GeneralSettings || page == Page::IniEntries) && section &&
                    selected == static_cast<int>(section->entries.size()));
            const std::string counter = restoring ? "DEFAULTS" : std::to_string(selected + 1) + " / " +
                std::to_string(grouped ? section->entries.size() :
                    page == Page::GeneralSettings || page == Page::MenuSettings ? count - 1 : count);
            const float counterWidth = static_cast<float>(counter.size()) * 0.0036f;
            NativeText(counter, contentRightX - counterWidth, categoryHeadingY,
                17, 206, 187, 158);
        }

        static int firstVisibleRow = 0;
        static Page previousPage = Page::Root;
        static int previousSection = -1;
        if (page != previousPage || currentSection != previousSection)
            firstVisibleRow = 0;
        previousPage = page;
        previousSection = currentSection;
        const int maxFirstRow = (std::max)(0, count - visibleRows);
        firstVisibleRow = (std::max)(0, (std::min)(firstVisibleRow, maxFirstRow));
        if (selectedRow < firstVisibleRow)
            firstVisibleRow = selectedRow;
        else if (selectedRow >= firstVisibleRow + visibleRows)
            firstVisibleRow = (std::min)(maxFirstRow, selectedRow - visibleRows + 1);
        if (grouped && selected == 0)
            firstVisibleRow = 0;
        const int first = firstVisibleRow;
        const int last = (std::min)(count, first + visibleRows);
        if (!count)
            NativeText("No configuration entries found", labelX, 0.254f, 18, 219, 208, 194);

        for (int index = first; index < last; ++index)
        {
            const bool saveAction = page == Page::Root && index == 3;
            const bool reloadAction = page == Page::Root && index == 4;
            const int entryIndex = grouped ? section->menuRows[index].entryIndex : index;
            const bool headingRow = grouped && entryIndex == -1;
            const bool resetRow = (page == Page::MenuSettings && index == 5) ||
                ((page == Page::GeneralSettings || page == Page::IniEntries) && section &&
                    (grouped ? entryIndex == -2 : index == static_cast<int>(section->entries.size())));
            const float y = firstY + static_cast<float>(index - first) * rowStep +
                (page == Page::Root && index >= 3 ? actionGap : resetRow ? resetGap : 0.0f);
            const float rowCentre = y + rowHeight * 0.5f;
            const float rowTextY = rowCentre - 0.013f;
            const bool active = !headingRow && (resetRow ?
                (page == Page::MenuSettings ? selected == 5 : selected == static_cast<int>(section->entries.size())) :
                selected == entryIndex);

            if (headingRow)
            {
                NativeText(section->menuRows[index].heading, labelX + 0.005f, rowTextY, 19, 238, 226, 205);
                NativeRect(centreX, y + rowHeight - 0.001f, innerWidth - 0.012f, 0.001f, 206, 187, 158, 175);
                continue;
            }

            if (reloadAction || resetRow)
                NativeRect(centreX, rowCentre, innerWidth, rowHeight,
                    active ? 107 : 69, active ? 35 : 28, active ? 32 : 27, active ? 244 : 215);
            else
                NativeRect(centreX, rowCentre, innerWidth, rowHeight,
                    active ? 38 : 17, active ? 35 : 17, active ? 32 : 16, active ? 250 : 235);

            if (genericLoaded)
            {
                if (reloadAction || resetRow)
                    NativeSprite("generic_textures", "selection_box_bg_1c", centreX, rowCentre,
                        innerWidth, rowHeight, 140, 140, 140, active ? 96 : 54);
                else
                    NativeSprite("generic_textures", "selection_box_bg_1c", centreX, rowCentre,
                        innerWidth, rowHeight, active ? 155 : 137, active ? 142 : 124,
                        active ? 129 : 113, active ? 62 : 30);
            }

            if (saveAction || resetRow)
                NativeRect(centreX, y - (resetRow ? 0.005f : 0.007f), innerWidth, 0.0013f, 116, 116, 116, 155);
            if (active)
            {
                NativeRect(centreX, y - 0.0003f, innerWidth + 0.004f, 0.0038f, 48, 15, 13, 215);
                NativeRect(centreX, y + rowHeight + 0.0003f, innerWidth + 0.004f, 0.0038f, 48, 15, 13, 215);
                NativeRect(leftX - 0.0003f, rowCentre, 0.0038f, rowHeight + 0.005f, 48, 15, 13, 215);
                NativeRect(rightX + 0.0003f, rowCentre, 0.0038f, rowHeight + 0.005f, 48, 15, 13, 215);

                NativeRect(centreX, y, innerWidth + 0.001f, 0.0022f, 211, 42, 34, 255);
                NativeRect(centreX, y + rowHeight, innerWidth + 0.001f, 0.0022f, 211, 42, 34, 255);
                NativeRect(leftX, rowCentre, 0.0022f, rowHeight + 0.001f, 211, 42, 34, 255);
                NativeRect(rightX, rowCentre, 0.0022f, rowHeight + 0.001f, 211, 42, 34, 255);
            }

            std::wstring label;
            if (page == Page::Root)
                label = index == 0 ? L"Menu Settings" : index == 1 ? L"General Settings" :
                    index == 2 ? L"Feature Settings" : index == 3 ? L"Save Settings to .ini" : L"Reload Mod";
            else if (resetRow)
                label = L"Restore Defaults";
            else if (page == Page::MenuSettings)
                label = index == 0 ? L"Position X" : index == 1 ? L"Position Y" :
                    index == 2 ? L"Size" : index == 3 ? L"Opacity" : L"Menu Audio";
            else if (page == Page::IniSections)
                label = sections[index].name;
            else if (section)
                label = SettingsLabel(section->entries[entryIndex].key);

            const std::string rowLabel = Narrow(label);
            const int labelSize = rowLabel.size() > 34 ? 18 : 20;
            NativeText(rowLabel, labelX, rowTextY, labelSize,
                active ? 250 : reloadAction || resetRow ? 249 : 229,
                active ? 243 : reloadAction || resetRow ? 208 : 217,
                active ? 226 : reloadAction || resetRow ? 197 : 199);

            if (folders || (page == Page::Root && index < 3))
            {
                if (menuLoaded)
                    NativeSprite("menu_textures", "selection_arrow_right", contentRightX - 0.005f, rowCentre,
                        0.010f, 0.016f, active ? 248 : 230, active ? 233 : 220, active ? 204 : 201, 245);
                else
                    NativeText(">", contentRightX - 0.007f, rowTextY + 0.001f, 18, active ? 248 : 237,
                        active ? 233 : 220, active ? 204 : 196);
            }
            else if (page != Page::Root && !resetRow)
            {
                if ((page == Page::MenuSettings && index == 4) ||
                    (section && section->entries[entryIndex].type == ValueType::Boolean))
                {
                    if (genericLoaded)
                    {
                        NativeSprite("generic_textures", "tick_box", contentRightX - 0.0085f, rowCentre,
                            0.017f, 0.030f, active ? 238 : 218, active ? 223 : 205,
                            active ? 196 : 184, 245);
                        if (page == Page::MenuSettings ? menuAudioEnabled : Same(section->entries[entryIndex].value, L"true"))
                            NativeSprite("generic_textures", "tick", contentRightX - 0.0085f, rowCentre,
                                0.017f, 0.030f, 235, 36, 31, 255);
                    }
                    else
                        NativeRect(contentRightX - 0.0075f, rowCentre, 0.015f, 0.027f, 62, 56, 50, 235);
                }
                else
                {
                    std::string value;
                    if (page == Page::MenuSettings)
                        value = std::to_string(index == 0 ? menuPositionX : index == 1 ? menuPositionY :
                            index == 2 ? menuSize : menuOpacity) + "%";
                    else if (section)
                        value = DisplayValue(section->entries[entryIndex]);
                    const float valueWidth = (std::min)(0.088f, static_cast<float>(value.size()) * 0.0046f);
                    const float valueX = contentRightX - 0.002f - valueWidth;
                    NativeText(value, valueX, rowTextY, 20,
                        active ? 250 : 233, active ? 228 : 216, active ? 198 : 184);
                }
            }
        }

        NativeRect(centreX, listBottomY + 0.001f, innerWidth, 0.0018f, 184, 156, 117, 205);
        if (genericLoaded)
            NativeSprite("generic_textures", "menu_bar", centreX, listBottomY + 0.001f,
                innerWidth, 0.004f, 211, 193, 163, 185);
        for (size_t index = 0; index < description.size(); ++index)
            NativeText(description[index], labelX, listBottomY + 0.014f + static_cast<float>(index) * 0.023f, 19,
                saved || reloadSuccess ? 133 : 243, saved || reloadSuccess ? 207 : 232, saved || reloadSuccess ? 139 : 215);
        const float nextLineY = listBottomY + 0.014f + static_cast<float>(description.size()) * 0.023f;
        if (restartWarning)
            NativeText(restartPending ? "Some Changes Require Restart" : "Requires Game Restart",
                labelX, nextLineY, 19, 226, 64, 58);
        if (unsaved)
            NativeText("UNSAVED CHANGES", labelX, listBottomY + unsavedY, 17, 226, 64, 58);

        if (count > visibleRows)
        {
            const float trackHeight = rowStep * visibleRows - 0.007f;
            const float thumbHeight = trackHeight * static_cast<float>(visibleRows) / static_cast<float>(count);
            const float position = static_cast<float>(first) / static_cast<float>(count - visibleRows);
            NativeRect(0.328f, firstY + trackHeight * 0.5f, 0.002f, trackHeight, 85, 85, 85, 215);
            NativeRect(0.328f, firstY + thumbHeight * 0.5f + position * (trackHeight - thumbHeight),
                0.003f, thumbHeight, 250, 241, 221, 207);
        }
    }

    void DrawKeySelector()
    {
        nativeInit(setScriptGfxDrawOrder);
        nativePush64(3);
        nativeCall();

        ScreenRect(0.158f, 0.103f, 0.280f, 0.116f, 0, 0, 0, 179);
        ScreenRect(0.019f, 0.103f, 0.001f, 0.116f, 66, 62, 56, 179);
        ScreenRect(0.297f, 0.103f, 0.001f, 0.116f, 66, 62, 56, 179);
        ScreenText("SELECT ANY KEY", 0.033f, 0.061f, 0.27f, 239, 236, 226, true);
        ScreenText(captureMode.load(std::memory_order_acquire) == 1 ? "Menu Key" : "Menu Modifier", 0.033f, 0.096f, 0.22f, 226, 220, 211);
        ScreenText("Delete: None   Backspace: Cancel", 0.033f, 0.132f, 0.19f, 213, 207, 198);
    }

    void StageEntryValue(Section& section, Entry& entry, const std::wstring& next);

    bool SaveCapturedBinding(int mode, int key)
    {
        if (page != Page::GeneralSettings || selected < 0 || selected >= static_cast<int>(generalSection.entries.size()))
            return false;
        Entry& entry = generalSection.entries[selected];
        if ((mode == 1 && entry.type != ValueType::MenuKey) ||
            (mode == 2 && entry.type != ValueType::Modifier))
            return false;
        const std::wstring value = key == -2 ? L"none" : KeyName(static_cast<DWORD>(key));
        StageEntryValue(generalSection, entry, value);
        Logger::Log("[INFO] Settings menu: hotkey updated.");
        return true;
    }

    bool ChangeMenuPreference(int direction)
    {
        int* value = selected == 0 ? &menuPositionX : selected == 1 ? &menuPositionY :
            selected == 2 ? &menuSize : selected == 3 ? &menuOpacity : nullptr;
        if (!value)
            return false;
        const int minimum = selected <= 1 ? 0 : selected == 2 ? 70 : 30;
        const int maximum = selected <= 1 ? 100 : selected == 2 ? 130 : 100;
        const int increment = selected <= 1 ? 1 : 10;
        const int next = (std::max)(minimum, (std::min)(maximum, *value + direction * increment));
        if (next == *value)
            return false;
        *value = next;
        RefreshMenuPreferencesDirty();
        actionStatus.clear();
        return true;
    }

    bool MatchesEntryValue(const Entry& entry, const std::wstring& left, const std::wstring& right)
    {
        if (entry.type == ValueType::Number)
        {
            float leftNumber = 0.0f;
            float rightNumber = 0.0f;
            return ParseNumber(left, leftNumber) && ParseNumber(right, rightNumber) &&
                std::fabs(leftNumber - rightNumber) < 0.0001f;
        }
        if (entry.type == ValueType::Boolean || entry.type == ValueType::MenuKey || entry.type == ValueType::Modifier)
            return Same(left, right.c_str());
        return left == right;
    }

    void StageEntryValue(Section& section, Entry& entry, const std::wstring& next)
    {
        if (MatchesEntryValue(entry, next, entry.value))
            return;
        const auto key = std::make_pair(section.name, entry.key);
        if (Main::ModReload::RequiresGameRestart(section.name.c_str(), entry.key.c_str()))
        {
            const auto original = originalRestartValues.find(key);
            if (original == originalRestartValues.end())
                originalRestartValues.emplace(key, entry.value);
            else if (MatchesEntryValue(entry, next, original->second) &&
                MatchesEntryValue(entry, entry.savedValue, original->second))
                originalRestartValues.erase(original);
        }
        const bool matchesSaved = MatchesEntryValue(entry, next, entry.savedValue);
        entry.value = matchesSaved ? entry.savedValue : next;
        if (matchesSaved)
            pendingChanges.erase(key);
        else
            pendingChanges[key] = next;
        GameplayFixes::ApplyMenuSetting(section.name.c_str(), entry.key.c_str(), next);
        actionStatus.clear();
    }

    void RestoreSectionDefaults()
    {
        Section* section = SelectedSection();
        if (!section)
            return;
        for (auto& entry : section->entries)
        {
            const wchar_t* defaultValue = DefaultSettingValue(section->name, entry.key);
            if (defaultValue)
                StageEntryValue(*section, entry, defaultValue);
        }
        actionStatus = "Category defaults selected. Save settings to keep them.";
    }

    void RestoreMenuDefaults()
    {
        menuPositionX = 3;
        menuPositionY = 5;
        menuSize = 100;
        menuOpacity = 100;
        menuAudioEnabled = true;
        RefreshMenuPreferencesDirty();
        actionStatus = "Menu defaults selected. Save settings to keep them.";
    }

    bool ChangeEntry(int direction)
    {
        Section* section = SelectedSection();
        if (!section)
            return false;
        auto& entries = section->entries;
        if (selected < 0 || selected >= static_cast<int>(entries.size()))
            return false;
        Entry& entry = entries[selected];
        std::wstring next = entry.value;
        switch (entry.type)
        {
        case ValueType::Boolean:
            next = Same(entry.value, L"true") ? L"false" : L"true";
            break;
        case ValueType::Number:
        {
            float value = 0.0f;
            if (!ParseNumber(entry.value, value))
                return false;
            value = (std::max)(entry.minValue, (std::min)(entry.maxValue, value + direction * entry.increment));
            if (entry.increment >= 1.0f)
                next = std::to_wstring(static_cast<int>(std::round(value)));
            else
            {
                wchar_t number[32] = {};
                swprintf_s(number, L"%.2f", static_cast<double>(value));
                next = number;
            }
            break;
        }
        default:
            return false;
        }
        if (MatchesEntryValue(entry, next, entry.value))
            return false;
        StageEntryValue(*section, entry, next);
        return true;
    }

    bool SaveAllSettings()
    {
        if (configPath.empty())
            return false;
        bool success = true;
        for (auto it = pendingChanges.begin(); it != pendingChanges.end();)
        {
            if (WritePrivateProfileStringW(it->first.first.c_str(), it->first.second.c_str(),
                it->second.c_str(), configPath.c_str()))
            {
                Section* target = Same(it->first.first, L"General") ? &generalSection : nullptr;
                if (!target)
                    for (auto& section : sections)
                        if (Same(section.name, it->first.first.c_str()))
                        {
                            target = &section;
                            break;
                        }
                if (target)
                    for (auto& entry : target->entries)
                        if (Same(entry.key, it->first.second.c_str()))
                        {
                            entry.savedValue = it->second;
                            const auto original = originalRestartValues.find(it->first);
                            if (original != originalRestartValues.end() &&
                                MatchesEntryValue(entry, entry.savedValue, original->second))
                                originalRestartValues.erase(original);
                            break;
                        }
                it = pendingChanges.erase(it);
            }
            else
                success = false, ++it;
        }
        if (menuPreferencesDirty)
        {
            if (SaveMenuPreferences())
                RecordSavedMenuPreferences();
            else
                success = false;
        }
        Logger::Log(success ? "[SUCCESS] Settings menu: saved." :
            "[ERROR] Settings menu: some options could not be saved.");
        return success;
    }
}

namespace Main::ConfigMenu
{
    void Initialize(const std::wstring& iniPath, const GameplayFixes::Settings& settings)
    {
        configPath = iniPath;
        cachePath = CacheFilePath(iniPath);
        if (!menuPreferencesDirty)
            LoadMenuPreferences();
        if (!settings.settingsMenu)
        {
            menuOpen.store(false, std::memory_order_release);
            menuReveal = 0.0f;
            menuTransitionActive = false;
        }
        SetHotkey(settings.menuKey, settings.menuModifier);
        configured.store(settings.settingsMenu && !configPath.empty(), std::memory_order_release);
    }

    void SetHotkey(int key, unsigned int modifier)
    {
        menuKey.store(key, std::memory_order_release);
        menuModifier.store(modifier, std::memory_order_release);
    }

    void Shutdown()
    {
        configured.store(false, std::memory_order_release);
        menuOpen.store(false, std::memory_order_release);
        pendingInput.store(0, std::memory_order_release);
        capturedKey.store(0, std::memory_order_release);
        captureMode.store(0, std::memory_order_release);
        pendingChanges.clear();
        originalRestartValues.clear();
        restartPendingFromDisk = false;
        menuPreferencesDirty = false;
        lastNavigationSoundAt = 0;
        menuReveal = 0.0f;
        menuRevealStart = 0.0f;
        menuRevealTarget = 0.0f;
        menuTransitionStartedAt = 0;
        menuTransitionActive = false;
        actionStatus.clear();
    }

    bool IsOpen()
    {
        return menuOpen.load(std::memory_order_acquire) || menuTransitionActive;
    }

    void OnKeyboard(DWORD key, WORD, BYTE scanCode, BOOL isExtended, BOOL, BOOL wasDownBefore, BOOL isUpNow)
    {
        if (!configured.load(std::memory_order_acquire) || isUpNow || (isExtended && key == VK_RETURN))
            return;

        const bool keypadSelect = !isExtended && (key == VK_NUMPAD5 || (key == VK_CLEAR && scanCode == 0x4C));
        if (!keypadSelect && (GetKeyState(VK_NUMLOCK) & 1) == 0 &&
            ((!isExtended && (scanCode == 0x37 || (scanCode >= 0x47 && scanCode <= 0x53))) ||
                (isExtended && scanCode == 0x35)))
            return;
        if (keypadSelect)
            key = VK_NUMPAD5;
        if (captureMode.load(std::memory_order_acquire) != 0)
        {
            if (!wasDownBefore && key > 0 && key < 256)
                capturedKey.store(key == VK_BACK || key == VK_NUMPAD0 ? -1 :
                    key == VK_DELETE ? -2 : static_cast<int>(key), std::memory_order_release);
            return;
        }
        if (menuKey.load(std::memory_order_acquire) != 0 &&
            key == static_cast<DWORD>(menuKey.load(std::memory_order_acquire)) && !wasDownBefore)
        {
            const unsigned modifiers = menuModifier.load(std::memory_order_acquire);
            const bool control = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
            const bool alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
            if ((modifiers & 0x10000U) ?
                ((GetAsyncKeyState(static_cast<int>(modifiers & 0xFFU)) & 0x8000) != 0) :
                (!((modifiers & 1U) != 0) || control) &&
                (!((modifiers & 2U) != 0) || shift) &&
                (!((modifiers & 4U) != 0) || alt))
            {
                pendingInput.fetch_or(Toggle, std::memory_order_release);
                Logger::Debug("Settings menu: toggle key received.");
            }
            return;
        }
        if (!menuOpen.load(std::memory_order_acquire))
            return;
        unsigned action = 0;
        if (key == VK_UP || key == VK_NUMPAD8)
            action = Up;
        else if (key == VK_DOWN || key == VK_NUMPAD2)
            action = Down;
        else if (key == VK_LEFT || key == VK_NUMPAD4)
            action = Left;
        else if (key == VK_RIGHT || key == VK_NUMPAD6)
            action = Right;
        else if ((key == VK_RETURN || keypadSelect) && !wasDownBefore)
            action = Select;
        else if ((key == VK_BACK || key == VK_ESCAPE || key == VK_NUMPAD0) && !wasDownBefore)
            action = Back;
        if (action)
            pendingInput.fetch_or(action, std::memory_order_release);
    }

    void Update()
    {
        if (!configured.load(std::memory_order_acquire))
            return;
        const unsigned actions = pendingInput.exchange(0, std::memory_order_acq_rel);
        bool open = menuOpen.load(std::memory_order_acquire);
        if (!open && !(actions & Toggle) && !menuTransitionActive)
            return;
        const ULONGLONG now = GetTickCount64();
        UpdateMenuTransitions(now);
        if (!EngineReady())
        {
            if (actions & Toggle)
                Logger::Debug("Settings menu: toggle blocked; game is loading, paused or player is not ready.");
            if (open)
                menuOpen.store(false, std::memory_order_release);
            menuReveal = 0.0f;
            menuTransitionActive = false;
            captureMode.store(0, std::memory_order_release);
            capturedKey.store(0, std::memory_order_release);
            return;
        }
        const int bindingMode = captureMode.load(std::memory_order_acquire);
        if (bindingMode != 0)
        {
            const int key = capturedKey.exchange(0, std::memory_order_acq_rel);
            if (key != 0)
            {
                captureMode.store(0, std::memory_order_release);
                if (key == -1)
                    PlayMenuSound("BACK");
                else if (SaveCapturedBinding(bindingMode, key))
                    PlayMenuSound("SELECT");
            }
            if (captureMode.load(std::memory_order_acquire) != 0)
                DrawKeySelector();
            else
                DrawMenu();
            return;
        }
        if (actions & Toggle)
        {
            open = !open;
            menuOpen.store(open, std::memory_order_release);
            BeginMenuTransition(open, now);
            PlayMenuSound(open ? "MENU_ENTER" : "MENU_CLOSE");
            if (open)
            {
                Main::NotifStats::Dismiss();
                ReloadSections();
                page = Page::Root;
                currentSection = -1;
                selected = 0;
                actionStatus.clear();
                capturedKey.store(0, std::memory_order_release);
                captureMode.store(0, std::memory_order_release);
            }
        }
        if (!open)
        {
            if (menuReveal > 0.0f)
                DrawMenu();
            return;
        }
        if (actions & Back)
        {
            if (page == Page::Root)
            {
                PlayMenuSound("MENU_CLOSE");
                menuOpen.store(false, std::memory_order_release);
                BeginMenuTransition(false, now);
                DrawMenu();
                return;
            }
            PlayMenuSound("BACK");
            if (page == Page::IniEntries)
            {
                page = Page::IniSections;
                selected = currentSection;
            }
            else
            {
                const Page previous = page;
                page = Page::Root;
                selected = previous == Page::MenuSettings ? 0 : previous == Page::GeneralSettings ? 1 : 2;
            }
        }
        else
        {
            Section* section = SelectedSection();
            const int count = page == Page::Root ? 5 : page == Page::MenuSettings ? 6 :
                page == Page::IniSections ? static_cast<int>(sections.size()) :
                section ? static_cast<int>(section->entries.size()) + 1 : 0;
            if (count > 0)
            {
                const int previousSelected = selected;
                if (actions & Up)
                    selected = (selected + count - 1) % count;
                else if (actions & Down)
                    selected = (selected + 1) % count;
                if (selected != previousSelected)
                {
                    PlayMenuSound((actions & Up) ? "NAV_UP" : "NAV_DOWN", true);
                    if (page == Page::Root)
                        actionStatus.clear();
                }
                if (page == Page::Root && (actions & Select))
                {
                    PlayMenuSound("SELECT");
                    if (selected < 3)
                    {
                        page = selected == 0 ? Page::MenuSettings : selected == 1 ? Page::GeneralSettings : Page::IniSections;
                        selected = 0;
                        actionStatus.clear();
                    }
                    else if (selected == 3)
                        actionStatus = SaveAllSettings() ? "Settings saved." : "Unable to save some settings. Check log.";
                    else
                    {
                        bool restartNeeded = false;
                        const bool reloaded = GameplayFixes::Reload(&restartNeeded, &pendingChanges);
                        if (reloaded)
                        {
                            restartPendingFromDisk = restartNeeded;
                            ReloadSections();
                        }
                        actionStatus = !reloaded ? "Reload failed. Check log." : restartNeeded ?
                            "Runtime reloaded. Restart RDR2 for startup fixes." : "Runtime settings reloaded from INI.";
                    }
                }
                else if (page == Page::IniSections && (actions & Select))
                {
                    PlayMenuSound("SELECT");
                    currentSection = selected;
                    page = Page::IniEntries;
                    selected = 0;
                    actionStatus.clear();
                }
                else if (page == Page::MenuSettings && (actions & Select) && selected == 5)
                {
                    RestoreMenuDefaults();
                    PlayMenuSound("SELECT");
                }
                else if (page == Page::MenuSettings && selected == 4 && (actions & (Select | Left | Right)))
                {
                    if (menuAudioEnabled)
                        PlayMenuSound("SELECT");
                    menuAudioEnabled = !menuAudioEnabled;
                    RefreshMenuPreferencesDirty();
                    actionStatus.clear();
                    if (menuAudioEnabled)
                        PlayMenuSound("SELECT");
                }
                else if (page == Page::MenuSettings && selected < 4 && (actions & (Select | Left | Right)))
                {
                    if (ChangeMenuPreference((actions & Left) ? -1 : 1))
                        PlayMenuSound((actions & Left) ? "NAV_LEFT" : "NAV_RIGHT", true);
                }
                else if ((page == Page::GeneralSettings || page == Page::IniEntries) &&
                    (actions & (Select | Left | Right)) && section)
                {
                    if (selected == static_cast<int>(section->entries.size()))
                    {
                        if (actions & Select)
                        {
                            RestoreSectionDefaults();
                            PlayMenuSound("SELECT");
                        }
                    }
                    else
                    {
                        Entry& entry = section->entries[selected];
                        if (entry.type == ValueType::MenuKey || entry.type == ValueType::Modifier)
                        {
                            if (actions & Select)
                            {
                                capturedKey.store(0, std::memory_order_release);
                                captureMode.store(entry.type == ValueType::MenuKey ? 1 : 2, std::memory_order_release);
                                PlayMenuSound("SELECT");
                            }
                        }
                        else if (ChangeEntry((actions & Left) ? -1 : 1))
                            PlayMenuSound((actions & Select) ? "SELECT" : (actions & Left) ? "NAV_LEFT" : "NAV_RIGHT", !(actions & Select));
                    }
                }
            }
        }
        if (captureMode.load(std::memory_order_acquire) != 0)
            DrawKeySelector();
        else
            DrawMenu();
    }
}
