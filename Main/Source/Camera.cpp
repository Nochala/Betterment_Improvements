#include "Camera.h"
#include "../../Headers/Logger.h"
#include "main.h"
#include <windows.h>
#include <shlobj.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <string>
#include <vector>

#pragma comment(lib, "shell32.lib")

namespace
{
    constexpr std::uintptr_t smoothingCameraVtableRva = 0x337EA90;
    constexpr std::uint32_t smoothingModifierHash = 0x1385680D;
    constexpr std::size_t smoothingScanBytes = 8 * 1024 * 1024;

    struct CameraModifierList
    {
        std::uint32_t* entries;
        std::uint16_t count;
        std::uint16_t capacity;
    };

    bool smoothingDisabled = false;
    std::uintptr_t smoothingScanCursor = 0;
    std::vector<std::uintptr_t> smoothingScanBuffer;
    struct CameraSmoothingTarget
    {
        std::uint32_t cameraHash;
        std::uint32_t modifierSequence[5];
        CameraModifierList* list = nullptr;
        CameraModifierList original{};
        std::vector<std::uint32_t> entries;
    };

    CameraSmoothingTarget smoothingTargets[] = {
        { 0xFBE36564, { 0xD9A293BF, 0x52264339, smoothingModifierHash, 0x1DFC2FC7, 0xC9B22720 } },
        { 0xB611DAF3, { 0xA93D4712, 0x52264339, smoothingModifierHash, 0x85B59D8B, 0x19149792 } }
    };

    bool HasCameraSmoothingWork()
    {
        return smoothingDisabled && smoothingScanCursor != UINTPTR_MAX &&
            std::any_of(std::begin(smoothingTargets), std::end(smoothingTargets),
                [](const CameraSmoothingTarget& target) { return target.list == nullptr; });
    }

    bool ReadCameraMemory(const void* address, void* output, std::size_t size)
    {
        SIZE_T read = 0;
        return ReadProcessMemory(GetCurrentProcess(), address, output, size, &read) && read == size;
    }

    bool WriteCameraMemory(void* address, const void* input, std::size_t size)
    {
        SIZE_T written = 0;
        return WriteProcessMemory(GetCurrentProcess(), address, input, size, &written) && written == size;
    }

    bool SetCameraSmoothingList(std::uintptr_t camera, CameraSmoothingTarget& target)
    {
        auto* list = reinterpret_cast<CameraModifierList*>(camera + 0x30);
        CameraModifierList original{};
        if (!ReadCameraMemory(list, &original, sizeof(original)) || !original.entries ||
            original.count < std::size(target.modifierSequence) || original.count > original.capacity ||
            original.capacity > 256)
            return false;

        std::vector<std::uint32_t> entries(original.count);
        if (!ReadCameraMemory(original.entries, entries.data(), entries.size() * sizeof(entries[0])))
            return false;

        const auto modifier = std::search(entries.begin(), entries.end(),
            std::begin(target.modifierSequence), std::end(target.modifierSequence));
        if (modifier == entries.end() || std::count(entries.begin(), entries.end(), smoothingModifierHash) != 1)
            return false;

        auto filtered = entries;
        filtered.erase(filtered.begin() + (modifier - entries.begin()) + 2);
        const auto count = static_cast<std::uint16_t>(filtered.size());
        if (!WriteCameraMemory(&list->count, &count, sizeof(count)))
            return false;
        if (!WriteCameraMemory(original.entries, filtered.data(), filtered.size() * sizeof(filtered[0])))
        {
            if (WriteCameraMemory(original.entries, entries.data(), entries.size() * sizeof(entries[0])))
                WriteCameraMemory(&list->count, &original.count, sizeof(original.count));
            return false;
        }

        target.list = list;
        target.original = original;
        target.entries = std::move(entries);
        Logger::Log("[INFO] DisableCameraSmoothing: third-person camera modifier removed.");
        return true;
    }

    void RestoreCameraSmoothing(CameraSmoothingTarget& target)
    {
        if (!target.list)
            return;

        CameraModifierList current{};
        std::vector<std::uint32_t> filtered(target.original.count - 1);
        auto expected = target.entries;
        expected.erase(std::find(expected.begin(), expected.end(), smoothingModifierHash));
        if (ReadCameraMemory(target.list, &current, sizeof(current)) &&
            current.entries == target.original.entries && current.capacity == target.original.capacity &&
            current.count == filtered.size() &&
            ReadCameraMemory(current.entries, filtered.data(), filtered.size() * sizeof(filtered[0])) &&
            filtered == expected &&
            WriteCameraMemory(current.entries, target.entries.data(), target.entries.size() * sizeof(target.entries[0])) &&
            WriteCameraMemory(&target.list->count, &target.original.count, sizeof(target.original.count)))
        {
            Logger::Log("[INFO] DisableCameraSmoothing: third-person camera modifier restored.");
        }

        target.list = nullptr;
        target.original = {};
        target.entries.clear();
    }

    void RestoreCameraSmoothing()
    {
        for (auto& target : smoothingTargets)
            RestoreCameraSmoothing(target);
    }

    void UpdateCameraSmoothing()
    {
        if (!HasCameraSmoothingWork())
            return;

        const auto image = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        const auto vtable = image + smoothingCameraVtableRva;
        if (smoothingScanBuffer.empty())
            smoothingScanBuffer.resize(smoothingScanBytes / sizeof(std::uintptr_t) + 1);

        MEMORY_BASIC_INFORMATION region{};
        while (VirtualQuery(reinterpret_cast<const void*>(smoothingScanCursor), &region, sizeof(region)) == sizeof(region))
        {
            const auto end = reinterpret_cast<std::uintptr_t>(region.BaseAddress) + region.RegionSize;
            const auto cursor = smoothingScanCursor;
            smoothingScanCursor = end;
            if (region.State != MEM_COMMIT || region.Type != MEM_PRIVATE || region.Protect != PAGE_READWRITE)
                continue;

            const auto size = (std::min)(end - cursor, smoothingScanBytes + sizeof(std::uintptr_t));
            smoothingScanCursor = cursor + (std::min)(end - cursor, smoothingScanBytes);
            if (ReadCameraMemory(reinterpret_cast<const void*>(cursor), smoothingScanBuffer.data(), size))
            {
                const auto words = size / sizeof(std::uintptr_t);
                for (std::size_t index = 0; index + 1 < words; ++index)
                {
                    if (smoothingScanBuffer[index] != vtable)
                        continue;

                    const auto hash = static_cast<std::uint32_t>(smoothingScanBuffer[index + 1]);
                    for (auto& target : smoothingTargets)
                    {
                        if (!target.list && target.cameraHash == hash)
                            SetCameraSmoothingList(cursor + index * sizeof(std::uintptr_t), target);
                    }

                    if (!HasCameraSmoothingWork())
                    {
                        std::vector<std::uintptr_t>().swap(smoothingScanBuffer);
                        return;
                    }
                }
            }
            return;
        }

        smoothingScanCursor = UINTPTR_MAX;
        std::vector<std::uintptr_t>().swap(smoothingScanBuffer);
        Logger::Log("[WARNING] DisableCameraSmoothing: one or more matching camera records were unavailable.");
    }

    constexpr std::uint64_t animpostfxIsRunning = 0x4A123E85D7C4CA0BULL;
    constexpr std::uint64_t animpostfxSetStrength = 0xCAB4DD2D5B2B7246ULL;
    constexpr const char* drunkFilters[] = {
        "PlayerDrunk01",
        "PlayerDrunkSaloon1",
        "PlayerDrunkAberdeen",
        "PlayerWakeUpDrunk",
        "PlayerWakeUpAberdeen"
    };
    constexpr const char* eagleEyeFilter = "EagleEye";
    constexpr const char* deadEyeFilter = "DEADEYE";
    constexpr const char* weaponWheelFilter = "WheelHUDIn";
    constexpr const char* lowHealthFilters[] = {
        "PlayerHealthLow",
        "PlayerHealthPoor"
    };
    constexpr std::uint64_t isPlayerControlOn = 0x7964097FCE4C244BULL;
    constexpr std::uint64_t isGameplayCamRendering = 0x8660EA714834E412ULL;
    constexpr std::uint64_t isLoadingScreenActive = 0x71D4BF5890659B0CULL;
    constexpr std::uint64_t isScreenFadedIn = 0x37F9A426FBCF4AF2ULL;
    constexpr std::uint64_t playerPedId = 0x096275889B8E0EE0ULL;
    constexpr std::uint64_t isPedOnFoot = 0x01FEE67DB37F59B2ULL;
    constexpr std::uint64_t isPedOnMount = 0x460BC76A0E10655EULL;
    constexpr std::uint64_t getMount = 0xE7E11B8DCBED1058ULL;
    constexpr std::uint64_t getEntitySpeed = 0xFB6BA510A533DF81ULL;
    constexpr std::uint64_t setGameplayCamShakeAmplitude = 0x570E35F5C4A44838ULL;
    constexpr std::uint64_t isGameplayCamShaking = 0xEA4C5F4AA0A4DBEFULL;
    constexpr std::uint64_t stopGameplayCamShaking = 0xE0DE43D290FB65F9ULL;
    constexpr std::uint64_t isInCinematicMode = 0x74F1D22EFA71FAB8ULL;
    constexpr std::uint64_t isCinematicCamRendering = 0xBF7C780731AADBF8ULL;
    constexpr std::uint64_t isControlPressed = 0xF3A21BCD95725A4AULL;
    constexpr std::uint64_t disableAllControlActions = 0x5F4B6931816E599BULL;
    constexpr std::uint64_t disableCinematicModeThisFrame = 0x8910C24B7E0046ECULL;
    constexpr std::uint64_t invalidateCinematicVehicleIdleMode = 0x634F4A0562CF19B8ULL;
    constexpr std::uint32_t cinematicCameraControl = 0x620A6C5E;
    constexpr std::uint32_t cinematicCameraHoldControl = 0xD7E7B375;
    constexpr ULONGLONG vehicleRefreshMs = 1000;
    constexpr ULONGLONG manualCameraAllowanceMs = 3000;

    bool enabled = false;
    bool drunkFilterDisabled = false;
    bool eagleEyeFilterDisabled = false;
    bool deadEyeFilterDisabled = false;
    bool weaponWheelFilterDisabled = false;
    bool lowHealthFilterDisabled = false;
    bool drunkFilterStatusReported = false;
    bool eagleEyeFilterStatusReported = false;
    bool deadEyeFilterStatusReported = false;
    bool weaponWheelFilterStatusReported = false;
    bool lowHealthFilterStatusReported = false;
    bool headBobReductionEnabled = false;
    bool headBobReductionApplied = false;
    bool headBobStatusReported = false;
    bool shakeReductionEnabled = false;
    bool swayReductionEnabled = false;
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
    std::atomic<bool> windowFocusReady{ false };
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

        if (windowFocusReady && fullscreenRestorePending && fullscreenFocusReturnedAt != 0 &&
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
        if (windowFocusReady && minimizeGameWindow && IsWindow(minimizeGameWindow) && !IsIconic(minimizeGameWindow))
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

    void ResetWindowFocusTracking()
    {
        fullscreenFocusInitialized = false;
        fullscreenGameFocused = false;
        fullscreenRestorePending = false;
        fullscreenFocusReturnedAt = 0;
        fullscreenFocusLostAt = 0;
        minimizeGameFocused = false;
        minimizeGameWindow = nullptr;
    }

    void UpdateWindowFocus()
    {
        if (!windowFocusReady)
        {
            ResetWindowFocusTracking();
            return;
        }
        FixFullscreenTabOut();
        MinimizeOnTabOut();
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
            UpdateWindowFocus();
        } while (WaitForSingleObject(stop, 50) == WAIT_TIMEOUT);
        CloseHandle(stop);
        return 0;
    }

    bool InvokeBoolean(std::uint64_t hash)
    {
        nativeInit(hash);
        return *nativeCall() != 0;
    }

    void UpdateWindowFocusReadiness()
    {
        if ((!fullscreenTabOutEnabled && !minimizeOnTabOutEnabled) ||
            InvokeBoolean(isLoadingScreenActive) || !InvokeBoolean(playerPedId))
        {
            windowFocusReady = false;
            return;
        }
        if (!windowFocusReady && InvokeBoolean(isScreenFadedIn))
            windowFocusReady = true;
    }

    bool SuppressPostFx(const char* effect, bool observe)
    {
        nativeInit(animpostfxSetStrength);
        nativePush64(reinterpret_cast<std::uint64_t>(effect));
        nativePush64(0);
        nativeCall();

        if (!observe)
            return false;
        nativeInit(animpostfxIsRunning);
        nativePush64(reinterpret_cast<std::uint64_t>(effect));
        return *nativeCall() != 0;
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

    void SetShakeAmplitude(float amplitude)
    {
        std::uint32_t bits = 0;
        static_assert(sizeof(bits) == sizeof(amplitude));
        std::memcpy(&bits, &amplitude, sizeof(bits));
        nativeInit(setGameplayCamShakeAmplitude);
        nativePush64(bits);
        nativeCall();
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
        if (!effectiveFullscreenTabOut && !effectiveMinimizeOnTabOut)
            windowFocusReady = false;
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

    void Initialize(bool active, bool fixFullscreenTabOut, bool minimizeOnTabOut, bool disableCameraSmoothing, bool disableHeadBob, bool reduceCameraShake, bool reduceCameraSway,
        bool disableDrunkFilter, bool disableEagleEyeFilter, bool disableDeadEyeFilter,
        bool disableWeaponWheelFilter, bool disableLowHealthFilter)
    {
        enabled = active;
        if (smoothingDisabled != disableCameraSmoothing)
        {
            smoothingDisabled = disableCameraSmoothing;
            smoothingScanCursor = 0;
            if (!smoothingDisabled)
            {
                RestoreCameraSmoothing();
                std::vector<std::uintptr_t>().swap(smoothingScanBuffer);
            }
        }
        drunkFilterDisabled = disableDrunkFilter;
        eagleEyeFilterDisabled = disableEagleEyeFilter;
        deadEyeFilterDisabled = disableDeadEyeFilter;
        weaponWheelFilterDisabled = disableWeaponWheelFilter;
        lowHealthFilterDisabled = disableLowHealthFilter;
        drunkFilterStatusReported = false;
        eagleEyeFilterStatusReported = false;
        deadEyeFilterStatusReported = false;
        weaponWheelFilterStatusReported = false;
        lowHealthFilterStatusReported = false;
        headBobReductionEnabled = disableHeadBob && !reduceCameraShake;
        headBobReductionApplied = false;
        headBobStatusReported = false;
        shakeReductionEnabled = reduceCameraShake;
        swayReductionEnabled = reduceCameraSway;
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
    }

    bool NeedsFastUpdate()
    {
        return HasCameraSmoothingWork() || (fullscreenTabOutEnabled && fullscreenRestorePending && !fullscreenGameFocused);
    }

    void Shutdown()
    {
        RestoreCameraSmoothing();
        if (focusMonitorStop)
            SetEvent(focusMonitorStop);
    }

    void Update()
    {
        UpdateWindowFocusReadiness();
        UpdateCameraSmoothing();
        if (!focusMonitorStarted)
            UpdateWindowFocus();
        if (windowFocusReady && fullscreenTabOutEnabled && fullscreenRestorePending && !fullscreenGameFocused)
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
            swayReductionEnabled;
        if (!cameraFeaturesEnabled && !drunkFilterDisabled && !eagleEyeFilterDisabled && !deadEyeFilterDisabled &&
            !weaponWheelFilterDisabled && !lowHealthFilterDisabled)
            return;

        if (drunkFilterDisabled)
        {
            bool suppressed = false;
            const bool observe = !drunkFilterStatusReported && Logger::IsDebugEnabled();
            for (const char* effect : drunkFilters)
                suppressed = SuppressPostFx(effect, observe) || suppressed;
            if (suppressed && !drunkFilterStatusReported)
            {
                drunkFilterStatusReported = true;
                Logger::Log("[DEBUG] DisableDrunkFilter: active drunk post-effect was suppressed without clearing playback.");
            }
        }

        if (eagleEyeFilterDisabled && SuppressPostFx(eagleEyeFilter,
            !eagleEyeFilterStatusReported && Logger::IsDebugEnabled()) && !eagleEyeFilterStatusReported)
        {
            eagleEyeFilterStatusReported = true;
            Logger::Log("[DEBUG] DisableEagleEyeFilter: active Eagle Eye post-effect was suppressed.");
        }

        if (deadEyeFilterDisabled && SuppressPostFx(deadEyeFilter,
            !deadEyeFilterStatusReported && Logger::IsDebugEnabled()) && !deadEyeFilterStatusReported)
        {
            deadEyeFilterStatusReported = true;
            Logger::Log("[DEBUG] DisableDeadEyeFilter: active Dead Eye post-effect was suppressed.");
        }

        if (weaponWheelFilterDisabled && SuppressPostFx(weaponWheelFilter,
            !weaponWheelFilterStatusReported && Logger::IsDebugEnabled()) && !weaponWheelFilterStatusReported)
        {
            weaponWheelFilterStatusReported = true;
            Logger::Log("[DEBUG] DisableWeaponWheelFilter: active weapon wheel post-effect was suppressed.");
        }

        if (lowHealthFilterDisabled)
        {
            bool suppressed = false;
            const bool observe = !lowHealthFilterStatusReported && Logger::IsDebugEnabled();
            for (const char* effect : lowHealthFilters)
                suppressed = SuppressPostFx(effect, observe) || suppressed;
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
            if (shakeReductionApplied || headBobReductionApplied)
                SetShakeAmplitude(1.0f);
            shakeReductionApplied = false;
            headBobReductionApplied = false;
            return;
        }

        if (!enabled && !headBobReductionEnabled && !shakeReductionEnabled && !swayReductionEnabled)
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

        if (headBobReductionEnabled)
        {
            nativeInit(playerPedId);
            const std::uint64_t ped = *nativeCall();
            const bool shouldSuppress = PedMovingOnFootOrMount(ped);
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
                SetShakeAmplitude(1.0f);
                headBobReductionApplied = false;
            }
        }

    }
}
