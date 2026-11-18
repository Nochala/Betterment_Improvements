#include "Camera.h"
#include "../../Headers/Logger.h"
#include "main.h"
#include <windows.h>
#include <shlobj.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <string>

#pragma comment(lib, "shell32.lib")

namespace
{
    constexpr std::uint64_t animpostfxIsRunning = 0x4A123E85D7C4CA0BULL;
    constexpr std::uint64_t animpostfxSetStrength = 0xCAB4DD2D5B2B7246ULL;
    constexpr const char* drunkFilters[] = {
        "PlayerDrunk01",
        "PlayerDrunkSaloon1",
        "PlayerDrunkAberdeen"
    };
    constexpr const char* eagleEyeFilter = "EagleEye";
    constexpr const char* deadEyeFilter = "DEADEYE";
    constexpr const char* pauseMenuFilter = "PauseMenuIn";
    constexpr const char* weaponWheelFilter = "WheelHUDIn";
    constexpr const char* lowHealthFilters[] = {
        "PlayerHealthLow",
        "PlayerHealthPoor"
    };
    constexpr std::uint64_t isPlayerControlOn = 0x7964097FCE4C244BULL;
    constexpr std::uint64_t isGameplayCamRendering = 0x8660EA714834E412ULL;
    constexpr std::uint64_t isFirstPersonAimCamActive = 0xF63134C54B6EC212ULL;
    constexpr std::uint64_t isInFullFirstPersonMode = 0xD1BA66940E94C547ULL;
    constexpr std::uint64_t playerPedId = 0x096275889B8E0EE0ULL;
    constexpr std::uint64_t isPedShooting = 0x34616828CD07F1A1ULL;
    constexpr std::uint64_t isPedArmed = 0xCB690F680A3EA971ULL;
    constexpr std::uint64_t isPedOnFoot = 0x01FEE67DB37F59B2ULL;
    constexpr std::uint64_t isPedOnMount = 0x460BC76A0E10655EULL;
    constexpr std::uint64_t getMount = 0xE7E11B8DCBED1058ULL;
    constexpr std::uint64_t getEntitySpeed = 0xFB6BA510A533DF81ULL;
    constexpr std::uint64_t setGameplayCamShakeAmplitude = 0x570E35F5C4A44838ULL;
    constexpr std::uint64_t isGameplayCamShaking = 0xEA4C5F4AA0A4DBEFULL;
    constexpr std::uint64_t stopGameplayCamShaking = 0xE0DE43D290FB65F9ULL;
    constexpr std::uint64_t isGameplayCamShakingWithName = 0x0060B31968E60E41ULL;
    constexpr std::uint64_t stopGameplayCamShakingWithName = 0x4285804FD65D8066ULL;
    constexpr std::uint64_t isInCinematicMode = 0x74F1D22EFA71FAB8ULL;
    constexpr std::uint64_t isCinematicCamRendering = 0xBF7C780731AADBF8ULL;
    constexpr std::uint64_t isControlPressed = 0xF3A21BCD95725A4AULL;
    constexpr std::uint64_t disableAllControlActions = 0x5F4B6931816E599BULL;
    constexpr std::uint64_t disableCinematicModeThisFrame = 0x8910C24B7E0046ECULL;
    constexpr std::uint64_t invalidateCinematicVehicleIdleMode = 0x634F4A0562CF19B8ULL;
    constexpr std::uint32_t aimControl = 0xF84FA74F;
    constexpr std::uint32_t horseAimControl = 0x61470051;
    constexpr std::uint32_t cinematicCameraControl = 0x620A6C5E;
    constexpr std::uint32_t cinematicCameraHoldControl = 0xD7E7B375;
    constexpr ULONGLONG vehicleRefreshMs = 1000;
    constexpr ULONGLONG manualCameraAllowanceMs = 3000;
    constexpr ULONGLONG recoilReleaseMs = 350;
    constexpr ULONGLONG firstPersonRecoilReleaseMs = 350;
    constexpr const char* firstPersonRecoilShakes[] = {
        "DEFAULT_FIRST_PERSON_RECOIL_SHAKE",
        "FPS_PISTOL_RECOIL_SHAKE",
        "FPS_SHOTGUN_RECOIL_SHAKE",
        "FPS_ASSAULT_RIFLE_RECOIL_SHAKE"
    };
    constexpr const char* thirdPersonRecoilShakes[] = {
        "default_third_person_recoil_shake",
        "PISTOL_RECOIL_SHAKE",
        "SHOTGUN_RECOIL_SHAKE",
        "ASSAULT_RIFLE_RECOIL_SHAKE",
        "MG_RECOIL_SHAKE"
    };

    bool enabled = false;
    bool drunkFilterDisabled = false;
    bool eagleEyeFilterDisabled = false;
    bool deadEyeFilterDisabled = false;
    bool pauseMenuFilterDisabled = false;
    bool weaponWheelFilterDisabled = false;
    bool lowHealthFilterDisabled = false;
    bool drunkFilterStatusReported = false;
    bool eagleEyeFilterStatusReported = false;
    bool deadEyeFilterStatusReported = false;
    bool pauseMenuFilterStatusReported = false;
    bool weaponWheelFilterStatusReported = false;
    bool lowHealthFilterStatusReported = false;
    bool headBobReductionEnabled = false;
    bool headBobReductionApplied = false;
    bool headBobStatusReported = false;
    bool shakeReductionEnabled = false;
    bool swayReductionEnabled = false;
    bool recoilReductionEnabled = false;
    bool firstPersonRecoilReductionEnabled = false;
    bool recoilReductionApplied = false;
    bool firstPersonRecoilStatusReported = false;
    bool thirdPersonRecoilStatusReported = false;
    bool firstPersonNamedShakeDetected = false;
    bool thirdPersonNamedShakeDetected = false;
    float recoilShakeAmplitudeScale = 0.5f;
    float firstPersonRecoilShakeAmplitudeScale = 0.5f;
    ULONGLONG recoilReleaseUntil = 0;
    bool shakeReductionApplied = false;
    bool swayStopped = false;
    bool swayStatusReported = false;
    ULONGLONG swayObservationStarted = 0;
    ULONGLONG nextVehicleRefresh = 0;
    ULONGLONG manualCameraAllowanceUntil = 0;
    bool gameplayCameraBeforeFocusLoss = false;
    bool backgroundIdleStatusReported = false;
    std::atomic<bool> fullscreenTabOutEnabled{ false };
    std::atomic<bool> minimizeOnTabOutEnabled{ false };
    bool minimizeGameFocused = false;
    HWND minimizeGameWindow = nullptr;
    HANDLE focusMonitorStop = nullptr;
    bool focusMonitorStarted = false;
    bool fullscreenModeSelected = false;
    bool fullscreenModeKnown = false;
    bool fullscreenFocusInitialized = false;
    std::atomic<bool> fullscreenGameFocused{ false };
    std::atomic<bool> fullscreenRestorePending{ false };
    bool fullscreenBackgroundInputReported = false;
    ULONGLONG fullscreenFocusReturnedAt = 0;
    ULONGLONG fullscreenFocusLostAt = 0;
    ULONGLONG fullscreenLastRestoreInputAt = 0;
    ULONGLONG fullscreenModeCheckedAt = 0;
    std::wstring fullscreenSettingsPath;

    bool ReadFullscreenDisplayMode(bool& fullscreen)
    {
        if (fullscreenSettingsPath.empty())
            return false;

        HANDLE file = CreateFileW(fullscreenSettingsPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return false;

        LARGE_INTEGER length = {};
        const bool validLength = GetFileSizeEx(file, &length) && length.QuadPart > 0 && length.QuadPart <= 262144;
        if (!validLength)
        {
            CloseHandle(file);
            return false;
        }

        std::string content(static_cast<std::size_t>(length.QuadPart), '\0');
        DWORD bytesRead = 0;
        const bool read = ReadFile(file, content.data(), static_cast<DWORD>(content.size()), &bytesRead, nullptr) != 0;
        CloseHandle(file);
        if (!read || bytesRead == 0)
            return false;
        content.resize(bytesRead);

        const std::size_t video = content.find("<video>");
        if (video == std::string::npos)
            return false;
        const std::size_t endVideo = content.find("</video>", video);
        if (endVideo == std::string::npos)
            return false;
        const std::size_t setting = content.find("<windowed", video);
        if (setting == std::string::npos || setting >= endVideo)
            return false;
        const std::size_t endSetting = content.find('>', setting);
        if (endSetting == std::string::npos || endSetting > endVideo)
            return false;
        const std::size_t attribute = content.find("value=\"", setting);
        if (attribute == std::string::npos || attribute >= endSetting)
            return false;
        const std::size_t value = attribute + sizeof("value=\"") - 1;
        if (value + 1 >= endSetting || content[value + 1] != '"')
            return false;

        const char mode = content[value];
        if (mode != '0' && mode != '1' && mode != '2' && mode != '3')
            return false;
        fullscreen = mode == '0';
        return true;
    }

    bool ScriptHookV2InterfaceActive()
    {
        HMODULE scriptHook = GetModuleHandleW(L"ScriptHookRDR2.dll");
        if (!scriptHook)
            return false;

        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(scriptHook);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE ||
            dos->e_lfanew < static_cast<LONG>(sizeof(IMAGE_DOS_HEADER)) || dos->e_lfanew > 4096)
            return false;

        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
            reinterpret_cast<const std::uint8_t*>(scriptHook) + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE ||
            nt->OptionalHeader.SizeOfImage != 0x48A000 ||
            nt->FileHeader.TimeDateStamp != 0x669669F6)
            return false;

        wchar_t modulePath[MAX_PATH] = {};
        const DWORD length = GetModuleFileNameW(scriptHook, modulePath, MAX_PATH);
        if (!length || length >= MAX_PATH)
            return false;

        std::wstring configPath(modulePath, length);
        const std::size_t slash = configPath.find_last_of(L"\\/");
        if (slash == std::wstring::npos)
            return false;
        configPath.resize(slash + 1);
        configPath += L"ScriptHookConfig.ini";

        const DWORD attributes = GetFileAttributesW(configPath.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY))
            return false;

        constexpr const wchar_t* sections[] = { L"Interface", L"Overlay" };
        for (const wchar_t* section : sections)
        {
            wchar_t value[16] = {};
            GetPrivateProfileStringW(section, L"Enable", L"", value, 16, configPath.c_str());
            if (CompareStringOrdinal(value, -1, L"true", -1, TRUE) == CSTR_EQUAL ||
                std::wcscmp(value, L"1") == 0)
                return true;
        }
        return false;
    }

    void FixFullscreenTabOut()
    {
        if (!fullscreenTabOutEnabled)
            return;

        HWND foreground = GetForegroundWindow();
        DWORD process = 0;
        if (foreground)
            GetWindowThreadProcessId(foreground, &process);
        const bool focused = process == GetCurrentProcessId();
        const ULONGLONG now = GetTickCount64();

        if (!fullscreenFocusInitialized)
        {
            fullscreenFocusInitialized = true;
            fullscreenGameFocused = focused;
        }

        if (!focused)
        {
            if (fullscreenGameFocused)
            {
                fullscreenFocusLostAt = now;
                fullscreenRestorePending = fullscreenModeKnown && fullscreenModeSelected &&
                    (fullscreenLastRestoreInputAt == 0 || now - fullscreenLastRestoreInputAt >= 2500);
                fullscreenFocusReturnedAt = 0;
                if (fullscreenRestorePending && Logger::IsDebugEnabled())
                    Logger::Log("[DEBUG] FixFullscreenTabOut: focus lost; fullscreen restoration queued.");
            }
            fullscreenGameFocused = false;
            return;
        }

        HWND window = GetAncestor(foreground, GA_ROOT);
        if (!window || !IsWindow(window))
            window = foreground;

        if (!fullscreenGameFocused)
        {
            fullscreenGameFocused = true;
            fullscreenFocusReturnedAt = now;
            if (fullscreenFocusLostAt == 0 || now - fullscreenFocusLostAt < 250)
                fullscreenRestorePending = false;
        }

        if (fullscreenRestorePending && fullscreenFocusReturnedAt != 0 &&
            now - fullscreenFocusReturnedAt >= 1200 && !IsIconic(window) &&
            (GetAsyncKeyState(VK_MENU) & 0x8000) == 0 &&
            (GetAsyncKeyState(VK_TAB) & 0x8000) == 0 &&
            (GetAsyncKeyState(VK_RETURN) & 0x8000) == 0)
        {
            fullscreenRestorePending = false;
            if (fullscreenModeKnown && fullscreenModeSelected &&
                (fullscreenLastRestoreInputAt == 0 || now - fullscreenLastRestoreInputAt >= 2500))
            {
                fullscreenLastRestoreInputAt = now;
                INPUT keys[4] = {};
                for (INPUT& key : keys)
                    key.type = INPUT_KEYBOARD;
                keys[0].ki.wVk = VK_MENU;
                keys[1].ki.wVk = VK_RETURN;
                keys[2].ki.wVk = VK_RETURN;
                keys[2].ki.dwFlags = KEYEVENTF_KEYUP;
                keys[3].ki.wVk = VK_MENU;
                keys[3].ki.dwFlags = KEYEVENTF_KEYUP;
                if (SendInput(4, keys, sizeof(INPUT)) == 4)
                    Logger::Log("[INFO] FixFullscreenTabOut: fullscreen restore requested after focus return.");
                else
                    Logger::Log("[WARNING] FixFullscreenTabOut: fullscreen restore input failed.");
            }
            fullscreenModeCheckedAt = now;
            return;
        }

        if (!fullscreenRestorePending && now - fullscreenModeCheckedAt >= 3000)
        {
            bool selected = false;
            if (ReadFullscreenDisplayMode(selected))
            {
                fullscreenModeSelected = selected;
                fullscreenModeKnown = true;
            }
            else
                fullscreenModeKnown = false;
            fullscreenModeCheckedAt = now;
        }
    }

    void MinimizeOnTabOut()
    {
        if (!minimizeOnTabOutEnabled)
            return;
        if (fullscreenTabOutEnabled && fullscreenLastRestoreInputAt != 0 &&
            GetTickCount64() - fullscreenLastRestoreInputAt < 2500)
            return;

        HWND foreground = GetForegroundWindow();
        if (!foreground)
            return;

        DWORD process = 0;
        GetWindowThreadProcessId(foreground, &process);
        if (process == 0)
            return;

        if (process == GetCurrentProcessId())
        {
            HWND root = GetAncestor(foreground, GA_ROOT);
            if (root && IsWindow(root) && IsWindowVisible(root))
            {
                minimizeGameWindow = root;
                minimizeGameFocused = true;
            }
            return;
        }

        if (!minimizeGameFocused)
            return;

        minimizeGameFocused = false;
        if (minimizeGameWindow && IsWindow(minimizeGameWindow) && !IsIconic(minimizeGameWindow))
        {
            if (ShowWindowAsync(minimizeGameWindow, SW_MINIMIZE))
            {
                if (Logger::IsDebugEnabled())
                    Logger::Log("[DEBUG] MinimizeOnTabOut: minimize requested after focus loss.");
            }
            else
                Logger::Log("[WARNING] MinimizeOnTabOut: unable to request window minimization.");
        }
    }

    void ResetFullscreenTabOut()
    {
        fullscreenFocusInitialized = false;
        fullscreenGameFocused = false;
        fullscreenRestorePending = false;
        fullscreenFocusReturnedAt = 0;
        fullscreenFocusLostAt = 0;
        fullscreenModeCheckedAt = GetTickCount64();
        fullscreenSettingsPath.clear();
        fullscreenModeKnown = false;
        fullscreenModeSelected = false;
        if (!fullscreenTabOutEnabled)
            return;

        wchar_t documents[MAX_PATH] = {};
        if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, documents)))
        {
            fullscreenSettingsPath = documents;
            fullscreenSettingsPath += L"\\Rockstar Games\\Red Dead Redemption 2\\Settings\\system.xml";
        }
        fullscreenModeKnown = ReadFullscreenDisplayMode(fullscreenModeSelected);
        Logger::Log("[INFO] FixFullscreenTabOut: focus monitor enabled.");
        if (Logger::IsDebugEnabled())
            Logger::Log(!fullscreenModeKnown ?
                "[DEBUG] FixFullscreenTabOut: system.xml display mode unavailable; restore disabled until detected." :
                fullscreenModeSelected ?
                "[DEBUG] FixFullscreenTabOut: fullscreen mode selected in system.xml." :
                "[DEBUG] FixFullscreenTabOut: system.xml selects windowed or borderless mode.");
    }

    DWORD WINAPI WindowFocusMonitor(LPVOID context)
    {
        HANDLE stop = static_cast<HANDLE>(context);
        bool lastFullscreen = false;
        bool lastMinimize = false;
        do
        {
            const bool fullscreen = fullscreenTabOutEnabled.load();
            const bool minimize = minimizeOnTabOutEnabled.load();
            if (fullscreen != lastFullscreen)
            {
                ResetFullscreenTabOut();
                lastFullscreen = fullscreen;
            }
            if (minimize != lastMinimize)
            {
                minimizeGameFocused = false;
                minimizeGameWindow = nullptr;
                lastMinimize = minimize;
                if (minimize)
                    Logger::Log("[INFO] MinimizeOnTabOut: enabled.");
            }
            if (fullscreen)
                FixFullscreenTabOut();
            if (minimize)
                MinimizeOnTabOut();
        } while (WaitForSingleObject(stop, 50) == WAIT_TIMEOUT);
        CloseHandle(stop);
        return 0;
    }

    bool InvokeBoolean(std::uint64_t hash)
    {
        nativeInit(hash);
        return *nativeCall() != 0;
    }

    bool SuppressPostFx(const char* effect)
    {
        nativeInit(animpostfxIsRunning);
        nativePush64(reinterpret_cast<std::uint64_t>(effect));
        if (*nativeCall() == 0)
            return false;

        nativeInit(animpostfxSetStrength);
        nativePush64(reinterpret_cast<std::uint64_t>(effect));
        nativePush64(0);
        nativeCall();
        return true;
    }

    bool ControlPressed(std::uint32_t control)
    {
        nativeInit(isControlPressed);
        nativePush64(0);
        nativePush64(control);
        return *nativeCall() != 0;
    }

    void Invoke(std::uint64_t hash)
    {
        nativeInit(hash);
        nativeCall();
    }

    bool PedShooting(std::uint64_t ped)
    {
        nativeInit(isPedShooting);
        nativePush64(ped);
        return *nativeCall() != 0;
    }

    bool PedArmed(std::uint64_t ped)
    {
        nativeInit(isPedArmed);
        nativePush64(ped);
        nativePush64(4);
        return *nativeCall() != 0;
    }

    bool PedMovingOnFootOrMount(std::uint64_t ped)
    {
        if (ped == 0)
            return false;

        nativeInit(isPedOnMount);
        nativePush64(ped);
        const bool mounted = *nativeCall() != 0;
        std::uint64_t entity = ped;
        if (mounted)
        {
            nativeInit(getMount);
            nativePush64(ped);
            const std::uint64_t mount = *nativeCall();
            if (mount != 0)
                entity = mount;
        }
        else
        {
            nativeInit(isPedOnFoot);
            nativePush64(ped);
            if (*nativeCall() == 0)
                return false;
        }

        nativeInit(getEntitySpeed);
        nativePush64(entity);
        const std::uint32_t bits = static_cast<std::uint32_t>(*nativeCall());
        float speed = 0.0f;
        std::memcpy(&speed, &bits, sizeof(speed));
        return speed > 0.35f;
    }

    void SuppressRecoilShakes(bool firstPerson)
    {
        const char* const* names = firstPerson ? firstPersonRecoilShakes : thirdPersonRecoilShakes;
        const unsigned int count = firstPerson ?
            sizeof(firstPersonRecoilShakes) / sizeof(firstPersonRecoilShakes[0]) :
            sizeof(thirdPersonRecoilShakes) / sizeof(thirdPersonRecoilShakes[0]);
        bool& detected = firstPerson ? firstPersonNamedShakeDetected : thirdPersonNamedShakeDetected;
        for (unsigned int i = 0; i < count; ++i)
        {
            const char* name = names[i];
            if (!detected && Logger::IsDebugEnabled())
            {
                nativeInit(isGameplayCamShakingWithName);
                nativePush64(reinterpret_cast<std::uint64_t>(name));
                if (*nativeCall() != 0)
                {
                    detected = true;
                    Logger::Log(firstPerson ?
                        "[DEBUG] ReduceFirstPersonWeaponRecoil: active named first-person recoil shake detected." :
                        "[DEBUG] ReduceWeaponRecoil: active named third-person recoil shake detected.");
                }
            }
            nativeInit(stopGameplayCamShakingWithName);
            nativePush64(reinterpret_cast<std::uint64_t>(name));
            nativePush64(1);
            nativeCall();
        }
    }

    void SetShakeAmplitude(float amplitude)
    {
        std::uint32_t bits = 0;
        static_assert(sizeof(bits) == sizeof(amplitude));
        std::memcpy(&bits, &amplitude, sizeof(bits));
        nativeInit(setGameplayCamShakeAmplitude);
        nativePush64(bits);
        nativeCall();
    }

    void ClearRecoilOverride()
    {
        if (recoilReductionApplied)
        {
            SetShakeAmplitude(1.0f);
            recoilReductionApplied = false;
        }
        recoilReleaseUntil = 0;
    }
}

namespace Main::Camera
{
    void InitializeWindowFocus(bool fixFullscreenTabOut, bool minimizeOnTabOut)
    {
        const bool v2InterfaceActive = (fixFullscreenTabOut || minimizeOnTabOut) && ScriptHookV2InterfaceActive();
        const bool effectiveFullscreenTabOut = fixFullscreenTabOut && !v2InterfaceActive;
        const bool effectiveMinimizeOnTabOut = minimizeOnTabOut && !v2InterfaceActive;
        fullscreenTabOutEnabled.store(effectiveFullscreenTabOut);
        minimizeOnTabOutEnabled.store(effectiveMinimizeOnTabOut);
        if (v2InterfaceActive)
            Logger::Log("[WARNING] Tab-out compatibility: ScriptHook V2 interface/overlay is active; BettermentImprovements automatic minimize and fullscreen restore suppressed to avoid Vulkan resize conflicts.");
        if ((!effectiveFullscreenTabOut && !effectiveMinimizeOnTabOut) || focusMonitorStarted)
            return;

        HANDLE stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (stop)
        {
            HANDLE thread = CreateThread(nullptr, 0, WindowFocusMonitor, stop, 0, nullptr);
            if (thread)
            {
                focusMonitorStop = stop;
                focusMonitorStarted = true;
                CloseHandle(thread);
                return;
            }
            CloseHandle(stop);
        }
        Logger::Log("[WARNING] Tab-out monitor: could not start; using ScriptMain fallback.");
    }

    void Initialize(bool active, bool fixFullscreenTabOut, bool minimizeOnTabOut, bool disableHeadBob, bool reduceCameraShake, bool reduceCameraSway,
        bool disableDrunkFilter, bool disableEagleEyeFilter, bool disableDeadEyeFilter,
        bool disablePauseMenuFilter, bool disableWeaponWheelFilter, bool disableLowHealthFilter,
        bool reduceWeaponRecoil, float amplitudeScale,
        bool reduceFirstPersonWeaponRecoil, float firstPersonAmplitudeScale)
    {
        enabled = active;
        drunkFilterDisabled = disableDrunkFilter;
        eagleEyeFilterDisabled = disableEagleEyeFilter;
        deadEyeFilterDisabled = disableDeadEyeFilter;
        pauseMenuFilterDisabled = disablePauseMenuFilter;
        weaponWheelFilterDisabled = disableWeaponWheelFilter;
        lowHealthFilterDisabled = disableLowHealthFilter;
        drunkFilterStatusReported = false;
        eagleEyeFilterStatusReported = false;
        deadEyeFilterStatusReported = false;
        pauseMenuFilterStatusReported = false;
        weaponWheelFilterStatusReported = false;
        lowHealthFilterStatusReported = false;
        headBobReductionEnabled = disableHeadBob && !reduceCameraShake;
        headBobReductionApplied = false;
        headBobStatusReported = false;
        shakeReductionEnabled = reduceCameraShake;
        swayReductionEnabled = reduceCameraSway;
        recoilReductionEnabled = reduceWeaponRecoil;
        firstPersonRecoilReductionEnabled = reduceFirstPersonWeaponRecoil;
        recoilShakeAmplitudeScale = amplitudeScale;
        firstPersonRecoilShakeAmplitudeScale = firstPersonAmplitudeScale;
        recoilReductionApplied = false;
        firstPersonRecoilStatusReported = false;
        thirdPersonRecoilStatusReported = false;
        firstPersonNamedShakeDetected = false;
        thirdPersonNamedShakeDetected = false;
        recoilReleaseUntil = 0;
        shakeReductionApplied = false;
        swayStopped = false;
        swayStatusReported = false;
        swayObservationStarted = 0;
        nextVehicleRefresh = 0;
        manualCameraAllowanceUntil = 0;
        gameplayCameraBeforeFocusLoss = false;
        backgroundIdleStatusReported = false;
        InitializeWindowFocus(fixFullscreenTabOut, minimizeOnTabOut);
        if (enabled)
            Logger::Log("[INFO] DisableIdleCamera: enabled.");
        if (drunkFilterDisabled)
            Logger::Log("[INFO] DisableDrunkFilter: drunk screen effect disabled.");
        if (eagleEyeFilterDisabled)
            Logger::Log("[INFO] DisableEagleEyeFilter: Eagle Eye screen effect disabled.");
        if (deadEyeFilterDisabled)
            Logger::Log("[INFO] DisableDeadEyeFilter: Dead Eye screen effect disabled.");
        if (pauseMenuFilterDisabled)
            Logger::Log("[INFO] DisablePauseMenuFilter: pause menu screen effect disabled.");
        if (weaponWheelFilterDisabled)
            Logger::Log("[INFO] DisableWeaponWheelFilter: weapon wheel screen effect disabled.");
        if (lowHealthFilterDisabled)
            Logger::Log("[INFO] DisableLowHealthFilter: low-health screen effect disabled.");
        if (headBobReductionEnabled)
            Logger::Log("[INFO] DisableHeadBob: movement shake reduction enabled (on foot and mounted).");
        else if (disableHeadBob && reduceCameraShake)
            Logger::Log("[INFO] DisableHeadBob: covered by ReduceCameraShake.");
        if (shakeReductionEnabled)
            Logger::Log("[INFO] ReduceCameraShake: enabled.");
        if (swayReductionEnabled)
            Logger::Log("[INFO] ReduceCameraSway: enabled.");
        if (recoilReductionEnabled)
            Logger::Log((std::string("[INFO] ReduceWeaponRecoil: third-person recoil shake scale=") +
                std::to_string(recoilShakeAmplitudeScale) + ".").c_str());
        if (firstPersonRecoilReductionEnabled)
            Logger::Log((std::string("[INFO] ReduceFirstPersonWeaponRecoil: first-person recoil shake scale=") +
                std::to_string(firstPersonRecoilShakeAmplitudeScale) + ".").c_str());
    }

    bool NeedsFastUpdate()
    {
        return fullscreenTabOutEnabled && fullscreenRestorePending && !fullscreenGameFocused;
    }

    void Shutdown()
    {
        if (focusMonitorStop)
            SetEvent(focusMonitorStop);
    }

    void Update()
    {
        if (!focusMonitorStarted)
        {
            FixFullscreenTabOut();
            MinimizeOnTabOut();
        }
        if (fullscreenTabOutEnabled && fullscreenRestorePending && !fullscreenGameFocused)
        {
            nativeInit(disableAllControlActions);
            nativePush64(0);
            nativeCall();
            if (!fullscreenBackgroundInputReported && Logger::IsDebugEnabled())
                Logger::Log("[DEBUG] FixFullscreenTabOut: background controls blocked.");
            fullscreenBackgroundInputReported = true;
        }
        else if (fullscreenBackgroundInputReported)
        {
            fullscreenBackgroundInputReported = false;
            if (Logger::IsDebugEnabled())
                Logger::Log("[DEBUG] FixFullscreenTabOut: background controls restored.");
        }
        const bool cameraFeaturesEnabled = enabled || headBobReductionEnabled || shakeReductionEnabled ||
            swayReductionEnabled || recoilReductionEnabled || firstPersonRecoilReductionEnabled;
        if (!cameraFeaturesEnabled && !drunkFilterDisabled && !eagleEyeFilterDisabled && !deadEyeFilterDisabled &&
            !pauseMenuFilterDisabled && !weaponWheelFilterDisabled && !lowHealthFilterDisabled)
            return;

        if (drunkFilterDisabled)
        {
            bool suppressed = false;
            for (const char* effect : drunkFilters)
                suppressed = SuppressPostFx(effect) || suppressed;
            if (suppressed && !drunkFilterStatusReported && Logger::IsDebugEnabled())
            {
                drunkFilterStatusReported = true;
                Logger::Log("[DEBUG] DisableDrunkFilter: an active drunk post-effect was suppressed.");
            }
        }

        if (eagleEyeFilterDisabled && SuppressPostFx(eagleEyeFilter) &&
            !eagleEyeFilterStatusReported && Logger::IsDebugEnabled())
        {
            eagleEyeFilterStatusReported = true;
            Logger::Log("[DEBUG] DisableEagleEyeFilter: active Eagle Eye post-effect was suppressed.");
        }

        if (deadEyeFilterDisabled && SuppressPostFx(deadEyeFilter) &&
            !deadEyeFilterStatusReported && Logger::IsDebugEnabled())
        {
            deadEyeFilterStatusReported = true;
            Logger::Log("[DEBUG] DisableDeadEyeFilter: active Dead Eye post-effect was suppressed.");
        }

        if (pauseMenuFilterDisabled && SuppressPostFx(pauseMenuFilter) &&
            !pauseMenuFilterStatusReported && Logger::IsDebugEnabled())
        {
            pauseMenuFilterStatusReported = true;
            Logger::Log("[DEBUG] DisablePauseMenuFilter: active pause menu post-effect was suppressed.");
        }

        if (weaponWheelFilterDisabled && SuppressPostFx(weaponWheelFilter) &&
            !weaponWheelFilterStatusReported && Logger::IsDebugEnabled())
        {
            weaponWheelFilterStatusReported = true;
            Logger::Log("[DEBUG] DisableWeaponWheelFilter: active weapon wheel post-effect was suppressed.");
        }

        if (lowHealthFilterDisabled)
        {
            bool suppressed = false;
            for (const char* effect : lowHealthFilters)
                suppressed = SuppressPostFx(effect) || suppressed;
            if (suppressed && !lowHealthFilterStatusReported && Logger::IsDebugEnabled())
            {
                lowHealthFilterStatusReported = true;
                Logger::Log("[DEBUG] DisableLowHealthFilter: an active low health post-effect was suppressed.");
            }
        }

        if (!cameraFeaturesEnabled)
            return;

        nativeInit(isPlayerControlOn);
        nativePush64(0);
        const bool gameplayCameraActive = *nativeCall() != 0 && InvokeBoolean(isGameplayCamRendering);

        if (enabled)
        {
            DWORD foregroundProcessId = 0;
            GetWindowThreadProcessId(GetForegroundWindow(), &foregroundProcessId);
            const bool gameInForeground = foregroundProcessId == GetCurrentProcessId();
            if (gameInForeground)
                gameplayCameraBeforeFocusLoss = gameplayCameraActive;

            const bool backgroundGameplay = !gameInForeground && gameplayCameraBeforeFocusLoss;
            if (gameplayCameraActive || backgroundGameplay)
            {
                const ULONGLONG now = GetTickCount64();
                if (ControlPressed(cinematicCameraControl) || ControlPressed(cinematicCameraHoldControl))
                    manualCameraAllowanceUntil = now + manualCameraAllowanceMs;

                if (now >= manualCameraAllowanceUntil && !InvokeBoolean(isInCinematicMode) &&
                    !InvokeBoolean(isCinematicCamRendering))
                {
                    if (now >= nextVehicleRefresh)
                    {
                        Invoke(invalidateCinematicVehicleIdleMode);
                        nextVehicleRefresh = now + vehicleRefreshMs;
                    }

                    Invoke(disableCinematicModeThisFrame);
                    if (backgroundGameplay && !backgroundIdleStatusReported && Logger::IsDebugEnabled())
                    {
                        backgroundIdleStatusReported = true;
                        Logger::Log("[DEBUG] DisableIdleCamera: blocked while game is unfocused.");
                    }
                }
            }
        }

        if (!gameplayCameraActive)
        {
            if (recoilReductionApplied || shakeReductionApplied || headBobReductionApplied)
                SetShakeAmplitude(1.0f);
            recoilReductionApplied = false;
            shakeReductionApplied = false;
            headBobReductionApplied = false;
            recoilReleaseUntil = 0;
            return;
        }

        if (!enabled && !headBobReductionEnabled && !shakeReductionEnabled && !swayReductionEnabled &&
            !recoilReductionEnabled && !firstPersonRecoilReductionEnabled)
            return;

        if (swayReductionEnabled)
        {
            if (InvokeBoolean(isGameplayCamShaking))
            {
                nativeInit(stopGameplayCamShaking);
                nativePush64(1);
                nativeCall();
                if (!swayStopped)
                {
                    swayStopped = true;
                    if (Logger::IsDebugEnabled())
                        Logger::Log("[DEBUG] ReduceCameraSway: active camera shake stopped.");
                }
            }

            if (!swayStatusReported && Logger::IsDebugEnabled())
            {
                const ULONGLONG now = GetTickCount64();
                if (!swayObservationStarted)
                    swayObservationStarted = now;
                if (now - swayObservationStarted >= 30000)
                {
                    swayStatusReported = true;
                    Logger::Log(swayStopped ?
                        "[DEBUG] ReduceCameraSway: native shake stopped." :
                        "[DEBUG] ReduceCameraSway: no camera shake found in 30s; remaining motion may come from animations.");
                }
            }
        }

        if (shakeReductionEnabled)
        {
            SetShakeAmplitude(0.0f);
            if (!shakeReductionApplied)
            {
                shakeReductionApplied = true;
                if (Logger::IsDebugEnabled())
                    Logger::Log("[DEBUG] ReduceCameraShake: shake override applied.");
            }
        }

        if (recoilReductionEnabled || firstPersonRecoilReductionEnabled)
        {
            const bool firstPerson = InvokeBoolean(isFirstPersonAimCamActive) ||
                InvokeBoolean(isInFullFirstPersonMode);
            const bool reduceRecoil = firstPerson ? firstPersonRecoilReductionEnabled : recoilReductionEnabled;
            if (reduceRecoil)
            {
                nativeInit(playerPedId);
                const std::uint64_t ped = *nativeCall();
                const ULONGLONG now = GetTickCount64();
                const bool shooting = ped != 0 && PedShooting(ped);
                const bool aiming = ped != 0 && PedArmed(ped) &&
                    (ControlPressed(aimControl) || ControlPressed(horseAimControl));
                if (shooting)
                    recoilReleaseUntil = now + (firstPerson ? firstPersonRecoilReleaseMs : recoilReleaseMs);
                if (aiming || now < recoilReleaseUntil)
                {
                    const float scale = firstPerson ? firstPersonRecoilShakeAmplitudeScale : recoilShakeAmplitudeScale;
                    if (!shakeReductionEnabled)
                    {
                        SetShakeAmplitude(scale);
                        recoilReductionApplied = true;
                    }
                    if (scale == 0.0f && now < recoilReleaseUntil)
                    {
                        SuppressRecoilShakes(firstPerson);
                        if (InvokeBoolean(isGameplayCamShaking))
                        {
                            nativeInit(stopGameplayCamShaking);
                            nativePush64(1);
                            nativeCall();
                        }
                    }
                    if (firstPerson && !firstPersonRecoilStatusReported && Logger::IsDebugEnabled())
                    {
                        firstPersonRecoilStatusReported = true;
                        Logger::Log("[DEBUG] ReduceFirstPersonWeaponRecoil: firing detected; recoil native called (effect unverified).");
                    }
                    else if (!firstPerson && !thirdPersonRecoilStatusReported && Logger::IsDebugEnabled())
                    {
                        thirdPersonRecoilStatusReported = true;
                        Logger::Log("[DEBUG] ReduceWeaponRecoil: firing detected; recoil native called (effect unverified).");
                    }
                }
                else if (recoilReductionApplied || recoilReleaseUntil != 0)
                    ClearRecoilOverride();
            }
            else if (recoilReductionApplied || recoilReleaseUntil != 0)
                ClearRecoilOverride();
        }

        if (headBobReductionEnabled)
        {
            nativeInit(playerPedId);
            const std::uint64_t ped = *nativeCall();
            const bool shouldSuppress = !recoilReductionApplied && PedMovingOnFootOrMount(ped);
            if (shouldSuppress)
            {
                SetShakeAmplitude(0.0f);
                headBobReductionApplied = true;
                if (!headBobStatusReported && Logger::IsDebugEnabled())
                {
                    headBobStatusReported = true;
                    Logger::Log("[DEBUG] DisableHeadBob: movement shake reduced.");
                }
            }
            else if (headBobReductionApplied)
            {
                if (!recoilReductionApplied)
                    SetShakeAmplitude(1.0f);
                headBobReductionApplied = false;
            }
        }

    }
}
