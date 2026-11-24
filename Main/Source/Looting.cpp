#include "Looting.h"
#include "../../Headers/Logger.h"
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <vector>
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace
{
    constexpr std::uint32_t ammoDatasetHash = 0xB5E9FEF6;
    constexpr std::uint32_t instantAmmoHash = 0x08D00431;
    constexpr std::uint32_t originalEntryState = 0x00010001;
    constexpr SIZE_T scanChunkSize = 1024 * 1024;
    constexpr ULONGLONG firstScanDelayMs = 10000;
    constexpr ULONGLONG retryDelayMs = 10000;
    constexpr ULONGLONG scanDurationMs = 5500;
    constexpr std::uint64_t scanByteLimits[] =
    {
        768ULL * 1024 * 1024,
        1536ULL * 1024 * 1024,
        2048ULL * 1024 * 1024
    };

    constexpr std::uint32_t disabledEntryState = 0x00010000;
    std::mutex patchMutex;
    std::condition_variable workerWake;
    bool workerStarted = false;
    bool startupDelayPending = false;
    bool disableRequested = false;

    struct AmmoDataset
    {
        std::uintptr_t address = 0;
        std::uintptr_t imageStart = 0;
        std::uintptr_t imageEnd = 0;
        bool patched = false;
    };

    AmmoDataset ammoDataset;

    bool IsWritablePage(DWORD protection)
    {
        if (protection & (PAGE_GUARD | PAGE_NOACCESS))
            return false;
        const DWORD access = protection & 0xff;
        return access == PAGE_READWRITE || access == PAGE_EXECUTE_READWRITE;
    }

    bool ReadBytes(std::uintptr_t address, void* buffer, SIZE_T size)
    {
        SIZE_T copied = 0;
        return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address),
            buffer, size, &copied) && copied == size;
    }

    bool IsWritableObject(std::uintptr_t address, SIZE_T size)
    {
        if (address < 0x10000 || address > UINTPTR_MAX - size)
            return false;

        MEMORY_BASIC_INFORMATION information = {};
        if (!VirtualQuery(reinterpret_cast<LPCVOID>(address), &information, sizeof(information)) ||
            information.State != MEM_COMMIT || information.Type != MEM_PRIVATE ||
            !IsWritablePage(information.Protect))
            return false;

        const auto start = reinterpret_cast<std::uintptr_t>(information.BaseAddress);
        if (address < start || address - start >= information.RegionSize)
            return false;
        return size <= information.RegionSize - (address - start);
    }

    bool ChangeAmmoEntries(std::uintptr_t address, LONG expected, LONG desired)
    {
        auto* count = reinterpret_cast<volatile LONG*>(address);
#if defined(_MSC_VER)
        __try
        {
            return InterlockedCompareExchange(count, desired, expected) == expected;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
#else
        return InterlockedCompareExchange(count, desired, expected) == expected;
#endif
    }

    bool ReadDatasetState(const AmmoDataset& target, std::uint32_t& entryState)
    {
        const auto address = target.address;
        const auto imageStart = target.imageStart;
        const auto imageEnd = target.imageEnd;
        if ((address & 3) != 0 || !IsWritableObject(address, 0x20))
            return false;

        std::uint8_t dataset[0x20] = {};
        if (!ReadBytes(address, dataset, sizeof(dataset)))
            return false;

        std::uint32_t name = 0;
        std::uintptr_t datasetVtable = 0;
        std::uintptr_t actionAddress = 0;
        std::memcpy(&name, dataset, sizeof(name));
        std::memcpy(&datasetVtable, dataset + 0x08, sizeof(datasetVtable));
        std::memcpy(&actionAddress, dataset + 0x10, sizeof(actionAddress));
        std::memcpy(&entryState, dataset + 0x18, sizeof(entryState));

        if (name != ammoDatasetHash ||
            datasetVtable < imageStart || datasetVtable >= imageEnd ||
            !IsWritableObject(actionAddress, 0x10))
            return false;

        std::uint8_t action[0x10] = {};
        if (!ReadBytes(actionAddress, action, sizeof(action)))
            return false;

        std::uintptr_t actionVtable = 0;
        std::uint32_t actionName = 0;
        std::memcpy(&actionVtable, action, sizeof(actionVtable));
        std::memcpy(&actionName, action + 0x08, sizeof(actionName));

        if (actionName != instantAmmoHash || actionVtable < imageStart ||
            actionVtable >= imageEnd || datasetVtable - actionVtable != 0x10)
            return false;

        return true;
    }

    bool ApplyDataset(AmmoDataset& target, bool disable)
    {
        std::uint32_t state = 0;
        if (!ReadDatasetState(target, state) ||
            (state != originalEntryState && !(target.patched && state == disabledEntryState)))
            return false;

        const auto desired = disable ? disabledEntryState : originalEntryState;
        if (state != desired)
        {
            if (!ChangeAmmoEntries(target.address + 0x18, state, desired))
                return false;
            Logger::Log(disable ? "[SUCCESS] DisableAutoAmmoPickup: enabled." :
                "[SUCCESS] DisableAutoAmmoPickup: original ammo pickup restored.");
        }

        target.patched = disable;
        return true;
    }

    bool PatchDataset(std::uintptr_t address, std::uintptr_t imageStart, std::uintptr_t imageEnd)
    {
        std::lock_guard<std::mutex> lock(patchMutex);
        AmmoDataset candidate{ address, imageStart, imageEnd };
        if (!ApplyDataset(candidate, disableRequested))
            return false;
        ammoDataset = candidate;
        return true;
    }

    bool ScanAndPatch(std::uint64_t byteLimit)
    {
        const auto imageStart = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        if (!imageStart)
            return false;

        IMAGE_DOS_HEADER dos = {};
        if (!ReadBytes(imageStart, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
            dos.e_lfanew < 0 || dos.e_lfanew > 0x1000)
            return false;

        IMAGE_NT_HEADERS64 nt = {};
        if (!ReadBytes(imageStart + static_cast<std::uintptr_t>(dos.e_lfanew), &nt, sizeof(nt)) ||
            nt.Signature != IMAGE_NT_SIGNATURE || nt.OptionalHeader.SizeOfImage == 0 ||
            imageStart > UINTPTR_MAX - nt.OptionalHeader.SizeOfImage)
            return false;
        const auto imageEnd = imageStart + nt.OptionalHeader.SizeOfImage;

        SYSTEM_INFO systemInfo = {};
        GetNativeSystemInfo(&systemInfo);
        auto cursor = reinterpret_cast<std::uintptr_t>(systemInfo.lpMinimumApplicationAddress);
        const auto maximum = reinterpret_cast<std::uintptr_t>(systemInfo.lpMaximumApplicationAddress);
        const ULONGLONG started = GetTickCount64();
        std::uint64_t scanned = 0;
        std::uint32_t chunks = 0;
        std::vector<std::uint8_t> buffer(scanChunkSize);

        while (cursor < maximum && scanned < byteLimit &&
            GetTickCount64() - started < scanDurationMs)
        {
            MEMORY_BASIC_INFORMATION information = {};
            if (!VirtualQuery(reinterpret_cast<LPCVOID>(cursor), &information, sizeof(information)))
                break;

            const auto start = reinterpret_cast<std::uintptr_t>(information.BaseAddress);
            if (start > maximum || information.RegionSize > maximum - start)
                break;
            const auto end = start + information.RegionSize;
            if (end <= cursor)
                break;

            if (information.State == MEM_COMMIT && information.Type == MEM_PRIVATE &&
                IsWritablePage(information.Protect))
            {
                auto address = cursor > start ? cursor : start;
                while (address < end && scanned < byteLimit &&
                    GetTickCount64() - started < scanDurationMs)
                {
                    const SIZE_T remaining = static_cast<SIZE_T>(
                        (end - address) < scanChunkSize ? end - address : scanChunkSize);
                    SIZE_T copied = 0;
                    if (ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address),
                        buffer.data(), remaining, &copied) && copied >= sizeof(std::uint32_t))
                    {
                        scanned += copied;
                        const SIZE_T alignment = (4 - (address & 3)) & 3;
                        for (SIZE_T offset = alignment;
                            offset + sizeof(std::uint32_t) <= copied; offset += 4)
                        {
                            std::uint32_t value = 0;
                            std::memcpy(&value, buffer.data() + offset, sizeof(value));
                            if (value != ammoDatasetHash)
                                continue;
                            if (PatchDataset(address + offset, imageStart, imageEnd))
                                return true;
                        }
                    }
                    address += remaining;
                    if ((++chunks & 15) == 0)
                        Sleep(1);
                }
            }
            cursor = end;
        }

        return false;
    }

    DWORD WINAPI PatchWorker(LPVOID parameter)
    {
        HMODULE owner = static_cast<HMODULE>(parameter);
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
        {
            std::unique_lock<std::mutex> lock(patchMutex);
            if (startupDelayPending)
                workerWake.wait_for(lock, std::chrono::milliseconds(firstScanDelayMs),
                    [] { return !startupDelayPending; });
            startupDelayPending = false;
        }

        constexpr auto attemptCount = sizeof(scanByteLimits) / sizeof(scanByteLimits[0]);
        for (std::size_t attempt = 0; attempt < attemptCount; ++attempt)
        {
            {
                std::lock_guard<std::mutex> lock(patchMutex);
                if (!disableRequested)
                {
                    workerStarted = false;
                    break;
                }
            }

            const bool found = ScanAndPatch(scanByteLimits[attempt]);
            std::unique_lock<std::mutex> lock(patchMutex);
            if (found || !disableRequested || attempt + 1 == attemptCount)
            {
                workerStarted = false;
                if (!found && disableRequested)
                    Logger::Log("[WARNING] DisableAutoAmmoPickup: ammo dataset not found; game data unchanged.");
                break;
            }
            workerWake.wait_for(lock, std::chrono::milliseconds(retryDelayMs),
                [] { return !disableRequested; });
        }
        FreeLibraryAndExitThread(owner, 0);
        return 0;
    }

    void SetAmmoPickupDisabled(bool disable, bool early)
    {
        std::lock_guard<std::mutex> lock(patchMutex);
        disableRequested = disable;
        if (!early)
        {
            startupDelayPending = false;
            workerWake.notify_one();
        }

        if (ammoDataset.address && (disable || ammoDataset.patched))
        {
            if (ApplyDataset(ammoDataset, disable))
                return;
            if (!disable)
            {
                Logger::Log("[WARNING] DisableAutoAmmoPickup: could not verify or restore the patched ammo dataset.");
                return;
            }
        }

        if (!disable || workerStarted)
            return;

        HMODULE owner = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(&PatchWorker), &owner))
        {
            Logger::Log("[WARNING] DisableAutoAmmoPickup: could not retain module for patch worker.");
            return;
        }

        startupDelayPending = early;
        const HANDLE worker = CreateThread(nullptr, 0, PatchWorker, owner, 0, nullptr);
        if (!worker)
        {
            FreeLibrary(owner);
            Logger::Log("[WARNING] DisableAutoAmmoPickup: could not start patch worker.");
            return;
        }
        workerStarted = true;
        CloseHandle(worker);
    }
}

namespace Main::Looting
{
    void InitializeEarly(bool noAutoAmmoPickup)
    {
        SetAmmoPickupDisabled(noAutoAmmoPickup, true);
    }

    void SetNoAutoAmmoPickup(bool enabled)
    {
        SetAmmoPickupDisabled(enabled, false);
    }
}
