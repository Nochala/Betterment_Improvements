#include "../Headers/Version.h"
#include "../Headers/Logger.h"
#include <windows.h>
#include <winver.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#pragma comment(lib, "Version.lib")

namespace
{
    INIT_ONCE startupVersionLog = INIT_ONCE_STATIC_INIT;

    struct FileVersion
    {
        std::string full;
        std::string build;
    };

    struct ImageIdentity
    {
        DWORD timestamp = 0;
        DWORD size = 0;
        bool valid = false;
    };

    FileVersion ReadFileVersion(const wchar_t* path)
    {
        DWORD ignored = 0;
        const DWORD length = GetFileVersionInfoSizeW(path, &ignored);
        if (length == 0 || length > 1024 * 1024)
            return {};

        std::vector<std::uint8_t> data(length);
        if (!GetFileVersionInfoW(path, 0, length, data.data()))
            return {};

        VS_FIXEDFILEINFO* info = nullptr;
        UINT infoLength = 0;
        if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &infoLength) ||
            info == nullptr || infoLength < sizeof(VS_FIXEDFILEINFO) || info->dwSignature != 0xFEEF04BD)
            return {};

        const unsigned int major = HIWORD(info->dwFileVersionMS);
        const unsigned int minor = LOWORD(info->dwFileVersionMS);
        const unsigned int build = HIWORD(info->dwFileVersionLS);
        const unsigned int revision = LOWORD(info->dwFileVersionLS);
        FileVersion version;
        version.full = std::to_string(major) + "." + std::to_string(minor) + "." +
            std::to_string(build) + "." + std::to_string(revision);
        version.build = std::to_string(build) + "." + std::to_string(revision);
        return version;
    }

    ImageIdentity ReadImageIdentity(HMODULE module)
    {
        if (!module)
            return {};

        const auto* bytes = reinterpret_cast<const std::uint8_t*>(module);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE ||
            dos->e_lfanew < static_cast<LONG>(sizeof(IMAGE_DOS_HEADER)) || dos->e_lfanew > 4096)
            return {};

        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(bytes + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE ||
            nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
            return {};

        return { nt->FileHeader.TimeDateStamp, nt->OptionalHeader.SizeOfImage, true };
    }

    std::string FormatIdentity(const ImageIdentity& identity)
    {
        if (!identity.valid)
            return "PE metadata unavailable";

        char text[96] = {};
        std::snprintf(text, sizeof(text), "PE timestamp=0x%08lX, imageSize=0x%lX",
            static_cast<unsigned long>(identity.timestamp), static_cast<unsigned long>(identity.size));
        return text;
    }

    bool MatchesAlexanderBladeSignature(HMODULE module, const ImageIdentity& identity)
    {
        constexpr std::uint8_t signature[] = {
            0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20,
            0x41, 0x8B, 0xF8, 0x44, 0x8B, 0x41, 0x10, 0x49, 0x8B, 0xD9,
            0x44, 0x8B, 0xD2, 0x45, 0x85, 0xC0
        };
        constexpr std::size_t signatureOffset = 0x7850;
        if (!identity.valid || identity.size < signatureOffset + sizeof(signature))
            return false;

        const auto* address = reinterpret_cast<const std::uint8_t*>(module) + signatureOffset;
        MEMORY_BASIC_INFORMATION memory = {};
        if (VirtualQuery(address, &memory, sizeof(memory)) != sizeof(memory) ||
            memory.State != MEM_COMMIT || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
            return false;
        const auto regionEnd = reinterpret_cast<std::uintptr_t>(memory.BaseAddress) + memory.RegionSize;
        if (reinterpret_cast<std::uintptr_t>(address) > regionEnd ||
            regionEnd - reinterpret_cast<std::uintptr_t>(address) < sizeof(signature))
            return false;
        return std::memcmp(address, signature, sizeof(signature)) == 0;
    }

    BOOL CALLBACK LogVersionsOnce(PINIT_ONCE, PVOID, PVOID*)
    {
        Logger::Log((std::string("[VERSION] Betterment Improvements ") + Version::Mod + ".").c_str());
        wchar_t gamePath[MAX_PATH] = {};
        const DWORD gamePathLength = GetModuleFileNameW(nullptr, gamePath, MAX_PATH);
        const FileVersion gameVersion = gamePathLength != 0 && gamePathLength < MAX_PATH ?
            ReadFileVersion(gamePath) : FileVersion{};
        const ImageIdentity gameIdentity = ReadImageIdentity(GetModuleHandleW(nullptr));
        Logger::Log((std::string("[VERSION] RDR2.exe: version=") +
            (gameVersion.full.empty() ? "unavailable" : gameVersion.full) +
            ", build=" + (gameVersion.build.empty() ? "unavailable" : gameVersion.build) +
            ", " + FormatIdentity(gameIdentity) + ".").c_str());

        const HMODULE scriptHook = GetModuleHandleW(L"ScriptHookRDR2.dll");
        if (!scriptHook)
        {
            Logger::Log("[VERSION] ScriptHookRDR2: not loaded.");
            return TRUE;
        }

        wchar_t hookPath[MAX_PATH] = {};
        const DWORD hookPathLength = GetModuleFileNameW(scriptHook, hookPath, MAX_PATH);
        const FileVersion hookVersion = hookPathLength != 0 && hookPathLength < MAX_PATH ?
            ReadFileVersion(hookPath) : FileVersion{};
        const ImageIdentity hookIdentity = ReadImageIdentity(scriptHook);
        const bool knownV2 = hookIdentity.valid && hookIdentity.size == 0x48A000 &&
            hookIdentity.timestamp == 0x669669F6;
        const char* implementation = knownV2 ? "V2 (1.9 Tester, July 16 2024 build)" :
            MatchesAlexanderBladeSignature(scriptHook, hookIdentity) ?
            "Alexander Blade (recognized code signature)" : "unrecognized implementation/build";

        Logger::Log((std::string("[VERSION] ScriptHookRDR2: ") + implementation +
            ", fileVersion=" + (hookVersion.full.empty() ? "unavailable" : hookVersion.full) +
            ", " + FormatIdentity(hookIdentity) + ".").c_str());
        return TRUE;
    }
}

namespace Version
{
    void LogStartup()
    {
        InitOnceExecuteOnce(&startupVersionLog, LogVersionsOnce, nullptr, nullptr);
    }
}
