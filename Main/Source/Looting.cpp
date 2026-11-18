#include "Looting.h"
#include "../../Headers/Logger.h"
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <vector>
#include <atomic>

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

    std::atomic<bool> workerStarted{ false };

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

    bool DisableAmmoEntries(std::uintptr_t address)
    {
        auto* count = reinterpret_cast<volatile SHORT*>(address);
#if defined(_MSC_VER)
        __try
        {
            return InterlockedCompareExchange16(count, 0, 1) == 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
#else
        return InterlockedCompareExchange16(count, 0, 1) == 1;
#endif
    }

    bool PatchDataset(std::uintptr_t address, std::uintptr_t imageStart, std::uintptr_t imageEnd)
    {
        if (!IsWritableObject(address, 0x20))
            return false;

        std::uint8_t dataset[0x20] = {};
        if (!ReadBytes(address, dataset, sizeof(dataset)))
            return false;

        std::uint32_t name = 0;
        std::uintptr_t datasetVtable = 0;
        std::uintptr_t actionAddress = 0;
        std::uint32_t entryState = 0;
        std::memcpy(&name, dataset, sizeof(name));
        std::memcpy(&datasetVtable, dataset + 0x08, sizeof(datasetVtable));
        std::memcpy(&actionAddress, dataset + 0x10, sizeof(actionAddress));
        std::memcpy(&entryState, dataset + 0x18, sizeof(entryState));

        if (name != ammoDatasetHash || entryState != originalEntryState ||
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

        if (!IsWritableObject(address + 0x18, sizeof(entryState)) ||
            !ReadBytes(address + 0x18, &entryState, sizeof(entryState)) ||
            entryState != originalEntryState || !DisableAmmoEntries(address + 0x18))
            return false;

        Logger::Log("[SUCCESS] DisableAutoAmmoPickup: enabled.");
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
        Sleep(static_cast<DWORD>(firstScanDelayMs));

        bool applied = false;
        for (unsigned int attempt = 0; attempt < sizeof(scanByteLimits) / sizeof(scanByteLimits[0]); ++attempt)
        {
            if (ScanAndPatch(scanByteLimits[attempt]))
            {
                applied = true;
                break;
            }
            if (attempt + 1 < sizeof(scanByteLimits) / sizeof(scanByteLimits[0]))
                Sleep(static_cast<DWORD>(retryDelayMs));
        }

        if (!applied)
            Logger::Log("[WARNING] DisableAutoAmmoPickup: ammo dataset not found; game data unchanged.");
        FreeLibraryAndExitThread(owner, 0);
        return 0;
    }
}

namespace Main::Looting
{
    void InitializeEarly(bool noAutoAmmoPickup)
    {
        if (!noAutoAmmoPickup || workerStarted.exchange(true))
            return;

        HMODULE owner = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(&PatchWorker), &owner))
        {
            workerStarted.store(false);
            Logger::Log("[WARNING] DisableAutoAmmoPickup: could not retain module for startup worker.");
            return;
        }

        const HANDLE worker = CreateThread(nullptr, 0, PatchWorker, owner, 0, nullptr);
        if (!worker)
        {
            FreeLibrary(owner);
            workerStarted.store(false);
            Logger::Log("[WARNING] DisableAutoAmmoPickup: could not start patch worker.");
            return;
        }
        CloseHandle(worker);
    }
}
