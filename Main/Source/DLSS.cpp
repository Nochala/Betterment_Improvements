#include "DLSS.h"
#include "../../Headers/Logger.h"
#include <windows.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>

namespace
{
    using GetProcAddressFunction = FARPROC(WINAPI*)(HMODULE, LPCSTR);
    using SetParameterIntFunction = void(__cdecl*)(void*, const char*, int);
    using GetParameterIntFunction = int(__cdecl*)(void*, const char*, int*);
    using CreateFeatureFunction = int(__cdecl*)(void*, int, void*, void**);

    constexpr int autoExposureFlag = 1 << 6;
    constexpr int superSamplingFeature = 1;
    constexpr char featureFlagsName[] = "DLSS.Feature.Create.Flags";

    std::atomic<GetProcAddressFunction> originalGetProcAddress{ nullptr };
    std::atomic<SetParameterIntFunction> originalSetParameterInt{ nullptr };
    std::atomic<GetParameterIntFunction> originalGetParameterInt{ nullptr };
    std::atomic<CreateFeatureFunction> originalD3D12CreateFeature{ nullptr };
    std::atomic<CreateFeatureFunction> originalVulkanCreateFeature{ nullptr };
    std::atomic<HMODULE> ngxModule{ nullptr };
    std::atomic<void* volatile*> importSlot{ nullptr };
    std::atomic<unsigned int> flagWrites{ 0 };
    std::atomic<unsigned int> featureCreations{ 0 };
    std::atomic<unsigned int> patchedCreations{ 0 };
    std::atomic<unsigned int> creationReports{ 0 };
    std::atomic<bool> reportedMissingParameters{ false };
    std::atomic<bool> reportedSuccessfulCreation{ false };

    void ReportFlags(const char* source, int original, int updated)
    {
        if (!Logger::IsDebugEnabled())
            return;
        char message[256] = {};
        std::snprintf(message, sizeof(message),
            "EnableDLSSAutoExposure: %s flags 0x%08X -> 0x%08X.",
            source, static_cast<unsigned int>(original), static_cast<unsigned int>(updated));
        Logger::Debug(message);
    }

    void __cdecl InterceptSetParameterInt(void* parameters, const char* name, int value)
    {
        const auto original = originalSetParameterInt.load();
        if (!original)
            return;
        if (name && std::strcmp(name, featureFlagsName) == 0)
        {
            const int updated = value | autoExposureFlag;
            const unsigned int count = flagWrites.fetch_add(1);
            if (count < 3)
                ReportFlags("NGX parameter assignment", value, updated);
            value = updated;
        }
        original(parameters, name, value);
    }

    bool SetExposureBeforeCreation(void* parameters)
    {
        if (!parameters)
            return false;
        const auto getValue = originalGetParameterInt.load();
        const auto setValue = originalSetParameterInt.load();
        if (!getValue || !setValue)
        {
            if (!reportedMissingParameters.exchange(true))
                Logger::Debug("EnableDLSSAutoExposure: NGX accessors missing; DLSS setup unchanged.");
            return false;
        }
        int flags = 0;
        if (getValue(parameters, featureFlagsName, &flags) != 1)
            return false;
        if ((flags & autoExposureFlag) == 0)
        {
            setValue(parameters, featureFlagsName, flags | autoExposureFlag);
            if (creationReports.fetch_add(1) < 3)
                ReportFlags("DLSS feature creation", flags, flags | autoExposureFlag);
        }
        patchedCreations.fetch_add(1);
        return true;
    }

    int __cdecl InterceptD3D12CreateFeature(void* commandList, int feature,
        void* parameters, void** handle)
    {
        const auto original = originalD3D12CreateFeature.load();
        if (!original)
            return static_cast<int>(0xBAD00003u);
        bool prepared = false;
        if (feature == superSamplingFeature)
        {
            featureCreations.fetch_add(1);
            prepared = SetExposureBeforeCreation(parameters);
        }
        const int result = original(commandList, feature, parameters, handle);
        if (prepared && result == 1 && !reportedSuccessfulCreation.exchange(true))
            Logger::Log("[SUCCESS] EnableDLSSAutoExposure: DLSS auto exposure enabled.");
        return result;
    }

    int __cdecl InterceptVulkanCreateFeature(void* commandBuffer, int feature,
        void* parameters, void** handle)
    {
        const auto original = originalVulkanCreateFeature.load();
        if (!original)
            return static_cast<int>(0xBAD00003u);
        bool prepared = false;
        if (feature == superSamplingFeature)
        {
            featureCreations.fetch_add(1);
            prepared = SetExposureBeforeCreation(parameters);
        }
        const int result = original(commandBuffer, feature, parameters, handle);
        if (prepared && result == 1 && !reportedSuccessfulCreation.exchange(true))
            Logger::Log("[SUCCESS] EnableDLSSAutoExposure: DLSS auto exposure enabled.");
        return result;
    }

    bool IsNGXModule(HMODULE module)
    {
        if (!module)
            return false;
        if (module == GetModuleHandleW(nullptr))
            return true;
        wchar_t filename[MAX_PATH] = {};
        if (!GetModuleFileNameW(module, filename, MAX_PATH))
            return false;
        const wchar_t* base = filename;
        for (const wchar_t* cursor = filename; *cursor; ++cursor)
        {
            if (*cursor == L'\\' || *cursor == L'/')
                base = cursor + 1;
        }
        return _wcsicmp(base, L"nvngx.dll") == 0 || _wcsicmp(base, L"_nvngx.dll") == 0;
    }

    FARPROC WINAPI InterceptGetProcAddress(HMODULE module, LPCSTR name)
    {
        const auto original = originalGetProcAddress.load();
        if (!original)
            return nullptr;
        const FARPROC result = original(module, name);
        if (!result || reinterpret_cast<std::uintptr_t>(name) < 65536 ||
            std::strncmp(name, "NVSDK_NGX_", 10) != 0 || !IsNGXModule(module))
            return result;

        if (std::strcmp(name, "NVSDK_NGX_Parameter_SetI") == 0)
        {
            originalSetParameterInt.store(reinterpret_cast<SetParameterIntFunction>(result));
            ngxModule.store(module);
            return reinterpret_cast<FARPROC>(&InterceptSetParameterInt);
        }
        if (std::strcmp(name, "NVSDK_NGX_D3D12_CreateFeature") == 0 ||
            std::strcmp(name, "NVSDK_NGX_VULKAN_CreateFeature") == 0)
        {
            ngxModule.store(module);
            const HMODULE executable = GetModuleHandleW(nullptr);
            FARPROC setParameter = original(module, "NVSDK_NGX_Parameter_SetI");
            FARPROC getParameter = original(module, "NVSDK_NGX_Parameter_GetI");
            if (executable && (!setParameter || !getParameter))
            {
                if (!setParameter)
                    setParameter = original(executable, "NVSDK_NGX_Parameter_SetI");
                if (!getParameter)
                    getParameter = original(executable, "NVSDK_NGX_Parameter_GetI");
            }
            if (setParameter)
                originalSetParameterInt.store(reinterpret_cast<SetParameterIntFunction>(setParameter));
            if (getParameter)
                originalGetParameterInt.store(reinterpret_cast<GetParameterIntFunction>(getParameter));
            if (std::strcmp(name, "NVSDK_NGX_D3D12_CreateFeature") == 0)
            {
                originalD3D12CreateFeature.store(reinterpret_cast<CreateFeatureFunction>(result));
                return reinterpret_cast<FARPROC>(&InterceptD3D12CreateFeature);
            }
            originalVulkanCreateFeature.store(reinterpret_cast<CreateFeatureFunction>(result));
            return reinterpret_cast<FARPROC>(&InterceptVulkanCreateFeature);
        }
        return result;
    }

    bool InstallImportHook()
    {
        const auto base = reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(nullptr));
        if (!base)
            return false;
        const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0)
            return false;
        const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
            return false;
        const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (!directory.VirtualAddress || directory.Size < sizeof(IMAGE_IMPORT_DESCRIPTOR) ||
            directory.VirtualAddress >= nt->OptionalHeader.SizeOfImage)
            return false;
        const auto imports = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress);
        const std::size_t limit = directory.Size / sizeof(IMAGE_IMPORT_DESCRIPTOR);
        for (std::size_t index = 0; index < limit && imports[index].Name; ++index)
        {
            const IMAGE_IMPORT_DESCRIPTOR& entry = imports[index];
            if (!entry.OriginalFirstThunk || !entry.FirstThunk || entry.Name >= nt->OptionalHeader.SizeOfImage)
                continue;
            const char* library = reinterpret_cast<const char*>(base + entry.Name);
            if (_stricmp(library, "KERNEL32.dll") != 0 && _stricmp(library, "KERNELBASE.dll") != 0)
                continue;
            const auto names = reinterpret_cast<const IMAGE_THUNK_DATA64*>(base + entry.OriginalFirstThunk);
            auto slots = reinterpret_cast<IMAGE_THUNK_DATA64*>(const_cast<std::uint8_t*>(base) + entry.FirstThunk);
            const std::size_t available = (nt->OptionalHeader.SizeOfImage - entry.FirstThunk) / sizeof(IMAGE_THUNK_DATA64);
            for (std::size_t item = 0; item < available && names[item].u1.AddressOfData; ++item)
            {
                if (IMAGE_SNAP_BY_ORDINAL64(names[item].u1.Ordinal) ||
                    names[item].u1.AddressOfData >= nt->OptionalHeader.SizeOfImage)
                    continue;
                const auto imported = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names[item].u1.AddressOfData);
                if (std::strcmp(reinterpret_cast<const char*>(imported->Name), "GetProcAddress") != 0)
                    continue;
                auto slot = reinterpret_cast<void* volatile*>(&slots[item].u1.Function);
                void* prior = reinterpret_cast<void*>(slots[item].u1.Function);
                if (!prior || prior == reinterpret_cast<void*>(&InterceptGetProcAddress))
                    return false;
                DWORD protection = 0;
                if (!VirtualProtect(const_cast<void**>(slot), sizeof(void*), PAGE_READWRITE, &protection))
                    return false;
                originalGetProcAddress.store(reinterpret_cast<GetProcAddressFunction>(prior));
                const void* previous = InterlockedCompareExchangePointer(slot,
                    reinterpret_cast<void*>(&InterceptGetProcAddress), prior);
                DWORD ignored = 0;
                VirtualProtect(const_cast<void**>(slot), sizeof(void*), protection, &ignored);
                if (previous != prior)
                    return false;
                importSlot.store(slot);
                return true;
            }
        }
        return false;
    }
}

namespace Main
{
namespace DLSS
{
    void Initialize(bool enabled)
    {
        if (!enabled)
            return;
        if (InstallImportHook())
            Logger::Log("[INFO] EnableDLSSAutoExposure: NGX hook installed; waiting for DLSS.");
        else
            Logger::Log("[WARNING] EnableDLSSAutoExposure: NGX hook unavailable; no changes applied.");
    }

    void ReportStartupStatus()
    {
        if (!importSlot.load() || !Logger::IsDebugEnabled())
            return;
        char message[256] = {};
        std::snprintf(message, sizeof(message),
            "EnableDLSSAutoExposure: NGX module=%s, interceptedFlagWrites=%u, featureCreations=%u, creationOverrides=%u.",
            ngxModule.load() ? "resolved" : "not resolved", flagWrites.load(),
            featureCreations.load(), patchedCreations.load());
        Logger::Debug(message);
    }

    void Shutdown()
    {
        auto slot = importSlot.exchange(nullptr);
        const auto original = originalGetProcAddress.load();
        if (!slot || !original)
            return;
        DWORD protection = 0;
        if (!VirtualProtect(const_cast<void**>(slot), sizeof(void*), PAGE_READWRITE, &protection))
            return;
        InterlockedCompareExchangePointer(slot, reinterpret_cast<void*>(original),
            reinterpret_cast<void*>(&InterceptGetProcAddress));
        DWORD ignored = 0;
        VirtualProtect(const_cast<void**>(slot), sizeof(void*), protection, &ignored);
    }
}
}
