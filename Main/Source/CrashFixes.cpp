#include "CrashFixes.h"
#include "../../Headers/Logger.h"
#include <windows.h>
#include "main.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cwchar>
#include <atomic>
#include <limits>
#include <d3d12.h>
#include <dxgi.h>
#include <dxgi1_4.h>
#include <psapi.h>

namespace
{
    constexpr std::size_t signatureLength = 14;
    constexpr std::uint8_t returnTrue[] = { 0xB0, 0x01, 0xC3 };

    bool IsExecutable(const std::uint8_t* address, std::size_t length)
    {
        if (!address || length == 0)
            return false;

        MEMORY_BASIC_INFORMATION info = {};
        if (VirtualQuery(address, &info, sizeof(info)) != sizeof(info) || info.State != MEM_COMMIT ||
            (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
            return false;

        const DWORD protect = info.Protect & 0xFF;
        if (protect != PAGE_EXECUTE && protect != PAGE_EXECUTE_READ &&
            protect != PAGE_EXECUTE_READWRITE && protect != PAGE_EXECUTE_WRITECOPY)
            return false;

        const auto start = reinterpret_cast<std::uintptr_t>(address);
        const auto regionStart = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
        return start >= regionStart && length <= info.RegionSize &&
            start - regionStart <= info.RegionSize - length;
    }

    bool MatchesSignature(const std::uint8_t* bytes)
    {
        return bytes[0] == 0xE8 && bytes[5] == 0xE9 && bytes[10] == 0x48 &&
            bytes[11] == 0x8D && bytes[12] == 0x55 && bytes[13] == 0x3F;
    }

    bool HasValidImage(const std::uint8_t* image, const IMAGE_NT_HEADERS64*& nt)
    {
        if (!image)
            return false;

        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < static_cast<LONG>(sizeof(IMAGE_DOS_HEADER)) ||
            dos->e_lfanew > 4096)
            return false;

        nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(image + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
            nt->FileHeader.NumberOfSections == 0 || nt->FileHeader.NumberOfSections > 96)
            return false;

        const std::size_t imageSize = nt->OptionalHeader.SizeOfImage;
        const auto* sections = IMAGE_FIRST_SECTION(nt);
        const std::size_t sectionOffset = reinterpret_cast<const std::uint8_t*>(sections) - image;
        return sectionOffset < imageSize && nt->FileHeader.NumberOfSections <=
            (imageSize - sectionOffset) / sizeof(IMAGE_SECTION_HEADER);
    }

    bool IsInExecutableSection(const std::uint8_t* image, const IMAGE_NT_HEADERS64* nt,
        const std::uint8_t* address, std::size_t length)
    {
        const std::size_t imageSize = nt->OptionalHeader.SizeOfImage;
        const auto addr = reinterpret_cast<std::uintptr_t>(address);
        const auto base = reinterpret_cast<std::uintptr_t>(image);
        if (addr < base || addr - base >= imageSize || length > imageSize - (addr - base))
            return false;

        const auto* sections = IMAGE_FIRST_SECTION(nt);
        for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        {
            const auto& section = sections[i];
            const std::size_t offset = section.VirtualAddress;
            const std::size_t size = section.Misc.VirtualSize;
            if ((section.Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0 ||
                offset >= imageSize || size > imageSize - offset)
                continue;
            const std::size_t rva = addr - base;
            if (rva >= offset && length <= size && rva - offset <= size - length)
                return IsExecutable(address, length);
        }
        return false;
    }

    const std::uint8_t* FindUniqueSignature(const std::uint8_t* image, const IMAGE_NT_HEADERS64* nt)
    {
        const std::size_t imageSize = nt->OptionalHeader.SizeOfImage;
        const auto* sections = IMAGE_FIRST_SECTION(nt);
        const std::uint8_t* found = nullptr;
        unsigned count = 0;

        for (unsigned index = 0; index < nt->FileHeader.NumberOfSections; ++index)
        {
            const auto& section = sections[index];
            if ((section.Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0)
                continue;

            const std::size_t offset = section.VirtualAddress;
            const std::size_t size = section.Misc.VirtualSize;
            if (offset >= imageSize || size < signatureLength || size > imageSize - offset)
                continue;

            const auto sectionStart = reinterpret_cast<std::uintptr_t>(image) + offset;
            const auto end = sectionStart + size;
            std::uintptr_t cursor = sectionStart;
            while (cursor < end)
            {
                MEMORY_BASIC_INFORMATION region = {};
                if (VirtualQuery(reinterpret_cast<LPCVOID>(cursor), &region, sizeof(region)) != sizeof(region))
                    break;

                const auto regionEnd = reinterpret_cast<std::uintptr_t>(region.BaseAddress) + region.RegionSize;
                if (regionEnd <= cursor)
                    break;

                const auto scanEnd = regionEnd < end ? regionEnd : end;
                if (scanEnd - cursor >= signatureLength && IsExecutable(reinterpret_cast<const std::uint8_t*>(cursor), signatureLength))
                {
                    for (auto position = cursor; position <= scanEnd - signatureLength; ++position)
                    {
                        const auto* bytes = reinterpret_cast<const std::uint8_t*>(position);
                        if (!MatchesSignature(bytes))
                            continue;

                        found = bytes;
                        if (++count > 1)
                            return nullptr;
                    }
                }
                cursor = scanEnd;
            }
        }
        return count == 1 ? found : nullptr;
    }

    std::uint8_t* DecodeBranch(const std::uint8_t* instruction)
    {
        std::int32_t displacement = 0;
        std::memcpy(&displacement, instruction + 1, sizeof(displacement));
        const auto destination = reinterpret_cast<std::intptr_t>(instruction + 5) + displacement;
        return reinterpret_cast<std::uint8_t*>(destination);
    }
}

namespace
{
    constexpr std::uint32_t textureConfigOriginalHash = 0x30E78D5F;
    constexpr std::uint32_t textureConfigDisabledHash = 0x7151CFAD;
    constexpr std::uintptr_t textureConfigDescriptorRva = 0x39BE880;
    std::uint32_t* textureConfigModifiedDescriptor = nullptr;
    ULONGLONG textureConfigSuppressionStartedAt = 0;

    constexpr std::uintptr_t graphicsConfigRegistrationRva = 0x39C6AF0;
    constexpr std::uintptr_t graphicsConfigMemberArrayRva = 0x33D0760;
    constexpr std::uintptr_t graphicsConfigOffsetTableRva = 0x33D20F8;

    bool IsGraphicsConfigLayoutValid(const std::uint8_t* image, const IMAGE_NT_HEADERS64* nt)
    {
        if (!image || !nt || graphicsConfigRegistrationRva + 36 > nt->OptionalHeader.SizeOfImage ||
            graphicsConfigMemberArrayRva + 7 * sizeof(std::uintptr_t) > nt->OptionalHeader.SizeOfImage ||
            graphicsConfigOffsetTableRva + 7 * sizeof(std::uint32_t) > nt->OptionalHeader.SizeOfImage)
            return false;

        const auto* registration = image + graphicsConfigRegistrationRva;
        std::uintptr_t members = 0;
        std::uintptr_t offsets = 0;
        std::uintptr_t texture = 0;
        std::uint32_t registrationHash = 0;
        std::memcpy(&members, registration, sizeof(members));
        std::memcpy(&offsets, registration + 8, sizeof(offsets));
        std::memcpy(&registrationHash, registration + 32, sizeof(registrationHash));
        std::memcpy(&texture, image + graphicsConfigMemberArrayRva + 6 * sizeof(texture), sizeof(texture));
        if (members != reinterpret_cast<std::uintptr_t>(image + graphicsConfigMemberArrayRva) ||
            offsets != reinterpret_cast<std::uintptr_t>(image + graphicsConfigOffsetTableRva) ||
            texture != reinterpret_cast<std::uintptr_t>(image + textureConfigDescriptorRva) ||
            registrationHash != 0xFBC958E3)
            return false;

        for (unsigned i = 0; i < 7; ++i)
        {
            std::uint32_t offset = 0;
            std::memcpy(&offset, image + graphicsConfigOffsetTableRva + i * sizeof(offset), sizeof(offset));
            if (offset != i * sizeof(std::uint32_t))
                return false;
        }
        return true;
    }

    bool IsTextureConfigDescriptorValid(const std::uint8_t* image,
        const IMAGE_NT_HEADERS64* nt, const std::uint8_t* candidate)
    {
        if (!image || !nt || !candidate ||
            textureConfigDescriptorRva > nt->OptionalHeader.SizeOfImage ||
            nt->OptionalHeader.SizeOfImage - textureConfigDescriptorRva < 68)
            return false;

        const auto* sections = IMAGE_FIRST_SECTION(nt);
        bool inInitializedData = false;
        for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        {
            const auto& section = sections[i];
            if (std::memcmp(section.Name, ".data", 5) != 0 ||
                (section.Characteristics & IMAGE_SCN_MEM_WRITE) == 0)
                continue;

            const std::size_t offset = section.VirtualAddress;
            const std::size_t size = section.SizeOfRawData;
            if (textureConfigDescriptorRva >= offset &&
                textureConfigDescriptorRva - offset <= size &&
                size - (textureConfigDescriptorRva - offset) >= 68)
                inInitializedData = true;
        }
        if (!inInitializedData)
            return false;

        MEMORY_BASIC_INFORMATION region = {};
        if (VirtualQuery(candidate, &region, sizeof(region)) != sizeof(region) ||
            region.State != MEM_COMMIT ||
            (region.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
            return false;
        const auto start = reinterpret_cast<std::uintptr_t>(candidate);
        const auto regionStart = reinterpret_cast<std::uintptr_t>(region.BaseAddress);
        if (start < regionStart || start - regionStart > region.RegionSize ||
            region.RegionSize - (start - regionStart) < 68)
            return false;

        constexpr std::uint8_t header[] =
        {
            0x5F, 0x8D, 0xE7, 0x30, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0x0D, 0x04, 0x09, 0x00, 0xFF, 0xFF, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
        };
        if (std::memcmp(candidate, header, sizeof(header)) != 0)
            return false;
        std::uint64_t firstSize = 0;
        std::uint64_t secondSize = 0;
        std::uintptr_t schema = 0;
        std::memcpy(&firstSize, candidate + 32, sizeof(firstSize));
        std::memcpy(&secondSize, candidate + 40, sizeof(secondSize));
        std::memcpy(&schema, candidate + 48, sizeof(schema));
        std::uint32_t adjacentHash = 0;
        std::memcpy(&adjacentHash, candidate + 64, sizeof(adjacentHash));
        return firstSize == 4 && secondSize == 4 &&
            schema == start - 0x60 && adjacentHash == 0xA796B8B4;
    }

    bool WriteTextureConfigHash(std::uint32_t* target, std::uint32_t expected,
        std::uint32_t replacement)
    {
        if (!target)
            return false;

        MEMORY_BASIC_INFORMATION region = {};
        if (VirtualQuery(target, &region, sizeof(region)) != sizeof(region) ||
            region.State != MEM_COMMIT || region.Type != MEM_IMAGE ||
            (region.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
            return false;
        const auto address = reinterpret_cast<std::uintptr_t>(target);
        const auto regionStart = reinterpret_cast<std::uintptr_t>(region.BaseAddress);
        if (address < regionStart || region.RegionSize < sizeof(*target) ||
            address - regionStart > region.RegionSize - sizeof(*target) ||
            (address & (alignof(std::uint32_t) - 1)) != 0)
            return false;

        DWORD oldProtection = 0;
        if (!VirtualProtect(target, sizeof(*target), PAGE_READWRITE, &oldProtection))
            return false;

        const LONG actual = InterlockedCompareExchange(
            reinterpret_cast<volatile LONG*>(target),
            static_cast<LONG>(replacement), static_cast<LONG>(expected));
        DWORD ignored = 0;
        const bool protectionRestored = VirtualProtect(target, sizeof(*target), oldProtection, &ignored) != FALSE;
        if (!protectionRestored && actual == static_cast<LONG>(expected))
            InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(target),
                static_cast<LONG>(expected), static_cast<LONG>(replacement));
        return actual == static_cast<LONG>(expected) && protectionRestored;
    }
}


namespace
{
    HANDLE gfxTraceFile = INVALID_HANDLE_VALUE;
    PVOID gfxExceptionHandler = nullptr;
    volatile LONG gfxTraceInHandler = 0;
    struct GFXExceptionKey
    {
        DWORD code;
        const void* address;
    };
    GFXExceptionKey gfxCapturedExceptions[24] = {};
    unsigned int gfxCapturedCount = 0;
    const char* const gfxKnownNames[] =
    {
        "RDR2.exe", "vulkan-1.dll", "d3d12.dll", "dxgi.dll", "nvngx_dlss.dll",
        "nvwgf2umx.dll", "amdvlk64.dll", "atidxx64.dll", "ScriptHookRDR2.dll",
        "FusionFixRDR2.asi", "ntdll.dll", "kernel32.dll", "KernelBase.dll"
    };
    PVOID volatile gfxKnownBases[sizeof(gfxKnownNames) / sizeof(gfxKnownNames[0])] = {};
    using LookupUnwindEntry = PRUNTIME_FUNCTION (WINAPI*)(DWORD64, PDWORD64, PUNWIND_HISTORY_TABLE);
    using UnwindFrame = PEXCEPTION_ROUTINE (WINAPI*)(DWORD, DWORD64, DWORD64,
        PRUNTIME_FUNCTION, PCONTEXT, PVOID*, PDWORD64, PKNONVOLATILE_CONTEXT_POINTERS);
    LookupUnwindEntry gfxLookupUnwindEntry = nullptr;
    UnwindFrame gfxUnwindFrame = nullptr;

    using GFXGetProcAddressFunction = FARPROC(WINAPI*)(HMODULE, LPCSTR);
    using GFXMessageBoxAFunction = int(WINAPI*)(HWND, LPCSTR, LPCSTR, UINT);
    using GFXMessageBoxWFunction = int(WINAPI*)(HWND, LPCWSTR, LPCWSTR, UINT);
    using GFXExitProcessFunction = void(WINAPI*)(UINT);
    using GFXTerminateProcessFunction = BOOL(WINAPI*)(HANDLE, UINT);
    using GFXCreateD3D12DeviceFunction = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
    using GFXD3D12GetDebugInterfaceFunction = HRESULT(WINAPI*)(REFIID, void**);
    using GFXCreateDXGIFactoryFunction = HRESULT(WINAPI*)(REFIID, void**);
    using GFXCreateDXGIFactory2Function = HRESULT(WINAPI*)(UINT, REFIID, void**);
    using GFXVulkanGetProcFunction = FARPROC(WINAPI*)(void*, const char*);
    using GFXVulkanSubmitFunction = std::int32_t(WINAPI*)(void*, std::uint32_t, const void*, void*);
    using GFXVulkanPresentFunction = std::int32_t(WINAPI*)(void*, const void*);
    using GFXVulkanAcquireFunction = std::int32_t(WINAPI*)(void*, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint32_t*);

    struct GFXImportHook
    {
        const char* name;
        void* replacement;
        PVOID volatile* slot = nullptr;
        void* original = nullptr;
    };
    FARPROC WINAPI GFXInterceptGetProcAddress(HMODULE, LPCSTR);
    int WINAPI GFXInterceptMessageBoxA(HWND, LPCSTR, LPCSTR, UINT);
    int WINAPI GFXInterceptMessageBoxW(HWND, LPCWSTR, LPCWSTR, UINT);
    void WINAPI GFXInterceptExitProcess(UINT);
    BOOL WINAPI GFXInterceptTerminateProcess(HANDLE, UINT);
    GFXImportHook gfxImports[] = {
        { "GetProcAddress", reinterpret_cast<void*>(&GFXInterceptGetProcAddress) },
        { "MessageBoxA", reinterpret_cast<void*>(&GFXInterceptMessageBoxA) },
        { "MessageBoxW", reinterpret_cast<void*>(&GFXInterceptMessageBoxW) },
        { "ExitProcess", reinterpret_cast<void*>(&GFXInterceptExitProcess) },
        { "TerminateProcess", reinterpret_cast<void*>(&GFXInterceptTerminateProcess) }
    };
    std::atomic<GFXGetProcAddressFunction> gfxOriginalGetProcAddress{ nullptr };
    std::atomic<GFXMessageBoxAFunction> gfxOriginalMessageBoxA{ nullptr };
    std::atomic<GFXMessageBoxWFunction> gfxOriginalMessageBoxW{ nullptr };
    std::atomic<GFXExitProcessFunction> gfxOriginalExitProcess{ nullptr };
    std::atomic<GFXTerminateProcessFunction> gfxOriginalTerminateProcess{ nullptr };
    std::atomic<GFXCreateD3D12DeviceFunction> gfxOriginalCreateD3D12Device{ nullptr };
    std::atomic<GFXCreateDXGIFactoryFunction> gfxOriginalCreateDXGIFactory{ nullptr };
    std::atomic<GFXCreateDXGIFactoryFunction> gfxOriginalCreateDXGIFactory1{ nullptr };
    std::atomic<GFXCreateDXGIFactory2Function> gfxOriginalCreateDXGIFactory2{ nullptr };

    std::atomic<GFXVulkanGetProcFunction> gfxOriginalVkGetInstanceProcAddr{ nullptr };
    std::atomic<GFXVulkanGetProcFunction> gfxOriginalVkGetDeviceProcAddr{ nullptr };
    std::atomic<GFXVulkanSubmitFunction> gfxOriginalVkQueueSubmit{ nullptr };
    std::atomic<GFXVulkanSubmitFunction> gfxOriginalVkQueueSubmit2{ nullptr };
    std::atomic<GFXVulkanPresentFunction> gfxOriginalVkQueuePresent{ nullptr };
    std::atomic<GFXVulkanAcquireFunction> gfxOriginalVkAcquireNextImage{ nullptr };
    PVOID volatile gfxTrackedD3DDevice = nullptr;
    volatile LONG gfxExitCaptured = 0;
    volatile LONG gfxAPIErrorCount = 0;
    volatile LONG gfxD3D12CreateCount = 0;
    volatile LONG gfxVulkanLookupCount = 0;
    volatile LONG gfxDredConfigured = 0;
    volatile LONG gfxDXGIFactoryLookups = 0;
    volatile LONG gfxDXGIFactoryCreates = 0;
    volatile LONG gfxDXGISwapChainCreates = 0;
    volatile LONG gfxDXGIResizeCalls = 0;
    volatile LONG gfxDXGIPresentFailures = 0;
    volatile LONG gfxDXGIHooksInstalled = 0;
    volatile LONG gfxDredAttempted = 0;
    volatile LONG64 gfxPresentStarted = 0;
    volatile LONG64 gfxPresentFinished = 0;
    volatile LONG64 gfxLastPresentStartTick = 0;
    volatile LONG64 gfxLastPresentFinishTick = 0;
    volatile LONG gfxPresentInFlight = 0;
    volatile LONG gfxPresentThread = 0;
    volatile LONG64 gfxScriptUpdateCount = 0;
    volatile LONG64 gfxLastScriptTick = 0;
    volatile LONG gfxScriptThread = 0;
    volatile LONG gfxWatchdogEnabled = 0;
    HANDLE gfxWatchdogStop = nullptr;
    HANDLE gfxWatchdogThread = nullptr;
    void GFXConfigureDRED();
    struct GFXDXGIVTablePatch
    {
        void** slot = nullptr;
        void* original = nullptr;
        void* replacement = nullptr;
    };
    GFXDXGIVTablePatch gfxDXGIVTables[96] = {};
    volatile LONG gfxDXGIVTableCount = 0;
    SRWLOCK gfxDXGITableLock = SRWLOCK_INIT;


    bool WriteGFXTrace(const char* message)
    {
        if (gfxTraceFile == INVALID_HANDLE_VALUE || message == nullptr)
            return false;

        const DWORD length = static_cast<DWORD>(std::strlen(message));
        DWORD written = 0;
        return WriteFile(gfxTraceFile, message, length, &written, nullptr) && written == length;
    }

    void ReportGraphicsModules(const char* phase)
    {
        char line[512] = {};
        int length = std::snprintf(line, sizeof(line), "[MODULES] %s", phase);
        for (unsigned int i = 0; i < sizeof(gfxKnownNames) / sizeof(gfxKnownNames[0]); ++i)
        {
            const HMODULE module = i == 0 ? GetModuleHandleW(nullptr) : GetModuleHandleA(gfxKnownNames[i]);
            InterlockedExchangePointer(&gfxKnownBases[i], module);
            if (length < 0 || length >= static_cast<int>(sizeof(line)))
                break;
            length += std::snprintf(line + length, sizeof(line) - length, " %s=%s",
                gfxKnownNames[i], module ? "loaded" : "absent");
        }
        if (length >= 0 && length < static_cast<int>(sizeof(line) - 2))
        {
            line[length++] = '\r';
            line[length++] = '\n';
            line[length] = '\0';
            WriteGFXTrace(line);
        }
    }

    void DescribeCrashLocation(const void* address, char (&buffer)[128])
    {
        MEMORY_BASIC_INFORMATION region = {};
        if (!address || VirtualQuery(address, &region, sizeof(region)) != sizeof(region) ||
            region.Type != MEM_IMAGE)
        {
            std::snprintf(buffer, sizeof(buffer), "0x%llX (unknown)",
                static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(address)));
            return;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(region.AllocationBase);
        const auto offset = reinterpret_cast<std::uintptr_t>(address) - base;
        for (unsigned int i = 0; i < sizeof(gfxKnownNames) / sizeof(gfxKnownNames[0]); ++i)
        {
            if (InterlockedCompareExchangePointer(&gfxKnownBases[i], nullptr, nullptr) == region.AllocationBase)
            {
                std::snprintf(buffer, sizeof(buffer), "%s+0x%llX", gfxKnownNames[i],
                    static_cast<unsigned long long>(offset));
                return;
            }
        }
        std::snprintf(buffer, sizeof(buffer), "imageBase=0x%llX+0x%llX",
            static_cast<unsigned long long>(base), static_cast<unsigned long long>(offset));
    }

    LONG CALLBACK TraceGraphicsException(PEXCEPTION_POINTERS details)
    {
        if (!details || !details->ExceptionRecord || !details->ContextRecord)
            return EXCEPTION_CONTINUE_SEARCH;

        const DWORD code = details->ExceptionRecord->ExceptionCode;
        switch (code)
        {
        case EXCEPTION_ACCESS_VIOLATION:
        case EXCEPTION_IN_PAGE_ERROR:
        case EXCEPTION_ILLEGAL_INSTRUCTION:
        case EXCEPTION_PRIV_INSTRUCTION:
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:
        case EXCEPTION_FLT_INVALID_OPERATION:
        case EXCEPTION_STACK_OVERFLOW:
        case 0xC0000374:
        case 0xC0000409:
        case 0xC0000417:
        case 0xC000041D:
        case 0xC0000602:
            break;
        default:
            return EXCEPTION_CONTINUE_SEARCH;
        }

        if (InterlockedCompareExchange(&gfxTraceInHandler, 1, 0) != 0)
            return EXCEPTION_CONTINUE_SEARCH;

        const CONTEXT* context = details->ContextRecord;
        const void* address = details->ExceptionRecord->ExceptionAddress;
        for (unsigned int i = 0; i < gfxCapturedCount; ++i)
        {
            if (gfxCapturedExceptions[i].code == code && gfxCapturedExceptions[i].address == address)
            {
                InterlockedExchange(&gfxTraceInHandler, 0);
                return EXCEPTION_CONTINUE_SEARCH;
            }
        }
        if (gfxCapturedCount == 24)
        {
            InterlockedExchange(&gfxTraceInHandler, 0);
            return EXCEPTION_CONTINUE_SEARCH;
        }
        gfxCapturedExceptions[gfxCapturedCount] = { code, address };
        const unsigned int count = ++gfxCapturedCount;
        MEMORY_BASIC_INFORMATION region = {};
        const bool mapped = address && VirtualQuery(address, &region, sizeof(region)) == sizeof(region);
        const auto base = mapped && region.Type == MEM_IMAGE ?
            reinterpret_cast<std::uintptr_t>(region.AllocationBase) : 0;
        const char* moduleName = "unknown";
        for (unsigned int i = 0; i < sizeof(gfxKnownNames) / sizeof(gfxKnownNames[0]); ++i)
        {
            const PVOID module = InterlockedCompareExchangePointer(&gfxKnownBases[i], nullptr, nullptr);
            if (module && reinterpret_cast<std::uintptr_t>(module) == base)
            {
                moduleName = gfxKnownNames[i];
                break;
            }
        }

        ULONG_PTR operation = 0;
        ULONG_PTR memoryAddress = 0;
        if ((code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR) &&
            details->ExceptionRecord->NumberParameters >= 2)
        {
            operation = details->ExceptionRecord->ExceptionInformation[0];
            memoryAddress = details->ExceptionRecord->ExceptionInformation[1];
        }

        char line[1024] = {};
        const int length = std::snprintf(line, sizeof(line),
            "[FIRST_CHANCE] event=%u thread=%lu code=0x%08lX flags=0x%08lX "
            "rip=0x%llX rsp=0x%llX rbp=0x%llX module=%s imageBase=0x%llX offset=0x%llX "
            "operation=%llu target=0x%llX tick=%llu\r\n",
            count, GetCurrentThreadId(), static_cast<unsigned long>(code),
            static_cast<unsigned long>(details->ExceptionRecord->ExceptionFlags),
            static_cast<unsigned long long>(context->Rip),
            static_cast<unsigned long long>(context->Rsp),
            static_cast<unsigned long long>(context->Rbp),
            moduleName, static_cast<unsigned long long>(base),
            static_cast<unsigned long long>(base ? reinterpret_cast<std::uintptr_t>(address) - base : 0),
            static_cast<unsigned long long>(operation),
            static_cast<unsigned long long>(memoryAddress),
            static_cast<unsigned long long>(GetTickCount64()));
        if (length > 0)
            WriteGFXTrace(line);
        std::snprintf(line, sizeof(line),
            "[REGISTERS] event=%u rax=0x%llX rbx=0x%llX rcx=0x%llX rdx=0x%llX "
            "rsi=0x%llX rdi=0x%llX r8=0x%llX r9=0x%llX r10=0x%llX r11=0x%llX\r\n",
            count, static_cast<unsigned long long>(context->Rax),
            static_cast<unsigned long long>(context->Rbx),
            static_cast<unsigned long long>(context->Rcx),
            static_cast<unsigned long long>(context->Rdx),
            static_cast<unsigned long long>(context->Rsi),
            static_cast<unsigned long long>(context->Rdi),
            static_cast<unsigned long long>(context->R8),
            static_cast<unsigned long long>(context->R9),
            static_cast<unsigned long long>(context->R10),
            static_cast<unsigned long long>(context->R11));
        WriteGFXTrace(line);

        if ((code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR) &&
            details->ExceptionRecord->NumberParameters >= 2)
        {
            MEMORY_BASIC_INFORMATION targetRegion = {};
            const bool targetMapped = memoryAddress &&
                VirtualQuery(reinterpret_cast<const void*>(memoryAddress), &targetRegion,
                    sizeof(targetRegion)) == sizeof(targetRegion);
            std::snprintf(line, sizeof(line),
                "[ACCESS] event=%u operation=%s address=0x%llX state=0x%lX protect=0x%lX type=0x%lX\r\n",
                count, operation == 0 ? "read" : operation == 1 ? "write" :
                    operation == 8 ? "execute" : "other",
                static_cast<unsigned long long>(memoryAddress),
                static_cast<unsigned long>(targetMapped ? targetRegion.State : 0),
                static_cast<unsigned long>(targetMapped ? targetRegion.Protect : 0),
                static_cast<unsigned long>(targetMapped ? targetRegion.Type : 0));
            WriteGFXTrace(line);
        }

        if (gfxLookupUnwindEntry && gfxUnwindFrame)
        {
            CONTEXT frame = *context;
            __try
            {
                for (unsigned int i = 0; i < 10 && frame.Rip; ++i)
                {
                    char location[128] = {};
                    DescribeCrashLocation(reinterpret_cast<const void*>(frame.Rip), location);
                    std::snprintf(line, sizeof(line),
                        "[FRAME] event=%u index=%u rip=0x%llX rsp=0x%llX location=%s\r\n",
                        count, i, static_cast<unsigned long long>(frame.Rip),
                        static_cast<unsigned long long>(frame.Rsp), location);
                    WriteGFXTrace(line);
                    const DWORD64 previousRip = frame.Rip;
                    const DWORD64 previousRsp = frame.Rsp;
                    DWORD64 imageBase = 0;
                    PRUNTIME_FUNCTION entry = gfxLookupUnwindEntry(frame.Rip, &imageBase, nullptr);
                    if (entry)
                    {
                        void* handlerData = nullptr;
                        DWORD64 establisherFrame = 0;
                        gfxUnwindFrame(0, imageBase, frame.Rip, entry, &frame,
                            &handlerData, &establisherFrame, nullptr);
                    }
                    else
                    {
                        DWORD64 returnAddress = 0;
                        SIZE_T bytesRead = 0;
                        if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(frame.Rsp),
                            &returnAddress, sizeof(returnAddress), &bytesRead) ||
                            bytesRead != sizeof(returnAddress))
                            break;
                        frame.Rip = returnAddress;
                        frame.Rsp += sizeof(returnAddress);
                    }
                    if (frame.Rip == previousRip || frame.Rsp <= previousRsp)
                        break;
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                WriteGFXTrace("[STACK] Unwind interrupted by an exception.\r\n");
            }
        }
        if (count == 24)
            WriteGFXTrace("[INFO] First-chance exception capture limit reached (24 unique events).\r\n");
        FlushFileBuffers(gfxTraceFile);
        InterlockedExchange(&gfxTraceInHandler, 0);
        return EXCEPTION_CONTINUE_SEARCH;
    }
    FARPROC WINAPI GFXInterceptVkGetInstanceProcAddr(void*, const char*);
    FARPROC WINAPI GFXInterceptVkGetDeviceProcAddr(void*, const char*);

    void GFXFlushTrace()
    {
        if (gfxTraceFile != INVALID_HANDLE_VALUE)
            FlushFileBuffers(gfxTraceFile);
    }

    void GFXReportAPIResult(const char* api, long result)
    {
        const LONG count = InterlockedIncrement(&gfxAPIErrorCount);
        if (count > 16)
            return;
        char line[256] = {};
        std::snprintf(line, sizeof(line), "[GRAPHICS_API_ERROR] api=%s result=0x%08lX thread=%lu tick=%llu\r\n",
            api, static_cast<unsigned long>(result), GetCurrentThreadId(),
            static_cast<unsigned long long>(GetTickCount64()));
        WriteGFXTrace(line);
        GFXFlushTrace();
    }

    void GFXCaptureDX12Device(const char* context)
    {
        auto* device = static_cast<ID3D12Device*>(InterlockedCompareExchangePointer(&gfxTrackedD3DDevice, nullptr, nullptr));
        if (!device)
        {
            WriteGFXTrace("[DX12] No ID3D12Device captured; DRED/device-removal status unavailable.\r\n");
            return;
        }
        __try
        {
            const HRESULT removed = device->GetDeviceRemovedReason();
            char line[320] = {};
            std::snprintf(line, sizeof(line), "[DX12] context=%s deviceRemovedReason=0x%08lX\r\n",
                context, static_cast<unsigned long>(removed));
            WriteGFXTrace(line);
            if (FAILED(removed))
            {
                ID3D12DeviceRemovedExtendedData* dred = nullptr;
                const HRESULT queried = device->QueryInterface(__uuidof(ID3D12DeviceRemovedExtendedData),
                    reinterpret_cast<void**>(&dred));
                if (SUCCEEDED(queried) && dred)
                {
                    D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT breadcrumbs = {};
                    const HRESULT history = dred->GetAutoBreadcrumbsOutput(&breadcrumbs);
                    std::snprintf(line, sizeof(line), "[DX12_DRED] breadcrumbs=0x%08lX\r\n",
                        static_cast<unsigned long>(history));
                    WriteGFXTrace(line);
                    if (SUCCEEDED(history))
                    {
                        const D3D12_AUTO_BREADCRUMB_NODE* node = breadcrumbs.pHeadAutoBreadcrumbNode;
                        for (unsigned int i = 0; node && i < 12; ++i, node = node->pNext)
                        {
                            std::snprintf(line, sizeof(line),
                                "[DX12_DRED] commandList=%p queue=%p ops=%u completed=%u\r\n",
                                static_cast<void*>(node->pCommandList), static_cast<void*>(node->pCommandQueue),
                                node->BreadcrumbCount,
                                node->pLastBreadcrumbValue ? *node->pLastBreadcrumbValue : 0);
                            WriteGFXTrace(line);
                        }
                    }
                    D3D12_DRED_PAGE_FAULT_OUTPUT fault = {};
                    const HRESULT page = dred->GetPageFaultAllocationOutput(&fault);
                    std::snprintf(line, sizeof(line),
                        "[DX12_DRED] pageFaultQuery=0x%08lX gpuVA=0x%llX existing=%p freed=%p\r\n",
                        static_cast<unsigned long>(page),
                        static_cast<unsigned long long>(fault.PageFaultVA),
                        fault.pHeadExistingAllocationNode, fault.pHeadRecentFreedAllocationNode);
                    WriteGFXTrace(line);
                    dred->Release();
                }
                else
                    WriteGFXTrace("[DX12_DRED] DeviceRemovedExtendedData interface unavailable.\r\n");
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            WriteGFXTrace("[DX12] Error querying device-removal diagnostics.\r\n");
        }
    }

    void GFXReportDXGIStatus(const char* operation, void* object, HRESULT result,
        UINT buffers, UINT width, UINT height, UINT format, UINT flags)
    {
        char line[320] = {};
        std::snprintf(line, sizeof(line),
            "[DXGI] %s swapchain=%p hr=0x%08lX buffers=%u size=%ux%u format=%u flags=0x%X thread=%lu tick=%llu\r\n",
            operation, object, static_cast<unsigned long>(result), buffers, width, height, format,
            flags, GetCurrentThreadId(), static_cast<unsigned long long>(GetTickCount64()));
        WriteGFXTrace(line);
        GFXFlushTrace();
    }

    void GFXReportDXGIPresent(const char* operation, void* chain, HRESULT result, UINT sync, UINT flags)
    {
        char line[256] = {};
        std::snprintf(line, sizeof(line),
            "[DXGI] %s swapchain=%p hr=0x%08lX syncInterval=%u presentFlags=0x%X thread=%lu tick=%llu\r\n",
            operation, chain, static_cast<unsigned long>(result), sync, flags,
            GetCurrentThreadId(), static_cast<unsigned long long>(GetTickCount64()));
        WriteGFXTrace(line);
        GFXFlushTrace();
    }

    void GFXReportDXGICounters(const char* phase)
    {
        char line[320] = {};
        std::snprintf(line, sizeof(line),
            "[DXGI_STATUS] %s factoryLookups=%ld factories=%ld swapchains=%ld resizeCalls=%ld presentFailures=%ld vtableHooks=%ld presentStarted=%lld presentFinished=%lld inFlight=%ld\r\n",
            phase,
            InterlockedCompareExchange(&gfxDXGIFactoryLookups, 0, 0),
            InterlockedCompareExchange(&gfxDXGIFactoryCreates, 0, 0),
            InterlockedCompareExchange(&gfxDXGISwapChainCreates, 0, 0),
            InterlockedCompareExchange(&gfxDXGIResizeCalls, 0, 0),
            InterlockedCompareExchange(&gfxDXGIPresentFailures, 0, 0),
            InterlockedCompareExchange(&gfxDXGIHooksInstalled, 0, 0),
            static_cast<long long>(InterlockedCompareExchange64(&gfxPresentStarted, 0, 0)),
            static_cast<long long>(InterlockedCompareExchange64(&gfxPresentFinished, 0, 0)),
            InterlockedCompareExchange(&gfxPresentInFlight, 0, 0));
        WriteGFXTrace(line);
    }

    bool GFXPatchDXGIVTable(void* object, std::size_t index, void* replacement)
    {
        if (!object || !replacement)
            return false;
        void** table = *reinterpret_cast<void***>(object);
        if (!table)
            return false;
        void** slot = &table[index];
        AcquireSRWLockExclusive(&gfxDXGITableLock);
        const LONG count = InterlockedCompareExchange(&gfxDXGIVTableCount, 0, 0);
        for (LONG i = 0; i < count; ++i)
        {
            if (gfxDXGIVTables[i].slot == slot)
            {
                const bool installed = gfxDXGIVTables[i].replacement == replacement;
                ReleaseSRWLockExclusive(&gfxDXGITableLock);
                return installed;
            }
        }
        if (count >= static_cast<LONG>(sizeof(gfxDXGIVTables) / sizeof(gfxDXGIVTables[0])))
        {
            ReleaseSRWLockExclusive(&gfxDXGITableLock);
            return false;
        }
        void* original = *slot;
        if (!original || original == replacement)
        {
            ReleaseSRWLockExclusive(&gfxDXGITableLock);
            return false;
        }
        DWORD protection = 0;
        if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &protection))
        {
            ReleaseSRWLockExclusive(&gfxDXGITableLock);
            return false;
        }
        gfxDXGIVTables[count] = { slot, original, replacement };
        InterlockedExchange(&gfxDXGIVTableCount, count + 1);
        const void* previous = InterlockedCompareExchangePointer(
            reinterpret_cast<PVOID volatile*>(slot), replacement, original);
        DWORD ignored = 0;
        VirtualProtect(slot, sizeof(void*), protection, &ignored);
        if (previous != original)
        {
            InterlockedExchange(&gfxDXGIVTableCount, count);
            gfxDXGIVTables[count] = {};
        }
        else
            InterlockedIncrement(&gfxDXGIHooksInstalled);
        ReleaseSRWLockExclusive(&gfxDXGITableLock);
        return previous == original;
    }

    template<typename Function>
    Function GFXDXGIOriginal(void* object, std::size_t index)
    {
        if (!object)
            return nullptr;
        void** table = *reinterpret_cast<void***>(object);
        if (!table)
            return nullptr;
        void** slot = &table[index];
        const LONG count = InterlockedCompareExchange(&gfxDXGIVTableCount, 0, 0);
        for (LONG i = 0; i < count; ++i)
        {
            if (gfxDXGIVTables[i].slot == slot)
                return reinterpret_cast<Function>(gfxDXGIVTables[i].original);
        }
        return nullptr;
    }

    using GFXPresentFunction = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
    using GFXPresent1Function = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
    using GFXResizeFunction = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
    using GFXResize1Function = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3*, UINT, UINT, UINT, DXGI_FORMAT, UINT, const UINT*, IUnknown* const*);
    using GFXFullscreenFunction = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, BOOL, IDXGIOutput*);
    using GFXResizeTargetFunction = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, const DXGI_MODE_DESC*);
    using GFXSwapLatencyFunction = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain2*, UINT);

    using GFXCreateSwapFunction = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);
    using GFXCreateSwapHwndFunction = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*, IUnknown*, HWND,
        const DXGI_SWAP_CHAIN_DESC1*, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*, IDXGISwapChain1**);
    using GFXCreateSwapCoreFunction = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*, IUnknown*, IUnknown*,
        const DXGI_SWAP_CHAIN_DESC1*, IDXGIOutput*, IDXGISwapChain1**);
    using GFXCreateSwapCompositionFunction = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*, IUnknown*,
        const DXGI_SWAP_CHAIN_DESC1*, IDXGIOutput*, IDXGISwapChain1**);

    void GFXPresentBegin()
    {
        InterlockedExchange64(&gfxLastPresentStartTick, static_cast<LONG64>(GetTickCount64()));
        InterlockedExchange(&gfxPresentThread, static_cast<LONG>(GetCurrentThreadId()));
        InterlockedIncrement(&gfxPresentInFlight);
        InterlockedIncrement64(&gfxPresentStarted);
    }

    void GFXPresentEnd()
    {
        InterlockedExchange64(&gfxLastPresentFinishTick, static_cast<LONG64>(GetTickCount64()));
        InterlockedIncrement64(&gfxPresentFinished);
        InterlockedDecrement(&gfxPresentInFlight);
    }

    HRESULT STDMETHODCALLTYPE GFXDXGIPresent(IDXGISwapChain* chain, UINT sync, UINT flags)
    {
        const auto original = GFXDXGIOriginal<GFXPresentFunction>(chain, 8);
        if (!original)
            return E_FAIL;
        GFXPresentBegin();
        const HRESULT result = original(chain, sync, flags);
        GFXPresentEnd();
        if (FAILED(result) && InterlockedIncrement(&gfxDXGIPresentFailures) <= 20)
        {
            GFXReportDXGIPresent("Present FAILED", chain, result, sync, flags);
            if (result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET ||
                result == DXGI_ERROR_DEVICE_HUNG)
                GFXCaptureDX12Device("Present failure");
        }
        return result;
    }

    HRESULT STDMETHODCALLTYPE GFXDXGIPresent1(IDXGISwapChain1* chain, UINT sync, UINT flags,
        const DXGI_PRESENT_PARAMETERS* parameters)
    {
        const auto original = GFXDXGIOriginal<GFXPresent1Function>(chain, 22);
        if (!original)
            return E_FAIL;
        GFXPresentBegin();
        const HRESULT result = original(chain, sync, flags, parameters);
        GFXPresentEnd();
        if (FAILED(result) && InterlockedIncrement(&gfxDXGIPresentFailures) <= 20)
        {
            GFXReportDXGIPresent("Present1 FAILED", chain, result, sync, flags);
            if (result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET ||
                result == DXGI_ERROR_DEVICE_HUNG)
                GFXCaptureDX12Device("Present1 failure");
        }
        return result;
    }

    HRESULT STDMETHODCALLTYPE GFXDXGIResize(IDXGISwapChain* chain, UINT buffers, UINT width,
        UINT height, DXGI_FORMAT format, UINT flags)
    {
        const auto original = GFXDXGIOriginal<GFXResizeFunction>(chain, 13);
        if (!original)
            return E_FAIL;
        InterlockedIncrement(&gfxDXGIResizeCalls);
        GFXReportDXGIStatus("ResizeBuffers BEGIN", chain, S_OK, buffers, width, height, format, flags);
        const HRESULT result = original(chain, buffers, width, height, format, flags);
        GFXReportDXGIStatus("ResizeBuffers END", chain, result, buffers, width, height, format, flags);
        return result;
    }

    HRESULT STDMETHODCALLTYPE GFXDXGIResize1(IDXGISwapChain3* chain, UINT buffers, UINT width,
        UINT height, DXGI_FORMAT format, UINT flags, const UINT* nodes, IUnknown* const* queues)
    {
        const auto original = GFXDXGIOriginal<GFXResize1Function>(chain, 39);
        if (!original)
            return E_FAIL;
        InterlockedIncrement(&gfxDXGIResizeCalls);
        GFXReportDXGIStatus("ResizeBuffers1 BEGIN", chain, S_OK, buffers, width, height, format, flags);
        const HRESULT result = original(chain, buffers, width, height, format, flags, nodes, queues);
        GFXReportDXGIStatus("ResizeBuffers1 END", chain, result, buffers, width, height, format, flags);
        return result;
    }

    HRESULT STDMETHODCALLTYPE GFXDXGIFullscreen(IDXGISwapChain* chain, BOOL fullscreen, IDXGIOutput* output)
    {
        const auto original = GFXDXGIOriginal<GFXFullscreenFunction>(chain, 10);
        if (!original)
            return E_FAIL;
        const HRESULT result = original(chain, fullscreen, output);
        GFXReportDXGIStatus(fullscreen ? "SetFullscreenState ON" : "SetFullscreenState OFF",
            chain, result, 0, 0, 0, 0, 0);
        return result;
    }

    HRESULT STDMETHODCALLTYPE GFXDXGIResizeTarget(IDXGISwapChain* chain, const DXGI_MODE_DESC* target)
    {
        const auto original = GFXDXGIOriginal<GFXResizeTargetFunction>(chain, 14);
        if (!original)
            return E_FAIL;
        GFXReportDXGIStatus("ResizeTarget BEGIN", chain, S_OK, 0,
            target ? target->Width : 0, target ? target->Height : 0,
            target ? static_cast<UINT>(target->Format) : 0, 0);
        const HRESULT result = original(chain, target);
        GFXReportDXGIStatus("ResizeTarget END", chain, result, 0,
            target ? target->Width : 0, target ? target->Height : 0,
            target ? static_cast<UINT>(target->Format) : 0, 0);
        return result;
    }

    HRESULT STDMETHODCALLTYPE GFXDXGISetLatency(IDXGISwapChain2* chain, UINT latency)
    {
        const auto original = GFXDXGIOriginal<GFXSwapLatencyFunction>(chain, 31);
        if (!original)
            return E_FAIL;
        const HRESULT result = original(chain, latency);
        char line[220] = {};
        std::snprintf(line, sizeof(line),
            "[DXGI] SetMaximumFrameLatency swapchain=%p hr=0x%08lX latency=%u thread=%lu\r\n",
            chain, static_cast<unsigned long>(result), latency, GetCurrentThreadId());
        WriteGFXTrace(line);
        GFXFlushTrace();
        return result;
    }

    void GFXObserveSwapChain(IDXGISwapChain* chain, const char* source)
    {
        if (!chain)
            return;
        const LONG count = InterlockedIncrement(&gfxDXGISwapChainCreates);
        IDXGISwapChain3* chain3 = nullptr;
        IDXGISwapChain2* chain2 = nullptr;
        IDXGISwapChain1* chain1 = nullptr;
        if (SUCCEEDED(chain->QueryInterface(__uuidof(IDXGISwapChain3),
            reinterpret_cast<void**>(&chain3))) && chain3)
        {
            GFXPatchDXGIVTable(chain3, 39, reinterpret_cast<void*>(&GFXDXGIResize1));
            GFXPatchDXGIVTable(chain3, 22, reinterpret_cast<void*>(&GFXDXGIPresent1));
        }
        if (SUCCEEDED(chain->QueryInterface(__uuidof(IDXGISwapChain2),
            reinterpret_cast<void**>(&chain2))) && chain2)
            GFXPatchDXGIVTable(chain2, 31, reinterpret_cast<void*>(&GFXDXGISetLatency));
        if (SUCCEEDED(chain->QueryInterface(__uuidof(IDXGISwapChain1),
            reinterpret_cast<void**>(&chain1))) && chain1)
            GFXPatchDXGIVTable(chain1, 22, reinterpret_cast<void*>(&GFXDXGIPresent1));
        GFXPatchDXGIVTable(chain, 8, reinterpret_cast<void*>(&GFXDXGIPresent));
        GFXPatchDXGIVTable(chain, 13, reinterpret_cast<void*>(&GFXDXGIResize));
        GFXPatchDXGIVTable(chain, 10, reinterpret_cast<void*>(&GFXDXGIFullscreen));
        GFXPatchDXGIVTable(chain, 14, reinterpret_cast<void*>(&GFXDXGIResizeTarget));
        DXGI_SWAP_CHAIN_DESC desc = {};
        const HRESULT result = chain->GetDesc(&desc);
        if (count <= 12)
        {
            GFXReportDXGIStatus(source, chain, result,
                SUCCEEDED(result) ? desc.BufferCount : 0,
                SUCCEEDED(result) ? desc.BufferDesc.Width : 0,
                SUCCEEDED(result) ? desc.BufferDesc.Height : 0,
                SUCCEEDED(result) ? static_cast<UINT>(desc.BufferDesc.Format) : 0,
                SUCCEEDED(result) ? desc.Flags : 0);
        }
        if (chain1)
            chain1->Release();
        if (chain2)
            chain2->Release();
        if (chain3)
            chain3->Release();
    }

    HRESULT STDMETHODCALLTYPE GFXDXGICreateSwap(IDXGIFactory* factory, IUnknown* device,
        DXGI_SWAP_CHAIN_DESC* desc, IDXGISwapChain** output)
    {
        const auto original = GFXDXGIOriginal<GFXCreateSwapFunction>(factory, 10);
        if (!original)
            return E_FAIL;
        const HRESULT result = original(factory, device, desc, output);
        if (SUCCEEDED(result) && output && *output)
            GFXObserveSwapChain(*output, "CreateSwapChain");
        else if (FAILED(result))
            GFXReportDXGIStatus("CreateSwapChain FAILED", nullptr, result, desc ? desc->BufferCount : 0,
                desc ? desc->BufferDesc.Width : 0, desc ? desc->BufferDesc.Height : 0,
                desc ? static_cast<UINT>(desc->BufferDesc.Format) : 0, desc ? desc->Flags : 0);
        return result;
    }

    HRESULT STDMETHODCALLTYPE GFXDXGICreateSwapHwnd(IDXGIFactory2* factory, IUnknown* device,
        HWND window, const DXGI_SWAP_CHAIN_DESC1* desc,
        const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreen, IDXGIOutput* outputTarget, IDXGISwapChain1** output)
    {
        const auto original = GFXDXGIOriginal<GFXCreateSwapHwndFunction>(factory, 15);
        if (!original)
            return E_FAIL;
        const HRESULT result = original(factory, device, window, desc, fullscreen, outputTarget, output);
        if (SUCCEEDED(result) && output && *output)
            GFXObserveSwapChain(*output, "CreateSwapChainForHwnd");
        else if (FAILED(result))
            GFXReportDXGIStatus("CreateSwapChainForHwnd FAILED", nullptr, result,
                desc ? desc->BufferCount : 0, desc ? desc->Width : 0, desc ? desc->Height : 0,
                desc ? static_cast<UINT>(desc->Format) : 0, desc ? desc->Flags : 0);
        return result;
    }

    HRESULT STDMETHODCALLTYPE GFXDXGICreateSwapCore(IDXGIFactory2* factory, IUnknown* device,
        IUnknown* window, const DXGI_SWAP_CHAIN_DESC1* desc, IDXGIOutput* target, IDXGISwapChain1** output)
    {
        const auto original = GFXDXGIOriginal<GFXCreateSwapCoreFunction>(factory, 16);
        if (!original)
            return E_FAIL;
        const HRESULT result = original(factory, device, window, desc, target, output);
        if (SUCCEEDED(result) && output && *output)
            GFXObserveSwapChain(*output, "CreateSwapChainForCoreWindow");
        else if (FAILED(result))
            GFXReportDXGIStatus("CreateSwapChainForCoreWindow FAILED", nullptr, result,
                desc ? desc->BufferCount : 0, desc ? desc->Width : 0, desc ? desc->Height : 0,
                desc ? static_cast<UINT>(desc->Format) : 0, desc ? desc->Flags : 0);
        return result;
    }

    HRESULT STDMETHODCALLTYPE GFXDXGICreateSwapComposition(IDXGIFactory2* factory, IUnknown* device,
        const DXGI_SWAP_CHAIN_DESC1* desc, IDXGIOutput* target, IDXGISwapChain1** output)
    {
        const auto original = GFXDXGIOriginal<GFXCreateSwapCompositionFunction>(factory, 24);
        if (!original)
            return E_FAIL;
        const HRESULT result = original(factory, device, desc, target, output);
        if (SUCCEEDED(result) && output && *output)
            GFXObserveSwapChain(*output, "CreateSwapChainForComposition");
        else if (FAILED(result))
            GFXReportDXGIStatus("CreateSwapChainForComposition FAILED", nullptr, result,
                desc ? desc->BufferCount : 0, desc ? desc->Width : 0, desc ? desc->Height : 0,
                desc ? static_cast<UINT>(desc->Format) : 0, desc ? desc->Flags : 0);
        return result;
    }

    void GFXObserveDXGIFactory(IUnknown* factory, const char* source)
    {
        if (!factory)
            return;
        InterlockedIncrement(&gfxDXGIFactoryCreates);
        IDXGIFactory2* factory2 = nullptr;
        if (SUCCEEDED(factory->QueryInterface(__uuidof(IDXGIFactory2),
            reinterpret_cast<void**>(&factory2))) && factory2)
        {
            GFXPatchDXGIVTable(factory2, 15, reinterpret_cast<void*>(&GFXDXGICreateSwapHwnd));
            GFXPatchDXGIVTable(factory2, 16, reinterpret_cast<void*>(&GFXDXGICreateSwapCore));
            GFXPatchDXGIVTable(factory2, 24, reinterpret_cast<void*>(&GFXDXGICreateSwapComposition));
            factory2->Release();
        }
        IDXGIFactory* base = nullptr;
        if (SUCCEEDED(factory->QueryInterface(__uuidof(IDXGIFactory),
            reinterpret_cast<void**>(&base))) && base)
        {
            GFXPatchDXGIVTable(base, 10, reinterpret_cast<void*>(&GFXDXGICreateSwap));
            base->Release();
        }
        char line[220] = {};
        std::snprintf(line, sizeof(line), "[DXGI] %s factory=%p factory2=%s hooks=%ld\r\n",
            source, factory, factory2 ? "supported" : "unavailable",
            InterlockedCompareExchange(&gfxDXGIHooksInstalled, 0, 0));
        WriteGFXTrace(line);
        GFXFlushTrace();
    }

    HRESULT WINAPI GFXInterceptCreateDXGIFactory(REFIID iid, void** output)
    {
        const auto original = gfxOriginalCreateDXGIFactory.load();
        if (!original)
            return E_FAIL;
        const HRESULT result = original(iid, output);
        if (SUCCEEDED(result) && output && *output)
            GFXObserveDXGIFactory(static_cast<IUnknown*>(*output), "CreateDXGIFactory");
        else if (FAILED(result))
            GFXReportAPIResult("CreateDXGIFactory", result);
        return result;
    }

    HRESULT WINAPI GFXInterceptCreateDXGIFactory1(REFIID iid, void** output)
    {
        const auto original = gfxOriginalCreateDXGIFactory1.load();
        if (!original)
            return E_FAIL;
        const HRESULT result = original(iid, output);
        if (SUCCEEDED(result) && output && *output)
            GFXObserveDXGIFactory(static_cast<IUnknown*>(*output), "CreateDXGIFactory1");
        else if (FAILED(result))
            GFXReportAPIResult("CreateDXGIFactory1", result);
        return result;
    }

    HRESULT WINAPI GFXInterceptCreateDXGIFactory2(UINT flags, REFIID iid, void** output)
    {
        const auto original = gfxOriginalCreateDXGIFactory2.load();
        if (!original)
            return E_FAIL;
        const HRESULT result = original(flags, iid, output);
        if (SUCCEEDED(result) && output && *output)
            GFXObserveDXGIFactory(static_cast<IUnknown*>(*output), "CreateDXGIFactory2");
        else if (FAILED(result))
            GFXReportAPIResult("CreateDXGIFactory2", result);
        return result;
    }

    void GFXRemoveDXGIVTableHooks()
    {
        AcquireSRWLockExclusive(&gfxDXGITableLock);
        const LONG count = InterlockedCompareExchange(&gfxDXGIVTableCount, 0, 0);
        for (LONG i = count - 1; i >= 0; --i)
        {
            const auto& patch = gfxDXGIVTables[i];
            DWORD protection = 0;
            if (patch.slot && VirtualProtect(patch.slot, sizeof(void*), PAGE_READWRITE, &protection))
            {
                InterlockedCompareExchangePointer(reinterpret_cast<PVOID volatile*>(patch.slot),
                    patch.original, patch.replacement);
                DWORD ignored = 0;
                VirtualProtect(patch.slot, sizeof(void*), protection, &ignored);
            }
        }
        InterlockedExchange(&gfxDXGIVTableCount, 0);
        ReleaseSRWLockExclusive(&gfxDXGITableLock);
    }

    void GFXCaptureFatal(const char* source)
    {
        if (InterlockedCompareExchange(&gfxExitCaptured, 1, 0) != 0)
            return;
        char line[256] = {};
        std::snprintf(line, sizeof(line), "[FATAL_PATH] source=%s thread=%lu tick=%llu\r\n",
            source, GetCurrentThreadId(), static_cast<unsigned long long>(GetTickCount64()));
        WriteGFXTrace(line);
        std::snprintf(line, sizeof(line),
            "[HOOK_STATUS] d3d12CreateCalls=%ld vulkanProcLookups=%ld apiErrors=%ld dredConfigured=%ld\r\n",
            InterlockedCompareExchange(&gfxD3D12CreateCount, 0, 0),
            InterlockedCompareExchange(&gfxVulkanLookupCount, 0, 0),
            InterlockedCompareExchange(&gfxAPIErrorCount, 0, 0),
            InterlockedCompareExchange(&gfxDredConfigured, 0, 0));
        WriteGFXTrace(line);
        ReportGraphicsModules("fatal path");
        GFXReportDXGICounters("fatal path");
        GFXCaptureDX12Device(source);
        if (auto* device = static_cast<ID3D12Device*>(InterlockedExchangePointer(&gfxTrackedD3DDevice, nullptr)))
            device->Release();
        GFXFlushTrace();
    }

    bool GFXContainsError(const char* text)
    {
        return text && (std::strstr(text, "ERR_GFX_STATE") != nullptr ||
            std::strstr(text, "ERR_GFX") != nullptr);
    }

    bool GFXContainsError(const wchar_t* text)
    {
        return text && (std::wcsstr(text, L"ERR_GFX_STATE") != nullptr ||
            std::wcsstr(text, L"ERR_GFX") != nullptr);
    }

    int WINAPI GFXInterceptMessageBoxA(HWND window, LPCSTR text, LPCSTR caption, UINT type)
    {
        if (GFXContainsError(text) || GFXContainsError(caption))
        {
            char line[400] = {};
            std::snprintf(line, sizeof(line), "[GAME_ERROR_DIALOG] %.*s | %.*s\r\n",
                100, caption ? caption : "", 180, text ? text : "");
            WriteGFXTrace(line);
            GFXCaptureFatal("MessageBoxA ERR_GFX");
        }
        const auto original = gfxOriginalMessageBoxA.load();
        return original ? original(window, text, caption, type) : 0;
    }

    int WINAPI GFXInterceptMessageBoxW(HWND window, LPCWSTR text, LPCWSTR caption, UINT type)
    {
        if (GFXContainsError(text) || GFXContainsError(caption))
        {
            char utf8[320] = {};
            if (text)
                WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8, sizeof(utf8), nullptr, nullptr);
            char line[400] = {};
            std::snprintf(line, sizeof(line), "[GAME_ERROR_DIALOG] ERR_GFX_STATE or related: %.*s\r\n", 250, utf8);
            WriteGFXTrace(line);
            GFXCaptureFatal("MessageBoxW ERR_GFX");
        }
        const auto original = gfxOriginalMessageBoxW.load();
        return original ? original(window, text, caption, type) : 0;
    }

    void WINAPI GFXInterceptExitProcess(UINT code)
    {
        char line[128] = {};
        std::snprintf(line, sizeof(line), "[PROCESS_EXIT] ExitProcess code=%u\r\n", code);
        WriteGFXTrace(line);
        GFXCaptureFatal("ExitProcess");
        const auto original = gfxOriginalExitProcess.load();
        if (original)
            original(code);
    }

    BOOL WINAPI GFXInterceptTerminateProcess(HANDLE process, UINT code)
    {
        if (process == GetCurrentProcess() || GetProcessId(process) == GetCurrentProcessId())
        {
            char line[128] = {};
            std::snprintf(line, sizeof(line), "[PROCESS_EXIT] TerminateProcess code=%u\r\n", code);
            WriteGFXTrace(line);
            GFXCaptureFatal("TerminateProcess");
        }
        const auto original = gfxOriginalTerminateProcess.load();
        return original ? original(process, code) : FALSE;
    }

    HRESULT WINAPI GFXInterceptD3D12CreateDevice(IUnknown* adapter, D3D_FEATURE_LEVEL level,
        REFIID iid, void** output)
    {
        const auto original = gfxOriginalCreateD3D12Device.load();
        if (!original)
            return E_FAIL;
        InterlockedIncrement(&gfxD3D12CreateCount);
        if (InterlockedCompareExchange(&gfxDredConfigured, 0, 0) == 0)
            GFXConfigureDRED();
        const HRESULT result = original(adapter, level, iid, output);
        if (FAILED(result))
            GFXReportAPIResult("D3D12CreateDevice", result);
        if (SUCCEEDED(result) && output && *output &&
            InterlockedCompareExchangePointer(&gfxTrackedD3DDevice, nullptr, nullptr) == nullptr)
        {
            ID3D12Device* device = nullptr;
            if (SUCCEEDED(static_cast<IUnknown*>(*output)->QueryInterface(__uuidof(ID3D12Device),
                reinterpret_cast<void**>(&device))) && device)
            {
                if (InterlockedCompareExchangePointer(&gfxTrackedD3DDevice, device, nullptr) != nullptr)
                    device->Release();
                else
                {
                    WriteGFXTrace("[DX12] ID3D12Device captured for device-removal diagnostics.\r\n");
                    GFXFlushTrace();
                }
            }
        }
        return result;
    }

    std::int32_t WINAPI GFXInterceptVkQueueSubmit(void* queue, std::uint32_t count,
        const void* submits, void* fence)
    {
        const auto original = gfxOriginalVkQueueSubmit.load();
        if (!original)
            return -3;
        const std::int32_t result = original(queue, count, submits, fence);
        if (result < 0)
            GFXReportAPIResult("vkQueueSubmit", result);
        return result;
    }

    std::int32_t WINAPI GFXInterceptVkQueueSubmit2(void* queue, std::uint32_t count,
        const void* submits, void* fence)
    {
        const auto original = gfxOriginalVkQueueSubmit2.load();
        if (!original)
            return -3;
        const std::int32_t result = original(queue, count, submits, fence);
        if (result < 0)
            GFXReportAPIResult("vkQueueSubmit2", result);
        return result;
    }

    std::int32_t WINAPI GFXInterceptVkQueuePresent(void* queue, const void* info)
    {
        const auto original = gfxOriginalVkQueuePresent.load();
        if (!original)
            return -3;
        const std::int32_t result = original(queue, info);
        if (result < 0 && result != -1000001004)
            GFXReportAPIResult("vkQueuePresentKHR", result);
        return result;
    }

    std::int32_t WINAPI GFXInterceptVkAcquireNextImage(void* device, std::uint64_t swapchain,
        std::uint64_t timeout, std::uint64_t semaphore, std::uint64_t fence, std::uint32_t* index)
    {
        const auto original = gfxOriginalVkAcquireNextImage.load();
        if (!original)
            return -3;
        const std::int32_t result = original(device, swapchain, timeout, semaphore, fence, index);
        if (result < 0 && result != -1000001004 && result != 2)
            GFXReportAPIResult("vkAcquireNextImageKHR", result);
        return result;
    }

    FARPROC GFXWrapVulkanFunction(LPCSTR name, FARPROC address)
    {
        if (!name || !address)
            return address;
        if (std::strcmp(name, "vkGetInstanceProcAddr") == 0)
        {
            gfxOriginalVkGetInstanceProcAddr.store(reinterpret_cast<GFXVulkanGetProcFunction>(address));
            return reinterpret_cast<FARPROC>(&GFXInterceptVkGetInstanceProcAddr);
        }
        if (std::strcmp(name, "vkGetDeviceProcAddr") == 0)
        {
            gfxOriginalVkGetDeviceProcAddr.store(reinterpret_cast<GFXVulkanGetProcFunction>(address));
            return reinterpret_cast<FARPROC>(&GFXInterceptVkGetDeviceProcAddr);
        }
        if (std::strcmp(name, "vkQueueSubmit") == 0)
        {
            gfxOriginalVkQueueSubmit.store(reinterpret_cast<GFXVulkanSubmitFunction>(address));
            return reinterpret_cast<FARPROC>(&GFXInterceptVkQueueSubmit);
        }
        if (std::strcmp(name, "vkQueueSubmit2") == 0)
        {
            gfxOriginalVkQueueSubmit2.store(reinterpret_cast<GFXVulkanSubmitFunction>(address));
            return reinterpret_cast<FARPROC>(&GFXInterceptVkQueueSubmit2);
        }
        if (std::strcmp(name, "vkQueuePresentKHR") == 0)
        {
            gfxOriginalVkQueuePresent.store(reinterpret_cast<GFXVulkanPresentFunction>(address));
            return reinterpret_cast<FARPROC>(&GFXInterceptVkQueuePresent);
        }
        if (std::strcmp(name, "vkAcquireNextImageKHR") == 0)
        {
            gfxOriginalVkAcquireNextImage.store(reinterpret_cast<GFXVulkanAcquireFunction>(address));
            return reinterpret_cast<FARPROC>(&GFXInterceptVkAcquireNextImage);
        }
        return address;
    }

    FARPROC WINAPI GFXInterceptVkGetInstanceProcAddr(void* instance, const char* name)
    {
        const auto original = gfxOriginalVkGetInstanceProcAddr.load();
        if (!original)
            return nullptr;
        InterlockedIncrement(&gfxVulkanLookupCount);
        return GFXWrapVulkanFunction(name, original(instance, name));
    }

    FARPROC WINAPI GFXInterceptVkGetDeviceProcAddr(void* device, const char* name)
    {
        const auto original = gfxOriginalVkGetDeviceProcAddr.load();
        if (!original)
            return nullptr;
        InterlockedIncrement(&gfxVulkanLookupCount);
        return GFXWrapVulkanFunction(name, original(device, name));
    }

    FARPROC WINAPI GFXInterceptGetProcAddress(HMODULE module, LPCSTR name)
    {
        const auto original = gfxOriginalGetProcAddress.load();
        if (!original)
            return nullptr;
        const FARPROC address = original(module, name);
        if (!address || reinterpret_cast<std::uintptr_t>(name) <= 65535)
            return address;
        if (module == GetModuleHandleW(L"d3d12.dll") &&
            std::strcmp(name, "D3D12CreateDevice") == 0)
        {
            gfxOriginalCreateD3D12Device.store(reinterpret_cast<GFXCreateD3D12DeviceFunction>(address));
            WriteGFXTrace("[DX12] D3D12CreateDevice resolved through RDR2.exe GetProcAddress.\r\n");
            GFXFlushTrace();
            return reinterpret_cast<FARPROC>(&GFXInterceptD3D12CreateDevice);
        }
        if (module == GetModuleHandleW(L"dxgi.dll"))
        {
            if (std::strcmp(name, "CreateDXGIFactory") == 0)
            {
                gfxOriginalCreateDXGIFactory.store(reinterpret_cast<GFXCreateDXGIFactoryFunction>(address));
                InterlockedIncrement(&gfxDXGIFactoryLookups);
                WriteGFXTrace("[DXGI] CreateDXGIFactory intercepted through GetProcAddress.\r\n");
                return reinterpret_cast<FARPROC>(&GFXInterceptCreateDXGIFactory);
            }
            if (std::strcmp(name, "CreateDXGIFactory1") == 0)
            {
                gfxOriginalCreateDXGIFactory1.store(reinterpret_cast<GFXCreateDXGIFactoryFunction>(address));
                InterlockedIncrement(&gfxDXGIFactoryLookups);
                WriteGFXTrace("[DXGI] CreateDXGIFactory1 intercepted through GetProcAddress.\r\n");
                return reinterpret_cast<FARPROC>(&GFXInterceptCreateDXGIFactory1);
            }
            if (std::strcmp(name, "CreateDXGIFactory2") == 0)
            {
                gfxOriginalCreateDXGIFactory2.store(reinterpret_cast<GFXCreateDXGIFactory2Function>(address));
                InterlockedIncrement(&gfxDXGIFactoryLookups);
                WriteGFXTrace("[DXGI] CreateDXGIFactory2 intercepted through GetProcAddress.\r\n");
                return reinterpret_cast<FARPROC>(&GFXInterceptCreateDXGIFactory2);
            }
        }
        if (module == GetModuleHandleW(L"vulkan-1.dll"))
            return GFXWrapVulkanFunction(name, address);
        return address;
    }

    void GFXConfigureDRED()
    {
        const HMODULE module = GetModuleHandleW(L"d3d12.dll");
        const auto create = module ? reinterpret_cast<GFXD3D12GetDebugInterfaceFunction>(
            GetProcAddress(module, "D3D12GetDebugInterface")) : nullptr;
        if (!create)
        {
            WriteGFXTrace("[DX12_DRED] d3d12.dll or D3D12GetDebugInterface not yet available.\r\n");
            return;
        }
        if (InterlockedCompareExchange(&gfxDredAttempted, 1, 0) != 0)
            return;
        ID3D12DeviceRemovedExtendedDataSettings* settings = nullptr;
        const HRESULT result = create(__uuidof(ID3D12DeviceRemovedExtendedDataSettings),
            reinterpret_cast<void**>(&settings));
        if (FAILED(result) || !settings)
        {
            char line[144] = {};
            std::snprintf(line, sizeof(line), "[DX12_DRED] Settings interface unavailable HRESULT=0x%08lX\r\n",
                static_cast<unsigned long>(result));
            WriteGFXTrace(line);
            return;
        }
        settings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        settings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        settings->Release();
        InterlockedExchange(&gfxDredConfigured, 1);
        WriteGFXTrace("[DX12_DRED] Auto-breadcrumbs and page-fault reporting requested before device creation.\r\n");
        GFXFlushTrace();
    }

    unsigned int GFXInstallImportHooks()
    {
        const auto* base = reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(nullptr));
        const IMAGE_NT_HEADERS64* nt = nullptr;
        if (!HasValidImage(base, nt))
            return 0;
        const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (!directory.VirtualAddress || directory.VirtualAddress >= nt->OptionalHeader.SizeOfImage ||
            directory.Size < sizeof(IMAGE_IMPORT_DESCRIPTOR))
            return 0;
        const auto* entries = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress);
        const unsigned int limit = directory.Size / sizeof(IMAGE_IMPORT_DESCRIPTOR);
        unsigned int installed = 0;
        for (unsigned int i = 0; i < limit && entries[i].Name; ++i)
        {
            const auto& entry = entries[i];
            if (!entry.OriginalFirstThunk || !entry.FirstThunk || entry.Name >= nt->OptionalHeader.SizeOfImage ||
                entry.OriginalFirstThunk >= nt->OptionalHeader.SizeOfImage ||
                entry.FirstThunk >= nt->OptionalHeader.SizeOfImage)
                continue;
            const char* library = reinterpret_cast<const char*>(base + entry.Name);
            if (_stricmp(library, "KERNEL32.dll") != 0 && _stricmp(library, "KERNELBASE.dll") != 0 &&
                _stricmp(library, "USER32.dll") != 0)
                continue;
            const auto* names = reinterpret_cast<const IMAGE_THUNK_DATA64*>(base + entry.OriginalFirstThunk);
            const auto* slots = reinterpret_cast<const IMAGE_THUNK_DATA64*>(base + entry.FirstThunk);
            const std::size_t available = (static_cast<std::size_t>(nt->OptionalHeader.SizeOfImage) - entry.FirstThunk) / sizeof(IMAGE_THUNK_DATA64);
            const std::size_t namedAvailable = (static_cast<std::size_t>(nt->OptionalHeader.SizeOfImage) - entry.OriginalFirstThunk) / sizeof(IMAGE_THUNK_DATA64);
            for (std::size_t j = 0; j < available && j < namedAvailable && names[j].u1.AddressOfData; ++j)
            {
                if (IMAGE_SNAP_BY_ORDINAL64(names[j].u1.Ordinal) ||
                    names[j].u1.AddressOfData >= nt->OptionalHeader.SizeOfImage)
                    continue;
                const auto* imported = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names[j].u1.AddressOfData);
                for (auto& hook : gfxImports)
                {
                    if (hook.slot || std::strcmp(hook.name, reinterpret_cast<const char*>(imported->Name)) != 0)
                        continue;
                    auto* slot = reinterpret_cast<PVOID volatile*>(const_cast<ULONGLONG*>(&slots[j].u1.Function));
                    void* prior = reinterpret_cast<void*>(slots[j].u1.Function);
                    if (!prior || prior == hook.replacement)
                        continue;
                    DWORD protection = 0;
                    if (!VirtualProtect(const_cast<PVOID*>(slot), sizeof(void*), PAGE_READWRITE, &protection))
                        continue;
                    if (std::strcmp(hook.name, "GetProcAddress") == 0)
                        gfxOriginalGetProcAddress.store(reinterpret_cast<GFXGetProcAddressFunction>(prior));
                    else if (std::strcmp(hook.name, "MessageBoxA") == 0)
                        gfxOriginalMessageBoxA.store(reinterpret_cast<GFXMessageBoxAFunction>(prior));
                    else if (std::strcmp(hook.name, "MessageBoxW") == 0)
                        gfxOriginalMessageBoxW.store(reinterpret_cast<GFXMessageBoxWFunction>(prior));
                    else if (std::strcmp(hook.name, "ExitProcess") == 0)
                        gfxOriginalExitProcess.store(reinterpret_cast<GFXExitProcessFunction>(prior));
                    else if (std::strcmp(hook.name, "TerminateProcess") == 0)
                        gfxOriginalTerminateProcess.store(reinterpret_cast<GFXTerminateProcessFunction>(prior));
                    void* old = InterlockedCompareExchangePointer(slot, hook.replacement, prior);
                    DWORD ignored = 0;
                    VirtualProtect(const_cast<PVOID*>(slot), sizeof(void*), protection, &ignored);
                    if (old != prior)
                        continue;
                    hook.slot = slot;
                    hook.original = prior;
                    ++installed;
                }
            }
        }
        return installed;
    }

    ULONGLONG GFXAgeMs(ULONGLONG now, LONG64 earlier)
    {
        return earlier > 0 && now >= static_cast<ULONGLONG>(earlier) ?
            now - static_cast<ULONGLONG>(earlier) : 0;
    }

    void GFXLogProcessMemory()
    {
        using QueryMemory = BOOL(WINAPI*)(HANDLE, PPROCESS_MEMORY_COUNTERS, DWORD);
        const HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
        const auto query = kernel ? reinterpret_cast<QueryMemory>(
            GetProcAddress(kernel, "K32GetProcessMemoryInfo")) : nullptr;
        PROCESS_MEMORY_COUNTERS_EX counters = {};
        counters.cb = sizeof(counters);
        MEMORYSTATUSEX memory = {};
        memory.dwLength = sizeof(memory);
        const bool processAvailable = query && query(GetCurrentProcess(),
            reinterpret_cast<PPROCESS_MEMORY_COUNTERS>(&counters), sizeof(counters));
        const bool systemAvailable = GlobalMemoryStatusEx(&memory) != FALSE;
        if (!processAvailable && !systemAvailable)
            return;
        char line[256] = {};
        std::snprintf(line, sizeof(line),
            "[MEMORY] processAvailable=%s workingSetMB=%llu privateMB=%llu pagefileMB=%llu availablePhysicalMB=%llu systemLoadPercent=%lu\r\n",
            processAvailable ? "yes" : "no",
            static_cast<unsigned long long>(processAvailable ? counters.WorkingSetSize / (1024 * 1024) : 0),
            static_cast<unsigned long long>(processAvailable ? counters.PrivateUsage / (1024 * 1024) : 0),
            static_cast<unsigned long long>(processAvailable ? counters.PagefileUsage / (1024 * 1024) : 0),
            static_cast<unsigned long long>(systemAvailable ? memory.ullAvailPhys / (1024 * 1024) : 0),
            static_cast<unsigned long>(systemAvailable ? memory.dwMemoryLoad : 0));
        WriteGFXTrace(line);
    }

    void GFXLogProcessCPU(ULONGLONG now)
    {
        static ULONGLONG lastCPU = 0;
        static ULONGLONG lastTick = 0;
        FILETIME created = {}, exited = {}, kernel = {}, user = {};
        if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
            return;
        ULARGE_INTEGER kernelTime = {}, userTime = {};
        kernelTime.LowPart = kernel.dwLowDateTime;
        kernelTime.HighPart = kernel.dwHighDateTime;
        userTime.LowPart = user.dwLowDateTime;
        userTime.HighPart = user.dwHighDateTime;
        const ULONGLONG cpu = (kernelTime.QuadPart + userTime.QuadPart) / 10000;
        if (lastTick != 0 && now > lastTick && cpu >= lastCPU)
        {
            char line[180] = {};
            std::snprintf(line, sizeof(line),
                "[CPU] sampleIntervalMs=%llu processCpuTimeDeltaMs=%llu (across all threads)\r\n",
                static_cast<unsigned long long>(now - lastTick),
                static_cast<unsigned long long>(cpu - lastCPU));
            WriteGFXTrace(line);
        }
        lastCPU = cpu;
        lastTick = now;
    }

    void GFXSnapshotHungThread(const char* role, DWORD id)
    {
        if (id == 0 || id == GetCurrentThreadId())
            return;

        HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
            FALSE, id);
        if (!thread)
        {
            char line[180] = {};
            std::snprintf(line, sizeof(line), "[HANG_THREAD] role=%s thread=%lu openFailed=%lu\r\n",
                role, static_cast<unsigned long>(id), static_cast<unsigned long>(GetLastError()));
            WriteGFXTrace(line);
            return;
        }

        CONTEXT context = {};
        context.ContextFlags = CONTEXT_CONTROL;
        const DWORD suspension = SuspendThread(thread);
        bool sampled = false;
        if (suspension != static_cast<DWORD>(-1))
        {
            sampled = GetThreadContext(thread, &context) != FALSE;
            ResumeThread(thread);
        }

        FILETIME created = {}, exited = {}, kernel = {}, user = {};
        const bool timesAvailable = GetThreadTimes(thread, &created, &exited, &kernel, &user) != FALSE;
        CloseHandle(thread);
        ULARGE_INTEGER kernelTime = {}, userTime = {};
        kernelTime.LowPart = kernel.dwLowDateTime;
        kernelTime.HighPart = kernel.dwHighDateTime;
        userTime.LowPart = user.dwLowDateTime;
        userTime.HighPart = user.dwHighDateTime;
        char location[128] = {};
        if (sampled)
            DescribeCrashLocation(reinterpret_cast<const void*>(context.Rip), location);
        char line[400] = {};
        std::snprintf(line, sizeof(line),
            "[HANG_THREAD] role=%s thread=%lu sampled=%s rip=0x%llX rsp=0x%llX location=%s cpuTotalMs=%llu\r\n",
            role, static_cast<unsigned long>(id), sampled ? "yes" : "no",
            static_cast<unsigned long long>(sampled ? context.Rip : 0),
            static_cast<unsigned long long>(sampled ? context.Rsp : 0),
            sampled ? location : "unavailable",
            static_cast<unsigned long long>(timesAvailable ?
                (kernelTime.QuadPart + userTime.QuadPart) / 10000 : 0));
        WriteGFXTrace(line);
    }

    DWORD WINAPI GFXWatchdogThread(LPVOID)
    {
        ULONGLONG lastReport = 0;
        bool suspectedHang = false;
        while (InterlockedCompareExchange(&gfxWatchdogEnabled, 0, 0) != 0)
        {
            if (WaitForSingleObject(gfxWatchdogStop, 3000) != WAIT_TIMEOUT)
                break;
            if (InterlockedCompareExchange(&gfxWatchdogEnabled, 0, 0) == 0)
                break;
            const ULONGLONG now = GetTickCount64();
            const LONG64 updates = InterlockedCompareExchange64(&gfxScriptUpdateCount, 0, 0);
            const LONG64 presents = InterlockedCompareExchange64(&gfxPresentStarted, 0, 0);
            const LONG64 finished = InterlockedCompareExchange64(&gfxPresentFinished, 0, 0);
            const LONG inflight = InterlockedCompareExchange(&gfxPresentInFlight, 0, 0);
            const ULONGLONG scriptAge = GFXAgeMs(now, InterlockedCompareExchange64(&gfxLastScriptTick, 0, 0));
            const ULONGLONG presentAge = GFXAgeMs(now, InterlockedCompareExchange64(&gfxLastPresentFinishTick, 0, 0));
            const ULONGLONG blockedAge = inflight > 0 ?
                GFXAgeMs(now, InterlockedCompareExchange64(&gfxLastPresentStartTick, 0, 0)) : 0;
            const HWND foreground = GetForegroundWindow();
            DWORD foregroundProcess = 0;
            if (foreground)
                GetWindowThreadProcessId(foreground, &foregroundProcess);
            const bool focused = foregroundProcess == GetCurrentProcessId();
            const bool stalledTogether = updates > 0 && finished > 0 &&
                scriptAge >= 15000 && presentAge >= 15000;
            const bool blockedPresent = focused && inflight > 0 && blockedAge >= 15000;
            const bool stalledRender = focused && finished > 0 && presentAge >= 15000;
            const bool stalledScript = updates > 0 && scriptAge >= 15000;
            const bool possibleHang = stalledTogether || blockedPresent || stalledRender;
            if (possibleHang && !suspectedHang)
            {
                char line[256] = {};
                std::snprintf(line, sizeof(line),
                    "[HANG_SUSPECT] kind=%s scriptAgeMs=%llu presentAgeMs=%llu presentInFlightMs=%llu tick=%llu (not proof of GPU failure)\r\n",
                    blockedPresent ? "Present blocked" :
                    stalledTogether ? "script and presentation stalled" :
                    stalledScript ? "presentation stalled with stale script heartbeat" :
                    "presentation stalled; script still updating",
                    static_cast<unsigned long long>(scriptAge),
                    static_cast<unsigned long long>(presentAge),
                    static_cast<unsigned long long>(blockedAge),
                    static_cast<unsigned long long>(now));
                WriteGFXTrace(line);
                const DWORD scriptThread = static_cast<DWORD>(InterlockedCompareExchange(&gfxScriptThread, 0, 0));
                const DWORD presentThread = static_cast<DWORD>(InterlockedCompareExchange(&gfxPresentThread, 0, 0));
                GFXSnapshotHungThread("script", scriptThread);
                if (presentThread != scriptThread)
                    GFXSnapshotHungThread("present", presentThread);
                GFXFlushTrace();
            }
            else if (!possibleHang && suspectedHang)
                WriteGFXTrace("[HANG_RECOVERY] Presentation/script progress or foreground state changed.\r\n");
            suspectedHang = possibleHang;
            if (lastReport != 0 && now - lastReport < 15000)
                continue;
            lastReport = now;
            char line[512] = {};
            std::snprintf(line, sizeof(line),
                "[WATCHDOG] tick=%llu foreground=%s scriptUpdates=%lld scriptAgeMs=%llu scriptThread=%lu dxgiPresentStarted=%lld dxgiPresentFinished=%lld inFlight=%ld presentAgeMs=%llu presentCallAgeMs=%llu presentThread=%lu\r\n",
                static_cast<unsigned long long>(now), focused ? "yes" : "no",
                static_cast<long long>(updates), static_cast<unsigned long long>(scriptAge),
                static_cast<unsigned long>(InterlockedCompareExchange(&gfxScriptThread, 0, 0)),
                static_cast<long long>(presents), static_cast<long long>(finished), inflight,
                static_cast<unsigned long long>(presentAge), static_cast<unsigned long long>(blockedAge),
                static_cast<unsigned long>(InterlockedCompareExchange(&gfxPresentThread, 0, 0)));
            WriteGFXTrace(line);
            GFXLogProcessMemory();
            GFXLogProcessCPU(now);
            GFXFlushTrace();
        }
        return 0;
    }

    void GFXRemoveImportHooks()
    {
        for (auto& hook : gfxImports)
        {
            if (!hook.slot || !hook.original)
                continue;
            DWORD protection = 0;
            if (VirtualProtect(const_cast<PVOID*>(hook.slot), sizeof(void*), PAGE_READWRITE, &protection))
            {
                InterlockedCompareExchangePointer(hook.slot, hook.original, hook.replacement);
                DWORD ignored = 0;
                VirtualProtect(const_cast<PVOID*>(hook.slot), sizeof(void*), protection, &ignored);
            }
            hook.slot = nullptr;
        }
    }

}

namespace Main::CrashFixes
{
    void InitializeCrashDiagnostics(bool enabled)
    {
        if (!enabled || gfxExceptionHandler)
            return;

        wchar_t path[MAX_PATH] = {};
        const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
        if (length == 0 || length >= MAX_PATH)
        {
            Logger::Log("[WARNING] CrashDiagnostics: game path not found; tracing disabled.");
            return;
        }

        wchar_t* filename = path + length;
        while (filename > path && filename[-1] != L'\\' && filename[-1] != L'/')
            --filename;
        constexpr wchar_t traceName[] = L"FusionFixRDR2\\FusionFixRDR2_CrashDiagnostics.log";
        if (static_cast<std::size_t>(filename - path) + sizeof(traceName) / sizeof(wchar_t) > MAX_PATH)
        {
            Logger::Log("[WARNING] CrashDiagnostics: log path too long; tracing disabled.");
            return;
        }
        std::memcpy(filename, traceName, sizeof(traceName));

        gfxTraceFile = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
        if (gfxTraceFile == INVALID_HANDLE_VALUE)
        {
            Logger::Log("[WARNING] CrashDiagnostics: could not create trace log.");
            return;
        }

        WriteGFXTrace("[INFO] FusionFixRDR2 opt-in crash diagnostics: exceptions, graphics API and process exit events; "
            "no rendering or pipeline-cache changes applied.\r\n"
            "[INFO] First-chance exceptions may be handled; a recorded exception is not proof of a fatal crash.\r\n"
            "[INFO] Stack frames are best-effort addresses, not verified source symbols.\r\n");
        ReportGraphicsModules("early startup");
        const auto* image = reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(nullptr));
        const IMAGE_NT_HEADERS64* nt = nullptr;
        if (HasValidImage(image, nt))
        {
            char build[192] = {};
            std::snprintf(build, sizeof(build),
                "[BUILD] process=%lu RDR2.exe timestamp=0x%08lX imageSize=0x%llX tick=%llu\r\n",
                GetCurrentProcessId(), static_cast<unsigned long>(nt->FileHeader.TimeDateStamp),
                static_cast<unsigned long long>(nt->OptionalHeader.SizeOfImage),
                static_cast<unsigned long long>(GetTickCount64()));
            WriteGFXTrace(build);
        }
        GFXConfigureDRED();
        const unsigned int imports = GFXInstallImportHooks();
        char hookStatus[180] = {};
        std::snprintf(hookStatus, sizeof(hookStatus), "[HOOK_STATUS] RDR2.exe import hooks installed=%u/5\r\n", imports);
        WriteGFXTrace(hookStatus);
        GFXFlushTrace();
        const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        if (ntdll)
        {
            gfxLookupUnwindEntry = reinterpret_cast<LookupUnwindEntry>(GetProcAddress(ntdll, "RtlLookupFunctionEntry"));
            gfxUnwindFrame = reinterpret_cast<UnwindFrame>(GetProcAddress(ntdll, "RtlVirtualUnwind"));
        }
        gfxExceptionHandler = AddVectoredExceptionHandler(0, TraceGraphicsException);
        if (gfxExceptionHandler)
        {
            gfxWatchdogStop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (gfxWatchdogStop)
            {
                InterlockedExchange(&gfxWatchdogEnabled, 1);
                gfxWatchdogThread = CreateThread(nullptr, 0, GFXWatchdogThread, nullptr, 0, nullptr);
                if (!gfxWatchdogThread)
                {
                    InterlockedExchange(&gfxWatchdogEnabled, 0);
                    CloseHandle(gfxWatchdogStop);
                    gfxWatchdogStop = nullptr;
                    WriteGFXTrace("[WARNING] Watchdog thread creation failed.\r\n");
                }
                else
                    WriteGFXTrace("[WATCHDOG] Monitoring started; 15-second snapshots, simultaneous-stall detection across focus changes.\r\n");
            }
            else
                WriteGFXTrace("[WARNING] Watchdog event creation failed.\r\n");
            char status[240] = {};
            std::snprintf(status, sizeof(status),
                "[SUCCESS] CrashDiagnostics: exception/graphics tracer installed; watchdog=%s, RDR2 import hooks=%u/5, DRED=%s; output=FusionFixRDR2\\FusionFixRDR2_CrashDiagnostics.log.",
                gfxWatchdogThread ? "enabled" : "unavailable", imports,
                InterlockedCompareExchange(&gfxDredConfigured, 0, 0) ? "enabled" :
                InterlockedCompareExchange(&gfxDredAttempted, 0, 0) ? "unavailable" : "pending DX12 device");
            Logger::Log(status);
        }
        else
        {
            Logger::Log("[WARNING] CrashDiagnostics: exception handler setup failed.");
            GFXRemoveImportHooks();
            CloseHandle(gfxTraceFile);
            gfxTraceFile = INVALID_HANDLE_VALUE;
        }
    }

    void RecordCrashDiagnosticsHeartbeat()
    {
        if (InterlockedCompareExchange(&gfxWatchdogEnabled, 0, 0) == 0)
            return;
        InterlockedExchange(&gfxScriptThread, static_cast<LONG>(GetCurrentThreadId()));
        InterlockedExchange64(&gfxLastScriptTick, static_cast<LONG64>(GetTickCount64()));
        InterlockedIncrement64(&gfxScriptUpdateCount);
    }

    void ReportCrashDiagnosticsStatus()
    {
        if (gfxExceptionHandler)
        {
            ReportGraphicsModules("gameplay initialization");
            char line[180] = {};
            std::snprintf(line, sizeof(line),
                "[EXCEPTION_STATUS] capturedUniqueFirstChance=%u limit=24 (not necessarily fatal)\r\n",
                gfxCapturedCount);
            WriteGFXTrace(line);
            std::snprintf(line, sizeof(line),
                "[HOOK_STATUS] d3d12CreateCalls=%ld vulkanProcLookups=%ld dredConfigured=%ld\r\n",
                InterlockedCompareExchange(&gfxD3D12CreateCount, 0, 0),
                InterlockedCompareExchange(&gfxVulkanLookupCount, 0, 0),
                InterlockedCompareExchange(&gfxDredConfigured, 0, 0));
            WriteGFXTrace(line);
            GFXReportDXGICounters("gameplay initialization");
            GFXFlushTrace();
        }
    }

    void ShutdownCrashDiagnostics()
    {
        InterlockedExchange(&gfxWatchdogEnabled, 0);
        if (gfxWatchdogStop)
            SetEvent(gfxWatchdogStop);
        if (gfxWatchdogThread)
        {
            CloseHandle(gfxWatchdogThread);
            gfxWatchdogThread = nullptr;
        }
        GFXRemoveDXGIVTableHooks();
        GFXRemoveImportHooks();
        InterlockedExchangePointer(&gfxTrackedD3DDevice, nullptr);
        if (gfxExceptionHandler)
        {
            RemoveVectoredExceptionHandler(gfxExceptionHandler);
            gfxExceptionHandler = nullptr;
        }
        if (gfxTraceFile != INVALID_HANDLE_VALUE)
        {
            WriteGFXTrace("[INFO] Tracer shutdown during DLL detach.\r\n");
            CloseHandle(gfxTraceFile);
            gfxTraceFile = INVALID_HANDLE_VALUE;
        }
    }

    void InitializeTextureStreamingStutterFix(bool enabled)
    {
        if (!enabled)
            return;

        const auto* image = reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(nullptr));
        const IMAGE_NT_HEADERS64* nt = nullptr;
        if (!HasValidImage(image, nt) ||
            nt->FileHeader.TimeDateStamp != 0x65EEC92E ||
            nt->OptionalHeader.SizeOfImage != 0x7397600)
        {
            Logger::Log("[WARNING] TextureStreamingStutterFix: unsupported RDR2 build; patch skipped.");
            return;
        }

        const auto* candidate = image + textureConfigDescriptorRva;
        if (!IsTextureConfigDescriptorValid(image, nt, candidate) ||
            !IsGraphicsConfigLayoutValid(image, nt))
        {
            Logger::Log("[WARNING] TextureStreamingStutterFix: graphics configuration metadata validation failed; patch skipped.");
            return;
        }

        auto* target = reinterpret_cast<std::uint32_t*>(const_cast<std::uint8_t*>(candidate));
        if (!WriteTextureConfigHash(target, textureConfigOriginalHash, textureConfigDisabledHash))
        {
            Logger::Log("[WARNING] TextureStreamingStutterFix: graphics configuration metadata could not be safely changed; patch skipped.");
            return;
        }

        textureConfigModifiedDescriptor = target;
        textureConfigSuppressionStartedAt = GetTickCount64();
        std::uint32_t installedHash = 0;
        std::memcpy(&installedHash, target, sizeof(installedHash));
        if (installedHash == textureConfigDisabledHash)
            Logger::Log("[INFO] TextureStreamingStutterFix: startup texture memory override installed.");
        else
            Logger::Log("[WARNING] TextureStreamingStutterFix: metadata changed unexpectedly after installation; suppression not confirmed.");

        if (Logger::IsDebugEnabled())
        {
            char diagnostic[240] = {};
            std::snprintf(diagnostic, sizeof(diagnostic),
                "TextureStreamingStutterFix: install descriptorRva=0x%llX, originalHash=0x%08X, overrideHash=0x%08X, readback=0x%08X.",
                static_cast<unsigned long long>(textureConfigDescriptorRva),
                textureConfigOriginalHash, textureConfigDisabledHash, installedHash);
            Logger::Debug(diagnostic);
        }
    }

    void RestoreTextureStreamingStutterFix()
    {
        auto* target = textureConfigModifiedDescriptor;
        if (!target)
            return;

        std::uint32_t observedHash = 0;
        std::memcpy(&observedHash, target, sizeof(observedHash));
        const ULONGLONG elapsedMs = GetTickCount64() - textureConfigSuppressionStartedAt;
        if (Logger::IsDebugEnabled())
        {
            char diagnostic[224] = {};
            std::snprintf(diagnostic, sizeof(diagnostic),
                "TextureStreamingStutterFix: startup-end checkpoint elapsedMs=%llu, observedHash=0x%08X, expectedOverride=0x%08X.",
                static_cast<unsigned long long>(elapsedMs), observedHash, textureConfigDisabledHash);
            Logger::Debug(diagnostic);
        }

        if (!WriteTextureConfigHash(target, textureConfigDisabledHash, textureConfigOriginalHash))
        {
            Logger::Log("[WARNING] TextureStreamingStutterFix: could not safely restore metadata; startup override status unverified.");
            return;
        }
        std::uint32_t restoredHash = 0;
        std::memcpy(&restoredHash, target, sizeof(restoredHash));
        textureConfigModifiedDescriptor = nullptr;
        textureConfigSuppressionStartedAt = 0;
        if (observedHash == textureConfigDisabledHash && restoredHash == textureConfigOriginalHash)
            Logger::Log("[INFO] TextureStreamingStutterFix: startup override found; original metadata restored (engine use unverified).");
        else
            Logger::Log("[WARNING] TextureStreamingStutterFix: unexpected metadata state during restoration; verify engine behavior.");

        if (Logger::IsDebugEnabled())
        {
            char diagnostic[192] = {};
            std::snprintf(diagnostic, sizeof(diagnostic),
                "TextureStreamingStutterFix: restore elapsedMs=%llu, before=0x%08X, after=0x%08X, expectedOriginal=0x%08X.",
                static_cast<unsigned long long>(elapsedMs), observedHash, restoredHash, textureConfigOriginalHash);
            Logger::Debug(diagnostic);
        }
    }

    void Initialize(bool textureCrashFix)
    {
        if (!textureCrashFix)
            return;

        const auto* image = reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(nullptr));
        const IMAGE_NT_HEADERS64* nt = nullptr;
        if (!HasValidImage(image, nt))
        {
            Logger::Log("[WARNING] TextureCrashFix: executable image could not be validated; patch skipped.");
            return;
        }

        const auto* match = FindUniqueSignature(image, nt);
        if (!match)
        {
            Logger::Log("[WARNING] TextureCrashFix: unique supported signature not found; patch skipped.");
            return;
        }

        auto* target = DecodeBranch(match);
        const auto targetAddress = reinterpret_cast<std::uintptr_t>(target);
        const auto signatureAddress = reinterpret_cast<std::uintptr_t>(match);
        if (!IsInExecutableSection(image, nt, target, sizeof(returnTrue)) ||
            !IsInExecutableSection(image, nt, DecodeBranch(match + 5), 1) ||
            (targetAddress >= signatureAddress && targetAddress < signatureAddress + signatureLength))
        {
            Logger::Log("[WARNING] TextureCrashFix: engine branch target failed validation; patch skipped.");
            return;
        }

        if (std::memcmp(target, returnTrue, sizeof(returnTrue)) == 0)
        {
            Logger::Log("[INFO] TextureCrashFix: patch already active.");
            return;
        }

        if (target[0] == 0xE9 || target[0] == 0xEB || target[0] == 0xCC || target[0] == 0xC3 ||
            (target[0] == 0xFF && target[1] == 0x25))
        {
            Logger::Log("[WARNING] TextureCrashFix: engine function is already redirected or unsupported; patch skipped.");
            return;
        }

        std::uint8_t original[sizeof(returnTrue)] = {};
        std::memcpy(original, target, sizeof(original));
        DWORD previousProtection = 0;
        if (!VirtualProtect(target, sizeof(returnTrue), PAGE_EXECUTE_READWRITE, &previousProtection))
        {
            Logger::Log("[WARNING] TextureCrashFix: unable to unlock engine code; patch skipped.");
            return;
        }

        if (std::memcmp(target, original, sizeof(original)) != 0)
        {
            DWORD ignored = 0;
            VirtualProtect(target, sizeof(returnTrue), previousProtection, &ignored);
            Logger::Log("[WARNING] TextureCrashFix: engine code changed before patching; patch skipped.");
            return;
        }

        std::memcpy(target, returnTrue, sizeof(returnTrue));
        const bool flushed = FlushInstructionCache(GetCurrentProcess(), target, sizeof(returnTrue)) != FALSE;
        if (!flushed)
        {
            std::memcpy(target, original, sizeof(original));
            FlushInstructionCache(GetCurrentProcess(), target, sizeof(original));
        }
        DWORD ignored = 0;
        const bool protectedAgain = VirtualProtect(target, sizeof(returnTrue), previousProtection, &ignored) != FALSE;
        if (flushed && protectedAgain && std::memcmp(target, returnTrue, sizeof(returnTrue)) == 0)
            Logger::Log("[SUCCESS] TextureCrashFix: graphics.ytd check patched.");
        else
            Logger::Log("[WARNING] TextureCrashFix: engine code could not be patched or verified reliably.");
    }
}

namespace
{
    struct PoolSignatureResult
    {
        const std::uint8_t* address = nullptr;
        unsigned matches = 0;
    };

    PoolSignatureResult cachedPoolCaller;
    bool poolExtensionEnabled = false;
    bool poolStreamingEnabled = false;
    bool poolAssetEnabled = false;
    bool poolMemoryOptimizationEnabled = false;
    bool poolOverrideInstalled = false;
    bool poolDirectOverrideInstalled = false;
    const std::uint8_t* cachedABPoolGetter = nullptr;
    std::atomic<std::uint32_t> poolObservedHashes{0};
    std::atomic<std::uint32_t> poolRaisedHashes{0};
    std::atomic<bool> poolGameplayInitialized{false};
    std::atomic<void*> poolOriginalGetter{nullptr};
    void* volatile* poolGetterSlot = nullptr;
    std::uint8_t* poolDirectRelay = nullptr;
    std::uint8_t* poolDirectTarget = nullptr;
    std::uint8_t poolDirectPatch[8] = {};
    std::uint8_t poolDirectOriginal[8] = {};
    constexpr std::uint32_t assetPoolHashes[5] = {
        0x528ACB56, 0x941F03C9, 0x68065B7A, 0x3066500C, 0x6851786C
    };
    constexpr std::uint32_t assetPoolMinimums[5] = {
        145000, 90000, 35000, 12000, 4000
    };
    PoolSignatureResult FindPoolSignature(const std::uint8_t* image, const IMAGE_NT_HEADERS64* nt,
        const std::uint8_t* pattern, const char* mask, std::size_t patternLength)
    {
        PoolSignatureResult result;
        const std::size_t imageSize = nt->OptionalHeader.SizeOfImage;
        const auto* sections = IMAGE_FIRST_SECTION(nt);
        for (unsigned index = 0; index < nt->FileHeader.NumberOfSections; ++index)
        {
            const auto& section = sections[index];
            const std::size_t offset = section.VirtualAddress;
            const std::size_t size = section.Misc.VirtualSize;
            if ((section.Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0 || offset >= imageSize ||
                size < patternLength || size > imageSize - offset)
                continue;

            const auto start = reinterpret_cast<std::uintptr_t>(image) + offset;
            const auto end = start + size;
            for (auto cursor = start; cursor < end;)
            {
                MEMORY_BASIC_INFORMATION region = {};
                if (VirtualQuery(reinterpret_cast<const void*>(cursor), &region, sizeof(region)) != sizeof(region))
                    break;

                const auto regionStart = reinterpret_cast<std::uintptr_t>(region.BaseAddress);
                const auto regionEnd = regionStart + region.RegionSize;
                if (regionEnd <= cursor)
                    break;

                const auto scanEnd = regionEnd < end ? regionEnd : end;
                if (scanEnd - cursor >= patternLength &&
                    IsExecutable(reinterpret_cast<const std::uint8_t*>(cursor), patternLength))
                {
                    for (auto position = cursor; position <= scanEnd - patternLength; ++position)
                    {
                        const auto* bytes = reinterpret_cast<const std::uint8_t*>(position);
                        bool matches = true;
                        for (std::size_t i = 0; i < patternLength; ++i)
                        {
                            if (mask[i] != '?' && bytes[i] != pattern[i])
                            {
                                matches = false;
                                break;
                            }
                        }
                        if (matches)
                        {
                            result.address = bytes;
                            if (++result.matches > 1)
                                return result;
                        }
                    }
                }
                cursor = scanEnd;
            }
        }
        return result;
    }

    bool CanReadPoolMemory(const void* address, std::size_t length)
    {
        if (!address || length == 0)
            return false;

        MEMORY_BASIC_INFORMATION info = {};
        if (VirtualQuery(address, &info, sizeof(info)) != sizeof(info) || info.State != MEM_COMMIT ||
            (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
            return false;

        const DWORD protection = info.Protect & 0xFF;
        if (protection != PAGE_READONLY && protection != PAGE_READWRITE &&
            protection != PAGE_WRITECOPY && protection != PAGE_EXECUTE_READ &&
            protection != PAGE_EXECUTE_READWRITE && protection != PAGE_EXECUTE_WRITECOPY)
            return false;

        const auto addressValue = reinterpret_cast<std::uintptr_t>(address);
        const auto regionBase = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
        return addressValue >= regionBase && length <= info.RegionSize &&
            addressValue - regionBase <= info.RegionSize - length;
    }

    void DescribePoolCodeAddress(const std::uint8_t* image, const IMAGE_NT_HEADERS64* nt,
        const std::uint8_t* address, char* description, std::size_t capacity)
    {
        if (!address)
        {
            std::snprintf(description, capacity, "null");
            return;
        }

        const auto value = reinterpret_cast<std::uintptr_t>(address);
        const auto gameBase = reinterpret_cast<std::uintptr_t>(image);
        if (value >= gameBase && value - gameBase < nt->OptionalHeader.SizeOfImage)
        {
            std::snprintf(description, capacity, "RDR2.exe+0x%llX",
                static_cast<unsigned long long>(value - gameBase));
            return;
        }

        MEMORY_BASIC_INFORMATION memory = {};
        if (VirtualQuery(address, &memory, sizeof(memory)) != sizeof(memory) ||
            memory.State != MEM_COMMIT)
        {
            std::snprintf(description, capacity, "unmapped");
            return;
        }

        char moduleName[MAX_PATH] = {};
        if (memory.Type == MEM_IMAGE && GetModuleFileNameA(
            static_cast<HMODULE>(memory.AllocationBase), moduleName, MAX_PATH))
        {
            const char* name = std::strrchr(moduleName, '\\');
            name = name ? name + 1 : moduleName;
            std::snprintf(description, capacity, "%s+0x%llX", name,
                static_cast<unsigned long long>(value -
                    reinterpret_cast<std::uintptr_t>(memory.AllocationBase)));
            return;
        }

        std::snprintf(description, capacity, "non-image memory (%s)",
            IsExecutable(address, 1) ? "executable" : "not executable");
    }

    void LogPoolGetterDiagnostics(const std::uint8_t* image, const IMAGE_NT_HEADERS64* nt,
        const PoolSignatureResult& caller)
    {
        if (caller.matches != 1 || !IsInExecutableSection(image, nt, caller.address + 11, 5))
            return;

        const auto* branch = caller.address + 11;
        if (branch[0] != 0xE8 && branch[0] != 0xE9)
            return;

        const auto* target = DecodeBranch(branch);
        char callerLocation[96] = {};
        char targetLocation[96] = {};
        DescribePoolCodeAddress(image, nt, branch, callerLocation, sizeof(callerLocation));
        DescribePoolCodeAddress(image, nt, target, targetLocation, sizeof(targetLocation));
        char message[320] = {};
        std::snprintf(message, sizeof(message),
            "Pool extensions: caller branch=%s opcode=0x%02X destination=%s.",
            callerLocation, static_cast<unsigned>(branch[0]), targetLocation);
        Logger::Debug(message);

        if (!CanReadPoolMemory(target, 24))
        {
            Logger::Debug("Pool extensions: caller destination cannot be read safely; no hook attempted.");
            return;
        }

        char bytes[24 * 3 + 1] = {};
        for (std::size_t i = 0; i < 24; ++i)
            std::snprintf(bytes + i * 3, sizeof(bytes) - i * 3, "%02X ", static_cast<unsigned>(target[i]));
        std::snprintf(message, sizeof(message),
            "Pool extensions: destination executable=%s, first 24 bytes: %s",
            IsExecutable(target, 24) ? "yes" : "no", bytes);
        Logger::Debug(message);

        const std::uint8_t* next = nullptr;
        if (target[0] == 0xE9 || target[0] == 0xE8)
            next = DecodeBranch(target);
        else if (target[0] == 0xFF && target[1] == 0x25)
        {
            std::int32_t displacement = 0;
            std::memcpy(&displacement, target + 2, sizeof(displacement));
            const auto slotAddress = reinterpret_cast<std::intptr_t>(target + 6) + displacement;
            const auto* slot = reinterpret_cast<const std::uint8_t*>(slotAddress);
            if (CanReadPoolMemory(slot, sizeof(next)))
                std::memcpy(&next, slot, sizeof(next));
        }
        else if (target[0] == 0x4C && target[1] == 0x8B && target[2] == 0xC8 &&
            target[3] == 0x48 && target[4] == 0xB8 &&
            target[13] == 0x50 && target[14] == 0xC3)
            std::memcpy(&next, target + 5, sizeof(next));
        if (next)
        {
            char nextLocation[96] = {};
            DescribePoolCodeAddress(image, nt, next, nextLocation, sizeof(nextLocation));
            std::snprintf(message, sizeof(message),
                "Pool extensions: destination redirects to %s; existing hook preserved.",
                nextLocation);
            Logger::Debug(message);
            if (CanReadPoolMemory(next, 48))
            {
                char nextBytes[48 * 3 + 1] = {};
                for (std::size_t i = 0; i < 48; ++i)
                    std::snprintf(nextBytes + i * 3, sizeof(nextBytes) - i * 3,
                        "%02X ", static_cast<unsigned>(next[i]));
                std::snprintf(message, sizeof(message),
                    "Pool extensions: redirect executable=%s, first 48 bytes: %s",
                    IsExecutable(next, 48) ? "yes" : "no", nextBytes);
                Logger::Debug(message);
            }
            else
                Logger::Debug("Pool extensions: redirected target cannot be read safely.");
        }
    }

    __declspec(noinline) std::uint32_t PoolSizeOverride(const void* config, std::uint32_t hash,
        std::uint32_t fallback, std::uintptr_t extra)
    {
        using Getter = std::uint32_t(*)(const void*, std::uint32_t, std::uint32_t, std::uintptr_t);
        const auto original = reinterpret_cast<Getter>(poolOriginalGetter.load(std::memory_order_acquire));
        if (!original)
            return fallback;
        const std::uint32_t current = original(config, hash, fallback, extra);
        std::uint32_t desired = 0;
        if (poolStreamingEnabled)
        {
            switch (hash)
            {
            case 0x41663795: desired = 4500; break;
            case 0x25B847D8: desired = 30000; break;
            case 0x414E53FE: desired = 19500; break;
            case 0xA23A10BF: desired = 22500; break;
            case 0xB758AEC3: desired = 1500; break;
            case 0x9B36D076: desired = 10000; break;
            case 0xB4419501: desired = 3500; break;
            case 0x2CDFE406: desired = 40000; break;
            default: break;
            }
        }
        if (poolMemoryOptimizationEnabled)
        {
            switch (hash)
            {
            case 0x41663795: desired = 4000; break;
            case 0x25B847D8: desired = 25000; break;
            case 0x414E53FE: desired = 16000; break;
            case 0xA23A10BF: desired = 18000; break;
            default: break;
            }
        }
        if (!desired && poolAssetEnabled)
        {
            for (unsigned asset = 0; asset < 5; ++asset)
            {
                if (hash == assetPoolHashes[asset])
                {
                    desired = assetPoolMinimums[asset];
                    break;
                }
            }
        }
        if (desired)
        {
            constexpr std::uint32_t streamingHashes[8] = {
                0x41663795, 0x25B847D8, 0x414E53FE, 0xA23A10BF,
                0xB758AEC3, 0x9B36D076, 0xB4419501, 0x2CDFE406
            };
            std::uint32_t bit = 0;
            for (unsigned i = 0; i < 8; ++i)
                if (hash == streamingHashes[i])
                {
                    bit = 1u << i;
                    break;
                }
            if (!bit)
                for (unsigned i = 0; i < 5; ++i)
                    if (hash == assetPoolHashes[i])
                    {
                        bit = 1u << (i + 8);
                        break;
                    }
            if (bit)
            {
                poolObservedHashes.fetch_or(bit, std::memory_order_relaxed);
                if (current < desired)
                    poolRaisedHashes.fetch_or(bit, std::memory_order_relaxed);
            }
        }
        return desired && current < desired ? desired : current;
    }

    void WritePoolAbsoluteJump(std::uint8_t* destination, const void* target)
    {
        constexpr std::uint8_t instruction[] = { 0xFF, 0x25, 0, 0, 0, 0 };
        std::memcpy(destination, instruction, sizeof(instruction));
        const auto address = reinterpret_cast<std::uintptr_t>(target);
        std::memcpy(destination + sizeof(instruction), &address, sizeof(address));
    }

    std::uint8_t* AllocatePoolRelay(const std::uint8_t* target)
    {
        SYSTEM_INFO system = {};
        GetSystemInfo(&system);
        const std::uintptr_t granularity = system.dwAllocationGranularity;
        if (granularity == 0 || (granularity & (granularity - 1)) != 0)
            return nullptr;

        const auto address = reinterpret_cast<std::uintptr_t>(target);
        const auto aligned = address & ~(granularity - 1);
        const auto minimum = reinterpret_cast<std::uintptr_t>(system.lpMinimumApplicationAddress);
        const auto maximum = reinterpret_cast<std::uintptr_t>(system.lpMaximumApplicationAddress);
        for (std::uintptr_t distance = granularity; distance <= 0x7FFF0000; distance += granularity)
        {
            const std::uintptr_t candidates[2] = {
                aligned >= distance ? aligned - distance : 0,
                aligned <= maximum - distance ? aligned + distance : 0
            };
            for (const auto candidate : candidates)
            {
                if (candidate < minimum || candidate > maximum - 4096)
                    continue;
                const std::int64_t displacement = static_cast<std::int64_t>(candidate) -
                    static_cast<std::int64_t>(address + 5);
                if (displacement < INT32_MIN || displacement > INT32_MAX)
                    continue;
                auto* allocated = static_cast<std::uint8_t*>(VirtualAlloc(
                    reinterpret_cast<void*>(candidate), 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
                if (allocated)
                    return allocated;
            }
        }
        return nullptr;
    }

    const std::uint8_t* ResolveABPoolGetter(const std::uint8_t* image,
        const IMAGE_NT_HEADERS64* nt, const PoolSignatureResult& caller)
    {
        if (nt->FileHeader.TimeDateStamp != 0x65EEC92E ||
            nt->OptionalHeader.SizeOfImage != 0x7397600 ||
            caller.matches != 1 || caller.address != image + 0x2B34ACB ||
            !IsInExecutableSection(image, nt, caller.address + 11, 5) ||
            caller.address[11] != 0xE9)
            return nullptr;

        const auto* engineTarget = DecodeBranch(caller.address + 11);
        constexpr std::uint8_t engineJump[] = { 0x4C, 0x8B, 0xC8, 0x48, 0xB8 };
        if (engineTarget != image + 0x2B09CAC ||
            !IsInExecutableSection(image, nt, engineTarget, 15) ||
            std::memcmp(engineTarget, engineJump, sizeof(engineJump)) != 0 ||
            engineTarget[13] != 0x50 || engineTarget[14] != 0xC3)
            return nullptr;

        const auto* scriptHook = reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(L"ScriptHookRDR2.dll"));
        const IMAGE_NT_HEADERS64* hookNt = nullptr;
        if (!HasValidImage(scriptHook, hookNt))
            return nullptr;

        const std::uint8_t* redirected = nullptr;
        std::memcpy(&redirected, engineTarget + 5, sizeof(redirected));
        constexpr std::uint8_t abPrologue[] = {
            0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20,
            0x41, 0x8B, 0xF8, 0x44, 0x8B, 0x41, 0x10, 0x49, 0x8B, 0xD9,
            0x44, 0x8B, 0xD2, 0x45, 0x85, 0xC0
        };
        if (!redirected || redirected != scriptHook + 0x7850 ||
            !IsInExecutableSection(scriptHook, hookNt, redirected, sizeof(abPrologue)) ||
            std::memcmp(redirected, abPrologue, sizeof(abPrologue)) != 0 ||
            (reinterpret_cast<std::uintptr_t>(redirected) & 7) != 0)
            return nullptr;

        return redirected;
    }

    bool TryInstallABPoolOverride(const std::uint8_t* image, const IMAGE_NT_HEADERS64* nt)
    {
        if (!poolExtensionEnabled || poolOverrideInstalled || poolDirectOverrideInstalled ||
            !cachedABPoolGetter ||
            ResolveABPoolGetter(image, nt, cachedPoolCaller) != cachedABPoolGetter)
            return false;

        auto* target = const_cast<std::uint8_t*>(cachedABPoolGetter);
        constexpr std::size_t trampolinePrologueSize = 10;
        std::uint8_t original[8] = {};
        std::memcpy(original, target, sizeof(original));
        auto* relay = AllocatePoolRelay(target);
        if (!relay)
            return false;

        auto* trampoline = relay + 32;
        WritePoolAbsoluteJump(relay, reinterpret_cast<const void*>(&PoolSizeOverride));
        std::memcpy(trampoline, target, trampolinePrologueSize);
        WritePoolAbsoluteJump(trampoline + trampolinePrologueSize, target + trampolinePrologueSize);

        DWORD relayProtection = 0;
        if (!VirtualProtect(relay, 4096, PAGE_EXECUTE_READ, &relayProtection) ||
            !FlushInstructionCache(GetCurrentProcess(), relay, 64))
        {
            VirtualFree(relay, 0, MEM_RELEASE);
            return false;
        }

        std::uint8_t patch[8] = { 0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90 };
        const auto distance = reinterpret_cast<std::intptr_t>(relay) -
            reinterpret_cast<std::intptr_t>(target + 5);
        if (distance < INT32_MIN || distance > INT32_MAX)
        {
            VirtualFree(relay, 0, MEM_RELEASE);
            return false;
        }
        const auto displacement = static_cast<std::int32_t>(distance);
        std::memcpy(patch + 1, &displacement, sizeof(displacement));

        DWORD originalProtection = 0;
        if (!VirtualProtect(target, sizeof(patch), PAGE_EXECUTE_READWRITE, &originalProtection))
        {
            VirtualFree(relay, 0, MEM_RELEASE);
            return false;
        }

        std::int64_t originalWord = 0;
        std::int64_t patchWord = 0;
        std::memcpy(&originalWord, original, sizeof(originalWord));
        std::memcpy(&patchWord, patch, sizeof(patchWord));
        poolOriginalGetter.store(trampoline, std::memory_order_release);
        if (InterlockedCompareExchange64(reinterpret_cast<volatile LONG64*>(target),
            patchWord, originalWord) != originalWord)
        {
            poolOriginalGetter.store(nullptr, std::memory_order_release);
            DWORD ignored = 0;
            VirtualProtect(target, sizeof(patch), originalProtection, &ignored);
            VirtualFree(relay, 0, MEM_RELEASE);
            return false;
        }

        const bool flushed = FlushInstructionCache(GetCurrentProcess(), target, sizeof(patch)) != FALSE;
        DWORD ignored = 0;
        const bool restoredProtection = VirtualProtect(target, sizeof(patch), originalProtection, &ignored) != FALSE;
        if (!flushed || !restoredProtection)
        {
            DWORD writable = 0;
            if (VirtualProtect(target, sizeof(patch), PAGE_EXECUTE_READWRITE, &writable))
            {
                InterlockedCompareExchange64(reinterpret_cast<volatile LONG64*>(target),
                    originalWord, patchWord);
                FlushInstructionCache(GetCurrentProcess(), target, sizeof(patch));
                DWORD ignoredAgain = 0;
                VirtualProtect(target, sizeof(patch), originalProtection, &ignoredAgain);
            }
            if (std::memcmp(target, original, sizeof(original)) == 0)
            {
                poolOriginalGetter.store(nullptr, std::memory_order_release);
                VirtualFree(relay, 0, MEM_RELEASE);
                return false;
            }
            Logger::Log("[WARNING] Pool extensions: ScriptHookRDR2 code protection or cache update failed after hook installation.");
        }

        poolDirectRelay = relay;
        poolDirectTarget = target;
        std::memcpy(poolDirectOriginal, original, sizeof(original));
        std::memcpy(poolDirectPatch, patch, sizeof(patch));
        poolDirectOverrideInstalled = true;
        poolOverrideInstalled = true;
        if (poolStreamingEnabled)
            Logger::Log("[INFO] ExtendStreamingPools: 8 pool limits updated (Alexander Blade ScriptHook).");
        if (poolMemoryOptimizationEnabled)
            Logger::Log("[INFO] OptimizeStreamingMemory: request limits set to 4000/25000/16000/18000 (Alexander Blade ScriptHook).");
        if (poolAssetEnabled)
            Logger::Log("[INFO] ExtendAssetPools: 5 pool limits updated (Alexander Blade ScriptHook).");
        return true;
    }

    bool TryInstallPoolSizeOverride()
    {
        if (!poolExtensionEnabled || poolOverrideInstalled)
            return false;

        const auto* scriptHook = reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(L"ScriptHookRDR2.dll"));
        const IMAGE_NT_HEADERS64* hookNt = nullptr;
        if (!HasValidImage(scriptHook, hookNt) || hookNt->OptionalHeader.SizeOfImage != 0x48A000 ||
            hookNt->FileHeader.TimeDateStamp != 0x669669F6)
            return false;

        const auto* handler = scriptHook + 0x8840;
        const auto* forward = scriptHook + 0x88ED;
        constexpr std::uint8_t handlerSignature[] = {
            0x41, 0x56, 0x56, 0x57, 0x53, 0x48, 0x83, 0xEC, 0x68, 0x44, 0x89, 0xC6,
            0x89, 0xD7, 0x48, 0x89, 0xCB
        };
        constexpr std::uint8_t forwardSignature[] = { 0x4C, 0x8B, 0x35 };
        if (!handler || !forward ||
            !IsInExecutableSection(scriptHook, hookNt, handler, sizeof(handlerSignature)) ||
            !IsInExecutableSection(scriptHook, hookNt, forward, 7) ||
            std::memcmp(handler, handlerSignature, sizeof(handlerSignature)) != 0 ||
            std::memcmp(forward, forwardSignature, sizeof(forwardSignature)) != 0)
            return false;

        std::int32_t displacement = 0;
        std::memcpy(&displacement, forward + 3, sizeof(displacement));
        const auto slotAddress = reinterpret_cast<std::intptr_t>(forward + 7) + displacement;
        if (slotAddress != reinterpret_cast<std::intptr_t>(scriptHook + 0x4400B0))
            return false;

        MEMORY_BASIC_INFORMATION info = {};
        if (VirtualQuery(reinterpret_cast<const void*>(slotAddress), &info, sizeof(info)) != sizeof(info) ||
            info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0 ||
            ((info.Protect & 0xFF) != PAGE_READWRITE &&
                (info.Protect & 0xFF) != PAGE_EXECUTE_READWRITE) ||
            !CanReadPoolMemory(reinterpret_cast<const void*>(slotAddress), sizeof(void*)))
            return false;

        auto* slot = reinterpret_cast<void* volatile*>(slotAddress);
        void* previous = InterlockedCompareExchangePointer(slot, nullptr, nullptr);
        if (!previous || previous == reinterpret_cast<void*>(&PoolSizeOverride) ||
            !IsExecutable(reinterpret_cast<const std::uint8_t*>(previous), 1))
            return false;

        poolOriginalGetter.store(previous, std::memory_order_release);
        void* replacement = reinterpret_cast<void*>(&PoolSizeOverride);
        if (InterlockedCompareExchangePointer(slot, replacement, previous) != previous)
        {
            poolOriginalGetter.store(nullptr, std::memory_order_release);
            return false;
        }

        poolGetterSlot = slot;
        poolOverrideInstalled = true;
        if (poolStreamingEnabled)
            Logger::Log("[INFO] ExtendStreamingPools: 8 pool limits updated (ScriptHook V2).");
        if (poolMemoryOptimizationEnabled)
            Logger::Log("[INFO] OptimizeStreamingMemory: request limits set to 4000/25000/16000/18000 (ScriptHook V2).");
        if (poolAssetEnabled)
            Logger::Log("[INFO] ExtendAssetPools: 5 pool limits updated (ScriptHook V2).");
        return true;
    }
}

namespace
{
    using PackfileInitializer = void(__fastcall*)(int);
    constexpr int requestedPackfileEntries = 10000;
    constexpr std::uint32_t pedMebibyte = 1024U * 1024U;
    constexpr std::uint32_t pedRequestedBytes = 1024U * pedMebibyte;
    std::uint8_t* pedMemoryState = nullptr;
    std::uint8_t* pedMemoryBudget = nullptr;
    std::atomic<bool> pedMemoryStarted{false};
    std::atomic<bool> pedMemoryPending{false};
    bool pedMemoryChanged = false;
    std::uint32_t pedMemoryInitialBudget = 0;

    bool IsPedDataWritable(const std::uint8_t* image, const IMAGE_NT_HEADERS64* nt,
        const std::uint8_t* address)
    {
        const auto base = reinterpret_cast<std::uintptr_t>(image);
        const auto candidate = reinterpret_cast<std::uintptr_t>(address);
        const auto imageSize = nt->OptionalHeader.SizeOfImage;
        if (candidate < base || candidate - base > imageSize ||
            imageSize - (candidate - base) < sizeof(std::uint32_t) ||
            (candidate & (alignof(std::uint32_t) - 1)) != 0 ||
            !CanReadPoolMemory(address, sizeof(std::uint32_t)))
            return false;

        const auto rva = candidate - base;
        const auto* sections = IMAGE_FIRST_SECTION(nt);
        bool writableSection = false;
        for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        {
            const auto& section = sections[i];
            if ((section.Characteristics & IMAGE_SCN_MEM_WRITE) == 0 ||
                section.VirtualAddress >= imageSize ||
                section.Misc.VirtualSize > imageSize - section.VirtualAddress)
                continue;
            if (rva >= section.VirtualAddress && rva - section.VirtualAddress <= section.Misc.VirtualSize &&
                section.Misc.VirtualSize - (rva - section.VirtualAddress) >= sizeof(std::uint32_t))
            {
                writableSection = true;
                break;
            }
        }
        if (!writableSection)
            return false;

        MEMORY_BASIC_INFORMATION memory = {};
        if (VirtualQuery(address, &memory, sizeof(memory)) != sizeof(memory) ||
            memory.State != MEM_COMMIT || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
            return false;
        const DWORD protection = memory.Protect & 0xFF;
        return protection == PAGE_READWRITE || protection == PAGE_WRITECOPY ||
            protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
    }

    std::uint8_t* DecodePedDataReference(const std::uint8_t* image, const IMAGE_NT_HEADERS64* nt,
        const std::uint8_t* instruction, std::size_t displacementOffset)
    {
        if (!instruction)
            return nullptr;
        std::int32_t displacement = 0;
        std::memcpy(&displacement, instruction + displacementOffset, sizeof(displacement));
        const std::int64_t rva = static_cast<std::int64_t>(instruction - image) +
            static_cast<std::int64_t>(displacementOffset) + 4 + displacement;
        if (rva < 0 || static_cast<std::uint64_t>(rva) >
            nt->OptionalHeader.SizeOfImage - sizeof(std::uint32_t))
            return nullptr;
        return const_cast<std::uint8_t*>(image) + static_cast<std::size_t>(rva);
    }

    DWORD WINAPI PedMemoryStartupMonitor(LPVOID context)
    {
        for (unsigned attempt = 0; attempt < 12000; ++attempt)
        {
            Main::CrashFixes::ObservePedMemoryLimits();
            if (!pedMemoryPending.load(std::memory_order_acquire))
                FreeLibraryAndExitThread(static_cast<HMODULE>(context), 0);
            Sleep(10);
        }

        if (pedMemoryPending.exchange(false, std::memory_order_acq_rel))
            Logger::Log("[WARNING] ExtendPedMemoryLimits: initialization timed out; monitor stopped.");
        FreeLibraryAndExitThread(static_cast<HMODULE>(context), 0);
        return 0;
    }

    std::atomic<PackfileInitializer> packfileOriginalInitializer{nullptr};
    std::atomic<bool> packfileInitializationReported{false};
    std::uint8_t* packfileCallAddress = nullptr;
    std::int32_t packfileOriginalCall = 0;
    std::int32_t packfileAdjustedCall = 0;

    void __fastcall PackfileInitializationOverride(int count)
    {
        const auto original = packfileOriginalInitializer.load(std::memory_order_acquire);
        if (!original)
            return;

        const int adjusted = count > requestedPackfileEntries ? count : requestedPackfileEntries;
        original(adjusted);

        if (!packfileInitializationReported.exchange(true, std::memory_order_acq_rel))
        {
            char message[192] = {};
            std::snprintf(message, sizeof(message),
                adjusted > count ?
                "[SUCCESS] ExtendPackfileLimits: startup packfile capacity raised from %d to %d entries." :
                "[INFO] ExtendPackfileLimits: game already requests %d packfile entries (minimum %d).",
                count, adjusted > count ? adjusted : requestedPackfileEntries);
            Logger::Log(message);
        }
    }

    std::uint8_t* AllocatePackfileCallRelay(const std::uint8_t* address)
    {
        SYSTEM_INFO system = {};
        GetSystemInfo(&system);
        const std::uintptr_t origin = reinterpret_cast<std::uintptr_t>(address);
        const std::uintptr_t minimum = reinterpret_cast<std::uintptr_t>(system.lpMinimumApplicationAddress);
        const std::uintptr_t maximum = reinterpret_cast<std::uintptr_t>(system.lpMaximumApplicationAddress);
        constexpr std::uintptr_t range = 0x70000000ULL;
        const std::uintptr_t lower = origin > range && origin - range > minimum ? origin - range : minimum;
        const std::uintptr_t upper = origin < maximum && range < maximum - origin ? origin + range : maximum;
        const std::uintptr_t granularity = system.dwAllocationGranularity;
        if (lower >= upper || granularity == 0)
            return nullptr;

        for (std::uintptr_t cursor = lower; cursor < upper;)
        {
            MEMORY_BASIC_INFORMATION info = {};
            if (VirtualQuery(reinterpret_cast<const void*>(cursor), &info, sizeof(info)) != sizeof(info))
                break;
            const std::uintptr_t regionStart = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
            const std::uintptr_t regionEnd = regionStart + info.RegionSize;
            if (regionEnd <= cursor)
                break;

            if (info.State == MEM_FREE && cursor <= (std::numeric_limits<std::uintptr_t>::max)() - (granularity - 1))
            {
                const std::uintptr_t aligned = (cursor + granularity - 1) / granularity * granularity;
                if (aligned >= cursor && aligned < regionEnd && aligned < upper &&
                    regionEnd - aligned >= system.dwPageSize && upper - aligned >= system.dwPageSize)
                {
                    auto* relay = static_cast<std::uint8_t*>(VirtualAlloc(
                        reinterpret_cast<void*>(aligned), system.dwPageSize,
                        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
                    if (relay)
                        return relay;
                }
            }
            cursor = regionEnd;
        }
        return nullptr;
    }

    using TextureReadInitializer = std::uintptr_t(__fastcall*)(void*, std::uintptr_t,
        std::uintptr_t, std::uintptr_t, std::uint32_t, std::uint32_t);
    constexpr std::uintptr_t textureReaderCallRva = 0x2602589;
    constexpr std::uintptr_t textureReaderInitializerRva = 0x260267C;
    constexpr std::uintptr_t textureNormalReaderRva = 0x5564090;
    constexpr std::uintptr_t texturePriorityReaderRva = 0x5567140;
    constexpr std::uintptr_t textureReaderStartupStateRva = 0x55612A0;
    std::atomic<TextureReadInitializer> textureOriginalReadInitializer{nullptr};
    std::uint8_t* textureReadCall = nullptr;
    std::uint8_t* textureReadRelay = nullptr;
    std::int32_t textureReadOriginalDisplacement = 0;
    std::int32_t textureReadAdjustedDisplacement = 0;
    std::atomic<std::uint32_t> textureReadObserved{0};
    bool textureReadHookRetired = false;

    std::uintptr_t __fastcall TextureReadInitializationOverride(void* reader, std::uintptr_t argument1,
        std::uintptr_t argument2, std::uintptr_t argument3, std::uint32_t readerType,
        std::uint32_t requestedSlots)
    {
        const auto original = textureOriginalReadInitializer.load(std::memory_order_acquire);
        if (!original)
            return 0;

        std::uint32_t adjustedSlots = requestedSlots;
        const auto base = reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(nullptr));
        if (base && readerType <= 1 &&
            reader == base + (readerType == 0 ? textureNormalReaderRva : texturePriorityReaderRva) &&
            requestedSlots == (readerType == 0 ? 12U : 3U))
        {
            std::uint32_t startupState = 0;
            std::memcpy(&startupState, base + textureReaderStartupStateRva, sizeof(startupState));
            if (startupState == 1)
                adjustedSlots = readerType == 0 ? 24U : 6U;
        }

        const auto result = original(reader, argument1, argument2, argument3,
            readerType, adjustedSlots);
        if (adjustedSlots != requestedSlots)
        {
            const auto bit = 1U << readerType;
            if ((textureReadObserved.fetch_or(bit, std::memory_order_acq_rel) & bit) == 0)
            {
                char message[160] = {};
                std::snprintf(message, sizeof(message),
                    "[INFO] ImproveTextureStreaming: %s reader startup request expanded from %u to %u slots.",
                    readerType == 0 ? "normal" : "priority", requestedSlots, adjustedSlots);
                Logger::Log(message);
            }
        }
        return result;
    }

    bool RestoreTextureReadInitializationCall()
    {
        if (!textureReadCall)
            return true;
        if (!IsExecutable(textureReadCall, 5) || textureReadCall[0] != 0xE8)
            return false;
        std::int32_t displacement = 0;
        std::memcpy(&displacement, textureReadCall + 1, sizeof(displacement));
        if (displacement == textureReadOriginalDisplacement)
            return true;
        if (displacement != textureReadAdjustedDisplacement ||
            DecodeBranch(textureReadCall) != textureReadRelay)
            return false;

        DWORD previous = 0;
        if (!VirtualProtect(textureReadCall, 5, PAGE_EXECUTE_READWRITE, &previous))
            return false;
        std::memcpy(&displacement, textureReadCall + 1, sizeof(displacement));
        const bool unchanged = textureReadCall[0] == 0xE8 &&
            displacement == textureReadAdjustedDisplacement;
        if (unchanged)
            std::memcpy(textureReadCall + 1, &textureReadOriginalDisplacement,
                sizeof(textureReadOriginalDisplacement));
        const bool flushed = unchanged && FlushInstructionCache(GetCurrentProcess(), textureReadCall, 5) != FALSE;
        std::memcpy(&displacement, textureReadCall + 1, sizeof(displacement));
        const bool verified = textureReadCall[0] == 0xE8 &&
            displacement == textureReadOriginalDisplacement;
        DWORD ignored = 0;
        const bool restoredProtection = VirtualProtect(textureReadCall, 5, previous, &ignored) != FALSE;
        if (!restoredProtection)
            Logger::Log("[WARNING] ImproveTextureStreaming: initializer page protection could not be restored.");
        return flushed && verified && restoredProtection;
    }

    bool RestorePackfileInitializationCall()
    {
        if (!packfileCallAddress)
            return true;
        if (!IsExecutable(packfileCallAddress, 5) || packfileCallAddress[0] != 0xE8)
            return false;

        std::int32_t current = 0;
        std::memcpy(&current, packfileCallAddress + 1, sizeof(current));
        if (current != packfileAdjustedCall)
            return false;

        DWORD previous = 0;
        if (!VirtualProtect(packfileCallAddress, 5, PAGE_EXECUTE_READWRITE, &previous))
            return false;

        std::memcpy(&current, packfileCallAddress + 1, sizeof(current));
        const bool matches = current == packfileAdjustedCall;
        if (matches)
            std::memcpy(packfileCallAddress + 1, &packfileOriginalCall, sizeof(packfileOriginalCall));
        const bool flushed = matches &&
            FlushInstructionCache(GetCurrentProcess(), packfileCallAddress, 5) != FALSE;
        DWORD ignored = 0;
        const bool restored = VirtualProtect(packfileCallAddress, 5, previous, &ignored) != FALSE;
        return flushed && restored;
    }
}

namespace Main::CrashFixes
{
    void ExtendPedMemoryLimits(bool enabled)
    {
        if (!enabled || pedMemoryStarted.exchange(true, std::memory_order_acq_rel))
            return;

        const auto* image = reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(nullptr));
        const IMAGE_NT_HEADERS64* nt = nullptr;
        if (!HasValidImage(image, nt))
        {
            Logger::Log("[WARNING] ExtendPedMemoryLimits: executable image validation failed; patch skipped.");
            return;
        }

        constexpr std::uint8_t stateSignature[] =
        {
            0x44, 0x8B, 0x0D, 0, 0, 0, 0, 0x0F, 0x57, 0xC0, 0x44, 0x8B, 0x05
        };
        constexpr std::uint8_t budgetSignature[] =
        {
            0x8B, 0x0D, 0, 0, 0, 0, 0x48, 0x8D, 0x15, 0, 0, 0, 0, 0x44, 0x8B, 0x0D
        };
        const auto state = FindPoolSignature(image, nt, stateSignature, "xxx????xxxxxx", sizeof(stateSignature));
        const auto budget = FindPoolSignature(image, nt, budgetSignature, "xx????xxx????xxx", sizeof(budgetSignature));
        if (state.matches != 1 || budget.matches != 1 ||
            !IsInExecutableSection(image, nt, state.address, sizeof(stateSignature)) ||
            !IsInExecutableSection(image, nt, budget.address, sizeof(budgetSignature)))
        {
            char message[180] = {};
            std::snprintf(message, sizeof(message),
                "[WARNING] ExtendPedMemoryLimits: unsupported or ambiguous runtime signatures (state=%u, budget=%u); patch skipped.",
                state.matches, budget.matches);
            Logger::Log(message);
            return;
        }

        auto* stateTarget = DecodePedDataReference(image, nt, state.address, 3);
        auto* budgetTarget = DecodePedDataReference(image, nt, budget.address, 2);
        if (!IsPedDataWritable(image, nt, stateTarget) || !IsPedDataWritable(image, nt, budgetTarget) ||
            stateTarget == budgetTarget)
        {
            Logger::Log("[WARNING] ExtendPedMemoryLimits: runtime budget pointers failed writable-data validation; patch skipped.");
            return;
        }

        pedMemoryState = stateTarget;
        pedMemoryBudget = budgetTarget;
        pedMemoryPending.store(true, std::memory_order_release);
        if (Logger::IsDebugEnabled())
        {
            char message[220] = {};
            std::snprintf(message, sizeof(message),
                "ExtendPedMemoryLimits: state at RDR2.exe+0x%llX, budget at RDR2.exe+0x%llX (target 1024 MiB).",
                static_cast<unsigned long long>(stateTarget - image),
                static_cast<unsigned long long>(budgetTarget - image));
            Logger::Debug(message);
        }
        ObservePedMemoryLimits();
        if (!pedMemoryPending.load(std::memory_order_acquire))
            return;

        HMODULE module = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(&PedMemoryStartupMonitor), &module))
        {
            pedMemoryPending.store(false, std::memory_order_release);
            Logger::Log("[WARNING] ExtendPedMemoryLimits: unable to retain module for startup monitoring; patch stopped.");
            return;
        }

        HANDLE worker = CreateThread(nullptr, 0, PedMemoryStartupMonitor, module, 0, nullptr);
        if (!worker)
        {
            pedMemoryPending.store(false, std::memory_order_release);
            FreeLibrary(module);
            Logger::Log("[WARNING] ExtendPedMemoryLimits: startup monitor could not be started; patch stopped.");
            return;
        }
        CloseHandle(worker);
    }

    void ObservePedMemoryLimits()
    {
        if (!pedMemoryPending.load(std::memory_order_acquire))
            return;

        auto* image = reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(nullptr));
        const IMAGE_NT_HEADERS64* nt = nullptr;
        if (!HasValidImage(image, nt) || !IsPedDataWritable(image, nt, pedMemoryState) ||
            !IsPedDataWritable(image, nt, pedMemoryBudget))
        {
            pedMemoryPending.store(false, std::memory_order_release);
            Logger::Log("[WARNING] ExtendPedMemoryLimits: runtime budget memory became inaccessible; patch stopped.");
            return;
        }

        std::int32_t state = 0;
        std::uint32_t current = 0;
        std::memcpy(&state, pedMemoryState, sizeof(state));
        std::memcpy(&current, pedMemoryBudget, sizeof(current));
        if (current != 0 &&
            (current % pedMebibyte != 0 || current < 128U * pedMebibyte ||
                current > 4095U * pedMebibyte))
        {
            pedMemoryPending.store(false, std::memory_order_release);
            Logger::Log("[WARNING] ExtendPedMemoryLimits: unexpected ped memory budget; patch stopped.");
            return;
        }

        if (state < 0)
        {
            pedMemoryPending.store(false, std::memory_order_release);
            Logger::Log("[WARNING] ExtendPedMemoryLimits: unexpected initialization state; patch stopped.");
            return;
        }

        if (state > 0)
        {
            pedMemoryPending.store(false, std::memory_order_release);
            if (current < pedRequestedBytes)
            {
                Logger::Log("[WARNING] ExtendPedMemoryLimits: ped memory initialized before the budget increase; patch skipped.");
                return;
            }

            char message[192] = {};
            if (pedMemoryChanged && pedMemoryInitialBudget != 0)
                std::snprintf(message, sizeof(message),
                    "[SUCCESS] ExtendPedMemoryLimits: ped memory budget raised from %u MiB to %u MiB.",
                    pedMemoryInitialBudget / pedMebibyte, current / pedMebibyte);
            else if (pedMemoryChanged)
                std::snprintf(message, sizeof(message),
                    "[SUCCESS] ExtendPedMemoryLimits: ped memory budget set to %u MiB.",
                    current / pedMebibyte);
            else
                std::snprintf(message, sizeof(message),
                    "[INFO] ExtendPedMemoryLimits: existing ped memory budget is %u MiB; no change needed.",
                    current / pedMebibyte);
            Logger::Log(message);
            return;
        }

        if (current >= pedRequestedBytes)
            return;

        auto* target = reinterpret_cast<volatile LONG*>(pedMemoryBudget);
        const LONG previous = InterlockedCompareExchange(target,
            static_cast<LONG>(pedRequestedBytes), static_cast<LONG>(current));
        if (static_cast<std::uint32_t>(previous) == current)
        {
            if (pedMemoryInitialBudget == 0 && current != 0)
                pedMemoryInitialBudget = current;
            pedMemoryChanged = true;
        }
    }

    void ExtendHeapLimits(bool enabled)
    {
        if (!enabled)
            return;

        const auto* image = reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(nullptr));
        const IMAGE_NT_HEADERS64* nt = nullptr;
        if (!HasValidImage(image, nt))
        {
            Logger::Log("[WARNING] ExtendHeapLimits: game executable image validation failed; patch skipped.");
            return;
        }

        constexpr std::uint8_t signature[] =
        {
            0x75, 0, 0x41, 0xB1, 0x01, 0x45, 0x33, 0xC0,
            0xBA, 0, 0, 0, 0, 0x49, 0x8B, 0xCE
        };
        const auto result = FindPoolSignature(image, nt, signature, "x?xxxxxxx????xxx", sizeof(signature));
        if (result.matches != 1 || !IsInExecutableSection(image, nt, result.address, sizeof(signature)))
        {
            char message[160] = {};
            std::snprintf(message, sizeof(message),
                "[WARNING] ExtendHeapLimits: unsupported or ambiguous runtime heap signature (matches=%u); patch skipped.",
                result.matches);
            Logger::Log(message);
            return;
        }

        constexpr std::uint32_t mebibyte = 1024U * 1024U;
        constexpr std::uint32_t requestedMiB = 1536U;
        constexpr std::uint32_t requestedBytes = requestedMiB * mebibyte;
        auto* target = const_cast<std::uint8_t*>(result.address) + 9;
        std::uint32_t originalBytes = 0;
        std::memcpy(&originalBytes, target, sizeof(originalBytes));
        if (originalBytes % mebibyte != 0 || originalBytes < 256U * mebibyte ||
            originalBytes > 2047U * mebibyte)
        {
            Logger::Log("[WARNING] ExtendHeapLimits: runtime heap constant failed validation; patch skipped.");
            return;
        }

        if (originalBytes >= requestedBytes)
        {
            char message[160] = {};
            std::snprintf(message, sizeof(message),
                "[INFO] ExtendHeapLimits: existing heap size is %u MiB; no increase needed.",
                originalBytes / mebibyte);
            Logger::Log(message);
            return;
        }

        DWORD previousProtection = 0;
        if (!VirtualProtect(target, sizeof(originalBytes), PAGE_EXECUTE_READWRITE, &previousProtection))
        {
            Logger::Log("[WARNING] ExtendHeapLimits: heap constant could not be made writable; patch skipped.");
            return;
        }

        std::uint32_t observedBytes = 0;
        std::memcpy(&observedBytes, target, sizeof(observedBytes));
        if (observedBytes != originalBytes)
        {
            DWORD ignored = 0;
            VirtualProtect(target, sizeof(originalBytes), previousProtection, &ignored);
            Logger::Log("[WARNING] ExtendHeapLimits: heap constant changed during validation; patch skipped.");
            return;
        }

        std::memcpy(target, &requestedBytes, sizeof(requestedBytes));
        const bool flushed = FlushInstructionCache(GetCurrentProcess(), target, sizeof(requestedBytes)) != FALSE;
        std::memcpy(&observedBytes, target, sizeof(observedBytes));
        if (!flushed || observedBytes != requestedBytes)
        {
            std::memcpy(target, &originalBytes, sizeof(originalBytes));
            FlushInstructionCache(GetCurrentProcess(), target, sizeof(originalBytes));
        }

        DWORD ignored = 0;
        const bool restored = VirtualProtect(target, sizeof(originalBytes), previousProtection, &ignored) != FALSE;
        if (!flushed || observedBytes != requestedBytes || !restored)
        {
            Logger::Log("[WARNING] ExtendHeapLimits: heap patch could not be applied and verified reliably.");
            return;
        }

        char message[160] = {};
        std::snprintf(message, sizeof(message),
            "[SUCCESS] ExtendHeapLimits: startup heap size constant patched from %u MiB to %u MiB.",
            originalBytes / mebibyte, requestedMiB);
        Logger::Log(message);
    }

    void InitializeImproveTextureStreaming(bool enabled)
    {
        if (!enabled || textureReadCall)
            return;

        const auto* image = reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(nullptr));
        const IMAGE_NT_HEADERS64* nt = nullptr;
        if (!HasValidImage(image, nt) || nt->FileHeader.TimeDateStamp != 0x65EEC92E ||
            nt->OptionalHeader.SizeOfImage != 0x7397600 ||
            !IsInExecutableSection(image, nt, image + textureReaderCallRva, 5) ||
            !IsInExecutableSection(image, nt, image + textureReaderInitializerRva, 1) ||
            !IsPedDataWritable(image, nt, image + textureNormalReaderRva) ||
            !IsPedDataWritable(image, nt, image + textureNormalReaderRva + 0x5C) ||
            !CanReadPoolMemory(image + textureNormalReaderRva, 0x60) ||
            !IsPedDataWritable(image, nt, image + texturePriorityReaderRva) ||
            !IsPedDataWritable(image, nt, image + texturePriorityReaderRva + 0x5C) ||
            !CanReadPoolMemory(image + texturePriorityReaderRva, 0x60) ||
            !IsPedDataWritable(image, nt, image + textureReaderStartupStateRva))
        {
            Logger::Log("[WARNING] ImproveTextureStreaming: unsupported executable or reader layout; native texture reading retained.");
            return;
        }

        auto* const site = const_cast<std::uint8_t*>(image + textureReaderCallRva);
        if (site[0] != 0xE8 || DecodeBranch(site) != image + textureReaderInitializerRva)
        {
            Logger::Log("[WARNING] ImproveTextureStreaming: startup read-initializer callsite differs from expected layout; native reading retained.");
            return;
        }

        auto* relay = AllocatePackfileCallRelay(site);
        if (!relay)
        {
            Logger::Log("[WARNING] ImproveTextureStreaming: startup relay allocation failed; native reading retained.");
            return;
        }

        const std::intptr_t delta = reinterpret_cast<std::intptr_t>(relay) -
            reinterpret_cast<std::intptr_t>(site + 5);
        if (delta < (std::numeric_limits<std::int32_t>::min)() ||
            delta > (std::numeric_limits<std::int32_t>::max)())
        {
            VirtualFree(relay, 0, MEM_RELEASE);
            Logger::Log("[WARNING] ImproveTextureStreaming: startup relay outside call range; native reading retained.");
            return;
        }

        constexpr std::uint8_t jump[] = { 0xFF, 0x25, 0, 0, 0, 0 };
        const auto handler = reinterpret_cast<std::uintptr_t>(&TextureReadInitializationOverride);
        std::uint8_t relayCode[sizeof(jump) + sizeof(handler)] = {};
        std::memcpy(relayCode, jump, sizeof(jump));
        std::memcpy(relayCode + sizeof(jump), &handler, sizeof(handler));
        std::memcpy(relay, relayCode, sizeof(relayCode));
        DWORD relayProtection = 0;
        if (!VirtualProtect(relay, sizeof(relayCode), PAGE_EXECUTE_READ, &relayProtection) ||
            !FlushInstructionCache(GetCurrentProcess(), relay, sizeof(relayCode)))
        {
            VirtualFree(relay, 0, MEM_RELEASE);
            Logger::Log("[WARNING] ImproveTextureStreaming: relay finalization failed; native reading retained.");
            return;
        }

        std::int32_t originalDisplacement = 0;
        std::memcpy(&originalDisplacement, site + 1, sizeof(originalDisplacement));
        const auto adjustedDisplacement = static_cast<std::int32_t>(delta);
        textureOriginalReadInitializer.store(reinterpret_cast<TextureReadInitializer>(
            const_cast<std::uint8_t*>(image + textureReaderInitializerRva)), std::memory_order_release);
        DWORD originalProtection = 0;
        if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &originalProtection))
        {
            textureOriginalReadInitializer.store(nullptr, std::memory_order_release);
            VirtualFree(relay, 0, MEM_RELEASE);
            Logger::Log("[WARNING] ImproveTextureStreaming: read-initializer callsite was not writable.");
            return;
        }

        std::int32_t observed = 0;
        std::memcpy(&observed, site + 1, sizeof(observed));
        const bool unchanged = site[0] == 0xE8 && observed == originalDisplacement &&
            DecodeBranch(site) == image + textureReaderInitializerRva;
        if (unchanged)
            std::memcpy(site + 1, &adjustedDisplacement, sizeof(adjustedDisplacement));
        const bool flushed = unchanged && FlushInstructionCache(GetCurrentProcess(), site, 5) != FALSE;
        std::memcpy(&observed, site + 1, sizeof(observed));
        const bool installed = flushed && site[0] == 0xE8 &&
            observed == adjustedDisplacement && DecodeBranch(site) == relay;
        bool rolledBack = false;
        if (!installed && unchanged)
        {
            std::memcpy(site + 1, &originalDisplacement, sizeof(originalDisplacement));
            const bool rollbackFlushed = FlushInstructionCache(GetCurrentProcess(), site, 5) != FALSE;
            std::memcpy(&observed, site + 1, sizeof(observed));
            rolledBack = rollbackFlushed && site[0] == 0xE8 &&
                observed == originalDisplacement;
        }
        DWORD ignored = 0;
        const bool restoredProtection = VirtualProtect(site, 5, originalProtection, &ignored) != FALSE;
        if (!installed && !unchanged)
        {
            textureOriginalReadInitializer.store(nullptr, std::memory_order_release);
            VirtualFree(relay, 0, MEM_RELEASE);
            Logger::Log("[WARNING] ImproveTextureStreaming: initializer callsite changed during installation; patch skipped.");
            if (!restoredProtection)
                Logger::Log("[WARNING] ImproveTextureStreaming: initializer page protection could not be restored.");
            return;
        }

        textureReadOriginalDisplacement = originalDisplacement;
        textureReadAdjustedDisplacement = adjustedDisplacement;
        textureReadCall = site;
        textureReadRelay = relay;
        if (!installed)
        {
            textureReadHookRetired = rolledBack && restoredProtection;
            Logger::Log(textureReadHookRetired ?
                "[WARNING] ImproveTextureStreaming: hook verification failed; original callsite restored; relay retained for safety." :
                "[WARNING] ImproveTextureStreaming: hook verification failed; callsite recovery unconfirmed; relay retained for safety.");
        }
        else
            Logger::Log("[INFO] ImproveTextureStreaming: startup hook installed; normal slots 12->24, priority slots 3->6.");
        if (!restoredProtection)
            Logger::Log("[WARNING] ImproveTextureStreaming: initializer page protection could not be restored.");
    }

    void ReportImproveTextureStreamingStatus()
    {
        if (!textureReadCall)
            return;
        const auto* base = reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(nullptr));
        if (!base)
            return;
        const auto observed = textureReadObserved.load(std::memory_order_acquire);
        bool verified = true;
        for (unsigned i = 0; i != 2; ++i)
        {
            const auto* reader = base + (i == 0 ? textureNormalReaderRva : texturePriorityReaderRva);
            std::uint32_t count = 0;
            std::uint32_t capacity = 0;
            std::memcpy(&count, reader + 0x14, sizeof(count));
            std::memcpy(&capacity, reader + 0x5C, sizeof(capacity));
            const auto expected = i == 0 ? 24U : 6U;
            const bool matched = (observed & (1U << i)) && count == expected && capacity == expected;
            verified = verified && matched;
            char message[180] = {};
            if (matched)
                std::snprintf(message, sizeof(message),
                    "[SUCCESS] ImproveTextureStreaming: %s reader native capacity verified at %u slots.",
                    i == 0 ? "normal" : "priority", expected);
            else
                std::snprintf(message, sizeof(message),
                    "[WARNING] ImproveTextureStreaming: %s reader capacity not verified (requested=%u, observed=%u/%u).",
                    i == 0 ? "normal" : "priority", expected, count, capacity);
            Logger::Log(message);
        }
        if (verified && !textureReadHookRetired)
        {
            if (RestoreTextureReadInitializationCall())
            {
                textureReadHookRetired = true;
                Logger::Log("[SUCCESS] ImproveTextureStreaming: buffer sizes verified; startup hook removed.");
            }
            else
                Logger::Log("[WARNING] ImproveTextureStreaming: buffer sizes verified; startup hook restoration unconfirmed.");
        }
    }

    void ShutdownImproveTextureStreaming()
    {
        if (!textureReadCall || textureReadHookRetired)
            return;
        if (RestoreTextureReadInitializationCall())
            textureReadHookRetired = true;
        else
            Logger::Log("[WARNING] ImproveTextureStreaming: read-initializer hook could not be restored.");
    }

    void ExtendPackfileLimits(bool enabled)
    {
        if (!enabled || packfileCallAddress)
            return;

        const auto* image = reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(nullptr));
        const IMAGE_NT_HEADERS64* nt = nullptr;
        if (!HasValidImage(image, nt))
        {
            Logger::Log("[WARNING] ExtendPackfileLimits: executable image validation failed; startup hook skipped.");
            return;
        }

        constexpr std::uint8_t signature[] =
        {
            0xE8, 0, 0, 0, 0, 0xE8, 0, 0, 0, 0,
            0xE8, 0, 0, 0, 0, 0x33, 0xD2, 0x33, 0xC9,
            0xE8, 0, 0, 0, 0, 0xE8, 0, 0, 0, 0, 0x33, 0xC9
        };
        constexpr char mask[] = "x????" "x????" "x????" "xxxx" "x????" "x????" "xx";
        static_assert(sizeof(signature) == sizeof(mask) - 1);
        const auto result = FindPoolSignature(image, nt, signature, mask, sizeof(signature));
        if (result.matches != 1 ||
            !IsInExecutableSection(image, nt, result.address, sizeof(signature)))
        {
            char message[192] = {};
            std::snprintf(message, sizeof(message),
                "[WARNING] ExtendPackfileLimits: unsupported or ambiguous runtime initializer signature (matches=%u); hook skipped.",
                result.matches);
            Logger::Log(message);
            return;
        }

        auto* const site = const_cast<std::uint8_t*>(result.address);
        auto* const originalTarget = DecodeBranch(site);
        if (!IsInExecutableSection(image, nt, originalTarget, 1))
        {
            Logger::Log("[WARNING] ExtendPackfileLimits: original packfile initializer target is unsupported; hook skipped.");
            return;
        }

        auto* const relay = AllocatePackfileCallRelay(site);
        if (!relay)
        {
            Logger::Log("[WARNING] ExtendPackfileLimits: nearby executable relay allocation failed; hook skipped.");
            return;
        }

        const std::intptr_t displacement = reinterpret_cast<std::intptr_t>(relay) -
            reinterpret_cast<std::intptr_t>(site + 5);
        if (displacement < (std::numeric_limits<std::int32_t>::min)() ||
            displacement > (std::numeric_limits<std::int32_t>::max)())
        {
            VirtualFree(relay, 0, MEM_RELEASE);
            Logger::Log("[WARNING] ExtendPackfileLimits: relay is outside relative call range; hook skipped.");
            return;
        }

        constexpr std::uint8_t branch[] = { 0xFF, 0x25, 0, 0, 0, 0 };
        const std::uintptr_t handler = reinterpret_cast<std::uintptr_t>(&PackfileInitializationOverride);
        std::uint8_t relayCode[sizeof(branch) + sizeof(handler)] = {};
        std::memcpy(relayCode, branch, sizeof(branch));
        std::memcpy(relayCode + sizeof(branch), &handler, sizeof(handler));
        std::memcpy(relay, relayCode, sizeof(relayCode));
        DWORD previousRelayProtection = 0;
        if (!VirtualProtect(relay, sizeof(relayCode), PAGE_EXECUTE_READ, &previousRelayProtection) ||
            !FlushInstructionCache(GetCurrentProcess(), relay, sizeof(relayCode)))
        {
            VirtualFree(relay, 0, MEM_RELEASE);
            Logger::Log("[WARNING] ExtendPackfileLimits: executable relay could not be finalized; hook skipped.");
            return;
        }

        std::int32_t originalDisplacement = 0;
        std::memcpy(&originalDisplacement, site + 1, sizeof(originalDisplacement));
        const std::int32_t newDisplacement = static_cast<std::int32_t>(displacement);
        packfileOriginalInitializer.store(reinterpret_cast<PackfileInitializer>(originalTarget),
            std::memory_order_release);
        DWORD previousProtection = 0;
        if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &previousProtection))
        {
            packfileOriginalInitializer.store(nullptr, std::memory_order_release);
            VirtualFree(relay, 0, MEM_RELEASE);
            Logger::Log("[WARNING] ExtendPackfileLimits: callsite could not be made writable; hook skipped.");
            return;
        }

        std::int32_t observedDisplacement = 0;
        std::memcpy(&observedDisplacement, site + 1, sizeof(observedDisplacement));
        bool signatureUnchanged = true;
        for (std::size_t index = 0; index < sizeof(signature); ++index)
        {
            if (mask[index] != '?' && site[index] != signature[index])
            {
                signatureUnchanged = false;
                break;
            }
        }
        if (!signatureUnchanged || observedDisplacement != originalDisplacement ||
            DecodeBranch(site) != originalTarget)
        {
            DWORD ignored = 0;
            VirtualProtect(site, 5, previousProtection, &ignored);
            packfileOriginalInitializer.store(nullptr, std::memory_order_release);
            VirtualFree(relay, 0, MEM_RELEASE);
            Logger::Log("[WARNING] ExtendPackfileLimits: initializer changed before patching; hook skipped.");
            return;
        }

        std::memcpy(site + 1, &newDisplacement, sizeof(newDisplacement));
        const bool flushed = FlushInstructionCache(GetCurrentProcess(), site, 5) != FALSE;
        std::memcpy(&observedDisplacement, site + 1, sizeof(observedDisplacement));
        const bool written = flushed && observedDisplacement == newDisplacement;
        if (!written)
        {
            std::memcpy(site + 1, &originalDisplacement, sizeof(originalDisplacement));
            FlushInstructionCache(GetCurrentProcess(), site, 5);
        }

        DWORD ignored = 0;
        const bool protectedAgain = VirtualProtect(site, 5, previousProtection, &ignored) != FALSE;
        if (!written)
        {
            packfileOriginalInitializer.store(nullptr, std::memory_order_release);
            VirtualFree(relay, 0, MEM_RELEASE);
            Logger::Log("[WARNING] ExtendPackfileLimits: startup hook could not be installed and verified reliably.");
            return;
        }

        packfileOriginalCall = originalDisplacement;
        packfileAdjustedCall = newDisplacement;
        packfileCallAddress = site;
        char message[192] = {};
        std::snprintf(message, sizeof(message),
            "[SUCCESS] ExtendPackfileLimits: startup initializer hook installed at RDR2.exe+0x%llX (minimum %d entries).",
            static_cast<unsigned long long>(site - image), requestedPackfileEntries);
        Logger::Log(message);
        if (!protectedAgain)
            Logger::Log("[WARNING] ExtendPackfileLimits: original callsite page protection could not be restored.");
    }

    void ShutdownPackfileLimits()
    {
        if (!packfileCallAddress)
            return;
        if (!RestorePackfileInitializationCall())
            Logger::Log("[WARNING] ExtendPackfileLimits: original startup initializer call could not be restored.");
        packfileCallAddress = nullptr;
    }

    void ExtendStreamingPools(bool enabled, bool extendAssets, bool optimizeStreamingMemory)
    {
        poolStreamingEnabled = enabled;
        poolAssetEnabled = extendAssets;
        poolMemoryOptimizationEnabled = optimizeStreamingMemory;
        poolExtensionEnabled = enabled || extendAssets || optimizeStreamingMemory;
        if (!poolExtensionEnabled)
            return;

        const auto* scriptHook = reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(L"ScriptHookRDR2.dll"));
        const IMAGE_NT_HEADERS64* hookNt = nullptr;
        if (HasValidImage(scriptHook, hookNt) &&
            hookNt->OptionalHeader.SizeOfImage == 0x48A000 &&
            hookNt->FileHeader.TimeDateStamp == 0x669669F6)
            return;

        const auto* image = reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(nullptr));
        const IMAGE_NT_HEADERS64* nt = nullptr;
        if (!HasValidImage(image, nt))
        {
            Logger::Log("[WARNING] Pool extensions: executable image validation failed; no changes made.");
            return;
        }

        constexpr std::uint8_t callerSignature[] = { 0xBA, 0x95, 0, 0, 0, 0x41, 0xB8, 0xB8, 0x0B, 0, 0 };
        cachedPoolCaller = FindPoolSignature(image, nt, callerSignature, "xx???xxxx??", sizeof(callerSignature));
        if (cachedPoolCaller.matches != 1)
        {
            char diagnostic[128] = {};
            std::snprintf(diagnostic, sizeof(diagnostic),
                "Pool extensions: engine caller matches=%u; Alexander Blade path will require a verified redirect.",
                cachedPoolCaller.matches);
            Logger::Debug(diagnostic);
        }
    }

    void ObserveStreamingPoolStartup()
    {
        if (!poolExtensionEnabled)
            return;

        const auto* image = reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(nullptr));
        const IMAGE_NT_HEADERS64* nt = nullptr;
        if (!HasValidImage(image, nt))
            return;

        bool versionKnown = false;
        bool isV2 = false;
        bool warnedReset = false;
        for (unsigned attempt = 0; attempt < 360; ++attempt)
        {
            if (poolGameplayInitialized.load(std::memory_order_acquire))
                break;
            if (!versionKnown)
            {
                const auto* scriptHook = reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(L"ScriptHookRDR2.dll"));
                const IMAGE_NT_HEADERS64* hookNt = nullptr;
                if (HasValidImage(scriptHook, hookNt))
                {
                    isV2 = hookNt->OptionalHeader.SizeOfImage == 0x48A000 &&
                        hookNt->FileHeader.TimeDateStamp == 0x669669F6;
                    versionKnown = true;
                    Logger::Debug(isV2 ? "Pool extensions: ScriptHookRDR2 V2 detected; using validated forwarding pointer." :
                        "Pool extensions: non-V2 ScriptHook detected; waiting for Alexander Blade's verified getter redirect.");
                }
            }

            if (versionKnown && isV2)
            {
                if (poolOverrideInstalled && poolGetterSlot &&
                    InterlockedCompareExchangePointer(poolGetterSlot, nullptr, nullptr) !=
                    reinterpret_cast<void*>(&PoolSizeOverride))
                {
                    poolOverrideInstalled = false;
                    poolGetterSlot = nullptr;
                    if (!warnedReset)
                    {
                        Logger::Log("[WARNING] Pool extensions: ScriptHookRDR2 V2 replaced the forwarding pointer during startup; restoring overrides.");
                        warnedReset = true;
                    }
                }
                if (!poolOverrideInstalled)
                    TryInstallPoolSizeOverride();
            }
            else if (versionKnown && cachedPoolCaller.matches == 1)
            {
                if (poolOverrideInstalled && poolDirectOverrideInstalled && poolDirectTarget &&
                    std::memcmp(poolDirectTarget, poolDirectPatch, sizeof(poolDirectPatch)) != 0)
                {
                    poolOverrideInstalled = false;
                    poolDirectOverrideInstalled = false;
                    if (!warnedReset)
                    {
                        Logger::Log("[WARNING] Pool extensions: Alexander Blade getter hook changed during startup; revalidating.");
                        warnedReset = true;
                    }
                }
                if (!poolOverrideInstalled)
                {
                    cachedABPoolGetter = ResolveABPoolGetter(image, nt, cachedPoolCaller);
                    if (cachedABPoolGetter)
                        TryInstallABPoolOverride(image, nt);
                }
            }

            if (attempt + 1 < 360)
                Sleep(250);
        }

        if (!poolOverrideInstalled)
        {
            if (!isV2 && cachedPoolCaller.matches == 1)
                LogPoolGetterDiagnostics(image, nt, cachedPoolCaller);
            Logger::Log("[WARNING] Pool extensions: neither supported ScriptHookRDR2 backend could be installed; pool overrides skipped.");
        }
    }

    void ReportPoolExtensionStatus()
    {
        poolGameplayInitialized.store(true, std::memory_order_release);
        if (!poolExtensionEnabled || !Logger::IsDebugEnabled())
            return;

        const std::uint32_t requested = (poolStreamingEnabled ? 0xFFu : 0u) |
            (poolMemoryOptimizationEnabled ? 0x0Fu : 0u) |
            (poolAssetEnabled ? 0x1F00u : 0u);
        std::uint32_t observed = poolObservedHashes.load(std::memory_order_relaxed) & requested;
        std::uint32_t raised = poolRaisedHashes.load(std::memory_order_relaxed) & requested;
        unsigned observedCount = 0;
        unsigned raisedCount = 0;
        for (std::uint32_t bit = 1; bit <= 0x1000; bit <<= 1)
        {
            if (observed & bit)
                ++observedCount;
            if (raised & bit)
                ++raisedCount;
        }
        char message[200] = {};
        std::snprintf(message, sizeof(message),
            "Pool extensions: at ScriptMain, %u requested pool hashes observed, %u raised above game-provided sizes; installed hook alone does not confirm consumption of every pool.",
            observedCount, raisedCount);
        Logger::Debug(message);
    }

    void ShutdownStreamingPoolOverride()
    {
        if (!poolOverrideInstalled)
            return;

        if (poolDirectOverrideInstalled && poolDirectTarget && poolDirectRelay)
        {
            DWORD originalProtection = 0;
            if (VirtualProtect(poolDirectTarget, sizeof(poolDirectPatch),
                PAGE_EXECUTE_READWRITE, &originalProtection))
            {
                std::int64_t originalWord = 0;
                std::int64_t patchWord = 0;
                std::memcpy(&originalWord, poolDirectOriginal, sizeof(originalWord));
                std::memcpy(&patchWord, poolDirectPatch, sizeof(patchWord));
                if (InterlockedCompareExchange64(reinterpret_cast<volatile LONG64*>(poolDirectTarget),
                    originalWord, patchWord) == patchWord)
                    FlushInstructionCache(GetCurrentProcess(), poolDirectTarget, sizeof(poolDirectPatch));
                DWORD ignored = 0;
                VirtualProtect(poolDirectTarget, sizeof(poolDirectPatch), originalProtection, &ignored);
            }
            poolDirectOverrideInstalled = false;
            poolOverrideInstalled = false;
            return;
        }

        const auto original = poolOriginalGetter.load(std::memory_order_acquire);
        if (poolGetterSlot && original && GetModuleHandleW(L"ScriptHookRDR2.dll"))
            InterlockedCompareExchangePointer(poolGetterSlot, original,
                reinterpret_cast<void*>(&PoolSizeOverride));
        poolOverrideInstalled = false;
    }

}
