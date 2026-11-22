#include "../Headers/Display.h"
#include "../Headers/WaterReflections.h"
#include "../../Headers/Logger.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <vector>

namespace
{
    struct RecordArray
    {
        std::uintptr_t entries;
        std::uint16_t count;
        std::uint16_t capacity;
    };

    struct ModifierValue
    {
        std::uint32_t field;
        std::uint32_t intensity;
        std::uint32_t secondary;
    };

    static_assert(offsetof(RecordArray, count) == 8);
    static_assert(sizeof(ModifierValue) == 12);

    inline bool Read(std::uintptr_t address, void* buffer, std::size_t size)
    {
        SIZE_T read = 0;
        return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address),
            buffer, size, &read) && read == size;
    }

    inline std::uintptr_t FindManager()
    {
        const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        IMAGE_DOS_HEADER dos{};
        if (!Read(base, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
            dos.e_lfanew <= 0 || dos.e_lfanew > 4096)
            return 0;

        IMAGE_NT_HEADERS64 nt{};
        if (!Read(base + dos.e_lfanew, &nt, sizeof(nt)) || nt.Signature != IMAGE_NT_SIGNATURE ||
            nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
            nt.FileHeader.TimeDateStamp != 0x65EEC92E || nt.OptionalHeader.SizeOfImage != 0x07397600)
            return 0;

        constexpr std::uint8_t expected[] = {
            0x48, 0x8D, 0x0D, 0x44, 0x4E, 0x67, 0x03, 0xE8, 0x47, 0x98, 0xB0, 0xFF
        };
        std::uint8_t actual[sizeof(expected)]{};
        if (!Read(base + 0x01422035, actual, sizeof(actual)) ||
            std::memcmp(actual, expected, sizeof(expected)) != 0)
            return 0;

        return base + 0x04A96E80;
    }

    inline bool WriteIntensity(std::uintptr_t address, const ModifierValue& expected, std::uint32_t intensity)
    {
        MEMORY_BASIC_INFORMATION region{};
        if (!VirtualQuery(reinterpret_cast<const void*>(address), &region, sizeof(region)) ||
            region.State != MEM_COMMIT || (region.Protect & PAGE_GUARD) ||
            (region.Protect & 0xFF) != PAGE_READWRITE)
            return false;

        ModifierValue current{};
        if (!Read(address, &current, sizeof(current)) ||
            current.field != expected.field || current.intensity != expected.intensity ||
            current.secondary != expected.secondary)
            return false;

        SIZE_T written = 0;
        return WriteProcessMemory(GetCurrentProcess(),
            reinterpret_cast<void*>(address + offsetof(ModifierValue, intensity)),
            &intensity, sizeof(intensity), &written) && written == sizeof(intensity);
    }

    struct SavedIntensity
    {
        std::uintptr_t modifier;
        std::uint32_t nameHash;
        ModifierValue value;
        unsigned int lastSeen;
    };

    template <std::uint32_t... Hashes>
    bool UpdateWeather(std::uintptr_t manager, const char* name, bool enabled, bool pending,
        const std::array<std::uint32_t, sizeof...(Hashes)>& fields)
    {
        if (!enabled && !pending)
            return false;

        constexpr std::size_t maxWeatherRecords = 64;
        constexpr std::size_t keyframeCount = 26;
        constexpr std::size_t fieldsPerKeyframe = 592;

        constexpr std::size_t weatherIdOffset = keyframeCount * fieldsPerKeyframe * sizeof(std::uint16_t);
        constexpr std::size_t recordSize = weatherIdOffset + 0x20;
        static_assert(recordSize == 0x7860);

        struct WeatherSnapshot
        {
            std::uint32_t id;
            std::array<std::array<std::uint16_t, keyframeCount>, sizeof...(Hashes)> intensity;
        };

        const auto base = manager - 0x04A96E80;
        constexpr std::uint8_t expectedLayout[] = {
            0x49, 0x81, 0xC1, 0x60, 0x78, 0x00, 0x00
        };
        std::uint8_t layout[sizeof(expectedLayout)]{};
        if (!Read(base + 0x02B38930, layout, sizeof(layout)) ||
            std::memcmp(layout, expectedLayout, sizeof(layout)) != 0)
            return pending;

        std::uintptr_t table = 0;
        std::uint64_t capacity = 0;
        if (!Read(manager + 0x68, &table, sizeof(table)) || !table ||
            !Read(table - 0x10, &capacity, sizeof(capacity)) || capacity != maxWeatherRecords)
            return pending;

        static std::uintptr_t previousTable = 0;
        static std::array<WeatherSnapshot, maxWeatherRecords> saved{};
        static std::array<std::uint8_t, recordSize> record;
        if (previousTable != table)
        {
            saved = {};
            previousTable = table;
        }

        unsigned int changed = 0;
        for (std::size_t i = 0; i < saved.size(); ++i)
        {
            const auto recordAddress = table + i * recordSize;
            if (!Read(recordAddress, record.data(), record.size()))
                continue;

            std::uint32_t id = 0;
            std::memcpy(&id, record.data() + weatherIdOffset, sizeof(id));
            auto& original = saved[i];
            if (id != original.id)
                original = WeatherSnapshot{ id, {} };
            if (!id)
                continue;

            for (std::size_t field = 0; field < fields.size(); ++field)
            {
                for (std::size_t keyframe = 0; keyframe < keyframeCount; ++keyframe)
                {
                    const auto offset = (keyframe * fieldsPerKeyframe + fields[field]) * sizeof(std::uint16_t);
                    const auto address = recordAddress + offset;
                    std::uint16_t value = 0;
                    std::memcpy(&value, record.data() + offset, sizeof(value));

                    auto& snapshot = original.intensity[field][keyframe];
                    const bool nonzero = (value & 0x7FFFu) != 0;
                    if (!enabled && nonzero)
                    {
                        snapshot = 0;
                        continue;
                    }
                    if (enabled ? !nonzero : snapshot == 0)
                        continue;

                    std::uintptr_t currentTable = 0;
                    std::uint32_t currentId = 0;
                    std::uint16_t currentValue = 0;
                    if (!Read(manager + 0x68, &currentTable, sizeof(currentTable)) || currentTable != table ||
                        !Read(recordAddress + weatherIdOffset, &currentId, sizeof(currentId)) || currentId != id)
                        break;
                    if (!Read(address, &currentValue, sizeof(currentValue)) || currentValue != value)
                        continue;

                    MEMORY_BASIC_INFORMATION region{};
                    if (!VirtualQuery(reinterpret_cast<const void*>(address), &region, sizeof(region)) ||
                        region.State != MEM_COMMIT || (region.Protect & PAGE_GUARD) ||
                        (region.Protect & 0xFF) != PAGE_READWRITE)
                        continue;

                    const std::uint16_t replacement = enabled ? 0 : snapshot;
                    SIZE_T written = 0;
                    if (WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address),
                        &replacement, sizeof(replacement), &written) && written == sizeof(replacement))
                    {
                        snapshot = enabled ? value : 0;
                        ++changed;
                    }
                }
            }
        }

        if (changed)
        {
            char message[128]{};
            std::snprintf(message, sizeof(message), enabled ?
                "[SUCCESS] %s: zeroed %u base-weather intensity values." :
                "[SUCCESS] %s: restored %u base-weather intensity values.", name, changed);
            Logger::Log(message);
        }

        for (const auto& weather : saved)
            for (const auto& field : weather.intensity)
                for (const auto value : field)
                    if (value)
                        return true;
        return false;
    }


    template <std::uint32_t... Hashes>
    void UpdateEffect(const char* name, bool enabled)
    {
        static std::unordered_map<std::uintptr_t, SavedIntensity> saved;
        static bool wasEnabled = false;
        static bool weatherPending = false;
        static unsigned int scan = 0;
        if (!enabled && saved.empty() && !weatherPending)
        {
            wasEnabled = false;
            return;
        }

        static const auto manager = FindManager();
        if (!manager)
        {
            static bool reportedUnsupported = false;
            if (!reportedUnsupported)
            {
                reportedUnsupported = true;
                char message[128]{};
                std::snprintf(message, sizeof(message), "[WARNING] %s: unsupported timecycle modifier manager.", name);
                Logger::Log(message);
            }
            return;
        }

        static ULONGLONG nextUpdate = 0;
        const auto now = GetTickCount64();
        if (enabled == wasEnabled && now < nextUpdate)
            return;
        wasEnabled = enabled;
        nextUpdate = now + 2000;

        constexpr std::array<std::uint32_t, sizeof...(Hashes)> hashes{ Hashes... };
        constexpr std::uint32_t fieldCount = 0x248;
        struct FieldDescriptor
        {
            std::uint32_t index;
            std::uint32_t hash;
            std::uint8_t other[16];
        };
        static_assert(sizeof(FieldDescriptor) == 24);

        const auto base = manager - 0x04A96E80;
        std::uintptr_t registry = 0;
        std::uint32_t count = 0;
        static std::array<FieldDescriptor, fieldCount> descriptors{};
        if (!Read(base + 0x059B769C, &count, sizeof(count)) || count == 0 ||
            !Read(base + 0x059B76A0, &registry, sizeof(registry)) || !registry)
            return;

        if (count != fieldCount || registry != base + 0x03A83F90 ||
            !Read(registry, descriptors.data(), sizeof(descriptors)))
        {
            static bool reportedRegistry = false;
            if (!reportedRegistry)
            {
                reportedRegistry = true;
                char message[160]{};
                std::snprintf(message, sizeof(message),
                    "[WARNING] %s: timecycle registry validation failed (fields=%u, expected=%u).",
                    name, count, fieldCount);
                Logger::Log(message);
            }
            return;
        }

        std::array<std::uint32_t, sizeof...(Hashes)> fields{};
        fields.fill(fieldCount);
        for (std::uint32_t i = 0; i < fieldCount; ++i)
        {
            if (descriptors[i].index != i)
                continue;
            for (std::size_t field = 0; field < hashes.size(); ++field)
                if (descriptors[i].hash == hashes[field])
                    fields[field] = i;
        }
        if (std::find(fields.begin(), fields.end(), fieldCount) != fields.end())
        {
            static bool reportedMissing = false;
            if (!reportedMissing)
            {
                reportedMissing = true;
                char message[128]{};
                std::snprintf(message, sizeof(message), "[WARNING] %s: required timecycle fields were not found.", name);
                Logger::Log(message);
            }
            return;
        }

        static bool reportedFields = false;
        if (!reportedFields)
        {
            reportedFields = true;
            char message[128]{};
            std::snprintf(message, sizeof(message),
                "[INFO] %s: resolved %zu timecycle intensity fields.", name, fields.size());
            Logger::Log(message);
        }

        weatherPending = UpdateWeather<Hashes...>(manager, name, enabled, weatherPending, fields);

        static std::array<std::uintptr_t, 4096> modifiers{};
        RecordArray list{};
        if (!Read(manager + 0x40, &list, sizeof(list)) || list.count == 0 ||
            list.count > list.capacity || list.capacity > modifiers.size() ||
            !Read(list.entries, modifiers.data(), list.count * sizeof(modifiers[0])))
            return;

        static std::array<ModifierValue, fieldCount> values{};
        unsigned int changed = 0;
        bool scanComplete = true;
        ++scan;
        for (std::size_t i = 0; i < list.count; ++i)
        {
            RecordArray entries{};
            if (!modifiers[i])
                continue;

            std::uint32_t nameHash = 0;
            if (!Read(modifiers[i], &entries, sizeof(entries)) ||
                !Read(modifiers[i] + 0x10, &nameHash, sizeof(nameHash)) ||
                entries.count > entries.capacity || entries.count > values.size() ||
                (entries.count && !Read(entries.entries, values.data(), entries.count * sizeof(values[0]))))
            {
                scanComplete = false;
                continue;
            }

            for (std::size_t j = 0; j < entries.count; ++j)
            {
                const auto& value = values[j];
                if (std::find(fields.begin(), fields.end(), value.field) == fields.end())
                    continue;

                const auto address = entries.entries + j * sizeof(values[0]);
                auto original = saved.find(address);
                if (original != saved.end() &&
                    (original->second.modifier != modifiers[i] || original->second.nameHash != nameHash ||
                        original->second.value.secondary != value.secondary))
                {
                    saved.erase(original);
                    original = saved.end();
                }

                if (original != saved.end())
                    original->second.lastSeen = scan;

                const bool nonzero = (value.intensity & 0x7FFFFFFFu) != 0;
                if (!enabled && original != saved.end() && nonzero)
                {
                    saved.erase(original);
                    continue;
                }
                if (enabled ? !nonzero : original == saved.end())
                    continue;

                RecordArray currentList{};
                RecordArray currentEntries{};
                std::uintptr_t currentModifier = 0;
                std::uint32_t currentNameHash = 0;
                if (!Read(manager + 0x40, &currentList, sizeof(currentList)) ||
                    currentList.entries != list.entries || currentList.count != list.count ||
                    !Read(list.entries + i * sizeof(currentModifier), &currentModifier, sizeof(currentModifier)) ||
                    currentModifier != modifiers[i] || !Read(currentModifier, &currentEntries, sizeof(currentEntries)) ||
                    currentEntries.entries != entries.entries || currentEntries.count != entries.count ||
                    !Read(currentModifier + 0x10, &currentNameHash, sizeof(currentNameHash)) || currentNameHash != nameHash)
                {
                    scanComplete = false;
                    break;
                }

                if (enabled)
                {
                    const SavedIntensity snapshot{ modifiers[i], nameHash, value, scan };
                    auto [position, inserted] = saved.try_emplace(address, snapshot);
                    if (WriteIntensity(address, value, 0))
                    {
                        position->second = snapshot;
                        ++changed;
                    }
                    else if (inserted)
                        saved.erase(position);
                }
                else if (WriteIntensity(address, value, original->second.value.intensity))
                {
                    saved.erase(original);
                    ++changed;
                }
            }
        }

        RecordArray currentList{};
        if (scanComplete && Read(manager + 0x40, &currentList, sizeof(currentList)) &&
            currentList.entries == list.entries && currentList.count == list.count)
        {
            for (auto it = saved.begin(); it != saved.end();)
            {
                if (it->second.lastSeen != scan)
                    it = saved.erase(it);
                else
                    ++it;
            }
        }

        if (changed != 0)
        {
            char message[128]{};
            std::snprintf(message, sizeof(message), enabled ?
                "[SUCCESS] %s: zeroed intensity in %u loaded timecycle modifier records." :
                "[SUCCESS] %s: restored intensity in %u loaded timecycle modifier records.", name, changed);
            Logger::Log(message);
        }
    }
    inline void UpdateBloomVisualSettings(bool enabled)
    {
        struct Setting
        {
            std::uint32_t hash;
            LONG value;
        };

        struct Snapshot
        {
            LONG value = 0;
            bool saved = false;
        };

        constexpr std::array<std::uint32_t, 26> hashes{
            0xC9C3DC42, 0xE7D08433, 0xA4180F6E, 0x404E5357, 0x9892E071,
            0xF1B4434B, 0xB550DC1A, 0x126F7111, 0xC73347F8, 0x1024D58D,
            0x5124A371, 0x18FCF5EA, 0x9CD4F294, 0xDD606388, 0x13708671,
            0x09F4FB6F, 0x1D86B546, 0xA4EA52ED, 0x565A7701, 0x7B130762,
            0xFFB6177C, 0xD55E0C2A, 0x749BFD44, 0xDDB2912D, 0x1356EE18,
            0x90FC6A67
        };
        constexpr std::size_t firstAiryColor = 5;
        constexpr std::size_t airyColorCount = hashes.size() - firstAiryColor;
        static_assert(sizeof(Setting) == 8);

        static std::array<Snapshot, hashes.size()> settings{};
        static std::array<Snapshot, airyColorCount> colors{};
        static std::uintptr_t previousTable = 0;
        static bool pending = false;
        static ULONGLONG nextUpdate = 0;
        static bool wasEnabled = false;
        if (!enabled && !pending)
        {
            wasEnabled = false;
            return;
        }

        const auto now = GetTickCount64();
        if (wasEnabled == enabled && now < nextUpdate)
            return;
        wasEnabled = enabled;
        nextUpdate = now + 2000;

        static const auto manager = FindManager();
        if (!manager)
            return;
        const auto base = manager - 0x04A96E80;
        constexpr std::uint8_t loadColors[] = { 0x48, 0x8D, 0x0D, 0x74, 0xC5, 0xA6, 0x03 };
        constexpr std::uint8_t readSetting[] = {
            0x48, 0x89, 0x5C, 0x24, 0x08, 0x44, 0x0F, 0xB7, 0x59, 0x10, 0x48, 0x8B, 0x59, 0x08
        };
        std::uint8_t colorCode[sizeof(loadColors)]{};
        std::uint8_t settingCode[sizeof(readSetting)]{};
        if (!Read(base + 0x0060B455, colorCode, sizeof(colorCode)) ||
            std::memcmp(colorCode, loadColors, sizeof(colorCode)) != 0 ||
            !Read(base + 0x00F2B3C4, settingCode, sizeof(settingCode)) ||
            std::memcmp(settingCode, readSetting, sizeof(settingCode)) != 0)
            return;

        unsigned int changed = 0;
        const auto updateValue = [&](std::uintptr_t address, LONG current, Snapshot& snapshot)
        {
            if (enabled ? (static_cast<std::uint32_t>(current) & 0x7FFFFFFF) == 0 : !snapshot.saved)
                return;
            if (!enabled && current != 0)
            {
                snapshot = {};
                return;
            }

            MEMORY_BASIC_INFORMATION region{};
            if (!VirtualQuery(reinterpret_cast<const void*>(address), &region, sizeof(region)) ||
                region.State != MEM_COMMIT || (region.Protect & PAGE_GUARD) ||
                (region.Protect & 0xFF) != PAGE_READWRITE)
                return;
            auto* value = reinterpret_cast<volatile LONG*>(address);
            if (InterlockedCompareExchange(value, enabled ? 0 : snapshot.value, current) != current)
                return;
            snapshot = enabled ? Snapshot{ current, true } : Snapshot{};
            ++changed;
        };

        RecordArray table{};
        if (Read(base + 0x04A96E00, &table, sizeof(table)))
        {
            if (table.entries != previousTable)
            {
                previousTable = table.entries;
                settings = {};
            }
            static std::array<Setting, 8192> records{};
            if (table.count && table.count <= table.capacity && table.count <= records.size() &&
                Read(table.entries, records.data(), table.count * sizeof(records[0])))
            {
                for (std::size_t i = 0; i < hashes.size(); ++i)
                {
                    const auto end = records.begin() + table.count;
                    const auto found = std::lower_bound(records.begin(), end, hashes[i],
                        [](const Setting& entry, std::uint32_t hash) { return entry.hash < hash; });
                    if (found != end && found->hash == hashes[i])
                    {
                        const auto address = table.entries + (found - records.begin()) * sizeof(Setting);
                        Setting current{};
                        std::uintptr_t activeTable = 0;
                        if (Read(base + 0x04A96E00, &activeTable, sizeof(activeTable)) && activeTable == table.entries &&
                            Read(address, &current, sizeof(current)) && current.hash == hashes[i])
                            updateValue(address + offsetof(Setting, value), current.value, settings[i]);
                    }
                }
            }
        }

        for (std::size_t i = 0; i < colors.size(); ++i)
        {
            const auto scale = i / 3;
            const auto channel = i % 3;
            const auto address = base + 0x040779F0 + scale * 0x10 + channel * sizeof(LONG);
            LONG current = 0;
            if (Read(address, &current, sizeof(current)))
                updateValue(address, current, colors[i]);
        }

        const auto isSaved = [](const Snapshot& snapshot) { return snapshot.saved; };
        pending = std::any_of(settings.begin(), settings.end(), isSaved) ||
            std::any_of(colors.begin(), colors.end(), isSaved);

        if (changed)
        {
            char message[128]{};
            std::snprintf(message, sizeof(message), enabled ?
                "[SUCCESS] DisableBloom: zeroed %u visual settings and Airy bloom color values." :
                "[SUCCESS] DisableBloom: restored %u visual settings and Airy bloom color values.", changed);
            Logger::Log(message);
        }
    }

    float overscanScale = 1.25f;
    std::atomic<float> cameraFovScale{ 1.0f };
    bool overscanProjectionRequired = false;
    bool runtimeFovReady = false;
    bool overscanFovEnabled = false;
    bool ssrCoverageEnabled = false;

    constexpr unsigned int maxFovProbeSamples = 48;
    struct FovProbeSample
    {
        std::atomic<bool> ready{ false };
        ULONGLONG elapsedMs = 0;
        float original = 0.0f;
        float expanded = 0.0f;
        std::uintptr_t camera = 0;
        std::uintptr_t context = 0;
    };
    FovProbeSample fovProbeSamples[maxFovProbeSamples];
    std::atomic<bool> fovProbeActive{ false };
    std::atomic<unsigned int> fovProbeCalls{ 0 };
    std::atomic<unsigned int> fovProbeCount{ 0 };
    std::atomic<int> fovProbeLastBucket{ -999999 };
    std::atomic<ULONGLONG> fovProbeLastCapture{ 0 };
    ULONGLONG fovProbeStart = 0;
    ULONGLONG fovProbeLastPoll = 0;
    ULONGLONG fovProbeNextStatus = 10000;
    unsigned int fovProbeReported = 0;

    void CaptureFovProbe(float original, float expanded, std::uintptr_t camera, std::uintptr_t context)
    {
        if (!fovProbeActive.load(std::memory_order_acquire))
            return;
        fovProbeCalls.fetch_add(1, std::memory_order_relaxed);
        if (!std::isfinite(original) || !std::isfinite(expanded) ||
            original < 1.0f || original > 170.0f ||
            fovProbeCount.load(std::memory_order_relaxed) >= maxFovProbeSamples)
            return;
        const int bucket = static_cast<int>(original / 2.0f);
        if (bucket == fovProbeLastBucket.load(std::memory_order_relaxed))
            return;
        const ULONGLONG now = GetTickCount64();
        ULONGLONG previous = fovProbeLastCapture.load(std::memory_order_relaxed);
        if (now - previous < 200 ||
            !fovProbeLastCapture.compare_exchange_strong(previous, now, std::memory_order_acq_rel))
            return;
        fovProbeLastBucket.store(bucket, std::memory_order_relaxed);
        const unsigned int slot = fovProbeCount.fetch_add(1, std::memory_order_acq_rel);
        if (slot >= maxFovProbeSamples)
            return;
        FovProbeSample& sample = fovProbeSamples[slot];
        sample.elapsedMs = now - fovProbeStart;
        sample.original = original;
        sample.expanded = expanded;
        sample.camera = camera;
        sample.context = context;
        sample.ready.store(true, std::memory_order_release);
    }

    bool IsReadableProjectionRange(const void* address, std::size_t bytes)
    {
        MEMORY_BASIC_INFORMATION region = {};
        if (!VirtualQuery(address, &region, sizeof(region)) || region.State != MEM_COMMIT ||
            (region.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
            return false;
        const DWORD protection = region.Protect & 0xff;
        if (protection != PAGE_READWRITE && protection != PAGE_WRITECOPY &&
            protection != PAGE_EXECUTE_READWRITE && protection != PAGE_EXECUTE_WRITECOPY &&
            protection != PAGE_READONLY && protection != PAGE_EXECUTE_READ)
            return false;
        const std::uintptr_t start = reinterpret_cast<std::uintptr_t>(address);
        const std::uintptr_t regionEnd = reinterpret_cast<std::uintptr_t>(region.BaseAddress) + region.RegionSize;
        return regionEnd >= start && bytes <= regionEnd - start;
    }

    constexpr std::uint32_t fovConstructorCallRva = 0x003182AFu;
    constexpr std::uint32_t fovConstructorRva = 0x026F50E4u;
    constexpr std::uint32_t fovWriteRva = 0x026F518Cu;
    constexpr std::uint32_t fovInputRva = 0x026F5108u;
    constexpr std::uint32_t fovProjectionReadRva = 0x026F658Au;
    constexpr std::uint8_t originalFovProjectionRead[] = { 0xF3, 0x0F, 0x10, 0x83, 0x4C, 0x06, 0x00, 0x00 };
    constexpr std::uint8_t patchedFovProjectionRead[] = { 0xF3, 0x0F, 0x10, 0x83, 0x40, 0x06, 0x00, 0x00 };
    std::uint8_t* fovProjectionReadSite = nullptr;
    constexpr std::uint8_t originalFovWrite[] = { 0xF3, 0x0F, 0x11, 0xB9, 0x40, 0x06, 0x00, 0x00 };
    constexpr std::uint8_t originalFovInput[] = { 0x0F, 0x28, 0xF9 };
    constexpr std::size_t fovCallLength = 5;
    std::uint8_t* fovCallSite = nullptr;
    std::uint8_t originalFovCall[fovCallLength] = {};
    std::uint8_t fovCallPatch[fovCallLength] = {};
    void* fovRelay = nullptr;
    float ExpandCameraFov(float original, std::uintptr_t camera, std::uintptr_t context)
    {
        if (!std::isfinite(original) || original < 1.0f || original > 170.0f)
        {
            CaptureFovProbe(original, original, camera, context);
            return original;
        }
        float result = original;
        if (overscanFovEnabled)
        {
            constexpr float radiansPerDegree = 0.017453292519943295769f;
            constexpr float degreesPerRadian = 57.295779513082320877f;
            const float expanded = 2.0f * std::atan(std::tan(original * radiansPerDegree * 0.5f) *
                overscanScale) * degreesPerRadian;
            result = std::isfinite(expanded) && expanded < 175.0f ? expanded : original;
        }
        const float fovScale = cameraFovScale.load(std::memory_order_relaxed);
        if (fovScale != 1.0f)
        {
            constexpr float radiansPerDegree = 0.017453292519943295769f;
            constexpr float degreesPerRadian = 57.295779513082320877f;
            const float adjusted = 2.0f * std::atan(std::tan(result * radiansPerDegree * 0.5f) *
                fovScale) * degreesPerRadian;
            if (std::isfinite(adjusted) && adjusted >= 1.0f && adjusted < 175.0f)
                result = adjusted;
        }
        CaptureFovProbe(original, result, camera, context);
        return result;
    }

    void* AllocateFovRelay(std::uintptr_t target)
    {
        SYSTEM_INFO system = {};
        GetSystemInfo(&system);
        const std::uintptr_t granularity = system.dwAllocationGranularity;
        const std::uintptr_t center = target & ~(granularity - 1u);
        for (std::uintptr_t offset = 0; offset <= 0x40000000u; offset += granularity)
        {
            for (unsigned int side = 0; side < 2; ++side)
            {
                if (!offset && side)
                    continue;
                if (side == 0 && center > (std::numeric_limits<std::uintptr_t>::max)() - offset)
                    continue;
                if (side == 1 && center < offset)
                    continue;
                const std::uintptr_t address = side ? center - offset : center + offset;
                const std::int64_t distance = static_cast<std::int64_t>(address) -
                    static_cast<std::int64_t>(target + 5);
                if (distance < (std::numeric_limits<std::int32_t>::min)() ||
                    distance > (std::numeric_limits<std::int32_t>::max)())
                    continue;
                void* block = VirtualAlloc(reinterpret_cast<void*>(address), 4096,
                    MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
                if (block)
                    return block;
            }
        }
        return nullptr;
    }

    bool InstallOverscanFovHook(float scale)
    {
        const auto* base = reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(nullptr));
        if (!base)
            return false;
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0)
            return false;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
            nt->FileHeader.TimeDateStamp != 0x65EEC92Eu || nt->OptionalHeader.SizeOfImage != 0x07397600u ||
            fovConstructorCallRva > nt->OptionalHeader.SizeOfImage - fovCallLength ||
            fovWriteRva > nt->OptionalHeader.SizeOfImage - sizeof(originalFovWrite) ||
            fovInputRva > nt->OptionalHeader.SizeOfImage - sizeof(originalFovInput) ||
            fovProjectionReadRva > nt->OptionalHeader.SizeOfImage - sizeof(originalFovProjectionRead))
            return false;

        auto* site = const_cast<std::uint8_t*>(base + fovConstructorCallRva);
        const auto* fovInput = base + fovInputRva;
        const auto* fovWrite = base + fovWriteRva;
        const auto* projectionRead = base + fovProjectionReadRva;
        if (!IsReadableProjectionRange(site, fovCallLength) ||
            !IsReadableProjectionRange(fovInput, sizeof(originalFovInput)) ||
            !IsReadableProjectionRange(fovWrite, sizeof(originalFovWrite)) ||
            !IsReadableProjectionRange(projectionRead, sizeof(originalFovProjectionRead)) ||
            site[0] != 0xE8 ||
            std::memcmp(fovInput, originalFovInput, sizeof(originalFovInput)) != 0 ||
            std::memcmp(fovWrite, originalFovWrite, sizeof(originalFovWrite)) != 0 ||
            std::memcmp(projectionRead, originalFovProjectionRead, sizeof(originalFovProjectionRead)) != 0)
            return false;

        std::int32_t originalDisplacement = 0;
        std::memcpy(&originalDisplacement, site + 1, sizeof(originalDisplacement));
        const auto originalTarget = reinterpret_cast<std::uintptr_t>(site + fovCallLength) +
            static_cast<std::intptr_t>(originalDisplacement);
        if (originalTarget != reinterpret_cast<std::uintptr_t>(base + fovConstructorRva))
            return false;

        void* relay = AllocateFovRelay(reinterpret_cast<std::uintptr_t>(site));
        if (!relay)
            return false;

        std::vector<std::uint8_t> code;
        code.reserve(256);
        const auto emit = [&code](std::initializer_list<std::uint8_t> bytes)
        {
            code.insert(code.end(), bytes.begin(), bytes.end());
        };
        const auto emitAddress = [&code](std::uintptr_t address)
        {
            const auto* bytes = reinterpret_cast<const std::uint8_t*>(&address);
            code.insert(code.end(), bytes, bytes + sizeof(address));
        };

        emit({ 0x9C, 0x50, 0x51, 0x52, 0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41, 0x53, 0x55 });
        emit({ 0x48, 0x89, 0xE5, 0x48, 0x83, 0xE4, 0xF0, 0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00 });
        for (std::uint8_t index = 0; index < 6; ++index)
            emit({ 0xF3, 0x0F, 0x7F, static_cast<std::uint8_t>(0x44 + index * 8),
                0x24, static_cast<std::uint8_t>(0x20 + index * 16) });
        emit({ 0x48, 0x89, 0xCA, 0x49, 0x89, 0xF8 });
        emit({ 0xF3, 0x0F, 0x10, 0xC1, 0x48, 0xB8 });
        emitAddress(reinterpret_cast<std::uintptr_t>(&ExpandCameraFov));
        emit({ 0xFF, 0xD0, 0xF3, 0x0F, 0x11, 0x44, 0x24, 0x30 });
        for (std::uint8_t index = 0; index < 6; ++index)
            emit({ 0xF3, 0x0F, 0x6F, static_cast<std::uint8_t>(0x44 + index * 8),
                0x24, static_cast<std::uint8_t>(0x20 + index * 16) });
        emit({ 0x48, 0x89, 0xEC, 0x5D, 0x41, 0x5B, 0x41, 0x5A, 0x41, 0x59, 0x41, 0x58, 0x5A, 0x59,
            0x58, 0x9D, 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 });
        emitAddress(originalTarget);

        if (code.size() > 4096)
        {
            VirtualFree(relay, 0, MEM_RELEASE);
            return false;
        }
        std::memcpy(relay, code.data(), code.size());
        DWORD relayProtection = 0;
        if (!VirtualProtect(relay, 4096, PAGE_EXECUTE_READ, &relayProtection) ||
            !FlushInstructionCache(GetCurrentProcess(), relay, code.size()))
        {
            VirtualFree(relay, 0, MEM_RELEASE);
            return false;
        }

        const std::int64_t displacement = static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(relay)) -
            static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(site + fovCallLength));
        if (displacement < (std::numeric_limits<std::int32_t>::min)() ||
            displacement > (std::numeric_limits<std::int32_t>::max)())
        {
            VirtualFree(relay, 0, MEM_RELEASE);
            return false;
        }
        const std::int32_t relative = static_cast<std::int32_t>(displacement);
        std::uint8_t patch[fovCallLength] = { 0xE8, 0, 0, 0, 0 };
        std::memcpy(patch + 1, &relative, sizeof(relative));
        DWORD oldProtection = 0;
        if (!VirtualProtect(site, sizeof(patch), PAGE_EXECUTE_READWRITE, &oldProtection))
        {
            VirtualFree(relay, 0, MEM_RELEASE);
            return false;
        }
        if (site[0] != 0xE8 || std::memcmp(site + 1, &originalDisplacement, sizeof(originalDisplacement)) != 0)
        {
            DWORD ignored = 0;
            VirtualProtect(site, sizeof(patch), oldProtection, &ignored);
            VirtualFree(relay, 0, MEM_RELEASE);
            return false;
        }
        overscanScale = scale;
        std::memcpy(originalFovCall, site, fovCallLength);
        std::memcpy(site, patch, sizeof(patch));
        const bool flushed = FlushInstructionCache(GetCurrentProcess(), site, sizeof(patch)) != FALSE;
        DWORD ignored = 0;
        const bool restored = VirtualProtect(site, sizeof(patch), oldProtection, &ignored) != FALSE;
        if (!flushed || !restored)
        {
            bool rolledBack = false;
            DWORD writable = 0;
            if (VirtualProtect(site, sizeof(patch), PAGE_EXECUTE_READWRITE, &writable))
            {
                if (std::memcmp(site, patch, sizeof(patch)) == 0)
                {
                    std::memcpy(site, originalFovCall, sizeof(originalFovCall));
                    rolledBack = FlushInstructionCache(GetCurrentProcess(), site, sizeof(patch)) != FALSE;
                }
                VirtualProtect(site, sizeof(patch), oldProtection, &ignored);
            }
            if (rolledBack)
                VirtualFree(relay, 0, MEM_RELEASE);
            else
            {
                fovCallSite = site;
                fovRelay = relay;
                std::memcpy(fovCallPatch, patch, sizeof(patch));
                Logger::Log("[WARNING] ExpandSSRCoverage: constructor call rollback could not be confirmed; relay retained for safety.");
            }
            return false;
        }
        fovCallSite = site;
        fovRelay = relay;
        std::memcpy(fovCallPatch, patch, sizeof(patch));
        return true;
    }

    void RemoveOverscanFovHook()
    {
        if (!fovCallSite)
            return;
        DWORD protection = 0;
        if (!VirtualProtect(fovCallSite, fovCallLength, PAGE_EXECUTE_READWRITE, &protection))
            return;
        const bool owned = std::memcmp(fovCallSite, fovCallPatch, sizeof(fovCallPatch)) == 0;
        if (owned)
        {
            std::memcpy(fovCallSite, originalFovCall, sizeof(originalFovCall));
            FlushInstructionCache(GetCurrentProcess(), fovCallSite, sizeof(originalFovCall));
        }
        DWORD ignored = 0;
        VirtualProtect(fovCallSite, fovCallLength, protection, &ignored);
        fovCallSite = nullptr;
        if (owned && fovRelay)
            VirtualFree(fovRelay, 0, MEM_RELEASE);
        fovRelay = nullptr;
    }

    bool InstallOverscanProjectionPatch()
    {
        if (fovProjectionReadSite)
            return false;
        const auto* base = reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(nullptr));
        if (!base)
            return false;
        auto* site = const_cast<std::uint8_t*>(base + fovProjectionReadRva);
        if (!IsReadableProjectionRange(site, sizeof(originalFovProjectionRead)) ||
            std::memcmp(site, originalFovProjectionRead, sizeof(originalFovProjectionRead)) != 0)
            return false;
        DWORD protection = 0;
        if (!VirtualProtect(site, sizeof(originalFovProjectionRead), PAGE_EXECUTE_READWRITE, &protection))
            return false;
        if (std::memcmp(site, originalFovProjectionRead, sizeof(originalFovProjectionRead)) != 0)
        {
            DWORD ignored = 0;
            VirtualProtect(site, sizeof(originalFovProjectionRead), protection, &ignored);
            return false;
        }
        site[4] = patchedFovProjectionRead[4];
        fovProjectionReadSite = site;
        const bool flushed = FlushInstructionCache(GetCurrentProcess(), site, sizeof(patchedFovProjectionRead)) != FALSE;
        DWORD ignored = 0;
        const bool restored = VirtualProtect(site, sizeof(patchedFovProjectionRead), protection, &ignored) != FALSE;
        return flushed && restored &&
            std::memcmp(site, patchedFovProjectionRead, sizeof(patchedFovProjectionRead)) == 0;
    }

    bool RemoveOverscanProjectionPatch()
    {
        if (!fovProjectionReadSite)
            return true;
        DWORD protection = 0;
        if (!VirtualProtect(fovProjectionReadSite, sizeof(patchedFovProjectionRead), PAGE_EXECUTE_READWRITE, &protection))
            return false;
        const bool owned = std::memcmp(fovProjectionReadSite, patchedFovProjectionRead, sizeof(patchedFovProjectionRead)) == 0;
        bool flushed = false;
        if (owned)
        {
            fovProjectionReadSite[4] = originalFovProjectionRead[4];
            flushed = FlushInstructionCache(GetCurrentProcess(), fovProjectionReadSite, sizeof(originalFovProjectionRead)) != FALSE;
        }
        DWORD ignored = 0;
        const bool restored = VirtualProtect(fovProjectionReadSite, sizeof(originalFovProjectionRead), protection, &ignored) != FALSE;
        if (restored && std::memcmp(fovProjectionReadSite, originalFovProjectionRead, sizeof(originalFovProjectionRead)) == 0)
            fovProjectionReadSite = nullptr;
        return owned && flushed && restored;
    }
}

namespace Main::Display
{
    void UpdateTimecycleEffects(bool vignetteEnabled, bool bloomEnabled, bool chromaticAberrationEnabled)
    {
        UpdateEffect<0xC51C8494>("DisableVignette", vignetteEnabled);
        UpdateEffect<0x5023E4B8, 0x2598D888>("DisableBloom", bloomEnabled);
        UpdateBloomVisualSettings(bloomEnabled);
        UpdateEffect<0xB0709481, 0xBC790EB8>("DisableChromaticAberration", chromaticAberrationEnabled);
    }

    void UpdateCompositeDithering(bool enabled)
    {
        static bool applied = false;
        static bool warned = false;
        if (enabled == applied)
        {
            warned = false;
            return;
        }

        static const auto manager = FindManager();
        if (!manager)
        {
            if (!warned)
                Logger::Log("[WARNING] DisableCompositeDithering: unsupported game build.");
            warned = true;
            return;
        }

        const auto base = manager - 0x04A96E80;
        constexpr std::uintptr_t firstOffset = 0x005E4408;
        constexpr std::uintptr_t secondOffset = 0x005E4428;
        constexpr LONG64 firstOriginal = 0xF3CB8B44DA140F00;
        constexpr LONG64 secondOriginal = 0x41C2140F44202444;
        constexpr LONG64 firstDisabled = 0xF3CB8B44DB570F00;
        constexpr LONG64 secondDisabled = 0x41C0570F45202444;
        const LONG64 firstBefore = enabled ? firstOriginal : firstDisabled;
        const LONG64 secondBefore = enabled ? secondOriginal : secondDisabled;
        const LONG64 firstAfter = enabled ? firstDisabled : firstOriginal;
        const LONG64 secondAfter = enabled ? secondDisabled : secondOriginal;
        auto* first = reinterpret_cast<LONG64*>(base + firstOffset);
        auto* second = reinterpret_cast<LONG64*>(base + secondOffset);
        constexpr auto span = secondOffset - firstOffset + sizeof(LONG64);

        LONG64 firstCurrent = 0;
        LONG64 secondCurrent = 0;
        auto reportFailure = [&]()
        {
            if (!warned)
                Logger::Log("[WARNING] DisableCompositeDithering: could not change the composite dither upload.");
            warned = true;
        };
        if (!Read(base + firstOffset, &firstCurrent, sizeof(firstCurrent)) ||
            !Read(base + secondOffset, &secondCurrent, sizeof(secondCurrent)) ||
            firstCurrent != firstBefore || secondCurrent != secondBefore)
        {
            reportFailure();
            return;
        }

        DWORD oldProtection = 0;
        if (!VirtualProtect(first, span, PAGE_EXECUTE_READWRITE, &oldProtection))
        {
            reportFailure();
            return;
        }

        const bool firstChanged = InterlockedCompareExchange64(first, firstAfter, firstBefore) == firstBefore;
        const bool secondChanged = firstChanged &&
            InterlockedCompareExchange64(second, secondAfter, secondBefore) == secondBefore;
        if (firstChanged && !secondChanged)
            InterlockedCompareExchange64(first, firstBefore, firstAfter);

        const bool flushed = FlushInstructionCache(GetCurrentProcess(), first, span) != FALSE;
        DWORD ignored = 0;
        const bool restored = VirtualProtect(first, span, oldProtection, &ignored) != FALSE;
        if (!secondChanged)
        {
            reportFailure();
            return;
        }

        applied = enabled;
        warned = false;
        if (!flushed || !restored)
            Logger::Log("[WARNING] DisableCompositeDithering: upload changed, but code cache or protection restoration failed.");
        else
            Logger::Log(enabled ?
                "[SUCCESS] DisableCompositeDithering: zeroed composite dither amplitudes." :
                "[SUCCESS] DisableCompositeDithering: restored composite dither amplitudes.");
    }

    void ReportCameraStartupStatus()
    {
        if (!Logger::IsDebugEnabled())
            return;
        if (fovCallSite && (ssrCoverageEnabled || cameraFovScale.load(std::memory_order_relaxed) != 1.0f))
        {
            fovProbeStart = GetTickCount64();
            fovProbeLastPoll = fovProbeStart;
            fovProbeActive.store(true, std::memory_order_release);
            Logger::Debug(ssrCoverageEnabled ?
                "ExpandSSRCoverage: FOV probe active for 90s (no camera changes)." :
                "CameraFOVOffset: FOV probe active for 90s (no camera changes).");
        }
    }

    void UpdateOverscanDiagnostics()
    {
        if (!fovProbeActive.load(std::memory_order_acquire))
            return;
        const ULONGLONG now = GetTickCount64();
        if (now - fovProbeLastPoll < 500)
            return;
        fovProbeLastPoll = now;
        unsigned int published = fovProbeCount.load(std::memory_order_acquire);
        if (published > maxFovProbeSamples)
            published = maxFovProbeSamples;
        while (fovProbeReported < published &&
            fovProbeSamples[fovProbeReported].ready.load(std::memory_order_acquire))
        {
            const FovProbeSample& sample = fovProbeSamples[fovProbeReported];
            char message[256] = {};
            std::snprintf(message, sizeof(message),
                "%s: FOV sample #%u at +%llums original=%.3f adjusted=%.3f camera=%p context=%p.",
                ssrCoverageEnabled ? "ExpandSSRCoverage" : "CameraFOVOffset",
                fovProbeReported + 1, static_cast<unsigned long long>(sample.elapsedMs),
                sample.original, sample.expanded, reinterpret_cast<void*>(sample.camera),
                reinterpret_cast<void*>(sample.context));
            Logger::Debug(message);
            ++fovProbeReported;
        }
        const ULONGLONG elapsed = now - fovProbeStart;
        if (elapsed >= fovProbeNextStatus || published >= maxFovProbeSamples || elapsed >= 90000)
        {
            char message[200] = {};
            std::snprintf(message, sizeof(message),
                "%s: FOV probe +%llums calls=%u captured=%u reported=%u.",
                ssrCoverageEnabled ? "ExpandSSRCoverage" : "CameraFOVOffset",
                static_cast<unsigned long long>(elapsed), fovProbeCalls.load(),
                published, fovProbeReported);
            Logger::Debug(message);
            fovProbeNextStatus = elapsed + 10000;
            if (published >= maxFovProbeSamples || elapsed >= 90000)
                fovProbeActive.store(false, std::memory_order_release);
        }
    }

    void InitializeOverscanFov(bool modEnabled, bool enabled, float scale, int testMode, float fovOffset)
    {
        if (!modEnabled)
            return;
        if (!std::isfinite(fovOffset))
            fovOffset = 0.0f;
        const float cameraFovOffset = std::clamp(fovOffset, -3.0f, 3.0f);
        ssrCoverageEnabled = enabled;
        overscanFovEnabled = enabled && testMode < 3;
        overscanProjectionRequired = enabled && (testMode == 0 || testMode == 2);
        const bool projectionEnabled = overscanProjectionRequired || cameraFovOffset != 0.0f;
        const bool cropEnabled = enabled && (testMode == 0 || testMode >= 3);
        overscanScale = scale;
        runtimeFovReady = InstallOverscanFovHook(overscanFovEnabled || cameraFovOffset == 0.0f ? scale : 1.0f);
        if (!runtimeFovReady && (overscanFovEnabled || cameraFovOffset != 0.0f))
        {
            Logger::Log(enabled ?
                "[WARNING] ExpandSSRCoverage: FOV constructor call hook rejected by build, instruction validation, or memory protection; no changes made." :
                "[WARNING] CameraFOVOffset: FOV constructor call hook rejected by build, instruction validation, or memory protection; no changes made.");
            return;
        }
        if (projectionEnabled)
        {
            if (!InstallOverscanProjectionPatch())
            {
                if (!RemoveOverscanProjectionPatch())
                    Logger::Log(enabled ?
                        "[WARNING] ExpandSSRCoverage: projection patch rollback could not be confirmed." :
                        "[WARNING] CameraFOVOffset: projection patch rollback could not be confirmed.");
                runtimeFovReady = false;
                RemoveOverscanFovHook();
                Logger::Log(enabled ?
                    "[WARNING] ExpandSSRCoverage: projection FOV read patch rejected; constructor hook rollback attempted." :
                    "[WARNING] CameraFOVOffset: projection FOV read patch rejected; constructor hook rollback attempted.");
                return;
            }
            Logger::Debug(enabled ?
                "ExpandSSRCoverage: projection FOV read +0x026F658A changed from [rbx+0x64C] to [rbx+0x640]." :
                "CameraFOVOffset: projection FOV read +0x026F658A changed from [rbx+0x64C] to [rbx+0x640].");
        }
        cameraFovScale.store(std::pow(1.25f, cameraFovOffset), std::memory_order_relaxed);
        const bool vulkanCrop = cropEnabled && (scale > 1.0f || testMode == 6) &&
            WaterReflections::InitializeOverscanCrop(overscanScale, testMode);
        char message[256] = {};
        if (enabled)
        {
            std::snprintf(message, sizeof(message),
                "[INFO] ExpandSSRCoverage: mode=%d, constructor=%s, projection=%s, VulkanShader=%s, scale=%.3f; DX12 crop not implemented.",
                testMode, fovCallSite ? "on" : "off", fovProjectionReadSite ? "on" : "off",
                vulkanCrop ? (testMode == 4 ? "pass-through" : testMode == 6 ? "identity" :
                    testMode == 7 ? "flip-728" : testMode == 10 ? "flip-640" : "cropped") : "off",
                testMode == 6 ? 1.0f : (testMode == 7 || testMode == 10) ? -1.0f :
                    testMode == 8 ? 1.25f : testMode == 9 ? 1.5f : scale);
            Logger::Log(message);
        }
        if (cameraFovOffset != 0.0f)
        {
            std::snprintf(message, sizeof(message),
                "[INFO] CameraFOVOffset: strength=%+.2f, FOV scale=%.3f, constructor=%s, projection=%s.",
                cameraFovOffset, cameraFovScale.load(std::memory_order_relaxed), fovCallSite ? "on" : "off", fovProjectionReadSite ? "on" : "off");
            Logger::Log(message);
        }
    }

    void ApplyCameraFovOffset(float offset)
    {
        if (!std::isfinite(offset))
            return;
        offset = std::clamp(offset, -3.0f, 3.0f);
        if (!runtimeFovReady)
        {
            Logger::Log("[WARNING] CameraFOVOffset: live FOV hook is unavailable.");
            return;
        }

        if (offset != 0.0f || overscanProjectionRequired)
        {
            if (!fovProjectionReadSite && !InstallOverscanProjectionPatch())
            {
                RemoveOverscanProjectionPatch();
                Logger::Log("[WARNING] CameraFOVOffset: projection patch rejected; offset not changed.");
                return;
            }
            cameraFovScale.store(std::pow(1.25f, offset), std::memory_order_relaxed);
        }
        else
        {
            cameraFovScale.store(1.0f, std::memory_order_relaxed);
            if (!RemoveOverscanProjectionPatch())
                Logger::Log("[WARNING] CameraFOVOffset: projection read restoration could not be confirmed.");
        }
    }

    void ShutdownCamera()
    {
        runtimeFovReady = false;
        fovProbeActive.store(false, std::memory_order_release);
        if (!RemoveOverscanProjectionPatch())
            Logger::Log(ssrCoverageEnabled ?
                "[WARNING] ExpandSSRCoverage: projection FOV read could not be restored during shutdown." :
                "[WARNING] CameraFOVOffset: projection FOV read could not be restored during shutdown.");
        RemoveOverscanFovHook();
    }
}
