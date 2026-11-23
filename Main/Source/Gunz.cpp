#include "Gunz.h"
#include "../../Headers/Logger.h"
#include "main.h"
#include <windows.h>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace
{
    constexpr std::uint64_t playerPedId = 0x096275889B8E0EE0ULL;
    constexpr std::uint64_t getLoadingScreenActive = 0x71D4BF5890659B0CULL;
    constexpr std::uint64_t getGameTimer = 0x4F67E8ECA7D3F667ULL;
    constexpr std::uint64_t doesEntityExist = 0xD42BD6EB2E0F1677ULL;
    constexpr std::uint64_t isPedReloading = 0x24B100C68C645951ULL;
    constexpr std::uint64_t isControlPressed = 0xF3A21BCD95725A4AULL;
    constexpr std::uint64_t isDisabledControlPressed = 0xE2587F8CBBD87B1DULL;
    constexpr std::uint64_t isControlJustPressed = 0x580417101DDB492FULL;
    constexpr std::uint64_t isDisabledControlJustPressed = 0x91AEF906BCA88877ULL;
    constexpr std::uint64_t setArrowTrailFx = 0x2EBF70E1D8C06683ULL;
    constexpr std::uint64_t getHashKey = 0xFD340785ADF8CFB7ULL;
    constexpr std::uint64_t getRunningScriptThreads = 0x8E34C953364A76DDULL;
    constexpr std::uint64_t getCurrentPedWeapon = 0x3A87E44BB9A01D54ULL;
    constexpr std::uint64_t getPedAmmoTypeFromWeapon = 0x7FEAD38B326B9F74ULL;
    constexpr std::uint64_t getPedAccuracy = 0x37F4AD56ECBC0CD6ULL;
    constexpr std::uint64_t setPedAccuracy = 0x7AEFB85C1D49DEB6ULL;
    constexpr std::uint64_t setPedResetFlag = 0xC1E8A365BF3B29F2ULL;
    constexpr std::uint64_t isFirstPersonAimCamActive = 0xF63134C54B6EC212ULL;
    constexpr std::uint64_t isWeaponRepeater = 0xDDB2578E95EF7138ULL;
    constexpr std::uint64_t getCurrentPedWeaponEntityIndex = 0x3B390A939AF0B5FCULL;
    constexpr std::uint64_t setWeaponDegradation = 0xA7A57E89E965D839ULL;
    constexpr std::uint64_t setWeaponDirt = 0x812CE61DEBCAB948ULL;
    constexpr std::uint64_t setWeaponSoot = 0xA9EF4AD10BDDDB57ULL;
    constexpr std::uint64_t setWeaponDamage = 0xE22060121602493BULL;
    constexpr std::uint64_t hasWeaponGotWeaponComponent = 0x76A18844E743BF91ULL;
    constexpr std::uint64_t getWeaponStatId = 0x8EC44AE8DECFF841ULL;
    constexpr std::uint64_t getWeaponClipSize = 0xD3750CCC00635FC2ULL;
    constexpr std::uint64_t getWeaponTypeGroup = 0xEDCA14CA5199FF25ULL;
    constexpr std::uint64_t getWeaponTypeSlot = 0x46F032B8DDF46CDEULL;
    constexpr std::uint64_t getWeaponTypeModel = 0xF70825EB340E7D15ULL;
    constexpr std::uint64_t getAmmoTypeForWeapon = 0x5C2EA6C44F515F34ULL;
    constexpr std::uint32_t reticleResetFlag = 293;
    constexpr std::uint32_t reticleSwayResetFlag = 294;
    constexpr std::uint32_t disabledArrowTrailFx = 0x34E12A73U;
    constexpr std::uint32_t defaultArrowTrailFx = 0xB88046AAU;
    constexpr std::uint32_t reloadControl = 0xE30CD707;
    constexpr std::uint32_t aimControl = 0xF84FA74FU;
    constexpr std::uint32_t scopeToggleControl = 0x3076E97CU;
    constexpr std::uint8_t scopeWeaponInfoAccessPattern[] = {
        0x48, 0x8B, 0x42, 0x60, 0x8B, 0x88, 0xF4, 0x02, 0x00, 0x00, 0x89, 0x4B, 0x2C
    };
    struct ScopeFieldPatch
    {
        std::size_t offset;
        std::uint32_t original;
        std::uint32_t replacement;
        std::size_t size;
    };
    constexpr ScopeFieldPatch carcanoScopeFields[] = {
        { 0x2F4, 0xBB570E9EU, 0xBACAE759U, 4 },
        { 0x304, 0xBB570E9EU, 0xA84858C8U, 4 },
        { 0x308, 0xFBA5DB84U, 0xC4E1F206U, 4 },
        { 0x33C, 0x0B68F056U, 0x82065DDFU, 4 },
        { 0x340, 0x00000000U, 0x7EA7439DU, 4 },
        { 0x370, 0x94638209U, 0x840A9C18U, 4 },
        { 0x924, 0x24610511U, 0x24610571U, 4 },
        { 0x93C, 0x00089292U, 0x02088292U, 4 },
        { 0x940, 0x00020800U, 0x00000000U, 4 },
        { 0x954, 0x00000000U, 0xA26E5F9CU, 4 },
        { 0x9F4, 0x00000000U, 0x000000FFU, 1 }
    };
    constexpr std::size_t carcanoMetadataSize = 0x9F5;
    constexpr ScopeFieldPatch rollingScopeFields[] = {
        { 0x2F4, 0xBB570E9EU, 0xBACAE759U, 4 },
        { 0x304, 0xBB570E9EU, 0xA84858C8U, 4 },
        { 0x308, 0xFBA5DB84U, 0xC4E1F206U, 4 },
        { 0x33C, 0x0B68F056U, 0x82065DDFU, 4 },
        { 0x340, 0x00000000U, 0x7EA7439DU, 4 },
        { 0x370, 0x94638209U, 0x840A9C18U, 4 },
        { 0x924, 0x24610511U, 0x24610571U, 4 },
        { 0x93C, 0x00089212U, 0x02088212U, 4 },
        { 0x940, 0x00020800U, 0x00000000U, 4 },
        { 0x954, 0x00000000U, 0xA26E5F9CU, 4 },
        { 0x9F4, 0x00000000U, 0x000000FFU, 1 }
    };

    constexpr std::uint8_t shouldReloadPattern[] = {
        0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x68, 0x10, 0x48, 0x89, 0x70,
        0x18, 0x44, 0x88, 0x48, 0x20, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
        0x48, 0x83, 0xEC, 0x40, 0x44, 0x8B, 0xF9, 0x4D, 0x8B, 0xE0, 0x48, 0x8B, 0xCA, 0x48,
        0x8B, 0xFA
    };
    constexpr std::uint8_t clipNeedsReloadPattern[] = {
        0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x68, 0x10, 0x48, 0x89, 0x70,
        0x18, 0x48, 0x89, 0x78, 0x20, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x30, 0x0F, 0x29, 0x70,
        0xE8, 0x33, 0xDB, 0x48, 0x8B, 0x41, 0x60, 0x44, 0x8A, 0xF2, 0x48, 0x8B, 0xF9, 0x66,
        0x39, 0x58, 0x68
    };
    constexpr std::uint8_t holsterPattern[] = {
        0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
        0x48, 0x8B, 0xEC, 0x48, 0x83, 0xEC, 0x48, 0x45, 0x33, 0xD2, 0x48, 0x8B, 0xF9
    };
    constexpr std::uint8_t holsterLegacyPattern[] = {
        0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x10, 0x48, 0x89, 0x70, 0x18, 0x48, 0x89,
        0x78, 0x20, 0x48, 0x89, 0x48, 0x08, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56,
        0x41, 0x57, 0x48, 0x8B, 0xEC, 0x48, 0x83, 0xEC, 0x40, 0x45, 0x33, 0xF6,
        0x48, 0x8B, 0xD9
    };
    constexpr std::uint8_t coverPattern[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74,
        0x24, 0x18, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83,
        0xEC, 0x20, 0x4C, 0x8B, 0x69, 0x10
    };
    constexpr std::uint8_t deadEyePattern[] = {
        0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x68, 0x10, 0x48, 0x89,
        0x70, 0x18, 0x48, 0x89, 0x78, 0x20, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x30,
        0x48, 0x8B, 0xF9
    };

    constexpr std::int16_t shouldReloadBodyPattern[] = {
        0x44, 0x8B, 0xF9, 0x4D, 0x8B, 0xE0, 0x48, 0x8B, 0xCA, 0x48, 0x8B, 0xFA
    };
    constexpr std::int16_t clipNeedsReloadBodyPattern[] = {
        0x0F, 0x29, 0x70, 0xE8, 0x33, 0xDB, 0x48, 0x8B, 0x41, 0x60, 0x44, 0x8A, 0xF2,
        0x48, 0x8B, 0xF9, 0x66, 0x39, 0x58, 0x68
    };
    constexpr std::int16_t coverBodyPattern[] = {
        0x4C, 0x8B, 0x69, 0x10, 0x41, 0xBC, 0x01, 0x00, 0x00, 0x00,
        0x40, 0x8A, 0xF2, 0x48, 0x8B, 0xF9
    };
    constexpr std::int16_t deadEyeBodyPattern[] = {
        0x48, 0x8B, 0xF9, 0xE8, -1, -1, -1, -1, 0x48, 0x8B, 0xD8,
        0x48, 0x85, 0xC0, 0x0F, 0x84, -1, -1, -1, -1, 0x48, 0x8B,
        0xC8, 0xE8, -1, -1, -1, -1, 0x48, 0x85, 0xC0, 0x0F, 0x85
    };
    constexpr std::int16_t coverProbePattern[] = {
        0x4C, 0x8B, 0x69, 0x10, 0x41, 0xBC, 0x01, 0x00, 0x00, 0x00
    };
    constexpr std::int16_t deadEyeProbePattern[] = {
        0x48, 0x8B, 0xF9, 0xE8, -1, -1, -1, -1, 0x48, 0x8B, 0xD8, 0x48, 0x85, 0xC0
    };

    struct PatternResult
    {
        std::uint8_t* samples[3] = {};
        unsigned count = 0;
    };

    struct Hook
    {
        std::uint8_t* address = nullptr;
        std::uint8_t* gateway = nullptr;
        std::uint8_t* relay = nullptr;
        std::uint8_t original[7] = {};
        std::uint8_t length = 0;
    };

    Hook holster;
    Hook cover;
    Hook deadEye;
    Hook shouldReload;
    Hook clipNeedsReload;
    Hook scopeInfoObserver;
    Hook rollingScopeObserver;
    alignas(8) volatile LONG64 observedCarcanoInfo = 0;
    alignas(8) volatile LONG64 observedRollingInfo = 0;
    alignas(8) volatile LONG64 observedRareRollingInfo = 0;
    ULONGLONG rollingScopeObserverDeadline = 0;
    bool rollingScopeObserverAttempted = false;
    bool rollingScopeObserverFinished = false;
    bool rollingScopeObserverRestoreWarningLogged = false;
    bool rollingScopeMetadataLogged[2] = {};
    bool rollingScopeReadWarningLogged[2] = {};
    std::uintptr_t patchedRollingInfo[2] = {};
    std::uint16_t rollingScopeWrittenMask[2] = {};
    std::uintptr_t rejectedRollingInfo[2] = {};
    ULONGLONG nextRollingObserverRetry = 0;
    ULONGLONG scopeInfoObserverDeadline = 0;
    bool scopeInfoObserverFinished = false;
    bool scopeInfoObserverRestoreWarningLogged = false;
    std::uintptr_t patchedCarcanoInfo = 0;
    std::uint16_t carcanoScopeWrittenMask = 0;
    std::uintptr_t rejectedCarcanoInfo = 0;
    ULONGLONG nextScopeObserverRetry = 0;
    std::uintptr_t trackedScopePlayer = 0;
    ULONGLONG nextScopeRecordCheck = 0;
    bool scopeCarcanoPatched = false;
    std::atomic<bool> autoReloadDisabled{ false };
    std::atomic<bool> deadEyeAutoReloadDisabled{ false };
    std::atomic<bool> emptyAutoReloadDisabled{ false };
    bool arrowTrailsDisabled = false;
    bool arrowTrailFxApplied = false;
    bool reticleSpreadDisabled = false;
    bool reticleSwayDisabled = false;
    bool forcedScopeProbeEnabled = false;
    bool forcedScopeRequested = false;
    bool forcedScopeRestorePending = false;
    bool forcedScopeRestoreWarned = false;
    ULONGLONG nextForcedScopeRestore = 0;
    std::uint32_t scopeExcludedMissionHash = 0;
    ULONGLONG nextScopeMissionCheck = 0;
    ULONGLONG nextScopeMissionRestore = 0;
    ULONGLONG scopeMissionInactiveSince = 0;
    bool scopeMissionSuspended = false;
    bool scopeMissionRecordsRestored = false;
    bool scopeMissionRestoreWarningLogged = false;
    bool scopeWeaponInfoAccessProbed = false;
    std::uint8_t* scopeWeaponInfoAnchor = nullptr;
    DWORD64 scopeWeaponInfoFunctionStart = 0;
    DWORD64 scopeWeaponInfoFunctionEnd = 0;
    ULONGLONG scopeLastLifecycleCheck = 0;
    ULONGLONG scopeLoadReadyAt = 0;
    std::uint32_t scopeLastGameTimer = 0;
    std::uint32_t scopeLastPlayerPed = 0;
    bool scopeLifecycleReady = false;
    bool scopeLoadPending = false;
    bool scopeAccessReferenceScanActive = false;
    std::uintptr_t scopeAccessReferenceTarget = 0;
    std::size_t scopeAccessReferenceSection = 0;
    std::uintptr_t scopeAccessReferenceCursor = 0;
    std::size_t scopeAccessReferenceBytes = 0;
    ULONGLONG scopeAccessReferenceScanMs = 0;
    unsigned scopeAccessDirectCalls = 0;
    unsigned scopeAccessDataReferences = 0;
    std::uint32_t carcanoWeapon = 0;
    std::uint32_t rollingBlockWeapon = 0;
    std::uint32_t exoticRollingBlockWeapon = 0;
    std::uint32_t lastScopeProbeWeapon = 0;
    int lastScopeProbeState = -1;
    unsigned scopeProbeEvents = 0;
    ULONGLONG nextScopeProbeRefresh = 0;
    unsigned scopeMetadataProbed = 0;
    std::uint32_t scopeProbeMissingRepeater = 0;
    ULONGLONG scopeProbeRetryAfter = 0;
    std::uint32_t shortScopeComponent = 0;
    std::uint32_t longScopeComponent = 0;
    struct ScopeProbeSnapshot
    {
        std::uint32_t weapon = 0;
        std::uint32_t stat = 0;
        std::uint32_t group = 0;
        std::uint32_t slot = 0;
        std::uint32_t model = 0;
        std::uint32_t ammo = 0;
        int clip = 0;
        int entity = 0;
        bool objectAttached = false;
        bool objectRead = false;
        bool infoIdentified = false;
        unsigned infoRootOffset = 0;
        unsigned infoHashOffset = 0;
        std::uintptr_t vtable = 0;
        std::uint8_t infoBytes[128] = {};
    };
    ScopeProbeSnapshot scopeSnapshots[4] = {};
    unsigned scopeSnapshotCount = 0;
    std::uint32_t scopeMetadataPendingWeapon = 0;
    ULONGLONG scopeMetadataPendingSince = 0;
    struct ScopeCodeReference
    {
        std::uint32_t functionStart = 0;
        std::uint32_t functionEnd = 0;
        unsigned weaponMask = 0;
        unsigned matches = 0;
    };
    ScopeCodeReference scopeCodeReferences[48] = {};
    unsigned scopeCodeReferenceCount = 0;
    std::uint32_t scopeCodeHashes[4] = {};
    unsigned scopeCodeHitTotals[4] = {};
    unsigned scopeCodeUnwindRejected = 0;
    std::size_t scopeCodeSectionIndex = 0;
    std::uintptr_t scopeCodeCursor = 0;
    std::size_t scopeCodeBytesScanned = 0;
    ULONGLONG scopeCodeScanTimeMs = 0;
    bool scopeCodeScanActive = false;
    bool scopeCodeScanFinished = false;
    std::uint32_t shotgunShellAmmo = 0;
    std::uint32_t incendiaryShellAmmo = 0;
    int reticleAccuracyPed = 0;
    int previousReticleAccuracy = 0;
    bool reticleAccuracyOverridden = false;
    std::atomic<bool> emptyAutoReloadHooksReady{ false };
    std::atomic<bool> emptyReloadSuppressionObserved{ false };
    bool emptyReloadSuppressionLogged = false;
    std::atomic<ULONGLONG> manualReloadAllowedUntil{ 0 };
    thread_local bool reloadInputScriptThread = false;
    thread_local unsigned automaticReloadDepth = 0;
    std::atomic<std::uintptr_t> playerAddress{ 0 };
    ULONGLONG nextPlayerRefresh = 0;
    bool weaponDegradationDisabled = false;
    ULONGLONG nextWeaponConditionRefresh = 0;
    bool hitmarkersDisabled = false;
    bool reticleHighlightingDisabled = false;
    bool reticlePatchComplete = false;
    std::uintptr_t reticleHitPatch = 0;
    std::uintptr_t reticleStylePatch = 0;
    ULONGLONG nextReticleValidation = 0;
    std::atomic<std::uintptr_t> reticleHitCandidate{ 0 };
    std::atomic<std::uintptr_t> reticleStyleCandidate{ 0 };
    std::atomic<bool> reticleWorkerStop{ false };
    std::atomic<bool> reticleWorkerFinished{ false };
    HANDLE reticleWorkerHandle = nullptr;

    constexpr std::uint8_t originalReticleAction[] = { 0x96, 0x07, 0x00, 0x04, 0x02, 0x07, 0x01, 0x00, 0x00, 0x00 };
    constexpr std::uint8_t hitmarkerReticleAction[] = { 0x99, 0x02, 0x00, 0x25, 0x00, 0x96, 0x02, 0x00, 0x04, 0x02 };
    constexpr std::uint8_t styleReticleAction[] = { 0x99, 0x02, 0x00, 0x41, 0x00, 0x96, 0x02, 0x00, 0x04, 0x02 };
    constexpr std::uint8_t hitmarkerReticleTrailer[] = {
        0x42, 0x96, 0x09, 0x00, 0x07, 0x01, 0x00, 0x00, 0x00, 0x04, 0x01, 0x08, 0x54,
        0x52, 0x17, 0x96, 0x0D, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    constexpr std::uint8_t styleReticleTrailer[] = {
        0x42, 0x96, 0x09, 0x00, 0x07, 0x01, 0x00, 0x00, 0x00, 0x04, 0x01, 0x08, 0x60,
        0x52, 0x17, 0x96, 0x04, 0x00, 0x04, 0x01, 0x08, 0x1C, 0x4E, 0x96, 0x09, 0x00 
    };


    bool ReticleReadableRegion(const MEMORY_BASIC_INFORMATION& info)
    {
        if (info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0 ||
            (info.Type != MEM_PRIVATE && info.Type != MEM_MAPPED && info.Type != MEM_IMAGE))
            return false;
        const DWORD protection = info.Protect & 0xFF;
        return protection == PAGE_READONLY || protection == PAGE_READWRITE || protection == PAGE_WRITECOPY ||
            protection == PAGE_EXECUTE_READ || protection == PAGE_EXECUTE_READWRITE ||
            protection == PAGE_EXECUTE_WRITECOPY;
    }

    bool PatchReticleAction(std::uint8_t* bytes, const std::uint8_t* expected, const std::uint8_t* replacement)
    {
        std::uint8_t current[sizeof(originalReticleAction)]{};
        SIZE_T transferred = 0;
        const auto process = GetCurrentProcess();
        if (!ReadProcessMemory(process, bytes, current, sizeof(current), &transferred) ||
            transferred != sizeof(current) || std::memcmp(current, expected, sizeof(current)) != 0)
            return false;

        DWORD previous = 0;
        if (!VirtualProtect(bytes, sizeof(current), PAGE_READWRITE, &previous))
            return false;
        const bool written = ReadProcessMemory(process, bytes, current, sizeof(current), &transferred) &&
            transferred == sizeof(current) && std::memcmp(current, expected, sizeof(current)) == 0 &&
            WriteProcessMemory(process, bytes, replacement, sizeof(current), &transferred) &&
            transferred == sizeof(current);
        DWORD ignored = 0;
        if (!VirtualProtect(bytes, sizeof(current), previous, &ignored))
            Logger::Log("[WARNING] Reticle: could not restore GFX page protection.");
        return written;
    }

    bool MatchReticleCandidate(std::uintptr_t address, bool hitmarker, bool patchedOnly = false)
    {
        std::uint8_t bytes[80] = {};
        SIZE_T transferred = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address),
                bytes, sizeof(bytes), &transferred) || transferred != sizeof(bytes))
            return false;
        const auto* replacement = hitmarker ? hitmarkerReticleAction : styleReticleAction;
        if (std::memcmp(bytes, replacement, sizeof(originalReticleAction)) != 0 &&
            (patchedOnly || std::memcmp(bytes, originalReticleAction, sizeof(originalReticleAction)) != 0))
            return false;
        if (hitmarker)
            return bytes[42] == 0x3E &&
                std::memcmp(bytes + 10, hitmarkerReticleTrailer, sizeof(hitmarkerReticleTrailer)) == 0;
        return bytes[70] == 0x3E &&
            std::memcmp(bytes + 10, styleReticleTrailer, sizeof(styleReticleTrailer)) == 0;
    }

    void FindReticleCandidates(const std::uint8_t* bytes, std::size_t length,
        std::uintptr_t base, bool& needHit, bool& needStyle)
    {
        constexpr std::size_t minimumSize = 80;
        if (length < minimumSize)
            return;
        const auto* end = bytes + length;
        const auto* cursor = bytes + 10;
        while ((needHit || needStyle) && cursor < end)
        {
            const auto* marker = static_cast<const std::uint8_t*>(std::memchr(cursor, 0x42, end - cursor));
            if (!marker)
                break;
            cursor = marker + 1;
            const auto* action = marker - 10;
            if (end - action < static_cast<std::ptrdiff_t>(minimumSize))
                continue;
            const bool validAction =
                std::memcmp(action, originalReticleAction, sizeof(originalReticleAction)) == 0 ||
                std::memcmp(action, hitmarkerReticleAction, sizeof(hitmarkerReticleAction)) == 0 ||
                std::memcmp(action, styleReticleAction, sizeof(styleReticleAction)) == 0;
            if (!validAction)
                continue;
            if (needHit && action[42] == 0x3E &&
                std::memcmp(action + 10, hitmarkerReticleTrailer, sizeof(hitmarkerReticleTrailer)) == 0)
            {
                reticleHitCandidate.store(base + static_cast<std::uintptr_t>(action - bytes), std::memory_order_release);
                needHit = false;
            }
            if (needStyle && action[70] == 0x3E &&
                std::memcmp(action + 10, styleReticleTrailer, sizeof(styleReticleTrailer)) == 0)
            {
                reticleStyleCandidate.store(base + static_cast<std::uintptr_t>(action - bytes), std::memory_order_release);
                needStyle = false;
            }
        }
    }

    DWORD WINAPI ScanReticleWorker(LPVOID options)
    {
        const unsigned enabled = static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(options));
        bool needHit = (enabled & 1U) != 0;
        bool needStyle = (enabled & 2U) != 0;
        constexpr std::size_t chunkSize = 1024 * 1024;
        constexpr std::size_t overlap = 80;
        std::vector<std::uint8_t> buffer(chunkSize + overlap);
        SYSTEM_INFO system = {};
        GetSystemInfo(&system);
        const auto maximum = reinterpret_cast<std::uintptr_t>(system.lpMaximumApplicationAddress);
        const auto process = GetCurrentProcess();

        for (unsigned pass = 1; pass <= 2 && (needHit || needStyle) &&
            !reticleWorkerStop.load(std::memory_order_relaxed); ++pass)
        {
            std::uintptr_t cursor = 0x10000;
            std::uint64_t scanned = 0;
            std::uint64_t nextProgress = 512ULL * 1024 * 1024;
            const ULONGLONG started = GetTickCount64();
            while (cursor < maximum && (needHit || needStyle) &&
                !reticleWorkerStop.load(std::memory_order_relaxed))
            {
                MEMORY_BASIC_INFORMATION info = {};
                if (VirtualQuery(reinterpret_cast<const void*>(cursor), &info, sizeof(info)) != sizeof(info))
                {
                    cursor += system.dwPageSize;
                    continue;
                }
                const auto regionEnd = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
                if (regionEnd <= cursor)
                    break;
                if (!ReticleReadableRegion(info))
                {
                    cursor = regionEnd;
                    continue;
                }
                const std::size_t available = regionEnd - cursor;
                const std::size_t consumed = available < chunkSize ? available : chunkSize;
                const std::size_t requested = available - consumed < overlap ? available : consumed + overlap;
                SIZE_T transferred = 0;
                if (ReadProcessMemory(process, reinterpret_cast<const void*>(cursor),
                    buffer.data(), requested, &transferred) && transferred == requested)
                {
                    FindReticleCandidates(buffer.data(), requested, cursor, needHit, needStyle);
                    scanned += consumed;
                }
                cursor += consumed;
                if (scanned >= nextProgress)
                {
                    char message[176] = {};
                    std::snprintf(message, sizeof(message),
                        "Reticle: background scan pass %u reached %llu MiB.", pass,
                        static_cast<unsigned long long>(scanned / (1024 * 1024)));
                    Logger::Debug(message);
                    nextProgress = scanned + 512ULL * 1024 * 1024;
                }
                Sleep(1);
            }
            if (reticleWorkerStop.load(std::memory_order_relaxed))
                break;
            char message[224] = {};
            std::snprintf(message, sizeof(message),
                "[INFO] Reticle: background scan pass %u completed, scanned=%llu MiB, elapsedMs=%llu, hit=%u, style=%u.",
                pass, static_cast<unsigned long long>(scanned / (1024 * 1024)),
                static_cast<unsigned long long>(GetTickCount64() - started),
                needHit ? 0U : 1U, needStyle ? 0U : 1U);
            Logger::Log(message);
            if ((needHit || needStyle) && pass < 2)
            {
                for (unsigned delay = 0; delay < 100 &&
                    !reticleWorkerStop.load(std::memory_order_relaxed); ++delay)
                    Sleep(50);
            }
        }
        reticleWorkerFinished.store(true, std::memory_order_release);
        return 0;
    }

    void RestoreReticleAction(std::uintptr_t& address, bool hitmarker)
    {
        if (!address)
            return;
        const auto* replacement = hitmarker ? hitmarkerReticleAction : styleReticleAction;
        if (!MatchReticleCandidate(address, hitmarker, true))
            address = 0;
        else if (PatchReticleAction(reinterpret_cast<std::uint8_t*>(address), replacement, originalReticleAction))
        {
            address = 0;
            Logger::Log(hitmarker ? "[SUCCESS] DisableHitmarkers: reticle GFX restored." :
                "[SUCCESS] DisableReticleHighlighting: reticle GFX restored.");
        }
    }

    void SetReticleActions(bool disableHitmarkers, bool disableReticleHighlighting)
    {
        if (hitmarkersDisabled == disableHitmarkers && reticleHighlightingDisabled == disableReticleHighlighting)
            return;
        hitmarkersDisabled = disableHitmarkers;
        reticleHighlightingDisabled = disableReticleHighlighting;
        reticlePatchComplete = false;
        reticleWorkerStop.store(true, std::memory_order_release);
        if (!hitmarkersDisabled)
            RestoreReticleAction(reticleHitPatch, true);
        if (!reticleHighlightingDisabled)
            RestoreReticleAction(reticleStylePatch, false);
    }

    void UpdateReticleActions()
    {
        if (!hitmarkersDisabled)
            RestoreReticleAction(reticleHitPatch, true);
        if (!reticleHighlightingDisabled)
            RestoreReticleAction(reticleStylePatch, false);

        const auto now = GetTickCount64();
        if ((reticleHitPatch || reticleStylePatch) && now >= nextReticleValidation)
        {
            nextReticleValidation = now + 1000;
            if (hitmarkersDisabled && reticleHitPatch && !MatchReticleCandidate(reticleHitPatch, true, true))
            {
                reticleHitPatch = 0;
                reticlePatchComplete = false;
                reticleWorkerStop.store(true, std::memory_order_release);
            }
            if (reticleHighlightingDisabled && reticleStylePatch && !MatchReticleCandidate(reticleStylePatch, false, true))
            {
                reticleStylePatch = 0;
                reticlePatchComplete = false;
                reticleWorkerStop.store(true, std::memory_order_release);
            }
        }

        const bool needHit = hitmarkersDisabled && !reticleHitPatch;
        const bool needStyle = reticleHighlightingDisabled && !reticleStylePatch;
        if (reticleWorkerHandle && reticleWorkerStop.load(std::memory_order_acquire))
        {
            if (!reticleWorkerFinished.load(std::memory_order_acquire))
                return;
            CloseHandle(reticleWorkerHandle);
            reticleWorkerHandle = nullptr;
        }
        if (reticlePatchComplete || (!needHit && !needStyle))
            return;

        if (!reticleWorkerHandle)
        {
            const unsigned options = (needHit ? 1U : 0U) | (needStyle ? 2U : 0U);
            reticleHitCandidate.store(0, std::memory_order_relaxed);
            reticleStyleCandidate.store(0, std::memory_order_relaxed);
            reticleWorkerStop.store(false, std::memory_order_relaxed);
            reticleWorkerFinished.store(false, std::memory_order_relaxed);
            reticleWorkerHandle = CreateThread(nullptr, 0, &ScanReticleWorker,
                reinterpret_cast<LPVOID>(static_cast<std::uintptr_t>(options)), 0, nullptr);
            if (!reticleWorkerHandle)
            {
                Logger::Log("[WARNING] Reticle: could not start background scan; no reticle changes applied.");
                reticlePatchComplete = true;
                return;
            }
            SetThreadPriority(reticleWorkerHandle, THREAD_PRIORITY_BELOW_NORMAL);
            Logger::Log("[INFO] Reticle: background GFX scan started.");
        }

        const auto apply = [](const std::atomic<std::uintptr_t>& candidate, std::uintptr_t& patch, bool hitmarker)
        {
            const auto address = candidate.load(std::memory_order_acquire);
            const auto* replacement = hitmarker ? hitmarkerReticleAction : styleReticleAction;
            if (address && MatchReticleCandidate(address, hitmarker) &&
                PatchReticleAction(reinterpret_cast<std::uint8_t*>(address), originalReticleAction, replacement))
            {
                patch = address;
                Logger::Log(hitmarker ? "[SUCCESS] DisableHitmarkers: reticle GFX patched." :
                    "[SUCCESS] DisableReticleHighlighting: reticle GFX patched.");
            }
        };
        if (needHit)
            apply(reticleHitCandidate, reticleHitPatch, true);
        if (needStyle)
            apply(reticleStyleCandidate, reticleStylePatch, false);

        if ((!needHit || reticleHitPatch) && (!needStyle || reticleStylePatch))
        {
            reticlePatchComplete = true;
            reticleWorkerStop.store(true, std::memory_order_release);
        }
        else if (reticleWorkerFinished.load(std::memory_order_acquire))
        {
            if (needHit && !reticleHitPatch)
                Logger::Log("[WARNING] DisableHitmarkers: reticle GFX action was not patched.");
            if (needStyle && !reticleStylePatch)
                Logger::Log("[WARNING] DisableReticleHighlighting: reticle GFX action was not patched.");
            reticlePatchComplete = true;
            CloseHandle(reticleWorkerHandle);
            reticleWorkerHandle = nullptr;
        }
    }

    bool ExecutableRegion(const MEMORY_BASIC_INFORMATION& information)
    {
        if (information.State != MEM_COMMIT || (information.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
            return false;
        const DWORD protection = information.Protect & 0xFF;
        return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ ||
            protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
    }

    std::uint8_t* FindUniqueCode(const std::uint8_t* pattern, std::size_t length, PatternResult& result)
    {
        auto* image = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
        if (!image || !pattern || length < 7)
            return nullptr;

        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 4096)
            return nullptr;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(image + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
            nt->FileHeader.NumberOfSections == 0 || nt->FileHeader.NumberOfSections > 96)
            return nullptr;
        const std::size_t imageSize = nt->OptionalHeader.SizeOfImage;
        const auto* sections = IMAGE_FIRST_SECTION(nt);
        if (imageSize == 0 || reinterpret_cast<const std::uint8_t*>(sections) < image ||
            static_cast<std::size_t>(reinterpret_cast<const std::uint8_t*>(sections) - image) > imageSize ||
            nt->FileHeader.NumberOfSections >
                (imageSize - static_cast<std::size_t>(reinterpret_cast<const std::uint8_t*>(sections) - image)) /
                sizeof(IMAGE_SECTION_HEADER))
            return nullptr;

        for (unsigned section = 0; section < nt->FileHeader.NumberOfSections; ++section)
        {
            const auto& current = sections[section];
            const std::size_t start = current.VirtualAddress;
            const std::size_t size = current.Misc.VirtualSize;
            if ((current.Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0 || start >= imageSize ||
                size < length || size > imageSize - start)
                continue;

            auto cursor = reinterpret_cast<std::uintptr_t>(image + start);
            const auto end = cursor + size;
            while (cursor < end)
            {
                MEMORY_BASIC_INFORMATION info = {};
                if (VirtualQuery(reinterpret_cast<LPCVOID>(cursor), &info, sizeof(info)) != sizeof(info))
                    break;
                const auto regionEnd = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
                if (regionEnd <= cursor)
                    break;
                const auto scanEnd = regionEnd < end ? regionEnd : end;
                if (ExecutableRegion(info) && scanEnd - cursor >= length)
                {
                    for (auto address = cursor; address <= scanEnd - length; ++address)
                    {
                        auto* candidate = reinterpret_cast<std::uint8_t*>(address);
                        if (candidate[0] != pattern[0] || std::memcmp(candidate, pattern, length) != 0)
                            continue;
                        if (result.count < 3)
                            result.samples[result.count] = candidate;
                        ++result.count;
                    }
                }
                cursor = scanEnd;
            }
        }
        return result.count == 1 ? result.samples[0] : nullptr;
    }

    PatternResult FindMaskedCode(const std::int16_t* pattern, std::size_t length)
    {
        PatternResult result;
        auto* image = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
        if (!image || !pattern || length == 0)
            return result;

        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 4096)
            return result;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(image + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
            nt->FileHeader.NumberOfSections == 0 || nt->FileHeader.NumberOfSections > 96)
            return result;
        const std::size_t imageSize = nt->OptionalHeader.SizeOfImage;
        const auto* sections = IMAGE_FIRST_SECTION(nt);
        if (imageSize == 0 || reinterpret_cast<const std::uint8_t*>(sections) < image ||
            static_cast<std::size_t>(reinterpret_cast<const std::uint8_t*>(sections) - image) > imageSize ||
            nt->FileHeader.NumberOfSections >
                (imageSize - static_cast<std::size_t>(reinterpret_cast<const std::uint8_t*>(sections) - image)) /
                sizeof(IMAGE_SECTION_HEADER))
            return result;

        for (unsigned section = 0; section < nt->FileHeader.NumberOfSections; ++section)
        {
            const auto& current = sections[section];
            const std::size_t start = current.VirtualAddress;
            const std::size_t size = current.Misc.VirtualSize;
            if ((current.Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0 || start >= imageSize ||
                size < length || size > imageSize - start)
                continue;

            auto cursor = reinterpret_cast<std::uintptr_t>(image + start);
            const auto end = cursor + size;
            while (cursor < end)
            {
                MEMORY_BASIC_INFORMATION info = {};
                if (VirtualQuery(reinterpret_cast<LPCVOID>(cursor), &info, sizeof(info)) != sizeof(info))
                    break;
                const auto regionEnd = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
                if (regionEnd <= cursor)
                    break;
                const auto scanEnd = regionEnd < end ? regionEnd : end;
                if (ExecutableRegion(info) && scanEnd - cursor >= length)
                {
                    for (auto address = cursor; address <= scanEnd - length; ++address)
                    {
                        const auto* candidate = reinterpret_cast<const std::uint8_t*>(address);
                        std::size_t index = 0;
                        for (; index < length; ++index)
                        {
                            if (pattern[index] >= 0 && candidate[index] != pattern[index])
                                break;
                        }
                        if (index != length)
                            continue;
                        if (result.count < 3)
                            result.samples[result.count] = const_cast<std::uint8_t*>(candidate);
                        ++result.count;
                    }
                }
                cursor = scanEnd;
            }
        }
        return result;
    }

    std::uint8_t* FindFunctionStart(std::uint8_t* candidate)
    {
        if (!candidate)
            return nullptr;
        DWORD64 imageBase = 0;
        const auto* unwind = RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(candidate), &imageBase, nullptr);
        if (!unwind || imageBase != reinterpret_cast<DWORD64>(GetModuleHandleW(nullptr)))
            return nullptr;
        auto* address = reinterpret_cast<std::uint8_t*>(imageBase + unwind->BeginAddress);
        if (address > candidate || candidate - address > 128 ||
            reinterpret_cast<DWORD64>(candidate) >= imageBase + unwind->EndAddress)
            return nullptr;
        return address;
    }

    bool ValidateHookTarget(std::uint8_t* address, std::uint8_t patchLength, const char*& reason)
    {
        if (!address || (patchLength != 5 && patchLength != 7))
        {
            reason = "unsupported patch length";
            return false;
        }

        const auto module = reinterpret_cast<DWORD64>(GetModuleHandleW(nullptr));
        DWORD64 imageBase = 0;
        const auto* unwind = RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(address), &imageBase, nullptr);
        if (!unwind || imageBase != module ||
            unwind->EndAddress <= unwind->BeginAddress ||
            reinterpret_cast<DWORD64>(address) != imageBase + unwind->BeginAddress ||
            unwind->EndAddress - unwind->BeginAddress < patchLength)
        {
            reason = "invalid function boundary";
            return false;
        }

        MEMORY_BASIC_INFORMATION info = {};
        if (VirtualQuery(address, &info, sizeof(info)) != sizeof(info) || !ExecutableRegion(info))
        {
            reason = "non-executable function entry";
            return false;
        }
        const auto start = reinterpret_cast<std::uintptr_t>(address);
        const auto regionStart = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
        if (start < regionStart || start - regionStart >= info.RegionSize ||
            info.RegionSize - (start - regionStart) < patchLength)
        {
            reason = "patch crosses an executable region boundary";
            return false;
        }

        constexpr std::uint8_t coverPrologue[] = { 0x48, 0x89, 0x5C, 0x24, 0x08 };
        constexpr std::uint8_t holsterPrologue[] = { 0x40, 0x55, 0x53, 0x56, 0x57 };
        constexpr std::uint8_t sevenBytePrologue[] = { 0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58 };
        const bool knownInstructions = patchLength == 5 ?
            std::memcmp(address, coverPrologue, sizeof(coverPrologue)) == 0 ||
            std::memcmp(address, holsterPrologue, sizeof(holsterPrologue)) == 0 :
            std::memcmp(address, sevenBytePrologue, sizeof(sevenBytePrologue)) == 0 &&
            (address[6] == 0x08 || address[6] == 0x10);
        if (!knownInstructions)
        {
            reason = "unrecognized instruction boundaries";
            return false;
        }

        const Hook* installed[] = { &holster, &cover, &deadEye, &shouldReload, &clipNeedsReload };
        for (const Hook* existing : installed)
        {
            if (!existing->address || !existing->relay || existing->address[0] != 0xE9)
                continue;
            std::int32_t relative = 0;
            std::memcpy(&relative, existing->address + 1, sizeof(relative));
            const auto other = reinterpret_cast<std::uintptr_t>(existing->address);
            if (other + 5 + relative != reinterpret_cast<std::uintptr_t>(existing->relay))
                continue;
            if (start < other + existing->length && other < start + patchLength)
            {
                reason = "overlaps an installed hook";
                return false;
            }
        }
        reason = "valid";
        return true;
    }

    std::uint8_t* ResolveFunctionEntry(std::uint8_t* candidate, std::uint8_t& patchLength)
    {
        auto* address = FindFunctionStart(candidate);
        if (!address)
            return nullptr;
        const char* reason = nullptr;
        if (ValidateHookTarget(address, 5, reason))
        {
            patchLength = 5;
            return address;
        }
        if (ValidateHookTarget(address, 7, reason))
        {
            patchLength = 7;
            return address;
        }
        return nullptr;
    }

    void ProbePattern(const char* feature, const char* anchor, const PatternResult& result);

    std::uint8_t* FindFallbackEntry(const char* feature, const std::int16_t* body, std::size_t length,
        const std::int16_t* probe, std::size_t probeLength, std::uint8_t& patchLength)
    {
        const PatternResult result = FindMaskedCode(body, length);
        ProbePattern(feature, "body", result);
        if (result.count == 1)
        {
            if (auto* entry = ResolveFunctionEntry(result.samples[0], patchLength))
                return entry;
        }
        if (probe)
            ProbePattern(feature, "short anchor", FindMaskedCode(probe, probeLength));
        return nullptr;
    }

    void ProbePattern(const char* feature, const char* anchor, const PatternResult& result)
    {
        if (!Logger::IsDebugEnabled())
            return;
        char message[256] = {};
        std::snprintf(message, sizeof(message),
            "Gunz: %s / %s matches=%u%s.", feature, anchor,
            result.count, result.count > 1 ? " (ambiguous)" : "");
        Logger::Debug(message);
    }

    std::uint8_t* AllocateNearby(std::uint8_t* address)
    {
        SYSTEM_INFO system = {};
        GetNativeSystemInfo(&system);
        const std::uintptr_t granularity = system.dwAllocationGranularity;
        if (granularity == 0)
            return nullptr;

        const auto origin = reinterpret_cast<std::uintptr_t>(address) & ~(granularity - 1);
        constexpr std::uintptr_t reach = 0x70000000;
        for (std::uintptr_t offset = granularity; offset < reach; offset += granularity)
        {
            const std::uintptr_t candidates[] = {
                origin > offset ? origin - offset : 0,
                origin <= UINTPTR_MAX - offset ? origin + offset : 0
            };
            for (auto location : candidates)
            {
                if (!location)
                    continue;
                const std::int64_t displacement = static_cast<std::int64_t>(location) + 64 -
                    static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(address)) - 5;
                if (displacement < INT32_MIN || displacement > INT32_MAX)
                    continue;
                MEMORY_BASIC_INFORMATION region = {};
                if (VirtualQuery(reinterpret_cast<LPCVOID>(location), &region, sizeof(region)) != sizeof(region) ||
                    region.State != MEM_FREE ||
                    reinterpret_cast<std::uintptr_t>(region.BaseAddress) > location ||
                    region.RegionSize < 128)
                    continue;
                auto* block = static_cast<std::uint8_t*>(VirtualAlloc(reinterpret_cast<void*>(location),
                    4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
                if (block)
                    return block;
            }
        }
        return nullptr;
    }

    void AbsoluteJump(std::uint8_t* destination, const void* target)
    {
        const std::uint8_t opcode[] = { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
        std::memcpy(destination, opcode, sizeof(opcode));
        const auto targetAddress = reinterpret_cast<std::uintptr_t>(target);
        std::memcpy(destination + sizeof(opcode), &targetAddress, sizeof(targetAddress));
    }

    bool InstalledHookMatches(const Hook& hook, const void* replacement)
    {
        if (!hook.address || !hook.gateway || !hook.relay ||
            (hook.length != 5 && hook.length != 7))
            return false;

        const auto validRange = [](const std::uint8_t* address, std::size_t size)
        {
            MEMORY_BASIC_INFORMATION region = {};
            if (VirtualQuery(address, &region, sizeof(region)) != sizeof(region) || !ExecutableRegion(region))
                return false;
            const auto start = reinterpret_cast<std::uintptr_t>(address);
            const auto base = reinterpret_cast<std::uintptr_t>(region.BaseAddress);
            return start >= base && start - base < region.RegionSize &&
                size <= region.RegionSize - (start - base);
        };

        if (!validRange(hook.address, hook.length) ||
            !validRange(hook.gateway, static_cast<std::size_t>(hook.length) + 14) || !validRange(hook.relay, 14))
            return false;

        if (hook.address[0] != 0xE9 ||
            (hook.length == 7 && (hook.address[5] != 0x90 || hook.address[6] != 0x90)) ||
            std::memcmp(hook.gateway, hook.original, hook.length) != 0)
            return false;

        std::int32_t relative = 0;
        std::memcpy(&relative, hook.address + 1, sizeof(relative));
        const auto destination = static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(hook.address)) +
            5 + relative;
        if (destination != static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(hook.relay)))
            return false;

        constexpr std::uint8_t jump[] = { 0xFF, 0x25, 0, 0, 0, 0 };
        if (std::memcmp(hook.gateway + hook.length, jump, sizeof(jump)) != 0 ||
            std::memcmp(hook.relay, jump, sizeof(jump)) != 0)
            return false;
        std::uintptr_t gatewayTarget = 0;
        std::uintptr_t relayTarget = 0;
        std::memcpy(&gatewayTarget, hook.gateway + hook.length + sizeof(jump), sizeof(gatewayTarget));
        std::memcpy(&relayTarget, hook.relay + sizeof(jump), sizeof(relayTarget));
        return gatewayTarget == reinterpret_cast<std::uintptr_t>(hook.address + hook.length) &&
            relayTarget == reinterpret_cast<std::uintptr_t>(replacement);
    }

    bool InstallHook(Hook& hook, const std::uint8_t* signature, std::size_t signatureLength,
        std::uint8_t length, const void* replacement, const char* feature,
        const std::uint8_t* alternate = nullptr, std::size_t alternateLength = 0,
        std::uint8_t alternatePatchLength = 0)
    {
        if (hook.address)
        {
            if (InstalledHookMatches(hook, replacement))
            {
                Logger::Debug((std::string("Gunz: ") + feature + ": existing engine hook verified and reused.").c_str());
                return true;
            }
            Logger::Log((std::string("[WARNING] ") + feature +
                ": existing hook state is inconsistent; duplicate installation skipped.").c_str());
            return false;
        }

        PatternResult entryMatches;
        auto* address = FindUniqueCode(signature, signatureLength, entryMatches);
        ProbePattern(feature, "entry", entryMatches);
        const char* validationReason = nullptr;
        if (address && !ValidateHookTarget(address, length, validationReason))
        {
            Logger::Log((std::string("[PROBE] Gunz: ") + feature +
                " / entry rejected: " + validationReason + ".").c_str());
            address = nullptr;
        }
        if (!address && alternate)
        {
            PatternResult alternateMatches;
            address = FindUniqueCode(alternate, alternateLength, alternateMatches);
            ProbePattern(feature, "legacy entry", alternateMatches);
            if (address)
            {
                length = alternatePatchLength;
                if (!ValidateHookTarget(address, length, validationReason))
                {
                    Logger::Log((std::string("[PROBE] Gunz: ") + feature +
                        " / legacy entry rejected: " + validationReason + ".").c_str());
                    address = nullptr;
                }
            }
        }
        if (!address && std::strcmp(feature, "DisableEmptyAutoReload (reload decision)") == 0)
            address = FindFallbackEntry(feature, shouldReloadBodyPattern,
                sizeof(shouldReloadBodyPattern) / sizeof(shouldReloadBodyPattern[0]), nullptr, 0, length);
        if (!address && std::strcmp(feature, "DisableEmptyAutoReload (clip check)") == 0)
            address = FindFallbackEntry(feature, clipNeedsReloadBodyPattern,
                sizeof(clipNeedsReloadBodyPattern) / sizeof(clipNeedsReloadBodyPattern[0]), nullptr, 0, length);
        if (!address && std::strcmp(feature, "DisableAutoReload (cover)") == 0)
            address = FindFallbackEntry(feature, coverBodyPattern, sizeof(coverBodyPattern) / sizeof(coverBodyPattern[0]),
                coverProbePattern, sizeof(coverProbePattern) / sizeof(coverProbePattern[0]), length);
        if (!address && std::strcmp(feature, "DisableDeadEyeAutoReload") == 0)
            address = FindFallbackEntry(feature, deadEyeBodyPattern, sizeof(deadEyeBodyPattern) / sizeof(deadEyeBodyPattern[0]),
                deadEyeProbePattern, sizeof(deadEyeProbePattern) / sizeof(deadEyeProbePattern[0]), length);
        if (!address)
        {
            Logger::Log((std::string("[WARNING] ") + feature + ": supported engine signature not found; patch skipped.").c_str());
            return false;
        }
        if (!ValidateHookTarget(address, length, validationReason))
        {
            Logger::Log((std::string("[WARNING] ") + feature +
                ": target validation failed (" + validationReason + "); patch skipped.").c_str());
            return false;
        }
        char validation[256] = {};
        std::snprintf(validation, sizeof(validation),
            "[PROBE] Gunz: %s / validated function entry, complete prologue instructions=%u bytes.",
            feature, length);
        Logger::Log(validation);
        auto* block = AllocateNearby(address);
        if (!block)
        {
            Logger::Log((std::string("[WARNING] ") + feature + ": nearby trampoline allocation failed; patch skipped.").c_str());
            return false;
        }

        hook.address = address;
        hook.length = length;
        hook.gateway = block;
        hook.relay = block + 64;
        std::memcpy(hook.original, address, length);
        std::memcpy(block, hook.original, length);
        AbsoluteJump(block + length, address + length);
        AbsoluteJump(hook.relay, replacement);
        FlushInstructionCache(GetCurrentProcess(), block, 128);

        const std::int64_t displacement = reinterpret_cast<std::intptr_t>(hook.relay) -
            (reinterpret_cast<std::intptr_t>(address) + 5);
        if (displacement < INT32_MIN || displacement > INT32_MAX)
        {
            VirtualFree(block, 0, MEM_RELEASE);
            hook = Hook{};
            return false;
        }

        std::uint8_t patch[7] = { 0xE9, 0, 0, 0, 0, 0x90, 0x90 };
        const auto relative = static_cast<std::int32_t>(displacement);
        std::memcpy(patch + 1, &relative, sizeof(relative));
        DWORD protection = 0;
        if (!VirtualProtect(address, length, PAGE_EXECUTE_READWRITE, &protection))
        {
            VirtualFree(block, 0, MEM_RELEASE);
            hook = Hook{};
            Logger::Log((std::string("[WARNING] ") + feature + ": memory protection change failed; patch skipped.").c_str());
            return false;
        }
        std::memcpy(address, patch, length);
        FlushInstructionCache(GetCurrentProcess(), address, length);
        DWORD restored = 0;
        VirtualProtect(address, length, protection, &restored);
        Logger::Log((std::string("[SUCCESS] ") + feature + ": engine hook installed.").c_str());
        return true;
    }

    void RestoreHook(Hook& hook)
    {
        if (!hook.address)
            return;
        if (hook.address[0] == 0xE9)
        {
            std::int32_t offset = 0;
            std::memcpy(&offset, hook.address + 1, sizeof(offset));
            if (hook.address + 5 + offset == hook.relay)
            {
                DWORD protection = 0;
                if (VirtualProtect(hook.address, hook.length, PAGE_EXECUTE_READWRITE, &protection))
                {
                    std::memcpy(hook.address, hook.original, hook.length);
                    FlushInstructionCache(GetCurrentProcess(), hook.address, hook.length);
                    DWORD restored = 0;
                    VirtualProtect(hook.address, hook.length, protection, &restored);
                }
            }
        }
    }

    bool IsPlayer(std::uintptr_t address)
    {
        const auto player = playerAddress.load(std::memory_order_relaxed);
        return player != 0 && address == player;
    }

    void UpdateWeaponCondition()
    {
        if (!weaponDegradationDisabled)
            return;
        const ULONGLONG now = GetTickCount64();
        if (now < nextWeaponConditionRefresh)
            return;
        nextWeaponConditionRefresh = now + 250;

        nativeInit(getLoadingScreenActive);
        if (*nativeCall() != 0)
            return;
        nativeInit(playerPedId);
        const int ped = static_cast<int>(*nativeCall());
        if (ped == 0)
            return;
        nativeInit(doesEntityExist);
        nativePush64(static_cast<std::uint32_t>(ped));
        if (*nativeCall() == 0)
            return;

        int previousWeaponObject = 0;
        for (int attachPoint = 0; attachPoint != 2; ++attachPoint)
        {
            nativeInit(getCurrentPedWeaponEntityIndex);
            nativePush64(static_cast<std::uint32_t>(ped));
            nativePush64(static_cast<std::uint32_t>(attachPoint));
            const int weaponObject = static_cast<int>(*nativeCall());
            if (weaponObject == 0 || weaponObject == previousWeaponObject)
                continue;
            nativeInit(doesEntityExist);
            nativePush64(static_cast<std::uint32_t>(weaponObject));
            if (*nativeCall() == 0)
                continue;
            previousWeaponObject = weaponObject;

            nativeInit(setWeaponDirt);
            nativePush64(static_cast<std::uint32_t>(weaponObject));
            nativePush64(0);
            nativePush64(0);
            nativeCall();
            nativeInit(setWeaponSoot);
            nativePush64(static_cast<std::uint32_t>(weaponObject));
            nativePush64(0);
            nativePush64(0);
            nativeCall();
            nativeInit(setWeaponDamage);
            nativePush64(static_cast<std::uint32_t>(weaponObject));
            nativePush64(0);
            nativePush64(0);
            nativeCall();
            nativeInit(setWeaponDegradation);
            nativePush64(static_cast<std::uint32_t>(weaponObject));
            nativePush64(0);
            nativeCall();
        }
    }

    bool IsArrowTrailWorldReady()
    {
        nativeInit(getLoadingScreenActive);
        if (*nativeCall() != 0)
            return false;
        nativeInit(playerPedId);
        const int player = static_cast<int>(*nativeCall());
        if (!player)
            return false;
        nativeInit(doesEntityExist);
        nativePush64(static_cast<std::uint32_t>(player));
        return *nativeCall() != 0;
    }

    bool ApplyArrowTrailFx(std::uint32_t fx)
    {
        int peds[1024];
        const int count = worldGetAllPeds(peds, 1024);
        bool applied = false;
        for (int i = 0; i < count && i < 1024; ++i)
        {
            if (!peds[i])
                continue;
            if (fx == defaultArrowTrailFx)
            {
                nativeInit(doesEntityExist);
                nativePush64(static_cast<std::uint32_t>(peds[i]));
                if (*nativeCall() == 0)
                    continue;
            }
            nativeInit(setArrowTrailFx);
            nativePush64(static_cast<std::uint32_t>(peds[i]));
            nativePush64(fx);
            nativeCall();
            applied = true;
        }
        return applied;
    }

    void UpdateArrowTrails()
    {
        if ((!arrowTrailsDisabled && !arrowTrailFxApplied) || !IsArrowTrailWorldReady())
            return;
        if (arrowTrailsDisabled)
            arrowTrailFxApplied = ApplyArrowTrailFx(disabledArrowTrailFx) || arrowTrailFxApplied;
        else if (ApplyArrowTrailFx(defaultArrowTrailFx))
            arrowTrailFxApplied = false;
    }

    void RestoreReticleAccuracy(int ped)
    {
        if (!reticleAccuracyOverridden || reticleAccuracyPed != ped)
            return;

        nativeInit(doesEntityExist);
        nativePush64(static_cast<std::uint32_t>(ped));
        if (*nativeCall() != 0)
        {
            nativeInit(getPedAccuracy);
            nativePush64(static_cast<std::uint32_t>(ped));
            if (static_cast<int>(*nativeCall()) == 100 && previousReticleAccuracy != 100)
            {
                nativeInit(setPedAccuracy);
                nativePush64(static_cast<std::uint32_t>(ped));
                nativePush64(static_cast<std::uint32_t>(previousReticleAccuracy));
                nativeCall();
            }
        }
        reticleAccuracyPed = 0;
        previousReticleAccuracy = 0;
        reticleAccuracyOverridden = false;
    }

    void UpdateReticleSpread()
    {
        if (!reticleSpreadDisabled && !reticleSwayDisabled)
            return;

        nativeInit(playerPedId);
        const int ped = static_cast<int>(*nativeCall());
        if (reticleAccuracyOverridden && reticleAccuracyPed != ped)
            RestoreReticleAccuracy(reticleAccuracyPed);
        if (ped == 0)
            return;

        nativeInit(doesEntityExist);
        nativePush64(static_cast<std::uint32_t>(ped));
        if (*nativeCall() == 0)
        {
            RestoreReticleAccuracy(ped);
            return;
        }

        if (reticleSwayDisabled)
        {
            nativeInit(setPedResetFlag);
            nativePush64(static_cast<std::uint32_t>(ped));
            nativePush64(reticleSwayResetFlag);
            nativePush64(1);
            nativeCall();
        }
        if (!reticleSpreadDisabled)
            return;

        std::uint32_t weapon = 0;
        nativeInit(getCurrentPedWeapon);
        nativePush64(static_cast<std::uint32_t>(ped));
        nativePush64(reinterpret_cast<std::uint64_t>(&weapon));
        nativePush64(1);
        nativePush64(0);
        nativePush64(0);
        if (*nativeCall() == 0 || weapon == 0)
        {
            RestoreReticleAccuracy(ped);
            return;
        }

        nativeInit(getPedAmmoTypeFromWeapon);
        nativePush64(static_cast<std::uint32_t>(ped));
        nativePush64(weapon);
        const auto ammo = static_cast<std::uint32_t>(*nativeCall());
        if (ammo == 0 || ammo == shotgunShellAmmo || ammo == incendiaryShellAmmo)
        {
            RestoreReticleAccuracy(ped);
            return;
        }

        if (!reticleAccuracyOverridden || reticleAccuracyPed != ped)
        {
            nativeInit(getPedAccuracy);
            nativePush64(static_cast<std::uint32_t>(ped));
            previousReticleAccuracy = static_cast<int>(*nativeCall());
            if (previousReticleAccuracy < 0 || previousReticleAccuracy > 100)
                return;
            reticleAccuracyPed = ped;
            reticleAccuracyOverridden = true;
        }

        nativeInit(setPedAccuracy);
        nativePush64(static_cast<std::uint32_t>(ped));
        nativePush64(100);
        nativeCall();

        nativeInit(setPedResetFlag);
        nativePush64(static_cast<std::uint32_t>(ped));
        nativePush64(reticleResetFlag);
        nativePush64(1);
        nativeCall();
    }

    bool ReadScopeProbeMemory(std::uintptr_t address, std::uint8_t* buffer,
        std::size_t capacity, std::size_t& length)
    {
        length = 0;
        if (address < 0x10000 || address > 0x00007FFFFFFF0000ULL || !buffer || !capacity)
            return false;
        MEMORY_BASIC_INFORMATION information = {};
        if (VirtualQuery(reinterpret_cast<LPCVOID>(address), &information, sizeof(information)) != sizeof(information) ||
            information.State != MEM_COMMIT || (information.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
            return false;
        const DWORD protection = information.Protect & 0xFF;
        if (protection != PAGE_READONLY && protection != PAGE_READWRITE && protection != PAGE_WRITECOPY &&
            protection != PAGE_EXECUTE_READ && protection != PAGE_EXECUTE_READWRITE &&
            protection != PAGE_EXECUTE_WRITECOPY)
            return false;
        const auto base = reinterpret_cast<std::uintptr_t>(information.BaseAddress);
        if (base > address || information.RegionSize > (std::numeric_limits<std::uintptr_t>::max)() - base ||
            address >= base + information.RegionSize)
            return false;
        const auto available = base + information.RegionSize - address;
        const auto requested = available < capacity ? available : capacity;
        if (requested < sizeof(std::uintptr_t))
            return false;
        SIZE_T transferred = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address),
                buffer, requested, &transferred) || transferred < sizeof(std::uintptr_t))
            return false;
        length = transferred;
        return true;
    }

    bool ReadCarcanoMetadata(std::uintptr_t address, std::uint8_t* metadata)
    {
        std::size_t length = 0;
        if (!ReadScopeProbeMemory(address, metadata, carcanoMetadataSize, length) ||
            length != carcanoMetadataSize)
            return false;
        std::uint32_t hash = 0;
        std::memcpy(&hash, metadata + 0x20, sizeof(hash));
        return hash == 0x53944780U;
    }

    unsigned MatchingCarcanoFields(const std::uint8_t* metadata, bool replaced)
    {
        unsigned count = 0;
        for (const auto& field : carcanoScopeFields)
        {
            std::uint32_t value = 0;
            std::memcpy(&value, metadata + field.offset, field.size);
            if (value == (replaced ? field.replacement : field.original))
                ++count;
        }
        return count;
    }

    bool PatchCarcanoMetadata(std::uintptr_t address, const std::uint8_t* metadata)
    {
        constexpr unsigned fieldCount = sizeof(carcanoScopeFields) / sizeof(carcanoScopeFields[0]);
        const unsigned originalMatches = MatchingCarcanoFields(metadata, false);
        const unsigned patchedMatches = MatchingCarcanoFields(metadata, true);
        char message[240] = {};
        if (patchedMatches == fieldCount)
        {
            carcanoScopeWrittenMask = 0;
            Logger::Debug("DisableForcedScope: Carcano metadata already patched; no writes needed.");
            return true;
        }
        if (originalMatches + patchedMatches != fieldCount)
        {
            std::snprintf(message, sizeof(message),
                "[WARNING] DisableForcedScope: Carcano metadata mismatch (%u/11 original, %u/11 patched); refusing writes.",
                originalMatches, patchedMatches);
            Logger::Log(message);
            return false;
        }

        MEMORY_BASIC_INFORMATION info = {};
        if (VirtualQuery(reinterpret_cast<LPCVOID>(address), &info, sizeof(info)) != sizeof(info) ||
            info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
        {
            Logger::Log("[WARNING] DisableForcedScope: Carcano metadata memory is unavailable; no writes made.");
            return false;
        }
        const DWORD protection = info.Protect & 0xFF;
        if (protection != PAGE_READWRITE && protection != PAGE_WRITECOPY &&
            protection != PAGE_EXECUTE_READWRITE && protection != PAGE_EXECUTE_WRITECOPY)
        {
            Logger::Log("[WARNING] DisableForcedScope: Carcano metadata is not writable; no writes made.");
            return false;
        }
        const auto regionBase = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
        if (regionBase > address || info.RegionSize > (std::numeric_limits<std::uintptr_t>::max)() - regionBase ||
            address >= regionBase + info.RegionSize ||
            regionBase + info.RegionSize - address < carcanoMetadataSize)
        {
            Logger::Log("[WARNING] DisableForcedScope: Carcano metadata crosses memory regions; no writes made.");
            return false;
        }

        const HANDLE process = GetCurrentProcess();
        std::uint16_t writtenMask = 0;
        unsigned written = 0;
        bool failed = false;
        for (unsigned i = 0; i < fieldCount; ++i)
        {
            const auto& field = carcanoScopeFields[i];
            std::uint32_t current = 0;
            SIZE_T transferred = 0;
            const auto location = reinterpret_cast<void*>(address + field.offset);
            if (!ReadProcessMemory(process, location, &current, field.size, &transferred) ||
                transferred != field.size)
            {
                failed = true;
                break;
            }
            if (current == field.replacement)
                continue;
            if (current != field.original ||
                !WriteProcessMemory(process, location, &field.replacement, field.size, &transferred) ||
                transferred != field.size)
            {
                failed = true;
                break;
            }
            writtenMask |= static_cast<std::uint16_t>(1U << i);
            ++written;
            current = 0;
            if (!ReadProcessMemory(process, location, &current, field.size, &transferred) ||
                transferred != field.size || current != field.replacement)
            {
                failed = true;
                break;
            }
        }

        std::uint8_t verify[carcanoMetadataSize] = {};
        if (!failed && ReadCarcanoMetadata(address, verify) &&
            MatchingCarcanoFields(verify, true) == fieldCount)
        {
            carcanoScopeWrittenMask = writtenMask;
            std::snprintf(message, sizeof(message),
                "[SUCCESS] DisableForcedScope: Carcano metadata patched (11 fields verified, %u changed, %u already set).",
                written, patchedMatches);
            Logger::Log(message);
            return true;
        }

        unsigned rolledBack = 0;
        for (unsigned i = fieldCount; i > 0; --i)
        {
            if ((writtenMask & (1U << (i - 1))) == 0)
                continue;
            const auto& field = carcanoScopeFields[i - 1];
            const auto location = reinterpret_cast<void*>(address + field.offset);
            std::uint32_t current = 0;
            SIZE_T transferred = 0;
            if (ReadProcessMemory(process, location, &current, field.size, &transferred) &&
                transferred == field.size && current == field.replacement &&
                WriteProcessMemory(process, location, &field.original, field.size, &transferred) &&
                transferred == field.size)
                ++rolledBack;
        }
        std::snprintf(message, sizeof(message),
            "[WARNING] DisableForcedScope: guarded Carcano write failed; written=%u/11, rolledBack=%u/11.",
            written, rolledBack);
        Logger::Log(message);
        return false;
    }

    bool ReadRollingMetadata(std::uintptr_t address, std::uint32_t expectedHash, std::uint8_t* metadata)
    {
        std::size_t length = 0;
        if (!ReadScopeProbeMemory(address, metadata, carcanoMetadataSize, length) ||
            length != carcanoMetadataSize)
            return false;
        std::uint32_t hash = 0;
        std::memcpy(&hash, metadata + 0x20, sizeof(hash));
        return hash == expectedHash;
    }

    unsigned MatchingRollingFields(const std::uint8_t* metadata, bool replaced)
    {
        unsigned count = 0;
        for (const auto& field : rollingScopeFields)
        {
            std::uint32_t value = 0;
            std::memcpy(&value, metadata + field.offset, field.size);
            if (value == (replaced ? field.replacement : field.original))
                ++count;
        }
        return count;
    }

    bool PatchRollingMetadata(std::uintptr_t address, std::uint32_t expectedHash,
        const char* name, unsigned index, const std::uint8_t* metadata)
    {
        constexpr unsigned fieldCount = sizeof(rollingScopeFields) / sizeof(rollingScopeFields[0]);
        const unsigned originalMatches = MatchingRollingFields(metadata, false);
        const unsigned replacedMatches = MatchingRollingFields(metadata, true);
        char message[260] = {};
        if (replacedMatches == fieldCount)
        {
            rollingScopeWrittenMask[index] = 0;
            std::snprintf(message, sizeof(message),
                "DisableForcedScope: %s metadata already patched; no writes needed.", name);
            Logger::Debug(message);
            return true;
        }
        if (originalMatches + replacedMatches != fieldCount)
        {
            std::snprintf(message, sizeof(message),
                "[WARNING] DisableForcedScope: %s metadata mismatch (%u/11 original, %u/11 patched); refusing writes.",
                name, originalMatches, replacedMatches);
            Logger::Log(message);
            return false;
        }
        std::uint32_t hash = 0;
        std::memcpy(&hash, metadata + 0x20, sizeof(hash));
        if (hash != expectedHash)
            return false;

        MEMORY_BASIC_INFORMATION info = {};
        if (VirtualQuery(reinterpret_cast<LPCVOID>(address), &info, sizeof(info)) != sizeof(info) ||
            info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
        {
            std::snprintf(message, sizeof(message),
                "[WARNING] DisableForcedScope: %s metadata memory unavailable; no writes made.", name);
            Logger::Log(message);
            return false;
        }
        const DWORD protection = info.Protect & 0xFF;
        if (protection != PAGE_READWRITE && protection != PAGE_WRITECOPY &&
            protection != PAGE_EXECUTE_READWRITE && protection != PAGE_EXECUTE_WRITECOPY)
        {
            std::snprintf(message, sizeof(message),
                "[WARNING] DisableForcedScope: %s metadata is not writable; no writes made.", name);
            Logger::Log(message);
            return false;
        }
        const auto regionBase = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
        if (regionBase > address || info.RegionSize > (std::numeric_limits<std::uintptr_t>::max)() - regionBase ||
            address >= regionBase + info.RegionSize ||
            regionBase + info.RegionSize - address < carcanoMetadataSize)
        {
            std::snprintf(message, sizeof(message),
                "[WARNING] DisableForcedScope: %s metadata crosses memory regions; no writes made.", name);
            Logger::Log(message);
            return false;
        }

        const HANDLE process = GetCurrentProcess();
        std::uint16_t writtenMask = 0;
        unsigned written = 0;
        bool failed = false;
        for (unsigned i = 0; i < fieldCount; ++i)
        {
            const auto& field = rollingScopeFields[i];
            std::uint32_t current = 0;
            SIZE_T transferred = 0;
            const auto location = reinterpret_cast<void*>(address + field.offset);
            if (!ReadProcessMemory(process, location, &current, field.size, &transferred) ||
                transferred != field.size)
            {
                failed = true;
                break;
            }
            if (current == field.replacement)
                continue;
            if (current != field.original ||
                !WriteProcessMemory(process, location, &field.replacement, field.size, &transferred) ||
                transferred != field.size)
            {
                failed = true;
                break;
            }
            writtenMask |= static_cast<std::uint16_t>(1U << i);
            ++written;
            current = 0;
            if (!ReadProcessMemory(process, location, &current, field.size, &transferred) ||
                transferred != field.size || current != field.replacement)
            {
                failed = true;
                break;
            }
        }
        std::uint8_t verify[carcanoMetadataSize] = {};
        if (!failed && ReadRollingMetadata(address, expectedHash, verify) &&
            MatchingRollingFields(verify, true) == fieldCount)
        {
            rollingScopeWrittenMask[index] = writtenMask;
            std::snprintf(message, sizeof(message),
                "[SUCCESS] DisableForcedScope: %s runtime metadata patched (11 fields verified, %u changed, %u already set).",
                name, written, replacedMatches);
            Logger::Log(message);
            return true;
        }

        unsigned rolledBack = 0;
        for (unsigned i = fieldCount; i > 0; --i)
        {
            if ((writtenMask & (1U << (i - 1))) == 0)
                continue;
            const auto& field = rollingScopeFields[i - 1];
            const auto location = reinterpret_cast<void*>(address + field.offset);
            std::uint32_t current = 0;
            SIZE_T transferred = 0;
            if (ReadProcessMemory(process, location, &current, field.size, &transferred) &&
                transferred == field.size && current == field.replacement &&
                WriteProcessMemory(process, location, &field.original, field.size, &transferred) &&
                transferred == field.size)
                ++rolledBack;
        }
        std::snprintf(message, sizeof(message),
            "[WARNING] DisableForcedScope: %s guarded write failed; written=%u/11, rolledBack=%u/11.",
            name, written, rolledBack);
        Logger::Log(message);
        return false;
    }

    bool RestoreMissionScopeMetadata(std::uintptr_t address, std::uint32_t expectedHash,
        const ScopeFieldPatch (&fields)[11], std::uint16_t writtenMask)
    {
        if (!address || !writtenMask)
            return true;
        MEMORY_BASIC_INFORMATION allocation{};
        if (VirtualQuery(reinterpret_cast<const void*>(address), &allocation, sizeof(allocation)) == sizeof(allocation) &&
            allocation.State != MEM_COMMIT)
            return true;

        std::uint8_t metadata[carcanoMetadataSize] = {};
        std::size_t length = 0;
        if (!ReadScopeProbeMemory(address, metadata, sizeof(metadata), length) ||
            length != sizeof(metadata))
            return false;
        std::uint32_t hash = 0;
        std::memcpy(&hash, metadata + 0x20, sizeof(hash));
        if (hash != expectedHash)
            return true;

        for (unsigned i = 0; i < 11; ++i)
        {
            if ((writtenMask & (1U << i)) == 0)
                continue;
            std::uint32_t current = 0;
            std::memcpy(&current, metadata + fields[i].offset, fields[i].size);
            if (current != fields[i].original && current != fields[i].replacement)
                return false;
        }

        MEMORY_BASIC_INFORMATION info = {};
        if (VirtualQuery(reinterpret_cast<LPCVOID>(address), &info, sizeof(info)) != sizeof(info) ||
            info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
            return false;
        const DWORD protection = info.Protect & 0xFF;
        if (protection != PAGE_READWRITE && protection != PAGE_WRITECOPY &&
            protection != PAGE_EXECUTE_READWRITE && protection != PAGE_EXECUTE_WRITECOPY)
            return false;
        const auto regionBase = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
        if (regionBase > address || info.RegionSize > (std::numeric_limits<std::uintptr_t>::max)() - regionBase ||
            address >= regionBase + info.RegionSize ||
            regionBase + info.RegionSize - address < carcanoMetadataSize)
            return false;

        const HANDLE process = GetCurrentProcess();
        std::uint16_t restoredMask = 0;
        bool failed = false;
        for (unsigned i = 0; i < 11; ++i)
        {
            if ((writtenMask & (1U << i)) == 0)
                continue;
            const auto& field = fields[i];
            const auto location = reinterpret_cast<void*>(address + field.offset);
            std::uint32_t current = 0;
            SIZE_T transferred = 0;
            if (!ReadProcessMemory(process, location, &current, field.size, &transferred) ||
                transferred != field.size)
            {
                failed = true;
                break;
            }
            if (current == field.original)
                continue;
            if (current != field.replacement ||
                !WriteProcessMemory(process, location, &field.original, field.size, &transferred) ||
                transferred != field.size)
            {
                failed = true;
                break;
            }
            restoredMask |= static_cast<std::uint16_t>(1U << i);
            current = 0;
            if (!ReadProcessMemory(process, location, &current, field.size, &transferred) ||
                transferred != field.size || current != field.original)
            {
                failed = true;
                break;
            }
        }
        if (!failed)
        {
            length = 0;
            if (ReadScopeProbeMemory(address, metadata, sizeof(metadata), length) &&
                length == sizeof(metadata))
            {
                bool restored = true;
                for (unsigned i = 0; i < 11; ++i)
                {
                    if ((writtenMask & (1U << i)) == 0)
                        continue;
                    std::uint32_t current = 0;
                    std::memcpy(&current, metadata + fields[i].offset, fields[i].size);
                    if (current != fields[i].original)
                    {
                        restored = false;
                        break;
                    }
                }
                if (restored)
                    return true;
            }
        }

        length = 0;
        if (ReadScopeProbeMemory(address, metadata, sizeof(metadata), length) &&
            length == sizeof(metadata))
        {
            std::memcpy(&hash, metadata + 0x20, sizeof(hash));
            if (hash == expectedHash)
            {
                for (unsigned i = 0; i < 11; ++i)
                {
                    if ((restoredMask & (1U << i)) == 0)
                        continue;
                    const auto& field = fields[i];
                    std::uint32_t current = 0;
                    std::memcpy(&current, metadata + field.offset, field.size);
                    if (current != field.original)
                        continue;
                    SIZE_T transferred = 0;
                    WriteProcessMemory(process, reinterpret_cast<void*>(address + field.offset),
                        &field.replacement, field.size, &transferred);
                }
            }
        }
        return false;
    }

    void RearmRollingObserver()
    {
        if (rollingScopeObserver.address && !rollingScopeObserverFinished)
        {
            RestoreHook(rollingScopeObserver);
            constexpr std::uint8_t original[] = { 0x8B, 0x88, 0xF4, 0x02, 0x00, 0x00 };
            if (std::memcmp(rollingScopeObserver.address, original, sizeof(original)) != 0)
                return;
        }
        rollingScopeObserver.address = nullptr;
        rollingScopeObserver.length = 0;
        rollingScopeObserverAttempted = false;
        rollingScopeObserverFinished = false;
        rollingScopeObserverRestoreWarningLogged = false;
        for (auto& warning : rollingScopeReadWarningLogged)
            warning = false;
        for (auto& pointer : rejectedRollingInfo)
            pointer = 0;
        InterlockedExchange64(&observedRollingInfo, 0);
        InterlockedExchange64(&observedRareRollingInfo, 0);
    }

    bool RearmCarcanoObserver()
    {
        constexpr std::uint8_t instruction[] = { 0x8B, 0x88, 0xF4, 0x02, 0x00, 0x00 };
        if (rollingScopeObserver.address && !rollingScopeObserverFinished)
        {
            RestoreHook(rollingScopeObserver);
            if (std::memcmp(rollingScopeObserver.address, instruction, sizeof(instruction)) != 0)
                return false;
        }
        if (scopeInfoObserver.address)
        {
            if (!scopeInfoObserverFinished)
                RestoreHook(scopeInfoObserver);
            if (std::memcmp(scopeInfoObserver.address, instruction, sizeof(instruction)) != 0)
                return false;
        }
        const bool carcanoRestored = RestoreMissionScopeMetadata(
            patchedCarcanoInfo, 0x53944780U, carcanoScopeFields, carcanoScopeWrittenMask);
        const bool rollingRestored = RestoreMissionScopeMetadata(
            patchedRollingInfo[0], 0xE1D2B317U, rollingScopeFields, rollingScopeWrittenMask[0]);
        const bool rareRestored = RestoreMissionScopeMetadata(
            patchedRollingInfo[1], 0x4E328256U, rollingScopeFields, rollingScopeWrittenMask[1]);
        if (!carcanoRestored || !rollingRestored || !rareRestored)
            return false;

        rollingScopeObserver.address = nullptr;
        rollingScopeObserver.length = 0;
        rollingScopeObserverAttempted = false;
        rollingScopeObserverFinished = false;
        rollingScopeObserverRestoreWarningLogged = false;
        for (auto& logged : rollingScopeMetadataLogged)
            logged = false;
        for (auto& logged : rollingScopeReadWarningLogged)
            logged = false;
        for (auto& pointer : patchedRollingInfo)
            pointer = 0;
        for (auto& mask : rollingScopeWrittenMask)
            mask = 0;
        for (auto& pointer : rejectedRollingInfo)
            pointer = 0;
        nextRollingObserverRetry = 0;
        InterlockedExchange64(&observedRollingInfo, 0);
        InterlockedExchange64(&observedRareRollingInfo, 0);
        scopeInfoObserver.address = nullptr;
        scopeInfoObserver.length = 0;
        scopeInfoObserverFinished = false;
        scopeInfoObserverRestoreWarningLogged = false;
        scopeWeaponInfoAccessProbed = false;
        patchedCarcanoInfo = 0;
        carcanoScopeWrittenMask = 0;
        rejectedCarcanoInfo = 0;
        nextScopeObserverRetry = 0;
        trackedScopePlayer = 0;
        scopeCarcanoPatched = false;
        InterlockedExchange64(&observedCarcanoInfo, 0);
        Logger::Debug("DisableForcedScope: runtime weapon records reset.");
        return true;
    }

    bool UpdateScopeMission(ULONGLONG now)
    {
        if (!scopeExcludedMissionHash)
            return false;
        if (now >= nextScopeMissionCheck)
        {
            nextScopeMissionCheck = now + 750;
            nativeInit(getRunningScriptThreads);
            nativePush64(scopeExcludedMissionHash);
            const bool running = static_cast<int>(*nativeCall()) > 0;
            if (running)
            {
                scopeMissionInactiveSince = 0;
                if (!scopeMissionSuspended)
                {
                    scopeMissionSuspended = true;
                    scopeMissionRecordsRestored = false;
                    scopeMissionRestoreWarningLogged = false;
                    nextScopeMissionRestore = 0;
                    Logger::Debug("DisableForcedScope: FIN1 mission detected; suspending scope modification.");
                }
            }
            else if (scopeMissionSuspended)
            {
                if (!scopeMissionInactiveSince)
                    scopeMissionInactiveSince = now;
                else if (now - scopeMissionInactiveSince >= 2000)
                {
                    RearmCarcanoObserver();
                    if (!scopeInfoObserver.address)
                    {
                        scopeMissionSuspended = false;
                        scopeMissionRecordsRestored = false;
                        scopeMissionRestoreWarningLogged = false;
                        scopeMissionInactiveSince = 0;
                        Logger::Debug("DisableForcedScope: FIN1 mission ended; resuming runtime scope patching.");
                    }
                }
            }
        }
        if (!scopeMissionSuspended)
            return false;
        if (scopeMissionRecordsRestored || now < nextScopeMissionRestore)
            return true;
        nextScopeMissionRestore = now + 750;

        constexpr std::uint8_t instruction[] = { 0x8B, 0x88, 0xF4, 0x02, 0x00, 0x00 };
        if (scopeInfoObserver.address && !scopeInfoObserverFinished)
            RestoreHook(scopeInfoObserver);
        if (scopeInfoObserver.address &&
            std::memcmp(scopeInfoObserver.address, instruction, sizeof(instruction)) != 0)
        {
            if (!scopeMissionRestoreWarningLogged)
            {
                Logger::Log("[WARNING] DisableForcedScope: FIN1 scope observer could not be removed; retrying safely.");
                scopeMissionRestoreWarningLogged = true;
            }
            return true;
        }

        if (RearmCarcanoObserver())
        {
            scopeMissionRecordsRestored = true;
            Logger::Debug("DisableForcedScope: FIN1 scope patch suspended; known rifle metadata restored where present.");
            return true;
        }

        if (!scopeMissionRestoreWarningLogged)
        {
            Logger::Log("[WARNING] DisableForcedScope: FIN1 metadata restore incomplete; retrying without new scope patches.");
            scopeMissionRestoreWarningLogged = true;
        }
        return true;
    }

    bool UpdateScopeLoadState(ULONGLONG now)
    {
        nativeInit(getLoadingScreenActive);
        if (static_cast<std::uint32_t>(*nativeCall()) != 0)
        {
            if (scopeLifecycleReady)
                scopeLoadPending = true;
            scopeLoadReadyAt = 0;
            scopeLastLifecycleCheck = now;
            scopeLastPlayerPed = 0;
            return false;
        }
        nativeInit(playerPedId);
        const auto ped = static_cast<std::uint32_t>(*nativeCall());
        nativeInit(getGameTimer);
        const auto gameTime = static_cast<std::uint32_t>(*nativeCall());

        if (scopeLifecycleReady && scopeLastLifecycleCheck != 0 &&
            (now - scopeLastLifecycleCheck > 3000 ||
            (scopeLastPlayerPed != 0 && ped != 0 && ped != scopeLastPlayerPed) ||
            (gameTime < scopeLastGameTimer && scopeLastGameTimer - gameTime > 1000)))
            scopeLoadPending = true;

        scopeLastLifecycleCheck = now;
        scopeLastGameTimer = gameTime;
        scopeLastPlayerPed = ped;
        if (ped == 0)
        {
            if (scopeLifecycleReady)
                scopeLoadPending = true;
            scopeLoadReadyAt = 0;
            return false;
        }
        scopeLifecycleReady = true;
        if (!scopeLoadPending)
            return true;

        if (!scopeLoadReadyAt)
            scopeLoadReadyAt = now + 750;
        if (now < scopeLoadReadyAt)
            return false;

        if (scopeInfoObserver.address)
        {
            if (!RearmCarcanoObserver())
                return false;
        }
        else
            scopeWeaponInfoAccessProbed = false;

        scopeLoadPending = false;
        scopeLoadReadyAt = 0;
        nextScopeRecordCheck = 0;
        Logger::Debug("DisableForcedScope: gameplay load detected; weapon metadata observers reset.");
        return true;
    }

    void CheckCarcanoRecord()
    {
        if (!scopeCarcanoPatched && !rollingScopeMetadataLogged[0] && !rollingScopeMetadataLogged[1])
            return;
        const ULONGLONG now = GetTickCount64();
        if (now < nextScopeRecordCheck)
            return;
        nextScopeRecordCheck = now + 5000;

        if (scopeCarcanoPatched)
        {
            std::uint8_t metadata[carcanoMetadataSize] = {};
            if (!ReadCarcanoMetadata(patchedCarcanoInfo, metadata) ||
                MatchingCarcanoFields(metadata, true) != sizeof(carcanoScopeFields) / sizeof(carcanoScopeFields[0]))
            {
                RearmCarcanoObserver();
                return;
            }
        }

        for (unsigned i = 0; i < 2; ++i)
        {
            if (!patchedRollingInfo[i])
                continue;
            std::uint8_t metadata[carcanoMetadataSize] = {};
            const auto hash = i == 0 ? 0xE1D2B317U : 0x4E328256U;
            if (!ReadRollingMetadata(patchedRollingInfo[i], hash, metadata) ||
                MatchingRollingFields(metadata, true) !=
                    sizeof(rollingScopeFields) / sizeof(rollingScopeFields[0]))
            {
                Logger::Debug("DisableForcedScope: Rolling Block metadata invalidated; recapturing.");
                RearmCarcanoObserver();
                return;
            }
        }

        nativeInit(playerPedId);
        const auto ped = static_cast<int>(*nativeCall());
        if (!ped)
            return;
        const auto player = reinterpret_cast<std::uintptr_t>(getScriptHandleBaseAddress(ped));
        if (!player)
            return;
        if (trackedScopePlayer && player != trackedScopePlayer)
        {
            RearmCarcanoObserver();
            return;
        }
        trackedScopePlayer = player;
    }

    void LogScopeWeaponInfoCode(const char* label, std::uintptr_t start, std::uintptr_t end,
        std::uintptr_t image)
    {
        for (auto address = start; address < end; address += 16)
        {
            const std::size_t remaining = end - address;
            const std::size_t count = remaining < 16 ? remaining : 16;
            if (count < sizeof(std::uintptr_t))
                break;
            std::uint8_t bytes[16] = {};
            std::size_t length = 0;
            if (!ReadScopeProbeMemory(address, bytes, count, length) || length != count)
                break;
            char hex[49] = {};
            for (std::size_t i = 0; i < count; ++i)
                std::snprintf(hex + i * 3, sizeof(hex) - i * 3, "%02X ", bytes[i]);
            char message[160] = {};
            std::snprintf(message, sizeof(message),
                "DisableForcedScope %s: rva=0x%llX, bytes=%s",
                label, static_cast<unsigned long long>(address - image), hex);
            Logger::Debug(message);
        }
    }

    bool InstallScopeInfoObserver(std::uint8_t* anchor, DWORD64 functionStart, DWORD64 functionEnd)
    {
        if (!anchor || !forcedScopeProbeEnabled ||
            carcanoWeapon != 0x53944780U || rollingBlockWeapon != 0xE1D2B317U ||
            exoticRollingBlockWeapon != 0x4E328256U || scopeInfoObserver.address || scopeInfoObserverFinished)
            return false;

        auto* instruction = anchor + 4;
        constexpr std::uint8_t expected[] = { 0x8B, 0x88, 0xF4, 0x02, 0x00, 0x00 };
        if (std::memcmp(instruction, expected, sizeof(expected)) != 0 ||
            reinterpret_cast<DWORD64>(instruction) < functionStart ||
            reinterpret_cast<DWORD64>(instruction) > functionEnd ||
            functionEnd - reinterpret_cast<DWORD64>(instruction) < sizeof(expected))
            return false;

        MEMORY_BASIC_INFORMATION region = {};
        if (VirtualQuery(instruction, &region, sizeof(region)) != sizeof(region) || !ExecutableRegion(region) ||
            reinterpret_cast<std::uintptr_t>(instruction) < reinterpret_cast<std::uintptr_t>(region.BaseAddress) ||
            region.RegionSize - (reinterpret_cast<std::uintptr_t>(instruction) -
                reinterpret_cast<std::uintptr_t>(region.BaseAddress)) < sizeof(expected))
            return false;

        const Hook* existing[] = { &holster, &cover, &deadEye, &shouldReload, &clipNeedsReload };
        for (const Hook* hook : existing)
        {
            if (hook->address && instruction < hook->address + hook->length &&
                hook->address < instruction + sizeof(expected))
                return false;
        }

        auto* block = scopeInfoObserver.relay ? scopeInfoObserver.relay : AllocateNearby(instruction);
        if (!block)
            return false;
        const bool allocatedNew = !scopeInfoObserver.relay;

        std::uint8_t stub[] = {
            0x9C, 0x41, 0x53,
            0x81, 0x78, 0x20, 0x80, 0x47, 0x94, 0x53,
            0x75, 0x0D,
            0x49, 0xBB, 0, 0, 0, 0, 0, 0, 0, 0,
            0x49, 0x89, 0x03,
            0x81, 0x78, 0x20, 0x17, 0xB3, 0xD2, 0xE1,
            0x75, 0x0D,
            0x49, 0xBB, 0, 0, 0, 0, 0, 0, 0, 0,
            0x49, 0x89, 0x03,
            0x81, 0x78, 0x20, 0x56, 0x82, 0x32, 0x4E,
            0x75, 0x0D,
            0x49, 0xBB, 0, 0, 0, 0, 0, 0, 0, 0,
            0x49, 0x89, 0x03,
            0x41, 0x5B, 0x9D,
            0x8B, 0x88, 0xF4, 0x02, 0x00, 0x00,
            0xFF, 0x25, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0
        };
        static_assert(sizeof(stub) == 92, "Invalid scope observer stub length");
#pragma warning(push)
#pragma warning(disable:28112)
        const auto carcanoDestination = reinterpret_cast<std::uintptr_t>(&observedCarcanoInfo);
        const auto rollingDestination = reinterpret_cast<std::uintptr_t>(&observedRollingInfo);
        const auto rareDestination = reinterpret_cast<std::uintptr_t>(&observedRareRollingInfo);
#pragma warning(pop)
        const auto resume = reinterpret_cast<std::uintptr_t>(instruction + sizeof(expected));
        std::memcpy(stub + 14, &carcanoDestination, sizeof(carcanoDestination));
        std::memcpy(stub + 36, &rollingDestination, sizeof(rollingDestination));
        std::memcpy(stub + 58, &rareDestination, sizeof(rareDestination));
        std::memcpy(stub + 84, &resume, sizeof(resume));
        std::memcpy(block, stub, sizeof(stub));
        FlushInstructionCache(GetCurrentProcess(), block, sizeof(stub));

        const std::int64_t displacement = reinterpret_cast<std::intptr_t>(block) -
            (reinterpret_cast<std::intptr_t>(instruction) + 5);
        if (displacement < INT32_MIN || displacement > INT32_MAX)
        {
            if (allocatedNew)
                VirtualFree(block, 0, MEM_RELEASE);
            return false;
        }

        std::uint8_t patch[] = { 0xE9, 0, 0, 0, 0, 0x90 };
        const auto relative = static_cast<std::int32_t>(displacement);
        std::memcpy(patch + 1, &relative, sizeof(relative));
        DWORD originalProtection = 0;
        if (!VirtualProtect(instruction, sizeof(expected), PAGE_EXECUTE_READWRITE, &originalProtection))
        {
            if (allocatedNew)
                VirtualFree(block, 0, MEM_RELEASE);
            return false;
        }
        if (std::memcmp(instruction, expected, sizeof(expected)) != 0)
        {
            DWORD restored = 0;
            VirtualProtect(instruction, sizeof(expected), originalProtection, &restored);
            if (allocatedNew)
                VirtualFree(block, 0, MEM_RELEASE);
            return false;
        }

        scopeInfoObserver.address = instruction;
        scopeInfoObserver.relay = block;
        scopeInfoObserver.length = static_cast<std::uint8_t>(sizeof(expected));
        std::memcpy(scopeInfoObserver.original, expected, sizeof(expected));
        InterlockedExchange64(&observedCarcanoInfo, 0);
        InterlockedExchange64(&observedRollingInfo, 0);
        InterlockedExchange64(&observedRareRollingInfo, 0);
        std::memcpy(instruction, patch, sizeof(patch));
        FlushInstructionCache(GetCurrentProcess(), instruction, sizeof(patch));
        DWORD restored = 0;
        VirtualProtect(instruction, sizeof(expected), originalProtection, &restored);
        scopeInfoObserverDeadline = GetTickCount64() + 120000;
        Logger::Debug("DisableForcedScope RAX observer: installed for Carcano and both Rolling Block variants, temporaryCodeHook=1, metadataReadOnly=1.");
        return true;
    }

    void ApplyRollingScopeMetadata(std::uintptr_t observed, std::uint32_t expectedHash,
        const char* name, unsigned index);

    void UpdateScopeInfoObserver()
    {
        if (!scopeInfoObserver.address || scopeInfoObserverFinished)
            return;
        const auto observed = static_cast<std::uintptr_t>(
            InterlockedCompareExchange64(&observedCarcanoInfo, 0, 0));
        if (observed && (!scopeCarcanoPatched || patchedCarcanoInfo != observed) &&
            rejectedCarcanoInfo != observed)
        {
            std::uint8_t metadata[carcanoMetadataSize] = {};
            if (!ReadCarcanoMetadata(observed, metadata))
            {
                char warning[192] = {};
                std::snprintf(warning, sizeof(warning),
                    "[WARNING] DisableForcedScope: captured Carcano candidate 0x%llX failed identity or memory validation.",
                    static_cast<unsigned long long>(observed));
                Logger::Log(warning);
                rejectedCarcanoInfo = observed;
            }
            else
            {
                char message[280] = {};
                std::snprintf(message, sizeof(message),
                    "DisableForcedScope RAX observer: carcanoInfo=0x%llX, identity=0x53944780, originalFieldsMatched=%u/11, patchedFieldsMatched=%u/11.",
                    static_cast<unsigned long long>(observed),
                    MatchingCarcanoFields(metadata, false), MatchingCarcanoFields(metadata, true));
                Logger::Debug(message);
                if (PatchCarcanoMetadata(observed, metadata))
                {
                    patchedCarcanoInfo = observed;
                    scopeCarcanoPatched = true;
                    nextScopeRecordCheck = GetTickCount64() + 5000;
                }
                else
                    rejectedCarcanoInfo = observed;
            }
        }

        const auto rolling = static_cast<std::uintptr_t>(
            InterlockedCompareExchange64(&observedRollingInfo, 0, 0));
        const auto rare = static_cast<std::uintptr_t>(
            InterlockedCompareExchange64(&observedRareRollingInfo, 0, 0));
        ApplyRollingScopeMetadata(rolling, 0xE1D2B317U, "Rolling Block", 0);
        ApplyRollingScopeMetadata(rare, 0x4E328256U, "Rare Rolling Block", 1);

        const bool complete = scopeCarcanoPatched && rollingScopeMetadataLogged[0] && rollingScopeMetadataLogged[1];
        const bool timedOut = GetTickCount64() >= scopeInfoObserverDeadline;
        if (!complete && !timedOut)
            return;

        RestoreHook(scopeInfoObserver);
        constexpr std::uint8_t originalInstruction[] = { 0x8B, 0x88, 0xF4, 0x02, 0x00, 0x00 };
        if (std::memcmp(scopeInfoObserver.address, originalInstruction, sizeof(originalInstruction)) != 0)
        {
            if (!scopeInfoObserverRestoreWarningLogged)
            {
                Logger::Debug("DisableForcedScope RAX observer: instruction restore failed; retaining observer for next attempt.");
                scopeInfoObserverRestoreWarningLogged = true;
            }
            return;
        }
        scopeInfoObserverFinished = true;
        nextScopeObserverRetry = GetTickCount64() + 3000;
        char message[240] = {};
        std::snprintf(message, sizeof(message),
            "DisableForcedScope: temporary observer safely removed; Carcano=%d, RollingBlock=%d, RareRollingBlock=%d. Missing records will be retried when equipped.",
            scopeCarcanoPatched ? 1 : 0, rollingScopeMetadataLogged[0] ? 1 : 0,
            rollingScopeMetadataLogged[1] ? 1 : 0);
        Logger::Debug(message);
    }

    void RearmMissingScopeObserver()
    {
        if (!scopeInfoObserver.address || !scopeInfoObserverFinished)
            return;
        constexpr std::uint8_t originalInstruction[] = { 0x8B, 0x88, 0xF4, 0x02, 0x00, 0x00 };
        if (std::memcmp(scopeInfoObserver.address, originalInstruction, sizeof(originalInstruction)) != 0)
            return;
        scopeInfoObserver.address = nullptr;
        scopeInfoObserver.length = 0;
        scopeInfoObserverFinished = false;
        scopeInfoObserverRestoreWarningLogged = false;
        scopeWeaponInfoAccessProbed = false;
        rejectedCarcanoInfo = 0;
        for (auto& pointer : rejectedRollingInfo)
            pointer = 0;
        InterlockedExchange64(&observedCarcanoInfo, 0);
        InterlockedExchange64(&observedRollingInfo, 0);
        InterlockedExchange64(&observedRareRollingInfo, 0);
        Logger::Debug("DisableForcedScope: rearming observer for a weapon whose metadata has not been captured.");
    }

    bool InstallRollingScopeObserver()
    {
        if (!scopeCarcanoPatched || !scopeInfoObserverFinished || !scopeInfoObserver.address ||
            rollingScopeObserver.address ||
            rollingBlockWeapon != 0xE1D2B317U || exoticRollingBlockWeapon != 0x4E328256U)
            return false;

        auto* instruction = scopeInfoObserver.address;
        constexpr std::uint8_t expected[] = { 0x8B, 0x88, 0xF4, 0x02, 0x00, 0x00 };
        MEMORY_BASIC_INFORMATION region = {};
        if (VirtualQuery(instruction, &region, sizeof(region)) != sizeof(region) || !ExecutableRegion(region) ||
            reinterpret_cast<std::uintptr_t>(instruction) < reinterpret_cast<std::uintptr_t>(region.BaseAddress) ||
            region.RegionSize - (reinterpret_cast<std::uintptr_t>(instruction) -
                reinterpret_cast<std::uintptr_t>(region.BaseAddress)) < sizeof(expected) ||
            std::memcmp(instruction, expected, sizeof(expected)) != 0)
            return false;

        DWORD64 imageBase = 0;
        const auto* function = RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(instruction), &imageBase, nullptr);
        const auto image = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        if (!function || imageBase != image ||
            reinterpret_cast<std::uintptr_t>(instruction) - image < function->BeginAddress ||
            reinterpret_cast<std::uintptr_t>(instruction) - image + sizeof(expected) > function->EndAddress)
            return false;

        const Hook* existing[] = { &holster, &cover, &deadEye, &shouldReload, &clipNeedsReload };
        for (const Hook* hook : existing)
        {
            if (hook->address && instruction < hook->address + hook->length &&
                hook->address < instruction + sizeof(expected))
                return false;
        }

        auto* block = rollingScopeObserver.relay ? rollingScopeObserver.relay : AllocateNearby(instruction);
        if (!block)
            return false;
        const bool allocatedNew = !rollingScopeObserver.relay;
        std::uint8_t stub[] = {
            0x9C, 0x41, 0x53,
            0x81, 0x78, 0x20, 0x17, 0xB3, 0xD2, 0xE1,
            0x75, 0x0D,
            0x49, 0xBB, 0, 0, 0, 0, 0, 0, 0, 0,
            0x49, 0x89, 0x03,
            0x81, 0x78, 0x20, 0x56, 0x82, 0x32, 0x4E,
            0x75, 0x0D,
            0x49, 0xBB, 0, 0, 0, 0, 0, 0, 0, 0,
            0x49, 0x89, 0x03,
            0x41, 0x5B, 0x9D,
            0x8B, 0x88, 0xF4, 0x02, 0x00, 0x00,
            0xFF, 0x25, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0
        };
        static_assert(sizeof(stub) == 70, "Invalid rolling scope observer stub length");
#pragma warning(push)
#pragma warning(disable:28112)
        const auto rollingDestination = reinterpret_cast<std::uintptr_t>(&observedRollingInfo);
        const auto rareDestination = reinterpret_cast<std::uintptr_t>(&observedRareRollingInfo);
#pragma warning(pop)
        const auto resume = reinterpret_cast<std::uintptr_t>(instruction + sizeof(expected));
        std::memcpy(stub + 14, &rollingDestination, sizeof(rollingDestination));
        std::memcpy(stub + 36, &rareDestination, sizeof(rareDestination));
        std::memcpy(stub + 62, &resume, sizeof(resume));
        std::memcpy(block, stub, sizeof(stub));
        FlushInstructionCache(GetCurrentProcess(), block, sizeof(stub));

        const std::int64_t displacement = reinterpret_cast<std::intptr_t>(block) -
            (reinterpret_cast<std::intptr_t>(instruction) + 5);
        if (displacement < INT32_MIN || displacement > INT32_MAX)
        {
            if (allocatedNew)
                VirtualFree(block, 0, MEM_RELEASE);
            return false;
        }

        std::uint8_t patch[] = { 0xE9, 0, 0, 0, 0, 0x90 };
        const auto relative = static_cast<std::int32_t>(displacement);
        std::memcpy(patch + 1, &relative, sizeof(relative));
        DWORD originalProtection = 0;
        if (!VirtualProtect(instruction, sizeof(expected), PAGE_EXECUTE_READWRITE, &originalProtection))
        {
            if (allocatedNew)
                VirtualFree(block, 0, MEM_RELEASE);
            return false;
        }
        if (std::memcmp(instruction, expected, sizeof(expected)) != 0)
        {
            DWORD restored = 0;
            VirtualProtect(instruction, sizeof(expected), originalProtection, &restored);
            if (allocatedNew)
                VirtualFree(block, 0, MEM_RELEASE);
            return false;
        }

        rollingScopeObserver.address = instruction;
        rollingScopeObserver.relay = block;
        rollingScopeObserver.length = static_cast<std::uint8_t>(sizeof(expected));
        std::memcpy(rollingScopeObserver.original, expected, sizeof(expected));
        InterlockedExchange64(&observedRollingInfo, 0);
        InterlockedExchange64(&observedRareRollingInfo, 0);
        std::memcpy(instruction, patch, sizeof(patch));
        FlushInstructionCache(GetCurrentProcess(), instruction, sizeof(patch));
        DWORD restored = 0;
        VirtualProtect(instruction, sizeof(expected), originalProtection, &restored);
        rollingScopeObserverDeadline = GetTickCount64() + 120000;
        Logger::Debug("DisableForcedScope Rolling Block observer: installed for both variants, temporaryCodeHook=1, metadataReadOnly=1.");
        return true;
    }

    void ApplyRollingScopeMetadata(std::uintptr_t observed, std::uint32_t expectedHash,
        const char* name, unsigned index)
    {
        if (!observed || rejectedRollingInfo[index] == observed ||
            (rollingScopeMetadataLogged[index] && patchedRollingInfo[index] == observed))
            return;
        std::uint8_t metadata[carcanoMetadataSize] = {};
        if (!ReadRollingMetadata(observed, expectedHash, metadata))
        {
            if (!rollingScopeReadWarningLogged[index])
            {
                Logger::Debug("DisableForcedScope Rolling Block observer: candidate memory or weapon identity invalid.");
                rollingScopeReadWarningLogged[index] = true;
            }
            return;
        }
        char message[280] = {};
        std::snprintf(message, sizeof(message),
            "DisableForcedScope %s CWeaponInfo: info=0x%llX, hash=0x%08X, originalFieldsMatched=%u/11, patchedFieldsMatched=%u/11.",
            name, static_cast<unsigned long long>(observed), expectedHash,
            MatchingRollingFields(metadata, false), MatchingRollingFields(metadata, true));
        Logger::Debug(message);
        if (PatchRollingMetadata(observed, expectedHash, name, index, metadata))
        {
            patchedRollingInfo[index] = observed;
            rollingScopeMetadataLogged[index] = true;
            rejectedRollingInfo[index] = 0;
        }
        else
        {
            rollingScopeReadWarningLogged[index] = true;
            rejectedRollingInfo[index] = observed;
        }
    }

    void UpdateRollingScopeObserver()
    {
        if (!rollingScopeObserver.address || rollingScopeObserverFinished)
            return;
        const auto rolling = static_cast<std::uintptr_t>(
            InterlockedCompareExchange64(&observedRollingInfo, 0, 0));
        const auto rare = static_cast<std::uintptr_t>(
            InterlockedCompareExchange64(&observedRareRollingInfo, 0, 0));
        ApplyRollingScopeMetadata(rolling, 0xE1D2B317U, "Rolling Block", 0);
        ApplyRollingScopeMetadata(rare, 0x4E328256U, "Rare Rolling Block", 1);
        const bool timedOut = GetTickCount64() >= rollingScopeObserverDeadline;
        if (!timedOut && (!rollingScopeMetadataLogged[0] || !rollingScopeMetadataLogged[1]))
            return;

        RestoreHook(rollingScopeObserver);
        constexpr std::uint8_t expected[] = { 0x8B, 0x88, 0xF4, 0x02, 0x00, 0x00 };
        if (std::memcmp(rollingScopeObserver.address, expected, sizeof(expected)) != 0)
        {
            if (!rollingScopeObserverRestoreWarningLogged)
            {
                Logger::Debug("DisableForcedScope Rolling Block observer: instruction restore failed; retaining observer for next attempt.");
                rollingScopeObserverRestoreWarningLogged = true;
            }
            return;
        }
        rollingScopeObserverFinished = true;
        nextRollingObserverRetry = GetTickCount64() + 3000;
        char message[200] = {};
        std::snprintf(message, sizeof(message),
            "DisableForcedScope Rolling Block observer: completed=1, rollingPatched=%d, rarePatched=%d, timeout=%d, hookRestored=1.",
            rollingScopeMetadataLogged[0] ? 1 : 0, rollingScopeMetadataLogged[1] ? 1 : 0, timedOut ? 1 : 0);
        Logger::Debug(message);
    }

    void ProbeScopeWeaponInfoAccess()
    {
        const ULONGLONG started = GetTickCount64();
        PatternResult results;
        auto* candidate = FindUniqueCode(scopeWeaponInfoAccessPattern,
            sizeof(scopeWeaponInfoAccessPattern), results);
        const auto image = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        char message[288] = {};
        std::snprintf(message, sizeof(message),
            "DisableForcedScope CWeaponInfo access: signatureMatches=%u, scanMs=%llu, readOnly=1, lookupVerified=0.",
            results.count, static_cast<unsigned long long>(GetTickCount64() - started));
        Logger::Debug(message);
        if (!candidate || !image)
        {
            Logger::Log("[WARNING] DisableForcedScope: unique CWeaponInfo access signature unavailable; Carcano unchanged.");
            for (unsigned i = 0; image && i < results.count && i < 3; ++i)
            {
                std::snprintf(message, sizeof(message),
                    "DisableForcedScope CWeaponInfo access: ambiguousCandidateRva=0x%llX, rejected=1.",
                    static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(results.samples[i]) - image));
                Logger::Debug(message);
            }
            return;
        }

        const auto address = reinterpret_cast<std::uintptr_t>(candidate);
        const auto anchorRva = address - image;
        DWORD64 unwindBase = 0;
        const auto* function = RtlLookupFunctionEntry(static_cast<DWORD64>(address), &unwindBase, nullptr);
        if (!function || unwindBase != image || function->EndAddress <= function->BeginAddress ||
            anchorRva < function->BeginAddress ||
            anchorRva + sizeof(scopeWeaponInfoAccessPattern) > function->EndAddress)
        {
            std::snprintf(message, sizeof(message),
                "DisableForcedScope CWeaponInfo access: anchorRva=0x%llX, unwind=unverified, rejected=1.",
                static_cast<unsigned long long>(anchorRva));
            Logger::Debug(message);
            return;
        }

        std::snprintf(message, sizeof(message),
            "DisableForcedScope CWeaponInfo access: anchorRva=0x%llX, cameraReadRva=0x%llX, functionStartRva=0x%X, functionEndRva=0x%X, functionBytes=%u, unwind=valid, lookupVerified=0, readOnly=1.",
            static_cast<unsigned long long>(anchorRva),
            static_cast<unsigned long long>(anchorRva + 4),
            function->BeginAddress, function->EndAddress,
            function->EndAddress - function->BeginAddress);
        Logger::Debug(message);

        const auto functionStart = image + function->BeginAddress;
        const auto functionEnd = image + function->EndAddress;
        scopeWeaponInfoAnchor = candidate;
        scopeWeaponInfoFunctionStart = static_cast<DWORD64>(functionStart);
        scopeWeaponInfoFunctionEnd = static_cast<DWORD64>(functionEnd);
        if (!InstallScopeInfoObserver(candidate, functionStart, functionEnd))
            Logger::Log("[WARNING] DisableForcedScope: Carcano observer installation rejected; metadata unchanged.");
    }

    void UpdateScopeWeaponInfoReferences()
    {
        if (!scopeAccessReferenceScanActive || !Logger::IsDebugEnabled())
            return;
        const ULONGLONG started = GetTickCount64();
        const auto image = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        if (!image || !scopeAccessReferenceTarget)
        {
            scopeAccessReferenceScanActive = false;
            return;
        }
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 4096)
        {
            scopeAccessReferenceScanActive = false;
            return;
        }
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(image + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
            nt->FileHeader.NumberOfSections == 0 || nt->FileHeader.NumberOfSections > 96)
        {
            scopeAccessReferenceScanActive = false;
            return;
        }
        const std::size_t imageSize = nt->OptionalHeader.SizeOfImage;
        const auto* sections = IMAGE_FIRST_SECTION(nt);
        if (!imageSize || reinterpret_cast<std::uintptr_t>(sections) < image ||
            reinterpret_cast<std::uintptr_t>(sections) - image > imageSize ||
            nt->FileHeader.NumberOfSections >
                (imageSize - (reinterpret_cast<std::uintptr_t>(sections) - image)) / sizeof(IMAGE_SECTION_HEADER))
        {
            scopeAccessReferenceScanActive = false;
            return;
        }

        std::size_t budget = 2 * 1024 * 1024;
        while (budget >= 8 && scopeAccessReferenceSection < nt->FileHeader.NumberOfSections)
        {
            const auto& section = sections[scopeAccessReferenceSection];
            const auto sectionOffset = static_cast<std::size_t>(section.VirtualAddress);
            const auto sectionSize = static_cast<std::size_t>(section.Misc.VirtualSize);
            const bool executable = (section.Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
            const bool constData = (section.Characteristics & IMAGE_SCN_MEM_READ) != 0 &&
                (section.Characteristics & (IMAGE_SCN_MEM_WRITE | IMAGE_SCN_MEM_EXECUTE)) == 0;
            if ((!executable && !constData) || sectionOffset >= imageSize ||
                !sectionSize || sectionSize > imageSize - sectionOffset)
            {
                ++scopeAccessReferenceSection;
                scopeAccessReferenceCursor = 0;
                continue;
            }
            const auto sectionStart = image + sectionOffset;
            const auto sectionEnd = sectionStart + sectionSize;
            if (!scopeAccessReferenceCursor)
                scopeAccessReferenceCursor = sectionStart;
            if (scopeAccessReferenceCursor >= sectionEnd)
            {
                ++scopeAccessReferenceSection;
                scopeAccessReferenceCursor = 0;
                continue;
            }

            MEMORY_BASIC_INFORMATION region = {};
            if (VirtualQuery(reinterpret_cast<LPCVOID>(scopeAccessReferenceCursor),
                    &region, sizeof(region)) != sizeof(region))
            {
                scopeAccessReferenceScanActive = false;
                break;
            }
            const auto regionStart = reinterpret_cast<std::uintptr_t>(region.BaseAddress);
            if (region.RegionSize > (std::numeric_limits<std::uintptr_t>::max)() - regionStart ||
                regionStart > scopeAccessReferenceCursor ||
                regionStart + region.RegionSize <= scopeAccessReferenceCursor)
            {
                scopeAccessReferenceScanActive = false;
                break;
            }
            const auto regionEnd = regionStart + region.RegionSize;
            auto scanEnd = scopeAccessReferenceCursor + budget;
            if (scanEnd > regionEnd)
                scanEnd = regionEnd;
            if (scanEnd > sectionEnd)
                scanEnd = sectionEnd;
            const auto scanStart = scopeAccessReferenceCursor;
            const auto protection = region.Protect & 0xFF;
            const bool readable = region.State == MEM_COMMIT &&
                (region.Protect & (PAGE_GUARD | PAGE_NOACCESS)) == 0 &&
                (protection == PAGE_READONLY || protection == PAGE_READWRITE ||
                protection == PAGE_WRITECOPY || protection == PAGE_EXECUTE_READ ||
                protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY);
            if (readable)
            {
                auto windowStart = scanStart;
                if (windowStart - sectionStart >= 7 && windowStart - regionStart >= 7)
                    windowStart -= 7;
                const std::size_t required = executable ? 5 : sizeof(std::uintptr_t);
                for (auto position = windowStart; position + required <= scanEnd; ++position)
                {
                    if (executable && *reinterpret_cast<const std::uint8_t*>(position) == 0xE8 &&
                        !(position < scanStart && position + 5 <= scanStart))
                    {
                        std::int32_t displacement = 0;
                        std::memcpy(&displacement, reinterpret_cast<const void*>(position + 1), sizeof(displacement));
                        const auto target = static_cast<std::int64_t>(position) + 5 + displacement;
                        if (target == static_cast<std::int64_t>(scopeAccessReferenceTarget))
                        {
                            DWORD64 unwindBase = 0;
                            const auto* caller = RtlLookupFunctionEntry(static_cast<DWORD64>(position),
                                &unwindBase, nullptr);
                            if (caller && unwindBase == image && caller->EndAddress > caller->BeginAddress &&
                                position - image >= caller->BeginAddress && position - image < caller->EndAddress)
                            {
                                ++scopeAccessDirectCalls;
                                if (scopeAccessDirectCalls <= 6)
                                {
                                    char message[256] = {};
                                    std::snprintf(message, sizeof(message),
                                        "DisableForcedScope function caller: callRva=0x%llX, callerStartRva=0x%X, callerEndRva=0x%X, possibleDirectCall=1, instructionBoundary=unverified, lookupVerified=0.",
                                        static_cast<unsigned long long>(position - image),
                                        caller->BeginAddress, caller->EndAddress);
                                    Logger::Debug(message);
                                    const auto callerStart = image + caller->BeginAddress;
                                    const auto callerEnd = image + caller->EndAddress;
                                    const auto dumpStart = position - callerStart < 64 ? callerStart : position - 64;
                                    const auto dumpEnd = callerEnd - position < 32 ? callerEnd : position + 32;
                                    LogScopeWeaponInfoCode("caller context", dumpStart, dumpEnd, image);
                                }
                            }
                        }
                    }
                    if (constData && (position & (sizeof(std::uintptr_t) - 1)) == 0 &&
                        !(position < scanStart && position + sizeof(std::uintptr_t) <= scanStart))
                    {
                        std::uintptr_t pointer = 0;
                        std::memcpy(&pointer, reinterpret_cast<const void*>(position), sizeof(pointer));
                        if (pointer == scopeAccessReferenceTarget)
                        {
                            ++scopeAccessDataReferences;
                            if (scopeAccessDataReferences <= 6)
                            {
                                char message[192] = {};
                                std::snprintf(message, sizeof(message),
                                    "DisableForcedScope function data reference: pointerRva=0x%llX, targetRva=0x%llX, virtualDispatch=unverified.",
                                    static_cast<unsigned long long>(position - image),
                                    static_cast<unsigned long long>(pointer - image));
                                Logger::Debug(message);
                                const auto dumpStart = position - sectionStart < 16 ? sectionStart : position - 16;
                                const auto dumpEnd = sectionEnd - position < 32 ? sectionEnd : position + 32;
                                LogScopeWeaponInfoCode("data reference", dumpStart, dumpEnd, image);
                            }
                        }
                    }
                }
            }
            const auto consumed = scanEnd - scanStart;
            scopeAccessReferenceBytes += consumed;
            scopeAccessReferenceCursor = scanEnd;
            budget -= consumed;
        }
        scopeAccessReferenceScanMs += GetTickCount64() - started;
        if (!scopeAccessReferenceScanActive || scopeAccessReferenceSection >= nt->FileHeader.NumberOfSections)
        {
            scopeAccessReferenceScanActive = false;
            char message[260] = {};
            std::snprintf(message, sizeof(message),
                "DisableForcedScope function references: completed=%d, scannedBytes=%llu, scanMs=%llu, directCallMatches=%u, dataPointerMatches=%u, lookupVerified=0, readOnly=1.",
                scopeAccessReferenceSection >= nt->FileHeader.NumberOfSections ? 1 : 0,
                static_cast<unsigned long long>(scopeAccessReferenceBytes),
                static_cast<unsigned long long>(scopeAccessReferenceScanMs),
                scopeAccessDirectCalls, scopeAccessDataReferences);
            Logger::Debug(message);
        }
    }

    std::uint32_t ReadWeaponNative(std::uint64_t hash, std::uint32_t weapon)
    {
        nativeInit(hash);
        nativePush64(weapon);
        return static_cast<std::uint32_t>(*nativeCall());
    }

    void ProbeScopeVtable(std::uint32_t weapon, std::uintptr_t vtable)
    {
        const auto image = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        if (!image || !vtable)
            return;
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 4096)
            return;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(image + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
            nt->OptionalHeader.SizeOfImage < sizeof(std::uintptr_t) * 2 || vtable < image ||
            vtable - image > nt->OptionalHeader.SizeOfImage - sizeof(std::uintptr_t) * 2)
            return;
        MEMORY_BASIC_INFORMATION info = {};
        if (VirtualQuery(reinterpret_cast<LPCVOID>(vtable), &info, sizeof(info)) != sizeof(info) ||
            info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0 ||
            (info.Protect & 0xFF) != PAGE_READONLY)
            return;

        std::uint8_t bytes[sizeof(std::uintptr_t) * 2] = {};
        std::size_t length = 0;
        if (!ReadScopeProbeMemory(vtable, bytes, sizeof(bytes), length) || length != sizeof(bytes))
            return;
        for (unsigned i = 0; i < 2; ++i)
        {
            std::uintptr_t target = 0;
            std::memcpy(&target, bytes + i * sizeof(target), sizeof(target));
            if (target < image || target - image >= nt->OptionalHeader.SizeOfImage)
                continue;
            MEMORY_BASIC_INFORMATION code = {};
            if (VirtualQuery(reinterpret_cast<LPCVOID>(target), &code, sizeof(code)) != sizeof(code) ||
                !ExecutableRegion(code))
                continue;
            DWORD64 unwindBase = 0;
            const auto* function = RtlLookupFunctionEntry(static_cast<DWORD64>(target), &unwindBase, nullptr);
            if (!function || unwindBase != image || function->EndAddress <= function->BeginAddress ||
                target - image != function->BeginAddress)
                continue;
            std::uint8_t prologue[12] = {};
            std::size_t prologueLength = 0;
            if (!ReadScopeProbeMemory(target, prologue, sizeof(prologue), prologueLength) ||
                prologueLength != sizeof(prologue))
                continue;
            char instructionBytes[sizeof(prologue) * 3 + 1] = {};
            for (unsigned j = 0; j < sizeof(prologue); ++j)
                std::snprintf(instructionBytes + static_cast<std::size_t>(j) * 3, sizeof(instructionBytes) - static_cast<std::size_t>(j) * 3,
                    "%02X ", prologue[j]);
            char message[320] = {};
            std::snprintf(message, sizeof(message),
                "DisableForcedScope function candidate: weapon=0x%08X, weaponEntityVtableRva=0x%llX, slot=%u, entryRva=0x%llX, functionBytes=%u, entryBytes=%s, identified=0.",
                weapon, static_cast<unsigned long long>(vtable - image), i,
                static_cast<unsigned long long>(target - image),
                function->EndAddress - function->BeginAddress, instructionBytes);
            Logger::Debug(message);
        }
    }

    void StartScopeCodeScan(std::uint32_t repeater)
    {
        if (!repeater || scopeCodeScanActive || scopeCodeScanFinished)
            return;
        scopeCodeHashes[0] = repeater;
        scopeCodeHashes[1] = carcanoWeapon;
        scopeCodeHashes[2] = rollingBlockWeapon;
        scopeCodeHashes[3] = exoticRollingBlockWeapon;
        scopeCodeScanActive = true;
        Logger::Debug("DisableForcedScope code references: scoped repeater baseline captured; beginning bounded runtime executable analysis, readOnly=1.");
    }

    void FinishScopeCodeScan(bool completed)
    {
        scopeCodeScanActive = false;
        scopeCodeScanFinished = true;
        char message[320] = {};
        std::snprintf(message, sizeof(message),
            "DisableForcedScope code references: completed=%d, scannedBytes=%llu, repeater=0x%08X matches=%u, carcano=0x%08X matches=%u, rolling=0x%08X matches=%u, rareRolling=0x%08X matches=%u, unwindRejected=%u, elapsedScanMs=%llu, interpretedAsCodeReferences=0, readOnly=1.",
            completed ? 1 : 0, static_cast<unsigned long long>(scopeCodeBytesScanned),
            scopeCodeHashes[0], scopeCodeHitTotals[0], scopeCodeHashes[1], scopeCodeHitTotals[1],
            scopeCodeHashes[2], scopeCodeHitTotals[2], scopeCodeHashes[3], scopeCodeHitTotals[3],
            scopeCodeUnwindRejected, static_cast<unsigned long long>(scopeCodeScanTimeMs));
        Logger::Debug(message);
        unsigned logged = 0;
        for (unsigned i = 0; i < scopeCodeReferenceCount && logged < 8; ++i)
        {
            const auto& candidate = scopeCodeReferences[i];
            const unsigned sniperCount = ((candidate.weaponMask & 2U) != 0) +
                ((candidate.weaponMask & 4U) != 0) + ((candidate.weaponMask & 8U) != 0);
            if (sniperCount < 2 && !((candidate.weaponMask & 1U) && sniperCount))
                continue;
            std::snprintf(message, sizeof(message),
                "DisableForcedScope code candidate: functionRva=0x%X, size=%u, hashMask=0x%X, literalMatches=%u, unwind=valid, instructionContext=unverified, weaponInfoLookup=unverified.",
                candidate.functionStart, candidate.functionEnd - candidate.functionStart,
                candidate.weaponMask, candidate.matches);
            Logger::Debug(message);
            ++logged;
        }
        if (!logged)
            Logger::Debug("DisableForcedScope code references: no shared multi-weapon code candidate identified; runtime metadata lookup still unverified.");
    }

    void UpdateScopeCodeScan()
    {
        if (!scopeCodeScanActive || !forcedScopeProbeEnabled || !Logger::IsDebugEnabled())
            return;
        const ULONGLONG started = GetTickCount64();
        const auto image = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        if (!image)
        {
            FinishScopeCodeScan(false);
            return;
        }
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 4096)
        {
            FinishScopeCodeScan(false);
            return;
        }
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(image + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
            nt->FileHeader.NumberOfSections == 0 || nt->FileHeader.NumberOfSections > 96)
        {
            FinishScopeCodeScan(false);
            return;
        }
        const auto imageSize = static_cast<std::size_t>(nt->OptionalHeader.SizeOfImage);
        const auto* sections = IMAGE_FIRST_SECTION(nt);
        const auto sectionOffset = reinterpret_cast<std::uintptr_t>(sections) - image;
        if (sectionOffset >= imageSize || nt->FileHeader.NumberOfSections >
            (imageSize - sectionOffset) / sizeof(IMAGE_SECTION_HEADER))
        {
            FinishScopeCodeScan(false);
            return;
        }

        std::size_t budget = 512 * 1024;
        while (budget >= 4 && scopeCodeSectionIndex < nt->FileHeader.NumberOfSections)
        {
            const auto& section = sections[scopeCodeSectionIndex];
            const auto offset = static_cast<std::size_t>(section.VirtualAddress);
            const auto length = static_cast<std::size_t>(section.Misc.VirtualSize);
            if ((section.Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0 || offset >= imageSize ||
                length < 4 || length > imageSize - offset)
            {
                ++scopeCodeSectionIndex;
                scopeCodeCursor = 0;
                continue;
            }
            const auto sectionStart = image + offset;
            const auto sectionEnd = sectionStart + length;
            if (!scopeCodeCursor)
                scopeCodeCursor = sectionStart;
            if (scopeCodeCursor + 4 > sectionEnd)
            {
                ++scopeCodeSectionIndex;
                scopeCodeCursor = 0;
                continue;
            }
            MEMORY_BASIC_INFORMATION region = {};
            if (VirtualQuery(reinterpret_cast<LPCVOID>(scopeCodeCursor), &region, sizeof(region)) != sizeof(region))
            {
                FinishScopeCodeScan(false);
                return;
            }
            const auto regionStart = reinterpret_cast<std::uintptr_t>(region.BaseAddress);
            if (regionStart > scopeCodeCursor || region.RegionSize >
                (std::numeric_limits<std::uintptr_t>::max)() - regionStart)
            {
                FinishScopeCodeScan(false);
                return;
            }
            const auto regionEnd = regionStart + region.RegionSize;
            if (regionEnd <= scopeCodeCursor)
            {
                FinishScopeCodeScan(false);
                return;
            }
            auto scanEnd = scopeCodeCursor + budget;
            if (scanEnd > sectionEnd)
                scanEnd = sectionEnd;
            if (scanEnd > regionEnd)
                scanEnd = regionEnd;
            const DWORD protection = region.Protect & 0xFF;
            const bool readableCode = protection == PAGE_EXECUTE_READ ||
                protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
            if (!ExecutableRegion(region) || !readableCode || scanEnd - scopeCodeCursor < 4)
            {
                const auto consumed = scanEnd - scopeCodeCursor;
                scopeCodeCursor = scanEnd;
                budget -= consumed;
                if (scopeCodeCursor >= sectionEnd)
                {
                    ++scopeCodeSectionIndex;
                    scopeCodeCursor = 0;
                }
                continue;
            }
            for (auto position = scopeCodeCursor; position <= scanEnd - 4; ++position)
            {
                std::uint32_t value = 0;
                std::memcpy(&value, reinterpret_cast<const void*>(position), sizeof(value));
                unsigned hashIndex = 4;
                for (unsigned k = 0; k < 4; ++k)
                    if (value == scopeCodeHashes[k])
                    {
                        hashIndex = k;
                        break;
                    }
                if (hashIndex == 4)
                    continue;
                ++scopeCodeHitTotals[hashIndex];
                DWORD64 unwindBase = 0;
                const auto* function = RtlLookupFunctionEntry(static_cast<DWORD64>(position), &unwindBase, nullptr);
                if (!function || unwindBase != image || function->EndAddress <= function->BeginAddress ||
                    position - image < function->BeginAddress || position - image >= function->EndAddress)
                {
                    ++scopeCodeUnwindRejected;
                    continue;
                }
                unsigned existing = 0;
                for (; existing < scopeCodeReferenceCount; ++existing)
                    if (scopeCodeReferences[existing].functionStart == function->BeginAddress)
                        break;
                if (existing == scopeCodeReferenceCount)
                {
                    if (scopeCodeReferenceCount == sizeof(scopeCodeReferences) / sizeof(scopeCodeReferences[0]))
                        continue;
                    ++scopeCodeReferenceCount;
                    scopeCodeReferences[existing].functionStart = function->BeginAddress;
                    scopeCodeReferences[existing].functionEnd = function->EndAddress;
                }
                scopeCodeReferences[existing].weaponMask |= 1U << hashIndex;
                ++scopeCodeReferences[existing].matches;
            }
            const auto consumed = scanEnd - scopeCodeCursor;
            scopeCodeBytesScanned += consumed;
            budget -= consumed;
            scopeCodeCursor = scanEnd;
            if (scopeCodeCursor >= sectionEnd)
            {
                ++scopeCodeSectionIndex;
                scopeCodeCursor = 0;
            }
            else if (scanEnd < regionEnd && scanEnd - sectionStart >= 4)
                scopeCodeCursor -= 3;
        }
        scopeCodeScanTimeMs += GetTickCount64() - started;
        if (scopeCodeSectionIndex == nt->FileHeader.NumberOfSections ||
            scopeCodeBytesScanned >= 128ULL * 1024 * 1024)
            FinishScopeCodeScan(scopeCodeSectionIndex == nt->FileHeader.NumberOfSections);
    }

    void ProbeScopeWeaponObject(int ped, ScopeProbeSnapshot& snapshot)
    {
        nativeInit(getCurrentPedWeaponEntityIndex);
        nativePush64(static_cast<std::uint32_t>(ped));
        nativePush64(0);
        snapshot.entity = static_cast<int>(*nativeCall());
        if (!snapshot.entity)
            return;
        nativeInit(doesEntityExist);
        nativePush64(static_cast<std::uint32_t>(snapshot.entity));
        if (*nativeCall() == 0)
        {
            snapshot.entity = 0;
            return;
        }

        nativeInit(hasWeaponGotWeaponComponent);
        nativePush64(static_cast<std::uint32_t>(snapshot.entity));
        nativePush64(snapshot.weapon == carcanoWeapon || snapshot.weapon == rollingBlockWeapon ||
            snapshot.weapon == exoticRollingBlockWeapon ? longScopeComponent : shortScopeComponent);
        snapshot.objectAttached = *nativeCall() != 0;
        if (snapshot.weapon != carcanoWeapon && snapshot.weapon != rollingBlockWeapon &&
            snapshot.weapon != exoticRollingBlockWeapon && !snapshot.objectAttached)
            return;

        const auto address = reinterpret_cast<std::uintptr_t>(getScriptHandleBaseAddress(snapshot.entity));
        std::uint8_t root[0x100] = {};
        std::size_t rootLength = 0;
        if (!ReadScopeProbeMemory(address, root, sizeof(root), rootLength))
            return;
        snapshot.objectRead = true;
        std::memcpy(&snapshot.vtable, root, sizeof(snapshot.vtable));
        ProbeScopeVtable(snapshot.weapon, snapshot.vtable);

        unsigned directHits = 0;
        unsigned linksRead = 0;
        unsigned candidates = 0;
        for (std::size_t offset = 0; offset + sizeof(std::uint32_t) <= rootLength; offset += 4)
        {
            std::uint32_t value = 0;
            std::memcpy(&value, root + offset, sizeof(value));
            if (value != snapshot.weapon)
                continue;
            ++directHits;
            if (++candidates == 1 && rootLength >= sizeof(snapshot.infoBytes))
            {
                snapshot.infoIdentified = true;
                snapshot.infoRootOffset = 0;
                snapshot.infoHashOffset = static_cast<unsigned>(offset);
                std::memcpy(snapshot.infoBytes, root, sizeof(snapshot.infoBytes));
            }
        }

        std::uintptr_t visited[12] = {};
        unsigned visitedCount = 0;
        for (std::size_t offset = 0; offset + sizeof(std::uintptr_t) <= rootLength && linksRead < 8;
            offset += sizeof(std::uintptr_t))
        {
            std::uintptr_t pointer = 0;
            std::memcpy(&pointer, root + offset, sizeof(pointer));
            if (pointer < 0x10000 || pointer > 0x00007FFFFFFF0000ULL || (pointer & 7) != 0 || pointer == address)
                continue;
            bool duplicate = false;
            for (unsigned i = 0; i < visitedCount; ++i)
                if (visited[i] == pointer)
                    duplicate = true;
            if (duplicate)
                continue;
            if (visitedCount < 12)
                visited[visitedCount++] = pointer;
            MEMORY_BASIC_INFORMATION info = {};
            if (VirtualQuery(reinterpret_cast<LPCVOID>(pointer), &info, sizeof(info)) != sizeof(info) ||
                ExecutableRegion(info))
                continue;
            std::uint8_t child[0xC0] = {};
            std::size_t length = 0;
            if (!ReadScopeProbeMemory(pointer, child, sizeof(child), length))
                continue;
            ++linksRead;
            for (std::size_t childOffset = 0; childOffset + sizeof(std::uint32_t) <= length; childOffset += 4)
            {
                std::uint32_t value = 0;
                std::memcpy(&value, child + childOffset, sizeof(value));
                if (value != snapshot.weapon)
                    continue;
                ++candidates;
                if (candidates == 1 && length >= sizeof(snapshot.infoBytes))
                {
                    snapshot.infoIdentified = true;
                    snapshot.infoRootOffset = static_cast<unsigned>(offset);
                    snapshot.infoHashOffset = static_cast<unsigned>(childOffset);
                    std::memcpy(snapshot.infoBytes, child, sizeof(snapshot.infoBytes));
                }
            }
        }
        if (candidates != 1)
            snapshot.infoIdentified = false;
        char message[288] = {};
        std::snprintf(message, sizeof(message),
            "DisableForcedScope weapon object: weapon=0x%08X, entity=%d, root=0x%llX, directHashHits=%u, oneHopReads=%u, hashCandidates=%u, candidateRootOffset=0x%X, candidateHashOffset=0x%X, unique=%d, readOnly=1.",
            snapshot.weapon, snapshot.entity, static_cast<unsigned long long>(address), directHits, linksRead,
            candidates, snapshot.infoRootOffset, snapshot.infoHashOffset, snapshot.infoIdentified ? 1 : 0);
        Logger::Debug(message);
    }

    void CompareScopeSnapshots(const ScopeProbeSnapshot& repeater, const ScopeProbeSnapshot& sniper)
    {
        char message[320] = {};
        std::snprintf(message, sizeof(message),
            "DisableForcedScope comparison: repeater=0x%08X, sniper=0x%08X, sameStat=%d, sameGroup=%d, sameSlot=%d, sameModel=%d, sameAmmo=%d, sameVtable=%d, sameClip=%d, component02Repeater=%d, component04Sniper=%d.",
            repeater.weapon, sniper.weapon, repeater.stat == sniper.stat, repeater.group == sniper.group,
            repeater.slot == sniper.slot, repeater.model == sniper.model, repeater.ammo == sniper.ammo,
            repeater.vtable != 0 && repeater.vtable == sniper.vtable, repeater.clip == sniper.clip,
            repeater.objectAttached, sniper.objectAttached);
        Logger::Debug(message);
        if (!repeater.infoIdentified || !sniper.infoIdentified ||
            repeater.infoRootOffset != sniper.infoRootOffset ||
            repeater.infoHashOffset != sniper.infoHashOffset)
            return;
        std::string differences;
        unsigned count = 0;
        for (std::size_t offset = 0; offset + sizeof(std::uint32_t) <= sizeof(repeater.infoBytes); offset += 4)
        {
            std::uint32_t left = 0;
            std::uint32_t right = 0;
            std::memcpy(&left, repeater.infoBytes + offset, sizeof(left));
            std::memcpy(&right, sniper.infoBytes + offset, sizeof(right));
            if (left == right)
                continue;
            if (count++ >= 6)
                continue;
            char item[72] = {};
            std::snprintf(item, sizeof(item), " +0x%X:%08X/%08X", static_cast<unsigned>(offset), left, right);
            differences += item;
        }
        std::snprintf(message, sizeof(message),
            "DisableForcedScope candidate comparison: repeater=0x%08X, sniper=0x%08X, rootOffset=0x%X, hashOffset=0x%X, changedDwords=%u, firstDifferences=%s, fieldsUnverified=1.",
            repeater.weapon, sniper.weapon, repeater.infoRootOffset, repeater.infoHashOffset,
            count, differences.c_str());
        Logger::Debug(message);
    }

    bool ProbeScopeMetadata(int ped, std::uint32_t weapon, bool repeater)
    {
        const ULONGLONG started = GetTickCount64();
        ScopeProbeSnapshot snapshot;
        snapshot.weapon = weapon;
        snapshot.stat = ReadWeaponNative(getWeaponStatId, weapon);
        snapshot.group = ReadWeaponNative(getWeaponTypeGroup, weapon);
        snapshot.slot = ReadWeaponNative(getWeaponTypeSlot, weapon);
        snapshot.model = ReadWeaponNative(getWeaponTypeModel, weapon);
        snapshot.ammo = ReadWeaponNative(getAmmoTypeForWeapon, weapon);
        snapshot.clip = static_cast<int>(ReadWeaponNative(getWeaponClipSize, weapon));

        ProbeScopeWeaponObject(ped, snapshot);
        if (repeater && snapshot.objectAttached)
            StartScopeCodeScan(weapon);
        if (repeater && !snapshot.objectAttached)
        {
            if (scopeProbeMissingRepeater != weapon)
            {
                scopeProbeMissingRepeater = weapon;
                char message[208] = {};
                std::snprintf(message, sizeof(message),
                    "DisableForcedScope repeater baseline skipped: weapon=0x%08X, short scope component not confirmed on equipped weapon object; equip a scoped repeater.",
                    weapon);
                Logger::Debug(message);
            }
            scopeProbeRetryAfter = GetTickCount64() + 4000;
            return false;
        }

        char message[320] = {};
        std::snprintf(message, sizeof(message),
            "DisableForcedScope native metadata: weapon=0x%08X, type=%s, stat=0x%08X, group=0x%08X, slot=0x%08X, model=0x%08X, ammo=0x%08X, clip=%d, scopeComponentPresent=%d, objectReadable=%d, durationMs=%llu, readOnly=1.",
            weapon, repeater ? "repeater" : "sniper", snapshot.stat, snapshot.group, snapshot.slot,
            snapshot.model, snapshot.ammo, snapshot.clip,
            snapshot.objectAttached, snapshot.objectRead,
            static_cast<unsigned long long>(GetTickCount64() - started));
        Logger::Debug(message);
        if (scopeSnapshotCount < 4)
        {
            scopeSnapshots[scopeSnapshotCount++] = snapshot;
            const auto& added = scopeSnapshots[scopeSnapshotCount - 1];
            for (unsigned i = 0; i + 1 < scopeSnapshotCount; ++i)
            {
                const auto& other = scopeSnapshots[i];
                const bool otherSniper = other.weapon == carcanoWeapon || other.weapon == rollingBlockWeapon ||
                    other.weapon == exoticRollingBlockWeapon;
                if (repeater && otherSniper)
                    CompareScopeSnapshots(added, other);
                else if (!repeater && !otherSniper)
                    CompareScopeSnapshots(other, added);
            }
        }
        return true;
    }

    void EnableForcedScope()
    {
        forcedScopeProbeEnabled = true;
        nextScopeProbeRefresh = 0;
        nativeInit(getHashKey);
        nativePush64(reinterpret_cast<std::uint64_t>("fin1"));
        scopeExcludedMissionHash = static_cast<std::uint32_t>(*nativeCall());
        nativeInit(getHashKey);
        nativePush64(reinterpret_cast<std::uint64_t>("WEAPON_SNIPERRIFLE_CARCANO"));
        carcanoWeapon = static_cast<std::uint32_t>(*nativeCall());
        nativeInit(getHashKey);
        nativePush64(reinterpret_cast<std::uint64_t>("WEAPON_SNIPERRIFLE_ROLLINGBLOCK"));
        rollingBlockWeapon = static_cast<std::uint32_t>(*nativeCall());
        nativeInit(getHashKey);
        nativePush64(reinterpret_cast<std::uint64_t>("WEAPON_SNIPERRIFLE_ROLLINGBLOCK_EXOTIC"));
        exoticRollingBlockWeapon = static_cast<std::uint32_t>(*nativeCall());
        nativeInit(getHashKey);
        nativePush64(reinterpret_cast<std::uint64_t>("COMPONENT_RIFLE_SCOPE02"));
        shortScopeComponent = static_cast<std::uint32_t>(*nativeCall());
        nativeInit(getHashKey);
        nativePush64(reinterpret_cast<std::uint64_t>("COMPONENT_RIFLE_SCOPE04"));
        longScopeComponent = static_cast<std::uint32_t>(*nativeCall());
        if (!carcanoWeapon || !rollingBlockWeapon || !exoticRollingBlockWeapon ||
            !shortScopeComponent || !longScopeComponent || !scopeExcludedMissionHash)
        {
            forcedScopeProbeEnabled = false;
            Logger::Debug("DisableForcedScope: weapon/component hash initialization failed; diagnostic skipped.");
        }
        else
            Logger::Debug("DisableForcedScope: runtime metadata patch enabled for Carcano and both Rolling Block variants.");
    }

    void UpdateForcedScopeToggle()
    {
        if (!forcedScopeRestorePending)
            return;

        const auto now = GetTickCount64();
        if (now < nextForcedScopeRestore)
            return;
        nextForcedScopeRestore = now + 750;

        if (RearmCarcanoObserver())
        {
            forcedScopeRestorePending = false;
            forcedScopeRestoreWarned = false;
            scopeMissionSuspended = false;
            scopeMissionRecordsRestored = false;
            scopeMissionRestoreWarningLogged = false;
            scopeMissionInactiveSince = 0;
            nextScopeMissionCheck = 0;
            nextScopeMissionRestore = 0;
            Logger::Log("[SUCCESS] DisableForcedScope: restored owned rifle metadata and stopped the observers.");
            if (forcedScopeRequested)
                EnableForcedScope();
            return;
        }

        if (!forcedScopeRestoreWarned)
        {
            forcedScopeRestoreWarned = true;
            Logger::Log("[WARNING] DisableForcedScope: restoration is incomplete; retrying without applying new patches.");
        }
    }

    void UpdateForcedScopeProbe()
    {
        UpdateForcedScopeToggle();
        if (!forcedScopeProbeEnabled)
            return;
        const ULONGLONG now = GetTickCount64();
        if (now < nextScopeProbeRefresh)
            return;
        nextScopeProbeRefresh = now + 250;
        if (UpdateScopeMission(now))
            return;
        if (!UpdateScopeLoadState(now))
            return;
        CheckCarcanoRecord();
        if (!scopeWeaponInfoAccessProbed)
        {
            scopeWeaponInfoAccessProbed = true;
            if (!scopeWeaponInfoAnchor ||
                std::memcmp(scopeWeaponInfoAnchor, scopeWeaponInfoAccessPattern,
                    sizeof(scopeWeaponInfoAccessPattern)) != 0 ||
                !InstallScopeInfoObserver(scopeWeaponInfoAnchor,
                    scopeWeaponInfoFunctionStart, scopeWeaponInfoFunctionEnd))
            {
                scopeWeaponInfoAnchor = nullptr;
                ProbeScopeWeaponInfoAccess();
            }
            else
                Logger::Debug("DisableForcedScope: verified camera-read signature reused after gameplay transition.");
        }
        UpdateScopeInfoObserver();
        if (scopeInfoObserverFinished && now >= nextScopeObserverRetry &&
            (!scopeCarcanoPatched || !rollingScopeMetadataLogged[0] || !rollingScopeMetadataLogged[1]))
        {
            nextScopeObserverRetry = now + 3000;
            nativeInit(playerPedId);
            const auto ped = static_cast<int>(*nativeCall());
            if (ped)
            {
                std::uint32_t weapon = 0;
                nativeInit(getCurrentPedWeapon);
                nativePush64(static_cast<std::uint32_t>(ped));
                nativePush64(reinterpret_cast<std::uint64_t>(&weapon));
                nativePush64(1);
                nativePush64(0);
                nativePush64(0);
                if (*nativeCall() != 0 &&
                    ((!scopeCarcanoPatched && weapon == carcanoWeapon) ||
                    (!rollingScopeMetadataLogged[0] && weapon == rollingBlockWeapon) ||
                    (!rollingScopeMetadataLogged[1] && weapon == exoticRollingBlockWeapon)))
                    RearmMissingScopeObserver();
            }
        }

    }

    bool ReadReloadControl(std::uint64_t native)
    {
        nativeInit(native);
        nativePush64(0);
        nativePush64(reloadControl);
        const auto* result = nativeCall();
        return result && static_cast<std::uint32_t>(*result) != 0;
    }

    bool IsReloadInputActive()
    {
        static thread_local bool readingInput = false;
        if (readingInput)
            return false;

        struct QueryScope
        {
            explicit QueryScope(bool& active) : active(active) { active = true; }
            ~QueryScope() { active = false; }
            bool& active;
        };
        const QueryScope query(readingInput);

        return ReadReloadControl(isControlJustPressed) || ReadReloadControl(isDisabledControlJustPressed) ||
            ReadReloadControl(isControlPressed) || ReadReloadControl(isDisabledControlPressed);
    }

    void UpdateManualReloadInput()
    {
        if (!(autoReloadDisabled && cover.gateway) && !(emptyAutoReloadDisabled && emptyAutoReloadHooksReady))
            return;

        const auto now = GetTickCount64();
        if (IsReloadInputActive())
            manualReloadAllowedUntil.store(now + 500, std::memory_order_relaxed);
        else if (now < manualReloadAllowedUntil.load(std::memory_order_relaxed))
        {
            nativeInit(playerPedId);
            const auto ped = static_cast<std::uint32_t>(*nativeCall());
            if (ped)
            {
                nativeInit(isPedReloading);
                nativePush64(ped);
                if (static_cast<std::uint32_t>(*nativeCall()) != 0)
                    manualReloadAllowedUntil.store(now + 500, std::memory_order_relaxed);
            }
        }

        if (emptyReloadSuppressionObserved.load(std::memory_order_relaxed) && !emptyReloadSuppressionLogged)
        {
            Logger::Debug("DisableEmptyAutoReload: suppressed an engine-triggered automatic reload decision.");
            emptyReloadSuppressionLogged = true;
        }
    }

    void __fastcall ShouldReloadHook(std::uint32_t state, void* ped, void* source, std::uint8_t enabled)
    {
        auto original = reinterpret_cast<void(__fastcall*)(std::uint32_t, void*, void*, std::uint8_t)>(shouldReload.gateway);
        if (!original)
            return;

        if (emptyAutoReloadDisabled && emptyAutoReloadHooksReady && state != 5 && ped &&
            IsPlayer(reinterpret_cast<std::uintptr_t>(ped)) &&
            GetTickCount64() >= manualReloadAllowedUntil.load(std::memory_order_relaxed))
        {
            ++automaticReloadDepth;
            original(state, ped, source, enabled);
            --automaticReloadDepth;
            return;
        }
        original(state, ped, source, enabled);
    }

    bool __fastcall ClipNeedsReloadHook(void* context, std::uint8_t requested)
    {
        auto original = reinterpret_cast<bool(__fastcall*)(void*, std::uint8_t)>(clipNeedsReload.gateway);
        if (!original || !original(context, requested))
            return false;
        if (emptyAutoReloadDisabled && emptyAutoReloadHooksReady && automaticReloadDepth != 0 &&
            context && requested)
        {
            const auto* weapon = *reinterpret_cast<const std::uint8_t* const*>(
                static_cast<const std::uint8_t*>(context) + 0x60);
            if (weapon)
            {
                const auto flags = *reinterpret_cast<const std::uint32_t*>(weapon + 0x924);
                const auto state = *reinterpret_cast<const std::uint32_t*>(weapon + 0x930);
                if ((flags & 0x10U) != 0 && (state & 0x800U) == 0)
                {
                    emptyReloadSuppressionObserved.store(true, std::memory_order_relaxed);
                    return false;
                }
            }
        }
        return true;
    }

    void __fastcall HolsterHook(void* context)
    {
        if (autoReloadDisabled && context &&
            IsPlayer(*reinterpret_cast<const std::uintptr_t*>(static_cast<const std::uint8_t*>(context) + 0x20)))
            return;
        auto original = reinterpret_cast<void(__fastcall*)(void*)>(holster.gateway);
        if (original)
            original(context);
    }

    bool __fastcall CoverHook(void* context, std::uint8_t state)
    {
        auto original = reinterpret_cast<bool(__fastcall*)(void*, std::uint8_t)>(cover.gateway);
        if (!original)
            return false;
        const auto* task = static_cast<const std::uint8_t*>(context);
        if (!task || !IsPlayer(*reinterpret_cast<const std::uintptr_t*>(task + 0x10)))
            return original(context, state);
        if (autoReloadDisabled)
        {
            const auto now = GetTickCount64();
            if (now >= manualReloadAllowedUntil.load(std::memory_order_relaxed))
            {
                if (!reloadInputScriptThread || !IsReloadInputActive())
                    return false;
                manualReloadAllowedUntil.store(now + 500, std::memory_order_relaxed);
            }
        }
        return original(context, state);
    }

    void __fastcall DeadEyeHook(void* context)
    {
        if (deadEyeAutoReloadDisabled && IsPlayer(reinterpret_cast<std::uintptr_t>(context)))
            return;
        auto original = reinterpret_cast<void(__fastcall*)(void*)>(deadEye.gateway);
        if (original)
            original(context);
    }
}

namespace Main
{
    namespace Gunz
    {
        void Initialize(bool enabled, bool disableAutoReload, bool disableDeadEyeAutoReload, bool disableEmptyAutoReload,
            bool disableArrowTrails, bool disableReticleSpread, bool disableReticleSway, bool disableForcedScope,
            bool disableWeaponDegradation, bool disableHitmarkers, bool disableReticleHighlighting)
        {
            weaponDegradationDisabled = disableWeaponDegradation;
            nextWeaponConditionRefresh = 0;
            if (weaponDegradationDisabled)
                Logger::Log("[INFO] DisableWeaponDegradation: weapon wear disabled.");
            RestoreReticleAccuracy(reticleAccuracyPed);
            reticleSpreadDisabled = disableReticleSpread;
            reticleSwayDisabled = disableReticleSway;
            if (reticleSwayDisabled)
                Logger::Log("[INFO] DisableReticleSway: enabled.");
            forcedScopeProbeEnabled = false;
            scopeExcludedMissionHash = 0;
            nextScopeMissionCheck = 0;
            nextScopeMissionRestore = 0;
            scopeMissionInactiveSince = 0;
            scopeMissionSuspended = false;
            scopeMissionRecordsRestored = false;
            scopeMissionRestoreWarningLogged = false;
            RestoreHook(scopeInfoObserver);
            scopeInfoObserver = Hook{};
            RestoreHook(rollingScopeObserver);
            rollingScopeObserver = Hook{};
            rollingScopeObserverDeadline = 0;
            rollingScopeObserverAttempted = false;
            rollingScopeObserverFinished = false;
            rollingScopeObserverRestoreWarningLogged = false;
            for (auto& logged : rollingScopeMetadataLogged)
                logged = false;
            for (auto& logged : rollingScopeReadWarningLogged)
                logged = false;
            for (auto& pointer : patchedRollingInfo)
                pointer = 0;
            for (auto& mask : rollingScopeWrittenMask)
                mask = 0;
            for (auto& pointer : rejectedRollingInfo)
                pointer = 0;
            nextRollingObserverRetry = 0;
            InterlockedExchange64(&observedRollingInfo, 0);
            InterlockedExchange64(&observedRareRollingInfo, 0);
            scopeInfoObserverDeadline = 0;
            scopeInfoObserverFinished = false;
            scopeInfoObserverRestoreWarningLogged = false;
            patchedCarcanoInfo = 0;
            carcanoScopeWrittenMask = 0;
            rejectedCarcanoInfo = 0;
            nextScopeObserverRetry = 0;
            trackedScopePlayer = 0;
            nextScopeRecordCheck = 0;
            scopeCarcanoPatched = false;
            InterlockedExchange64(&observedCarcanoInfo, 0);
            scopeWeaponInfoAccessProbed = false;
            scopeWeaponInfoAnchor = nullptr;
            scopeWeaponInfoFunctionStart = 0;
            scopeWeaponInfoFunctionEnd = 0;
            scopeLastLifecycleCheck = 0;
            scopeLoadReadyAt = 0;
            scopeLastGameTimer = 0;
            scopeLastPlayerPed = 0;
            scopeLifecycleReady = false;
            scopeLoadPending = false;
            scopeAccessReferenceScanActive = false;
            scopeAccessReferenceTarget = 0;
            scopeAccessReferenceSection = 0;
            scopeAccessReferenceCursor = 0;
            scopeAccessReferenceBytes = 0;
            scopeAccessReferenceScanMs = 0;
            scopeAccessDirectCalls = 0;
            scopeAccessDataReferences = 0;
            lastScopeProbeWeapon = 0;
            lastScopeProbeState = -1;
            nextScopeProbeRefresh = 0;
            scopeProbeEvents = 0;
            scopeMetadataProbed = 0;
            scopeProbeMissingRepeater = 0;
            scopeProbeRetryAfter = 0;
            scopeSnapshotCount = 0;
            for (auto& snapshot : scopeSnapshots)
                snapshot = ScopeProbeSnapshot{};
            scopeMetadataPendingWeapon = 0;
            scopeMetadataPendingSince = 0;
            scopeCodeReferenceCount = 0;
            for (auto& reference : scopeCodeReferences)
                reference = ScopeCodeReference{};
            for (auto& hash : scopeCodeHashes)
                hash = 0;
            for (auto& count : scopeCodeHitTotals)
                count = 0;
            scopeCodeUnwindRejected = 0;
            scopeCodeSectionIndex = 0;
            scopeCodeCursor = 0;
            scopeCodeBytesScanned = 0;
            scopeCodeScanTimeMs = 0;
            scopeCodeScanActive = false;
            scopeCodeScanFinished = false;
            carcanoWeapon = 0;
            rollingBlockWeapon = 0;
            exoticRollingBlockWeapon = 0;
            shortScopeComponent = 0;
            longScopeComponent = 0;
            ApplyForcedScope(disableForcedScope);
            shotgunShellAmmo = 0;
            incendiaryShellAmmo = 0;
            reticleAccuracyPed = 0;
            previousReticleAccuracy = 0;
            reticleAccuracyOverridden = false;
            if (reticleSpreadDisabled)
            {
                nativeInit(getHashKey);
                nativePush64(reinterpret_cast<std::uint64_t>("AMMO_SHOTGUN"));
                shotgunShellAmmo = static_cast<std::uint32_t>(*nativeCall());
                nativeInit(getHashKey);
                nativePush64(reinterpret_cast<std::uint64_t>("AMMO_SHOTGUN_BUCKSHOT_INCENDIARY"));
                incendiaryShellAmmo = static_cast<std::uint32_t>(*nativeCall());
                if (shotgunShellAmmo == 0 || incendiaryShellAmmo == 0)
                {
                    reticleSpreadDisabled = false;
                    Logger::Log("[WARNING] DisableReticleSpread: ammo type initialization failed; feature disabled.");
                }
                else
                    Logger::Log("[INFO] DisableReticleSpread: reticle spread override enabled.");
            }
            if (enabled)
                Logger::Debug("Gunz: resolving runtime reload hooks.");
            if (enabled)
            {
                const bool clipInstalled = InstallHook(clipNeedsReload, clipNeedsReloadPattern,
                    sizeof(clipNeedsReloadPattern), 7,
                    reinterpret_cast<const void*>(&ClipNeedsReloadHook), "DisableEmptyAutoReload (clip check)");
                const bool decisionInstalled = InstallHook(shouldReload, shouldReloadPattern,
                    sizeof(shouldReloadPattern), 7,
                    reinterpret_cast<const void*>(&ShouldReloadHook), "DisableEmptyAutoReload (reload decision)");
                emptyAutoReloadHooksReady = clipInstalled && decisionInstalled;
                if (emptyAutoReloadHooksReady)
                    Logger::Debug("DisableEmptyAutoReload: both engine hooks ready.");
                else
                    Logger::Log("[WARNING] DisableEmptyAutoReload: incomplete engine hook set; reload behavior left unchanged.");
                InstallHook(holster, holsterPattern, sizeof(holsterPattern), 5,
                    reinterpret_cast<const void*>(&HolsterHook), "DisableAutoReload (holstered weapons)",
                    holsterLegacyPattern, sizeof(holsterLegacyPattern), 7);
                InstallHook(cover, coverPattern, sizeof(coverPattern), 5,
                    reinterpret_cast<const void*>(&CoverHook), "DisableAutoReload (cover)");
                InstallHook(deadEye, deadEyePattern, sizeof(deadEyePattern), 7,
                    reinterpret_cast<const void*>(&DeadEyeHook), "DisableDeadEyeAutoReload");
            }
            ApplyRuntimeToggles(disableAutoReload, disableDeadEyeAutoReload, disableEmptyAutoReload,
                disableArrowTrails, disableHitmarkers, disableReticleHighlighting);
        }

        void ApplyForcedScope(bool enabled)
        {
            if (forcedScopeRequested == enabled)
                return;
            forcedScopeRequested = enabled;
            if (enabled)
            {
                if (!forcedScopeRestorePending)
                    EnableForcedScope();
                return;
            }

            forcedScopeProbeEnabled = false;
            forcedScopeRestorePending = true;
            nextForcedScopeRestore = 0;
            UpdateForcedScopeToggle();
        }

        void ApplyRuntimeToggles(bool disableAutoReload, bool disableDeadEyeAutoReload, bool disableEmptyAutoReload,
            bool disableArrowTrails, bool disableHitmarkers, bool disableReticleHighlighting)
        {
            if (emptyAutoReloadDisabled != disableEmptyAutoReload)
            {
                emptyReloadSuppressionObserved.store(false, std::memory_order_relaxed);
                emptyReloadSuppressionLogged = false;
            }
            if (autoReloadDisabled != disableAutoReload || emptyAutoReloadDisabled != disableEmptyAutoReload)
                manualReloadAllowedUntil.store(0, std::memory_order_relaxed);
            autoReloadDisabled = disableAutoReload;
            deadEyeAutoReloadDisabled = disableDeadEyeAutoReload;
            emptyAutoReloadDisabled = disableEmptyAutoReload;
            nextPlayerRefresh = 0;
            if (arrowTrailsDisabled != disableArrowTrails)
            {
                arrowTrailsDisabled = disableArrowTrails;
                UpdateArrowTrails();
            }
            SetReticleActions(disableHitmarkers, disableReticleHighlighting);
        }

        void ReloadRuntimeFeatures(bool disableReticleSpread, bool disableReticleSway, bool disableWeaponDegradation)
        {
            if (reticleSpreadDisabled && !disableReticleSpread)
                RestoreReticleAccuracy(reticleAccuracyPed);
            reticleSpreadDisabled = disableReticleSpread;
            reticleSwayDisabled = disableReticleSway;
            weaponDegradationDisabled = disableWeaponDegradation;
            nextWeaponConditionRefresh = 0;
            if (reticleSpreadDisabled)
            {
                nativeInit(getHashKey);
                nativePush64(reinterpret_cast<std::uint64_t>("AMMO_SHOTGUN"));
                shotgunShellAmmo = static_cast<std::uint32_t>(*nativeCall());
                nativeInit(getHashKey);
                nativePush64(reinterpret_cast<std::uint64_t>("AMMO_SHOTGUN_BUCKSHOT_INCENDIARY"));
                incendiaryShellAmmo = static_cast<std::uint32_t>(*nativeCall());
                if (!shotgunShellAmmo || !incendiaryShellAmmo)
                {
                    reticleSpreadDisabled = false;
                    Logger::Log("[WARNING] DisableReticleSpread: ammo type initialization failed; feature disabled.");
                }
            }
        }

        void Update()
        {
            UpdateReticleActions();
            UpdateWeaponCondition();
            UpdateArrowTrails();
            UpdateReticleSpread();
            UpdateForcedScopeProbe();
            reloadInputScriptThread = true;
            UpdateManualReloadInput();
            if (!autoReloadDisabled && !deadEyeAutoReloadDisabled && !emptyAutoReloadDisabled)
                return;
            const ULONGLONG now = GetTickCount64();
            if (now < nextPlayerRefresh)
                return;
            nextPlayerRefresh = now + 750;

            nativeInit(playerPedId);
            const auto ped = static_cast<int>(*nativeCall());
            std::uintptr_t address = 0;
            if (ped != 0)
            {
                nativeInit(doesEntityExist);
                nativePush64(static_cast<std::uint32_t>(ped));
                if (*nativeCall() != 0)
                    address = reinterpret_cast<std::uintptr_t>(getScriptHandleBaseAddress(ped));
            }
            playerAddress.store(address, std::memory_order_relaxed);
        }

        void Shutdown()
        {
            reticleWorkerStop.store(true, std::memory_order_release);
            if (reticleWorkerHandle)
            {
                CloseHandle(reticleWorkerHandle);
                reticleWorkerHandle = nullptr;
            }
            SetReticleActions(false, false);
            reticlePatchComplete = true;
            weaponDegradationDisabled = false;
            nextWeaponConditionRefresh = 0;
            autoReloadDisabled = false;
            deadEyeAutoReloadDisabled = false;
            emptyAutoReloadDisabled = false;
            if (arrowTrailsDisabled && arrowTrailFxApplied && IsArrowTrailWorldReady())
                ApplyArrowTrailFx(defaultArrowTrailFx);
            arrowTrailFxApplied = false;
            arrowTrailsDisabled = false;
            reticleSpreadDisabled = false;
            reticleSwayDisabled = false;
            forcedScopeProbeEnabled = false;
            scopeExcludedMissionHash = 0;
            nextScopeMissionCheck = 0;
            nextScopeMissionRestore = 0;
            scopeMissionInactiveSince = 0;
            scopeMissionSuspended = false;
            scopeMissionRecordsRestored = false;
            scopeMissionRestoreWarningLogged = false;
            RestoreHook(rollingScopeObserver);
            rollingScopeObserverDeadline = 0;
            rollingScopeObserverAttempted = false;
            rollingScopeObserverFinished = false;
            rollingScopeObserverRestoreWarningLogged = false;
            for (auto& logged : rollingScopeMetadataLogged)
                logged = false;
            for (auto& logged : rollingScopeReadWarningLogged)
                logged = false;
            for (auto& pointer : patchedRollingInfo)
                pointer = 0;
            for (auto& mask : rollingScopeWrittenMask)
                mask = 0;
            for (auto& pointer : rejectedRollingInfo)
                pointer = 0;
            nextRollingObserverRetry = 0;
            InterlockedExchange64(&observedRollingInfo, 0);
            InterlockedExchange64(&observedRareRollingInfo, 0);
            RestoreHook(scopeInfoObserver);
            scopeInfoObserverDeadline = 0;
            scopeInfoObserverFinished = false;
            scopeInfoObserverRestoreWarningLogged = false;
            patchedCarcanoInfo = 0;
            carcanoScopeWrittenMask = 0;
            rejectedCarcanoInfo = 0;
            nextScopeObserverRetry = 0;
            trackedScopePlayer = 0;
            nextScopeRecordCheck = 0;
            scopeCarcanoPatched = false;
            InterlockedExchange64(&observedCarcanoInfo, 0);
            scopeWeaponInfoAccessProbed = false;
            scopeWeaponInfoAnchor = nullptr;
            scopeWeaponInfoFunctionStart = 0;
            scopeWeaponInfoFunctionEnd = 0;
            scopeLastLifecycleCheck = 0;
            scopeLoadReadyAt = 0;
            scopeLastGameTimer = 0;
            scopeLastPlayerPed = 0;
            scopeLifecycleReady = false;
            scopeLoadPending = false;
            scopeAccessReferenceScanActive = false;
            scopeAccessReferenceTarget = 0;
            scopeAccessReferenceSection = 0;
            scopeAccessReferenceCursor = 0;
            scopeAccessReferenceBytes = 0;
            scopeAccessReferenceScanMs = 0;
            scopeAccessDirectCalls = 0;
            scopeAccessDataReferences = 0;
            carcanoWeapon = 0;
            rollingBlockWeapon = 0;
            exoticRollingBlockWeapon = 0;
            shortScopeComponent = 0;
            longScopeComponent = 0;
            lastScopeProbeWeapon = 0;
            lastScopeProbeState = -1;
            nextScopeProbeRefresh = 0;
            scopeProbeEvents = 0;
            scopeMetadataProbed = 0;
            scopeProbeMissingRepeater = 0;
            scopeProbeRetryAfter = 0;
            scopeSnapshotCount = 0;
            for (auto& snapshot : scopeSnapshots)
                snapshot = ScopeProbeSnapshot{};
            scopeMetadataPendingWeapon = 0;
            scopeMetadataPendingSince = 0;
            scopeCodeScanActive = false;
            scopeCodeScanFinished = false;
            scopeCodeReferenceCount = 0;
            scopeCodeSectionIndex = 0;
            scopeCodeCursor = 0;
            scopeCodeBytesScanned = 0;
            scopeCodeScanTimeMs = 0;
            scopeCodeUnwindRejected = 0;
            for (auto& reference : scopeCodeReferences)
                reference = ScopeCodeReference{};
            for (auto& hash : scopeCodeHashes)
                hash = 0;
            for (auto& count : scopeCodeHitTotals)
                count = 0;
            shotgunShellAmmo = 0;
            incendiaryShellAmmo = 0;
            reticleAccuracyPed = 0;
            previousReticleAccuracy = 0;
            reticleAccuracyOverridden = false;
            emptyAutoReloadHooksReady = false;
            manualReloadAllowedUntil.store(0, std::memory_order_relaxed);
            playerAddress.store(0, std::memory_order_relaxed);
            RestoreHook(holster);
            RestoreHook(cover);
            RestoreHook(deadEye);
            RestoreHook(shouldReload);
            RestoreHook(clipNeedsReload);
        }
    }
}
