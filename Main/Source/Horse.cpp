#include "Horse.h"
#include "../Headers/Helpers/MissionState.h"
#include "../../Headers/Logger.h"
#include "main.h"
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>

namespace
{
    constexpr std::uint64_t playerPedId = 0x096275889B8E0EE0ULL;
    constexpr std::uint64_t isPedOnMount = 0x460BC76A0E10655EULL;
    constexpr std::uint64_t getMount = 0xE7E11B8DCBED1058ULL;
    constexpr std::uint64_t setPedResetFlag = 0xC1E8A365BF3B29F2ULL;
    constexpr int disableHorseAutoJumpFlag = 247;
    constexpr std::uint64_t doesEntityExist = 0xD42BD6EB2E0F1677ULL;
    constexpr std::uint64_t setForceAutoEquip = 0xBE711B14A159E84FULL;
    constexpr std::uint64_t setInstantlyStoreLongarmsOnDismount = 0xB832F1A686B9B810ULL;
    constexpr std::uint64_t getCurrentPedWeapon = 0x3A87E44BB9A01D54ULL;
    constexpr std::uint64_t setCurrentPedWeapon = 0xADF692B254977C0CULL;
    constexpr std::uint64_t isWeaponTwoHanded = 0x0556E9D2ECF39D01ULL;
    constexpr std::uint64_t getWeaponFromHorseHolster = 0xAFFD0CCF31F469B8ULL;
    constexpr std::uint64_t isWeaponHolsterStateChanging = 0x2387D6E9C6B478AAULL;
    constexpr std::uint64_t isControlJustReleased = 0x50F940259D3841E6ULL;
    constexpr std::uint64_t animpostfxIsRunning = 0x4A123E85D7C4CA0BULL;
    constexpr std::uint64_t getHashKey = 0xFD340785ADF8CFB7ULL;
    constexpr std::uint64_t isControlJustPressed = 0x580417101DDB492FULL;
    constexpr std::uint64_t isControlPressed = 0xF3A21BCD95725A4AULL;
    constexpr std::uint64_t playerId = 0x217E9DC48139933DULL;
    constexpr std::uint64_t getSaddleHorseForPlayer = 0xB48050D326E9A2F3ULL;
    constexpr std::uint64_t isPedDeadOrDying = 0x3317DEDB88C95038ULL;
    constexpr std::uint64_t getRiderOfMount = 0xB676EFDA03DADA52ULL;
    constexpr std::uint64_t isPedBeingLed = 0xAC5045AB7F1A34FDULL;
    constexpr std::uint64_t isPedFleeing = 0xBBCCE00B381F8482ULL;
    constexpr std::uint64_t isPedOnFoot = 0x01FEE67DB37F59B2ULL;
    constexpr std::uint64_t getEntityCoords = 0xA86D5F069399F44DULL;
    constexpr std::uint64_t getSafeCoordForPed = 0xB61C8E878A4199CAULL;
    constexpr std::uint64_t getClosestVehicleNode = 0x240A18690AE96513ULL;
    constexpr std::uint64_t getGroundZFor3DCoord = 0x24FA4267BB8D2431ULL;
    constexpr std::uint64_t getWaterHeight = 0xFCA8B23F28813F69ULL;
    constexpr std::uint64_t setEntityCoordsNoOffset = 0x239A3351AC1DA385ULL;
    constexpr std::uint64_t getWhistleRangeMaxForBondingLevel = 0x78D8C1D4EB80C588ULL;
    constexpr std::uint64_t taskGoToWhistle = 0xBAD6545608CECA6EULL;
    constexpr std::uint64_t taskFollowToOffsetOfEntity = 0x304AE42E357B8C7EULL;
    constexpr std::uint64_t getEntityForwardVector = 0x2412D9C05BB09B97ULL;
    constexpr std::uint64_t getBlipFromEntity = 0x6D2C41A8BD6D6FD0ULL;
    constexpr std::uint64_t doesBlipExist = 0xCD82FA174080B3B1ULL;
    constexpr std::uint64_t blipAddModifier = 0x662D364ABF16DE2FULL;
    constexpr std::uint64_t blipRemoveModifier = 0xB059D7BD3D78C16FULL;
    constexpr std::uint64_t getLoadingScreenActive = 0x71D4BF5890659B0CULL;
    constexpr std::uint64_t isScreenFadedIn = 0x37F9A426FBCF4AF2ULL;
    constexpr std::uint64_t uiFeedGetCurrentMessage = 0xC17F69E1418CD11FULL;
    constexpr std::uint64_t uiFeedClearHelpTextFeed = 0x2F901291EF177B02ULL;
    constexpr ULONGLONG horseGameplayCheckMs = 150;
    constexpr ULONGLONG horseFollowRefreshMs = 650;
    constexpr ULONGLONG horseFollowRecoveryMs = 5500;
    constexpr ULONGLONG horseWhistleCooldownMs = 1000;
    constexpr ULONGLONG horseBlipRefreshMs = 800;
    constexpr ULONGLONG horseHelpPollMs = 75;
    constexpr ULONGLONG horseHelpClearRepeatMs = 250;
    constexpr ULONGLONG horseWhistleHelpWindowMs = 1000;
    constexpr ULONGLONG horseDistantWhistleGraceMs = 15000;
    constexpr ULONGLONG horseWeaponSampleMs = 100;
    constexpr ULONGLONG horseWeaponInputMs = 50;
    constexpr ULONGLONG horseWeaponStableMs = 5000;
    constexpr ULONGLONG horseWeaponManualGraceMs = 5000;
    constexpr ULONGLONG horseWeaponRestoreCooldownMs = 1200;
    constexpr int horseWeaponPoints[] = { 0, 7, 8, 9, 10 };

    bool disableAutoEquip = false;
    bool disableAutoHolster = false;
    bool disableAutoJump = false;
    bool disableWhistleDistance = false;
    bool fixFollowBehavior = false;
    bool followActive = false;
    bool followTaskIssued = false;
    bool pendingWhistle = false;
    std::uint32_t pendingWhistleHorse = 0;
    ULONGLONG pendingWhistleAt = 0;
    std::uint32_t followHorse = 0;
    std::uint32_t whistleControl = 0;
    std::uint32_t followControl = 0;
    std::uint32_t stayControl = 0;
    std::uint32_t fleeControl = 0;
    ULONGLONG nextFollowCheck = 0;
    ULONGLONG followTaskAt = 0;
    ULONGLONG lastFollowProgressAt = 0;
    ULONGLONG nextWhistleAt = 0;
    float lastFollowDistance = 0.0f;
    std::uint32_t horseBlip = 0;
    std::uint32_t horseBlipOwner = 0;
    std::uint32_t recentWhistleHorse = 0;
    bool horseBlipOverridden = false;
    bool horseGameplayReady = true;
    ULONGLONG nextHorseGameplayCheck = 0;
    std::uint32_t inRangeModifier = 0;
    std::uint32_t outOfRangeModifier = 0;
    std::uint32_t previousHelpMessage = 0;
    std::uint32_t helpMessageBeforeWhistle = 0;
    ULONGLONG nextBlipRefresh = 0;
    ULONGLONG nextHelpPoll = 0;
    ULONGLONG helpSuppressionUntil = 0;
    ULONGLONG recentDistantWhistleUntil = 0;
    ULONGLONG lastHelpClearAt = 0;
    std::uint32_t lastClearedHelpMessage = 0;
    std::uint64_t lastPlayerPed = 0;
    bool mounted = false;
    ULONGLONG recentDismountUntil = 0;
    std::uint32_t unarmedWeapon = 0;
    std::uint32_t bowWeapon = 0;
    std::uint32_t improvedBowWeapon = 0;
    std::uint32_t wheelControl = 0;
    std::uint32_t holsterControl = 0;
    std::uint32_t trackedWeapons[5] = {};
    ULONGLONG weaponChangedAt[5] = {};
    unsigned char weaponRestoreAttempts[5] = {};
    ULONGLONG nextWeaponSample = 0;
    ULONGLONG nextWeaponInputCheck = 0;
    ULONGLONG mountedSince = 0;
    ULONGLONG manualHolsterGraceUntil = 0;
    ULONGLONG lastWeaponRestore = 0;
    bool weaponTrackingReady = false;
    bool weaponSnapshotLogged = false;
    std::uint32_t lastReportedWeapons[5] = {};

    std::uint32_t HashName(const char* value)
    {
        nativeInit(getHashKey);
        nativePush64(reinterpret_cast<std::uint64_t>(value));
        return static_cast<std::uint32_t>(*nativeCall());
    }

    bool JustPressed(std::uint32_t control)
    {
        if (control == 0)
            return false;
        nativeInit(isControlJustPressed);
        nativePush64(0);
        nativePush64(control);
        return static_cast<std::uint32_t>(*nativeCall()) != 0;
    }

    bool Pressed(std::uint32_t control)
    {
        if (control == 0)
            return false;
        nativeInit(isControlPressed);
        nativePush64(0);
        nativePush64(control);
        return static_cast<std::uint32_t>(*nativeCall()) != 0;
    }

    bool JustReleased(std::uint32_t control)
    {
        if (control == 0)
            return false;
        nativeInit(isControlJustReleased);
        nativePush64(0);
        nativePush64(control);
        return static_cast<std::uint32_t>(*nativeCall()) != 0;
    }

    void ResetMountedCarrySnapshot();

    void ResetWeaponTracking()
    {
        ResetMountedCarrySnapshot();
        for (int i = 0; i < 5; ++i)
        {
            trackedWeapons[i] = 0;
            weaponChangedAt[i] = 0;
            weaponRestoreAttempts[i] = 0;
            lastReportedWeapons[i] = 0;
        }
        nextWeaponSample = 0;
        nextWeaponInputCheck = 0;
        mountedSince = 0;
        manualHolsterGraceUntil = 0;
        lastWeaponRestore = 0;
        weaponTrackingReady = false;
        weaponSnapshotLogged = false;
    }

    std::uint32_t ReadWeaponSlot(std::uint64_t ped, int attachPoint)
    {
        std::uint32_t weapon = 0;
        nativeInit(getCurrentPedWeapon);
        nativePush64(ped);
        nativePush64(reinterpret_cast<std::uint64_t>(&weapon));
        nativePush64(0);
        nativePush64(static_cast<std::uint64_t>(attachPoint));
        nativePush64(0);
        nativeCall();
        return weapon;
    }

    bool IsLongarm(std::uint32_t weapon)
    {
        if (weapon == 0 || weapon == unarmedWeapon)
            return false;
        if (weapon == bowWeapon || weapon == improvedBowWeapon)
            return true;
        nativeInit(isWeaponTwoHanded);
        nativePush64(weapon);
        return static_cast<std::uint32_t>(*nativeCall()) != 0;
    }

    bool WeaponWheelActive()
    {
        nativeInit(animpostfxIsRunning);
        nativePush64(reinterpret_cast<std::uint64_t>("WheelHUDIn"));
        return static_cast<std::uint32_t>(*nativeCall()) != 0;
    }

    bool InvokeBoolean(std::uint64_t hash)
    {
        nativeInit(hash);
        return static_cast<std::uint32_t>(*nativeCall()) != 0;
    }

    std::uint32_t InvokeInt(std::uint64_t hash, std::uint64_t value)
    {
        nativeInit(hash);
        nativePush64(value);
        return static_cast<std::uint32_t>(*nativeCall());
    }

    using WeaponSelection = bool(*)(const void*, const void*, const void*, const void*);
    using FistsSelection = bool(*)(const void*, const void*);
    using GetWeaponManager = std::uintptr_t(*)(std::uintptr_t);

    struct SelectionCall
    {
        std::uint8_t* site = nullptr;
        std::uint8_t patch[5] = {};
    };

    constexpr std::uint8_t timedSelectionBytes[] = {
        0xE8, 0x0D, 0x94, 0x04, 0x00, 0x48, 0x83, 0xC4, 0x40, 0x5B, 0xC3
    };
    constexpr std::uint8_t queuedFistsBytes[] = {
        0xE8, 0xE5, 0xA8, 0x04, 0x00, 0x48, 0x83, 0xC4, 0x38, 0xC3
    };
    std::atomic<std::uintptr_t> mountedPlayerAddress{ 0 };
    WeaponSelection originalWeaponSelection = nullptr;
    FistsSelection originalFistsSelection = nullptr;
    GetWeaponManager getWeaponManager = nullptr;
    SelectionCall timedSelectionCall;
    SelectionCall queuedFistsCall;

    struct WeaponIdentity
    {
        std::uint64_t low;
        std::uint64_t high;

        bool operator==(const WeaponIdentity& other) const
        {
            return low == other.low && high == other.high;
        }
    };

    constexpr WeaponIdentity unarmedIdentity{ 0x8000000000000000ULL, 0xAA5F3FF571E47700ULL };

    template<typename T>
    bool ReadGameValue(std::uintptr_t address, T& value)
    {
        SIZE_T read = 0;
        return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address),
            &value, sizeof(value), &read) && read == sizeof(value);
    }

    std::uintptr_t WeaponSlotEntry(std::uintptr_t manager, std::uintptr_t slots, int slot)
    {
        std::uint8_t index = 0xFF;
        std::uintptr_t entry = 0;
        if (ReadGameValue(manager + 0x98 + slot, index) && index != 0xFF)
            ReadGameValue(slots + index * sizeof(entry), entry);
        return entry;
    }

    SRWLOCK mountedCarryLock = SRWLOCK_INIT;
    std::uintptr_t mountedCarryOwner = 0;
    WeaponIdentity mountedCarry[4] = {};

    void ResetMountedCarrySnapshot()
    {
        AcquireSRWLockExclusive(&mountedCarryLock);
        mountedCarryOwner = 0;
        ReleaseSRWLockExclusive(&mountedCarryLock);
    }

    void UpdateMountedCarrySnapshot()
    {
        const auto player = mountedPlayerAddress.load(std::memory_order_acquire);
        if (!player || !getWeaponManager || !queuedFistsCall.site)
            return;

        const auto manager = getWeaponManager(player);
        std::uintptr_t owner = 0, slots = 0;
        if (!manager || !ReadGameValue(manager + 0x20, owner) || owner != player ||
            !ReadGameValue(manager + 0x88, slots) || !slots)
            return;

        const auto hand = WeaponSlotEntry(manager, slots, horseWeaponPoints[0]);
        WeaponIdentity current = {}, pending = {};
        if (!hand || !ReadGameValue(hand + 8, current) || !ReadGameValue(hand + 0x28, pending) ||
            !(current == unarmedIdentity) || !(pending == unarmedIdentity))
            return;

        WeaponIdentity carried[4] = {};
        for (int i = 1; i < 5; ++i)
        {
            const auto carry = WeaponSlotEntry(manager, slots, horseWeaponPoints[i]);
            std::uintptr_t weapon = 0;
            if (carry && ReadGameValue(carry + 0x68, weapon) && weapon)
                ReadGameValue(carry + 8, carried[i - 1]);
        }

        AcquireSRWLockExclusive(&mountedCarryLock);
        std::memcpy(mountedCarry, carried, sizeof(carried));
        mountedCarryOwner = player;
        ReleaseSRWLockExclusive(&mountedCarryLock);
    }

    bool PreserveMountedDraw(std::uintptr_t manager, std::uintptr_t options)
    {
        const auto player = mountedPlayerAddress.load(std::memory_order_acquire);
        std::uintptr_t owner = 0, slots = 0;
        std::uint32_t requestedSlot = 0;
        if (!player || !manager || !ReadGameValue(options, requestedSlot) || requestedSlot != 0 ||
            !ReadGameValue(manager + 0x20, owner) || owner != player ||
            !ReadGameValue(manager + 0x88, slots) || !slots)
            return false;

        const auto hand = WeaponSlotEntry(manager, slots, horseWeaponPoints[0]);
        WeaponIdentity current = {}, pending = {};
        std::uintptr_t weapon = 0;
        if (!hand || !ReadGameValue(hand + 8, current) || current == unarmedIdentity || !current.high ||
            !ReadGameValue(hand + 0x28, pending) || !(pending == current) ||
            !ReadGameValue(hand + 0x68, weapon) || !weapon)
            return false;

        bool wasCarried = false;
        AcquireSRWLockShared(&mountedCarryLock);
        if (mountedCarryOwner == player)
        {
            for (const auto& identity : mountedCarry)
                wasCarried |= identity == current;
        }
        ReleaseSRWLockShared(&mountedCarryLock);
        return wasCarried;
    }

    bool OnQueuedFistsSelection(const void* manager, const void* options)
    {
        if (PreserveMountedDraw(reinterpret_cast<std::uintptr_t>(manager),
            reinterpret_cast<std::uintptr_t>(options)))
            return false;
        return originalFistsSelection(manager, options);
    }

    bool PreserveMountedFists(std::uintptr_t manager, std::uintptr_t requested)
    {
        const auto player = mountedPlayerAddress.load(std::memory_order_acquire);
        std::uintptr_t owner = 0;
        std::uintptr_t slots = 0;
        if (!player || !manager || !ReadGameValue(manager + 0x20, owner) || owner != player ||
            !ReadGameValue(manager + 0x88, slots) || !slots)
            return false;

        const auto hand = WeaponSlotEntry(manager, slots, horseWeaponPoints[0]);
        WeaponIdentity current = {}, pending = {}, target = {};
        std::uintptr_t weapon = 0;
        if (!hand || !ReadGameValue(hand + 8, current) || !(current == unarmedIdentity) ||
            !ReadGameValue(hand + 0x28, pending) || !(pending == unarmedIdentity) ||
            !ReadGameValue(hand + 0x68, weapon) || weapon ||
            !ReadGameValue(requested, target) || target == unarmedIdentity)
            return false;

        for (int i = 1; i < 5; ++i)
        {
            const auto carry = WeaponSlotEntry(manager, slots, horseWeaponPoints[i]);
            WeaponIdentity identity = {};
            if (carry && ReadGameValue(carry + 8, identity) && identity == target)
                return true;
        }
        return false;
    }

    bool OnTimedWeaponSelection(const void* manager, const void* weapon, const void* options, const void* context)
    {
        if (PreserveMountedFists(reinterpret_cast<std::uintptr_t>(manager), reinterpret_cast<std::uintptr_t>(weapon)))
            return false;
        return originalWeaponSelection(manager, weapon, options, context);
    }

    std::uint8_t* AllocateTimedSelectionRelay(std::uintptr_t site)
    {
        SYSTEM_INFO system = {};
        GetNativeSystemInfo(&system);
        const auto granularity = static_cast<std::uintptr_t>(system.dwAllocationGranularity);
        if (!granularity)
            return nullptr;

        const auto origin = site & ~(granularity - 1);
        for (std::uintptr_t offset = granularity; offset < 0x70000000; offset += granularity)
        {
            const std::uintptr_t locations[] = { origin - offset, origin + offset };
            for (const auto location : locations)
            {
                MEMORY_BASIC_INFORMATION region = {};
                if (VirtualQuery(reinterpret_cast<const void*>(location), &region, sizeof(region)) != sizeof(region) ||
                    region.State != MEM_FREE)
                    continue;
                auto* page = static_cast<std::uint8_t*>(VirtualAlloc(reinterpret_cast<void*>(location),
                    4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
                if (page)
                    return page;
            }
        }
        return nullptr;
    }

    bool ReplaceTimedSelectionCall(std::uint8_t* site, const std::uint8_t* expected, const std::uint8_t* replacement)
    {
        if ((reinterpret_cast<std::uintptr_t>(site + 2) % alignof(LONG)) != 0 ||
            expected[0] != replacement[0] || expected[1] != replacement[1])
            return false;

        DWORD originalProtection = 0;
        if (!VirtualProtect(site, sizeof(timedSelectionCall.patch) + 1, PAGE_EXECUTE_READWRITE, &originalProtection))
            return false;

        constexpr std::uint8_t followingOpcode = 0x48;
        const std::uint8_t expectedWord[] = { expected[2], expected[3], expected[4], followingOpcode };
        const std::uint8_t replacementWord[] = { replacement[2], replacement[3], replacement[4], followingOpcode };
        LONG oldWord = 0;
        LONG newWord = 0;
        std::memcpy(&oldWord, expectedWord, sizeof(oldWord));
        std::memcpy(&newWord, replacementWord, sizeof(newWord));
        const bool owned = site[0] == expected[0] && site[1] == expected[1] &&
            InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(site + 2), newWord, oldWord) == oldWord;
        if (owned)
        {
            FlushInstructionCache(GetCurrentProcess(), site, sizeof(timedSelectionCall.patch));
        }
        DWORD ignored = 0;
        VirtualProtect(site, sizeof(timedSelectionCall.patch) + 1, originalProtection, &ignored);
        return owned;
    }

    std::uint8_t* SupportedWeaponSelectionImage()
    {
        auto* image = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
        if (!image)
            return nullptr;
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0)
            return nullptr;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(image + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.TimeDateStamp != 0x65EEC92E ||
            nt->OptionalHeader.SizeOfImage != 0x7397600)
            return nullptr;

        return image;
    }

    template<std::size_t N, typename Callback>
    bool InstallSelectionCall(SelectionCall& hook, std::uint8_t* site,
        const std::uint8_t (&expectedSite)[N], Callback callback)
    {
        if (hook.site)
            return std::memcmp(hook.site, hook.patch, sizeof(hook.patch)) == 0;
        if (std::memcmp(site, expectedSite, sizeof(expectedSite)) != 0)
            return false;

        auto* relayPage = AllocateTimedSelectionRelay(reinterpret_cast<std::uintptr_t>(site));
        if (!relayPage)
            return false;
        const auto originalTargetLow = (reinterpret_cast<std::uintptr_t>(site + sizeof(hook.patch)) +
            expectedSite[1]) & 0xFF;
        auto* relay = relayPage + originalTargetLow;

        constexpr std::size_t indirectJumpSize = 6;
        const auto target = reinterpret_cast<std::uintptr_t>(callback);
        std::uint8_t relayCode[indirectJumpSize + sizeof(target)] = { 0xFF, 0x25 };
        std::memcpy(relayCode + indirectJumpSize, &target, sizeof(target));
        std::memcpy(relay, relayCode, sizeof(relayCode));

        DWORD ignored = 0;
        if (!VirtualProtect(relay, sizeof(relayCode), PAGE_EXECUTE_READ, &ignored))
        {
            VirtualFree(relayPage, 0, MEM_RELEASE);
            return false;
        }
        FlushInstructionCache(GetCurrentProcess(), relay, sizeof(relayCode));

        const auto displacement = static_cast<std::int32_t>(reinterpret_cast<std::intptr_t>(relay) -
            reinterpret_cast<std::intptr_t>(site + sizeof(hook.patch)));
        hook.patch[0] = 0xE8;
        std::memcpy(hook.patch + 1, &displacement, sizeof(displacement));
        if (hook.patch[1] != expectedSite[1] ||
            !ReplaceTimedSelectionCall(site, expectedSite, hook.patch))
        {
            VirtualFree(relayPage, 0, MEM_RELEASE);
            return false;
        }
        hook.site = site;
        return true;
    }


    void InstallMountedWeaponHooks()
    {
        auto* image = SupportedWeaponSelectionImage();
        if (!image)
        {
            Logger::Log("[WARN] Horse: mounted weapon hooks unavailable for this executable.");
            return;
        }

        constexpr std::uint8_t selectionEntry[] = { 0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08 };
        const auto selection = image + 0x114C140;
        if (std::memcmp(selection, selectionEntry, sizeof(selectionEntry)) == 0)
        {
            if (!originalWeaponSelection)
                originalWeaponSelection = reinterpret_cast<WeaponSelection>(selection);
            if (InstallSelectionCall(timedSelectionCall, image + 0x1102D2E, timedSelectionBytes,
                &OnTimedWeaponSelection))
                Logger::Log("[SUCCESS] Horse: timed stow hook enabled.");
        }

        constexpr std::uint8_t fistsEntry[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x30 };
        constexpr std::uint8_t managerEntry[] = { 0x8B, 0x81, 0x9C, 0, 0, 0, 0x25, 0xFF, 0xFF, 0x01, 0 };
        const auto fists = image + 0x114D078;
        const auto manager = image + 0xCC8C58;
        if (std::memcmp(fists, fistsEntry, sizeof(fistsEntry)) == 0 &&
            std::memcmp(manager, managerEntry, sizeof(managerEntry)) == 0)
        {
            if (!originalFistsSelection)
                originalFistsSelection = reinterpret_cast<FistsSelection>(fists);
            getWeaponManager = reinterpret_cast<GetWeaponManager>(manager);
            if (InstallSelectionCall(queuedFistsCall, image + 0x110278E, queuedFistsBytes,
                &OnQueuedFistsSelection))
                Logger::Log("[SUCCESS] Horse: queued return-to-Fists hook enabled.");
        }
        if (!timedSelectionCall.site || !queuedFistsCall.site)
            Logger::Log("[WARN] Horse: one or more mounted weapon hooks could not be installed.");
    }

    void UpdateWeaponTracking(std::uint64_t ped, ULONGLONG now, bool onMount)
    {
        if (onMount || now >= nextWeaponInputCheck)
        {
            nextWeaponInputCheck = now + horseWeaponInputMs;
            if (Pressed(wheelControl) || JustReleased(wheelControl) ||
                JustPressed(holsterControl) || JustReleased(holsterControl))
                manualHolsterGraceUntil = now + horseWeaponManualGraceMs;
        }

        if (now < nextWeaponSample)
            return;
        nextWeaponSample = now + horseWeaponSampleMs;

        if (InvokeBoolean(getLoadingScreenActive) || !InvokeBoolean(isScreenFadedIn) ||
            InvokeInt(isPedDeadOrDying, ped))
        {
            ResetWeaponTracking();
            nextWeaponInputCheck = now + horseWeaponInputMs;
            nextWeaponSample = now + horseWeaponSampleMs;
            return;
        }

        if (onMount)
            UpdateMountedCarrySnapshot();

        const bool wheelActive = WeaponWheelActive();
        if (wheelActive)
            manualHolsterGraceUntil = now + horseWeaponManualGraceMs;

        std::uint32_t current[5] = {};
        for (int i = 0; i < 5; ++i)
            current[i] = ReadWeaponSlot(ped, horseWeaponPoints[i]);

        if (!weaponTrackingReady)
        {
            for (int i = 0; i < 5; ++i)
            {
                trackedWeapons[i] = current[i];
                weaponChangedAt[i] = now;
            }
            weaponTrackingReady = true;
            mountedSince = now;
            for (int i = 0; i < 5; ++i)
                lastReportedWeapons[i] = current[i];
            if (Logger::IsDebugEnabled())
            {
                char message[224] = {};
                std::snprintf(message, sizeof(message), "Horse: weapon slots initialized hand=%08X bow=%08X altBow=%08X rifle=%08X altRifle=%08X",
                    current[0], current[1], current[2], current[3], current[4]);
                Logger::Debug(message);
            }
            return;
        }

        if (onMount && !weaponSnapshotLogged && now - mountedSince >= 12000)
        {
            weaponSnapshotLogged = true;
            if (Logger::IsDebugEnabled())
            {
                char message[224] = {};
                std::snprintf(message, sizeof(message), "Horse: mounted weapon snapshot hand=%08X bow=%08X altBow=%08X rifle=%08X altRifle=%08X",
                    current[0], current[1], current[2], current[3], current[4]);
                Logger::Debug(message);
            }
        }

        for (int i = 1; i < 5; ++i)
        {
            if (Logger::IsDebugEnabled() && current[i] != lastReportedWeapons[i])
            {
                const std::uint32_t mount = onMount ? InvokeInt(getMount, ped) : 0;
                const std::uint32_t saddleWeapon = mount ? InvokeInt(getWeaponFromHorseHolster, mount) : 0;
                const bool holsterChanging = InvokeInt(isWeaponHolsterStateChanging, ped) != 0;
                char message[320] = {};
                std::snprintf(message, sizeof(message),
                    "Horse: weapon slot changed slot=%d previous=%08X current=%08X hand=%08X manualGrace=%s onMount=%s trackingMs=%llu saddleFirst=%08X holsterChanging=%s",
                    horseWeaponPoints[i], lastReportedWeapons[i], current[i], current[0],
                    now < manualHolsterGraceUntil ? "true" : "false", onMount ? "true" : "false",
                    static_cast<unsigned long long>(now - mountedSince), saddleWeapon, holsterChanging ? "true" : "false");
                Logger::Debug(message);
                lastReportedWeapons[i] = current[i];
            }
            if (current[i] == trackedWeapons[i])
            {
                weaponRestoreAttempts[i] = 0;
                continue;
            }

            bool weaponMoved = false;
            for (int j = 0; j < 5; ++j)
            {
                if (j != i && current[j] == trackedWeapons[i])
                {
                    weaponMoved = true;
                    break;
                }
            }

            const bool lostLongarm = !weaponMoved &&
                (current[i] == 0 || current[i] == unarmedWeapon) &&
                trackedWeapons[i] != 0 && trackedWeapons[i] != unarmedWeapon &&
                now - weaponChangedAt[i] >= horseWeaponStableMs &&
                now - mountedSince >= horseWeaponStableMs &&
                !wheelActive && now >= manualHolsterGraceUntil &&
                IsLongarm(trackedWeapons[i]);

            if (lostLongarm && weaponRestoreAttempts[i] < 2)
            {
                if (lastWeaponRestore == 0 || now - lastWeaponRestore >= horseWeaponRestoreCooldownMs)
                {
                    nativeInit(setCurrentPedWeapon);
                    nativePush64(ped);
                    nativePush64(trackedWeapons[i]);
                    nativePush64(0);
                    nativePush64(1);
                    nativePush64(0);
                    nativePush64(0);
                    nativeCall();
                    lastWeaponRestore = now;
                    ++weaponRestoreAttempts[i];
                    if (Logger::IsDebugEnabled())
                    {
                        char message[160] = {};
                        std::snprintf(message, sizeof(message), "Horse: restoring longarm slot=%d weapon=%08X attempt=%u",
                            horseWeaponPoints[i], trackedWeapons[i], static_cast<unsigned>(weaponRestoreAttempts[i]));
                        Logger::Debug(message);
                    }
                }
                continue;
            }

            trackedWeapons[i] = current[i];
            weaponChangedAt[i] = now;
            weaponRestoreAttempts[i] = 0;
        }

        if (current[0] != trackedWeapons[0])
        {
            trackedWeapons[0] = current[0];
            weaponChangedAt[0] = now;
        }
    }

    struct Position
    {
        alignas(8) float x;
        alignas(8) float y;
        alignas(8) float z;
    };

    static_assert(sizeof(Position) == 24, "");

    std::uint64_t FloatArg(float value)
    {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        return bits;
    }

    bool ReadPosition(std::uint32_t entity, Position& position)
    {
        nativeInit(getEntityCoords);
        nativePush64(entity);
        nativePush64(1);
        nativePush64(1);
        const auto* value = nativeCall();
        if (value == nullptr)
            return false;
        std::memcpy(&position, value, sizeof(position));
        return std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z);
    }

    bool ReadForward(std::uint32_t entity, Position& direction)
    {
        nativeInit(getEntityForwardVector);
        nativePush64(entity);
        const auto* value = nativeCall();
        if (!value)
            return false;
        std::memcpy(&direction, value, sizeof(direction));
        return std::isfinite(direction.x) && std::isfinite(direction.y);
    }

    float DistanceSquared(const Position& a, const Position& b)
    {
        const float x = a.x - b.x;
        const float y = a.y - b.y;
        const float z = a.z - b.z;
        return x * x + y * y + z * z;
    }

    void CancelFollow()
    {
        followActive = false;
        followTaskIssued = false;
        followHorse = 0;
        nextFollowCheck = 0;
        followTaskAt = 0;
        lastFollowProgressAt = 0;
        lastFollowDistance = 0.0f;
    }

    void ResetWhistleState()
    {
        pendingWhistle = false;
        pendingWhistleHorse = 0;
        pendingWhistleAt = 0;
        nextWhistleAt = 0;
        recentWhistleHorse = 0;
        helpSuppressionUntil = 0;
        recentDistantWhistleUntil = 0;
        helpMessageBeforeWhistle = 0;
        previousHelpMessage = 0;
        lastClearedHelpMessage = 0;
        lastHelpClearAt = 0;
        nextHelpPoll = 0;
    }

    std::uint32_t GetUsableHorse(std::uint32_t ped)
    {
        nativeInit(playerId);
        const std::uint32_t player = static_cast<std::uint32_t>(*nativeCall());
        const std::uint32_t horse = InvokeInt(getSaddleHorseForPlayer, player);
        if (horse == 0 || horse == ped || !InvokeInt(doesEntityExist, horse) ||
            InvokeInt(isPedDeadOrDying, horse))
            return 0;
        nativeInit(getRiderOfMount);
        nativePush64(horse);
        nativePush64(1);
        if (static_cast<std::uint32_t>(*nativeCall()) != 0 || InvokeInt(isPedBeingLed, horse))
            return 0;
        return horse;
    }

    struct RecallDiagnostics
    {
        unsigned int safeCoords = 0;
        unsigned int roadNodes = 0;
        unsigned int groundCoords = 0;
        unsigned int rejected = 0;
    };

    bool RecallPositionValid(const Position& player, const Position& candidate,
        float x, float y, float maxDistance, RecallDiagnostics& diagnostics)
    {
        if (!std::isfinite(candidate.x) || !std::isfinite(candidate.y) || !std::isfinite(candidate.z) ||
            std::fabs(candidate.z - player.z) > 23.0f)
        {
            ++diagnostics.rejected;
            return false;
        }
        const float sampleX = candidate.x - x;
        const float sampleY = candidate.y - y;
        if (sampleX * sampleX + sampleY * sampleY > 24.0f * 24.0f)
        {
            ++diagnostics.rejected;
            return false;
        }
        const Position landing = { candidate.x, candidate.y, candidate.z + 0.35f };
        const float distanceSq = DistanceSquared(player, landing);
        if (distanceSq < 22.0f * 22.0f || distanceSq > maxDistance * maxDistance)
        {
            ++diagnostics.rejected;
            return false;
        }
        float waterHeight = 0.0f;
        nativeInit(getWaterHeight);
        nativePush64(FloatArg(candidate.x));
        nativePush64(FloatArg(candidate.y));
        nativePush64(FloatArg(candidate.z + 3.0f));
        nativePush64(reinterpret_cast<std::uint64_t>(&waterHeight));
        if (static_cast<std::uint32_t>(*nativeCall()) != 0 &&
            std::isfinite(waterHeight) && candidate.z < waterHeight + 0.8f)
        {
            ++diagnostics.rejected;
            return false;
        }
        return true;
    }

    bool FindRecallPoint(const Position& player, float x, float y, float maxDistance,
        Position& destination, RecallDiagnostics& diagnostics, const char*& method)
    {
        Position safe = {};
        for (int onGround = 1; onGround >= 0; --onGround)
        {
            nativeInit(getSafeCoordForPed);
            nativePush64(FloatArg(x));
            nativePush64(FloatArg(y));
            nativePush64(FloatArg(player.z + 3.0f));
            nativePush64(static_cast<std::uint64_t>(onGround));
            nativePush64(reinterpret_cast<std::uint64_t>(&safe));
            nativePush64(0);
            if (static_cast<std::uint32_t>(*nativeCall()) != 0)
            {
                ++diagnostics.safeCoords;
                if (RecallPositionValid(player, safe, x, y, maxDistance, diagnostics))
                {
                    destination = safe;
                    method = "ped-navmesh";
                    return true;
                }
            }
        }
        safe = {};
        nativeInit(getClosestVehicleNode);
        nativePush64(FloatArg(x));
        nativePush64(FloatArg(y));
        nativePush64(FloatArg(player.z + 3.0f));
        nativePush64(reinterpret_cast<std::uint64_t>(&safe));
        nativePush64(1);
        nativePush64(FloatArg(3.0f));
        nativePush64(FloatArg(0.0f));
        if (static_cast<std::uint32_t>(*nativeCall()) != 0)
        {
            ++diagnostics.roadNodes;
            if (RecallPositionValid(player, safe, x, y, maxDistance, diagnostics))
            {
                destination = safe;
                method = "road-node";
                return true;
            }
        }
        float groundZ = 0.0f;
        nativeInit(getGroundZFor3DCoord);
        nativePush64(FloatArg(x));
        nativePush64(FloatArg(y));
        nativePush64(FloatArg(player.z + 35.0f));
        nativePush64(reinterpret_cast<std::uint64_t>(&groundZ));
        nativePush64(0);
        if (static_cast<std::uint32_t>(*nativeCall()) != 0)
        {
            ++diagnostics.groundCoords;
            safe = { x, y, groundZ };
            if (RecallPositionValid(player, safe, x, y, maxDistance, diagnostics))
            {
                destination = safe;
                method = "terrain-ground";
                return true;
            }
        }
        return false;
    }

    bool FindRecallPosition(const Position& player, const Position& horse,
        Position& destination, RecallDiagnostics& diagnostics, const char*& method)
    {
        nativeInit(getWhistleRangeMaxForBondingLevel);
        nativePush64(4);
        const auto* result = nativeCall();
        float whistleRange = 0.0f;
        if (result)
            std::memcpy(&whistleRange, result, sizeof(whistleRange));
        if (!std::isfinite(whistleRange) || whistleRange < 100.0f || whistleRange > 1000.0f)
            whistleRange = 350.0f;

        const float horseDistance = std::sqrt(DistanceSquared(player, horse));
        const float maxDistance = std::fmin(whistleRange, horseDistance) - 5.0f;
        const float heading = std::atan2(horse.y - player.y, horse.x - player.x);
        constexpr float angleOffsets[] = { 0.0f, 0.393f, -0.393f, 0.785f, -0.785f,
            1.571f, -1.571f, 2.356f, -2.356f, 3.142f };
        constexpr float radiusScales[] = { 0.99f, 0.85f, 0.65f, 0.45f, 0.25f };
        for (const float offset : angleOffsets)
        {
            const float directionX = std::cos(heading + offset);
            const float directionY = std::sin(heading + offset);
            for (const float scale : radiusScales)
            {
                const float radius = maxDistance * scale;
                const float x = player.x + radius * directionX;
                const float y = player.y + radius * directionY;
                if (FindRecallPoint(player, x, y, maxDistance, destination, diagnostics, method))
                    return true;
            }
        }
        return false;
    }

    void RelocateRecallHorse(std::uint32_t horse, const Position& player, const Position& destination,
        const char* method)
    {
        nativeInit(setEntityCoordsNoOffset);
        nativePush64(horse);
        nativePush64(FloatArg(destination.x));
        nativePush64(FloatArg(destination.y));
        nativePush64(FloatArg(destination.z + 0.35f));
        nativePush64(0);
        nativePush64(0);
        nativePush64(1);
        nativeCall();
        if (Logger::IsDebugEnabled())
        {
            Position moved = {};
            const bool confirmed = ReadPosition(horse, moved) &&
                DistanceSquared(moved, destination) < 8.0f * 8.0f;
            char message[256] = {};
            std::snprintf(message, sizeof(message),
                "DisableHorseWhistleDistance: relocation via %s at %.1f meters (verified=%s).",
                method,
                std::sqrt(DistanceSquared(player, destination)), confirmed ? "true" : "false");
            Logger::Debug(message);
        }
    }

    void RestoreHorseBlip()
    {
        if (horseBlipOverridden && horseBlip && horseBlipOwner &&
            InvokeInt(doesEntityExist, horseBlipOwner) &&
            InvokeInt(getBlipFromEntity, horseBlipOwner) == horseBlip &&
            InvokeInt(doesBlipExist, horseBlip))
        {
            nativeInit(blipRemoveModifier);
            nativePush64(horseBlip);
            nativePush64(inRangeModifier);
            nativeCall();
        }
        horseBlip = 0;
        horseBlipOwner = 0;
        horseBlipOverridden = false;
    }

    void UpdateHorseBlip(std::uint32_t player, bool onMount, ULONGLONG now)
    {
        if (!disableWhistleDistance || now < nextBlipRefresh)
            return;
        nextBlipRefresh = now + horseBlipRefreshMs;
        if (onMount || Main::MissionState::IsActiveOrRecentlyEnded(1500))
        {
            RestoreHorseBlip();
            return;
        }
        const std::uint32_t horse = GetUsableHorse(player);
        if (!horse)
        {
            RestoreHorseBlip();
            return;
        }
        if (recentWhistleHorse != 0 && recentWhistleHorse != horse)
            ResetWhistleState();
        Position playerPos = {}, horsePos = {};
        if (!ReadPosition(player, playerPos) || !ReadPosition(horse, horsePos))
            return;
        if (recentDistantWhistleUntil != 0 &&
            DistanceSquared(playerPos, horsePos) < 35.0f * 35.0f)
        {
            helpSuppressionUntil = 0;
            recentDistantWhistleUntil = 0;
            recentWhistleHorse = 0;
        }
        const std::uint32_t blip = InvokeInt(getBlipFromEntity, horse);
        if (!blip || !InvokeInt(doesBlipExist, blip))
        {
            RestoreHorseBlip();
            return;
        }
        if (blip != horseBlip || horse != horseBlipOwner)
        {
            RestoreHorseBlip();
            horseBlip = blip;
            horseBlipOwner = horse;
        }
        if (DistanceSquared(playerPos, horsePos) > 140.0f * 140.0f)
        {
            nativeInit(blipRemoveModifier);
            nativePush64(blip);
            nativePush64(outOfRangeModifier);
            nativeCall();
            nativeInit(blipAddModifier);
            nativePush64(blip);
            nativePush64(inRangeModifier);
            nativeCall();
            horseBlipOverridden = true;
        }
        else if (horseBlipOverridden)
            RestoreHorseBlip();
    }

    void UpdateWhistleHelp(ULONGLONG now)
    {
        if (!disableWhistleDistance || now < nextHelpPoll)
            return;
        nextHelpPoll = now + (helpSuppressionUntil > now ? horseHelpPollMs : horseBlipRefreshMs);
        const std::uint32_t message = InvokeInt(uiFeedGetCurrentMessage, 1);
        if (helpSuppressionUntil > now)
        {
            if (message != 0 && message != 0xFFFFFFFFu &&
                (message != helpMessageBeforeWhistle || message == lastClearedHelpMessage) &&
                (message != lastClearedHelpMessage || now - lastHelpClearAt >= horseHelpClearRepeatMs))
            {
                nativeInit(uiFeedClearHelpTextFeed);
                nativePush64(message);
                nativePush64(1);
                nativeCall();
                lastClearedHelpMessage = message;
                lastHelpClearAt = now;
                if (Logger::IsDebugEnabled())
                    Logger::Debug("DisableHorseWhistleDistance: cleared out-of-range whistle help message.");
            }
        }
        else
        {
            helpSuppressionUntil = 0;
            previousHelpMessage = message;
            if (recentDistantWhistleUntil <= now)
                lastClearedHelpMessage = 0;
        }
    }

    void RequestWhistle(std::uint32_t horse, std::uint32_t player)
    {
        nativeInit(taskGoToWhistle);
        nativePush64(horse);
        nativePush64(player);
        nativePush64(0);
        nativeCall();
    }

    void RequestFollow(std::uint32_t horse, std::uint32_t player)
    {
        nativeInit(taskFollowToOffsetOfEntity);
        nativePush64(horse);
        nativePush64(player);
        nativePush64(FloatArg(1.5f));
        nativePush64(FloatArg(-8.0f));
        nativePush64(FloatArg(0.0f));
        nativePush64(FloatArg(1.4f));
        nativePush64(static_cast<std::uint64_t>(-1));
        nativePush64(FloatArg(5.0f));
        nativePush64(1);
        nativePush64(0);
        nativePush64(0);
        nativePush64(0);
        nativePush64(0);
        nativeCall();
    }

    void UpdateHorseCommands(std::uint32_t player, bool onMount, ULONGLONG now)
    {
        if (!disableWhistleDistance && !fixFollowBehavior)
            return;
        if ((onMount || (helpSuppressionUntil > now && !InvokeInt(isPedOnFoot, player))) &&
            (pendingWhistle || followActive || helpSuppressionUntil > now || recentDistantWhistleUntil > now))
        {
            CancelFollow();
            ResetWhistleState();
        }
        UpdateWhistleHelp(now);
        if (disableWhistleDistance && helpSuppressionUntil > now &&
            recentDistantWhistleUntil > now && Pressed(whistleControl))
            helpSuppressionUntil = now + horseWhistleHelpWindowMs;
        const bool whistle = disableWhistleDistance && JustPressed(whistleControl);
        const bool follow = fixFollowBehavior && JustPressed(followControl);
        const bool stop = fixFollowBehavior && (JustPressed(stayControl) || JustPressed(fleeControl));
        if (!whistle && !follow && !stop && !followActive && !pendingWhistle)
            return;
        if (stop || onMount || !InvokeInt(isPedOnFoot, player) || Main::MissionState::IsActiveOrRecentlyEnded(1500))
        {
            CancelFollow();
            ResetWhistleState();
            return;
        }
        if (whistle || follow)
        {
            const std::uint32_t horse = GetUsableHorse(player);
            if (!horse)
            {
                CancelFollow();
                return;
            }
            if (follow)
            {
                followActive = true;
                followTaskIssued = false;
                followHorse = horse;
                followTaskAt = now + 400;
                lastFollowProgressAt = now;
                lastFollowDistance = 0.0f;
                nextFollowCheck = followTaskAt;
            }
            if (whistle)
            {
                Position playerPos = {}, horsePos = {};
                const bool positionsAvailable = ReadPosition(player, playerPos) && ReadPosition(horse, horsePos);
                const float distanceSq = positionsAvailable ? DistanceSquared(playerPos, horsePos) : 0.0f;
                const bool distantHorse = positionsAvailable && distanceSq > 140.0f * 140.0f;
                if (distantHorse)
                {
                    recentDistantWhistleUntil = now + horseDistantWhistleGraceMs;
                    recentWhistleHorse = horse;
                }
                else if (positionsAvailable && distanceSq < 35.0f * 35.0f)
                {
                    recentDistantWhistleUntil = 0;
                    recentWhistleHorse = 0;
                    helpSuppressionUntil = 0;
                }
                if (distantHorse || recentDistantWhistleUntil > now)
                {
                    if (helpSuppressionUntil <= now)
                        helpMessageBeforeWhistle = previousHelpMessage;
                    helpSuppressionUntil = now + horseWhistleHelpWindowMs;
                    nextHelpPoll = now + 50;
                }
                if (!pendingWhistle && now >= nextWhistleAt)
                {
                    nextWhistleAt = now + horseWhistleCooldownMs;
                    pendingWhistle = true;
                    pendingWhistleHorse = horse;
                    pendingWhistleAt = now + 350;
                }
            }
        }
        if (pendingWhistle && now >= pendingWhistleAt)
        {
            pendingWhistle = false;
            const std::uint32_t requestedHorse = pendingWhistleHorse;
            pendingWhistleHorse = 0;
            pendingWhistleAt = 0;
            const std::uint32_t horse = GetUsableHorse(player);
            if (horse != 0 && horse == requestedHorse)
            {
                Position playerPos = {}, horsePos = {};
                if (ReadPosition(player, playerPos) && ReadPosition(horse, horsePos))
                {
                    const float distanceSq = DistanceSquared(playerPos, horsePos);
                    if (distanceSq > 140.0f * 140.0f)
                    {
                        Position safe = {};
                        RecallDiagnostics diagnostics = {};
                        const char* method = "";
                        if (FindRecallPosition(playerPos, horsePos, safe, diagnostics, method))
                            RelocateRecallHorse(horse, playerPos, safe, method);
                        else if (Logger::IsDebugEnabled())
                        {
                            char message[256] = {};
                            std::snprintf(message, sizeof(message),
                                "DisableHorseWhistleDistance: no valid recall point (navmesh=%u, roads=%u, ground=%u, rejected=%u).",
                                diagnostics.safeCoords, diagnostics.roadNodes, diagnostics.groundCoords, diagnostics.rejected);
                            Logger::Debug(message);
                        }
                        RequestWhistle(horse, player);
                        if (Logger::IsDebugEnabled())
                            Logger::Debug("DisableHorseWhistleDistance: issued native whistle task.");
                    }
                }
            }
        }
        if (!followActive || now < nextFollowCheck)
            return;
        nextFollowCheck = now + horseFollowRefreshMs;
        const std::uint32_t horse = GetUsableHorse(player);
        if (horse == 0 || horse != followHorse)
        {
            CancelFollow();
            return;
        }
        if (InvokeInt(isPedFleeing, horse))
            return;
        Position playerPos = {}, horsePos = {};
        if (!ReadPosition(player, playerPos) || !ReadPosition(horse, horsePos))
            return;
        const float distanceSq = DistanceSquared(playerPos, horsePos);
        const float distance = std::sqrt(distanceSq);
        Position forward = {};
        const bool isAhead = ReadForward(player, forward) &&
            (horsePos.x - playerPos.x) * forward.x +
            (horsePos.y - playerPos.y) * forward.y > 2.0f;
        if (distance < 8.0f && !isAhead)
        {
            lastFollowDistance = distance;
            lastFollowProgressAt = now;
            return;
        }
        if (!followTaskIssued || lastFollowDistance <= 0.0f || distance < lastFollowDistance - 2.0f)
        {
            lastFollowDistance = distance;
            lastFollowProgressAt = now;
        }
        if (!followTaskIssued || ((isAhead || distance > 18.0f) &&
            now - lastFollowProgressAt >= horseFollowRecoveryMs &&
            now - followTaskAt >= horseFollowRecoveryMs))
        {
            RequestFollow(horse, player);
            followTaskIssued = true;
            followTaskAt = now;
            lastFollowProgressAt = now;
            lastFollowDistance = distance;
            if (Logger::IsDebugEnabled())
                Logger::Debug("FixHorseFollowBehavior: applied native persistent follow task.");
        }
    }

    void LogState(const char* event, std::uint64_t ped, bool onMount)
    {
        if (!Logger::IsDebugEnabled())
            return;

        char message[256] = {};
        std::snprintf(message, sizeof(message), "Horse: %s ped=%llu onMount=%s noAutoEquip=%s noAutoHolster=%s",
            event, static_cast<unsigned long long>(ped), onMount ? "true" : "false",
            disableAutoEquip ? "true" : "false", disableAutoHolster ? "true" : "false");
        Logger::Debug(message);
    }

}

namespace Main::Horse
{
    void Initialize(bool noAutoHorseEquip, bool noAutoHorseHolster, bool noHorseAutoJump,
        bool noHorseWhistleDistance, bool horseFollowBehaviorFix)
    {
        ResetWeaponTracking();
        disableAutoEquip = noAutoHorseEquip;
        mountedPlayerAddress.store(0, std::memory_order_release);
        disableAutoHolster = noAutoHorseHolster;
        if (!disableAutoEquip && !disableAutoHolster)
            recentDismountUntil = 0;
        disableAutoJump = noHorseAutoJump;
        disableWhistleDistance = noHorseWhistleDistance;
        fixFollowBehavior = horseFollowBehaviorFix;
        CancelFollow();
        RestoreHorseBlip();
        ResetWhistleState();
        nextBlipRefresh = 0;
        nextHorseGameplayCheck = 0;
        horseGameplayReady = true;
        if (disableWhistleDistance)
        {
            whistleControl = HashName("INPUT_WHISTLE");
            inRangeModifier = HashName("BLIP_MODIFIER_PLAYER_HORSE_IN_RANGE");
            outOfRangeModifier = HashName("BLIP_MODIFIER_PLAYER_HORSE_OUT_OF_RANGE");
        }
        if (fixFollowBehavior)
        {
            followControl = HashName("INPUT_HORSE_COMMAND_FOLLOW");
            stayControl = HashName("INPUT_HORSE_COMMAND_STAY");
            fleeControl = HashName("INPUT_HORSE_COMMAND_FLEE");
        }
        if (disableAutoHolster)
        {
            unarmedWeapon = HashName("WEAPON_UNARMED");
            bowWeapon = HashName("WEAPON_BOW");
            improvedBowWeapon = HashName("WEAPON_BOW_IMPROVED");
            wheelControl = HashName("INPUT_OPEN_WHEEL_MENU");
            holsterControl = HashName("INPUT_TOGGLE_HOLSTER");
        }
        if (disableAutoEquip || disableAutoHolster)
            Logger::Log("[INFO] Horse: weapon settings loaded.");
        if (disableAutoHolster)
            InstallMountedWeaponHooks();
        if (disableAutoJump)
            Logger::Log("[INFO] DisableHorseAutoJump: enabled.");
        if (disableWhistleDistance)
            Logger::Log("[INFO] DisableHorseWhistleDistance: recall enabled for loaded horses.");
        if (fixFollowBehavior)
            Logger::Log("[INFO] FixHorseFollowBehavior: enabled.");
    }

    void Update()
    {
        if (!disableAutoEquip && !disableAutoHolster && !disableAutoJump && !disableWhistleDistance && !fixFollowBehavior)
            return;

        nativeInit(playerPedId);
        const std::uint64_t ped = static_cast<std::uint32_t>(*nativeCall());
        if (ped == 0 || (ped != lastPlayerPed && !InvokeInt(doesEntityExist, ped)))
        {
            ResetWeaponTracking();
            mountedPlayerAddress.store(0, std::memory_order_release);
            lastPlayerPed = 0;
            mounted = false;
            recentDismountUntil = 0;
            CancelFollow();
            ResetWhistleState();
            RestoreHorseBlip();
            nextHorseGameplayCheck = 0;
            horseGameplayReady = false;
            return;
        }

        if (lastPlayerPed != ped)
        {
            ResetWeaponTracking();
            CancelFollow();
            ResetWhistleState();
            RestoreHorseBlip();
            lastPlayerPed = ped;
            nextHorseGameplayCheck = 0;
            mounted = false;
            recentDismountUntil = 0;
        }

        nativeInit(isPedOnMount);
        nativePush64(ped);
        const bool onMount = static_cast<std::uint32_t>(*nativeCall()) != 0;
        const auto playerAddress = disableAutoHolster && onMount && (timedSelectionCall.site || queuedFistsCall.site) ?
            reinterpret_cast<std::uintptr_t>(getScriptHandleBaseAddress(static_cast<int>(ped))) : 0;
        mountedPlayerAddress.store(playerAddress, std::memory_order_release);
        const ULONGLONG now = GetTickCount64();
        const bool mountChanged = mounted != onMount;
        if (disableWhistleDistance || fixFollowBehavior)
        {
            if (now >= nextHorseGameplayCheck)
            {
                nextHorseGameplayCheck = now + horseGameplayCheckMs;
                horseGameplayReady = !InvokeBoolean(getLoadingScreenActive) &&
                    InvokeBoolean(isScreenFadedIn) &&
                    !InvokeInt(isPedDeadOrDying, ped) &&
                    !Main::MissionState::IsActiveOrRecentlyEnded(1500);
            }
            if (horseGameplayReady)
            {
                UpdateHorseCommands(static_cast<std::uint32_t>(ped), onMount, now);
                UpdateHorseBlip(static_cast<std::uint32_t>(ped), onMount, now);
            }
            else
            {
                CancelFollow();
                ResetWhistleState();
                RestoreHorseBlip();
            }
        }
        if (mountChanged && !onMount)
            recentDismountUntil = now + 3000;

        if (onMount && disableAutoJump)
        {
            nativeInit(getMount);
            nativePush64(ped);
            const std::uint64_t mount = static_cast<std::uint32_t>(*nativeCall());
            if (mount != 0)
            {
                nativeInit(setPedResetFlag);
                nativePush64(mount);
                nativePush64(disableHorseAutoJumpFlag);
                nativePush64(1);
                nativeCall();
            }
        }

        if (mountChanged && onMount)
        {
            ResetWeaponTracking();
            recentDismountUntil = 0;
        }

        const bool protectWeapons = onMount || now < recentDismountUntil;
        if (protectWeapons && (disableAutoEquip || disableAutoHolster))
        {
            nativeInit(setForceAutoEquip);
            nativePush64(ped);
            nativePush64(0);
            nativeCall();
        }

        if (disableAutoHolster && protectWeapons)
        {
            nativeInit(setInstantlyStoreLongarmsOnDismount);
            nativePush64(ped);
            nativePush64(0);
            nativePush64(0);
            nativeCall();
            UpdateWeaponTracking(ped, now, onMount);
        }
        else if (weaponTrackingReady)
            ResetWeaponTracking();

        if (mountChanged && Logger::IsDebugEnabled())
            LogState(onMount ? "mounted" : "dismounted", ped, onMount);
        mounted = onMount;
    }

    void Shutdown()
    {
        mountedPlayerAddress.store(0, std::memory_order_release);
        if (timedSelectionCall.site)
            ReplaceTimedSelectionCall(timedSelectionCall.site, timedSelectionCall.patch, timedSelectionBytes);
        if (queuedFistsCall.site)
            ReplaceTimedSelectionCall(queuedFistsCall.site, queuedFistsCall.patch, queuedFistsBytes);
    }

    bool NeedsAutoJumpFrameUpdate()
    {
        return mounted && disableAutoJump;
    }

    bool NeedsFastUpdate()
    {
        return (disableAutoEquip || disableAutoHolster) && (mounted || GetTickCount64() < recentDismountUntil);
    }

    bool NeedsPerFrameUpdate()
    {
        return (disableAutoHolster && (mounted || GetTickCount64() < recentDismountUntil)) ||
            (mounted && disableAutoJump) || disableWhistleDistance || fixFollowBehavior;
    }
}
