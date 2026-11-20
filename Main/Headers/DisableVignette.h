#pragma once

#include "../../Headers/Logger.h"
#include <windows.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unordered_map>

namespace Main::DisableVignette
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

    inline bool UpdateWeather(std::uintptr_t manager, bool enabled, bool pending)
    {
        if (!enabled && !pending)
            return false;

        constexpr std::size_t maxWeatherRecords = 64;
        constexpr std::size_t keyframeCount = 26;
        constexpr std::size_t fieldsPerKeyframe = 592;
        constexpr std::size_t vignetteField = 0x1AD;
        constexpr std::size_t weatherIdOffset = keyframeCount * fieldsPerKeyframe * sizeof(std::uint16_t);
        constexpr std::size_t recordSize = weatherIdOffset + 0x20;
        static_assert(recordSize == 0x7860);

        struct WeatherSnapshot
        {
            std::uint32_t id;
            std::array<std::uint16_t, keyframeCount> intensity;
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

            for (std::size_t keyframe = 0; keyframe < keyframeCount; ++keyframe)
            {
                const auto offset = (keyframe * fieldsPerKeyframe + vignetteField) * sizeof(std::uint16_t);
                const auto address = recordAddress + offset;
                std::uint16_t value = 0;
                std::memcpy(&value, record.data() + offset, sizeof(value));

                auto& snapshot = original.intensity[keyframe];
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

        if (changed)
        {
            char message[128]{};
            std::snprintf(message, sizeof(message), enabled ?
                "[SUCCESS] DisableVignette: zeroed intensity in %u base-weather keyframes." :
                "[SUCCESS] DisableVignette: restored intensity in %u base-weather keyframes.", changed);
            Logger::Log(message);
        }

        for (const auto& weather : saved)
            for (const auto value : weather.intensity)
                if (value)
                    return true;
        return false;
    }


    inline void Update(bool enabled)
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
                Logger::Log("[WARNING] DisableVignette: unsupported timecycle modifier manager.");
            }
            return;
        }

        static ULONGLONG nextUpdate = 0;
        const auto now = GetTickCount64();
        if (enabled == wasEnabled && now < nextUpdate)
            return;
        wasEnabled = enabled;
        nextUpdate = now + 2000;

        constexpr std::uint32_t vignetteField = 0x1AD;
        constexpr std::uint32_t vignetteHash = 0xC51C8494;
        const auto base = manager - 0x04A96E80;
        std::uintptr_t registry = 0;
        std::uint32_t field[2]{};
        if (!Read(base + 0x059B76A0, &registry, sizeof(registry)) || registry != base + 0x03A83F90 ||
            !Read(registry + vignetteField * 24, field, sizeof(field)) ||
            field[0] != vignetteField || field[1] != vignetteHash)
            return;

        weatherPending = UpdateWeather(manager, enabled, weatherPending);

        static std::array<std::uintptr_t, 4096> modifiers{};
        RecordArray list{};
        if (!Read(manager + 0x40, &list, sizeof(list)) || list.count == 0 ||
            list.count > list.capacity || list.capacity > modifiers.size() ||
            !Read(list.entries, modifiers.data(), list.count * sizeof(modifiers[0])))
            return;

        static std::array<ModifierValue, 580> values{};
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
                if (value.field != vignetteField)
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
                "[SUCCESS] DisableVignette: zeroed intensity in %u loaded timecycle modifier records." :
                "[SUCCESS] DisableVignette: restored intensity in %u loaded timecycle modifier records.", changed);
            Logger::Log(message);
        }
    }
}
