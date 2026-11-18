#include "SkipIntro.h"
#include "../../Headers/Logger.h"
#include <windows.h>
#include <string>
#include <vector>
#include <cstring>
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace
{
    const char defaultStartupMeta[] = R"GFMETA(<?xml version="1.0" encoding="UTF-8"?>
<CDataFileMgr__ContentsOfDataFileXml>
 <disabledFiles />
 <includedXmlFiles itemType="CDataFileMgr__DataFileArray" />
 <includedDataFiles />
 <dataFiles itemType="CDataFileMgr__DataFile">
  <Item>
   <filename>platform:/data/cdimages/scaleform_platform_pc.rpf</filename>
   <fileType>RPF_FILE</fileType>
  </Item>
  <Item>
   <filename>platform:/data/ui/value_conversion.rpf</filename>
   <fileType>RPF_FILE</fileType>
  </Item>
  <Item>
   <filename>platform:/data/ui/widgets.rpf</filename>
   <fileType>RPF_FILE</fileType>
  </Item>
  <Item>
   <filename>platform:/textures/ui/ui_photo_stickers.rpf</filename>
   <fileType>RPF_FILE</fileType>
  </Item>
  <Item>
   <filename>platform:/textures/ui/ui_platform.rpf</filename>
   <fileType>RPF_FILE</fileType>
  </Item>
  <Item>
   <filename>platform:/data/ui/stylesCatalog</filename>
   <fileType>aWeaponizeDisputants</fileType>
  </Item>
  <Item>
   <filename>platform:/data/cdimages/scaleform_frontend.rpf</filename>
   <fileType>RPF_FILE_PRE_INSTALL</fileType>
  </Item>
  <Item>
   <filename>platform:/textures/ui/ui_startup_textures.rpf</filename>
   <fileType>RPF_FILE</fileType>
  </Item>
  <Item>
   <filename>platform:/data/ui/startup_data.rpf</filename>
   <fileType>RPF_FILE</fileType>
  </Item>
 </dataFiles>
 <contentChangeSets itemType="CDataFileMgr__ContentChangeSet" />
 <patchFiles />
</CDataFileMgr__ContentsOfDataFileXml>
)GFMETA";
    const char ownedRegistration[] = R"GFENTRY(  <Item>
   <filename>platform:/BettermentImprovements_data/legal_screen_patch.ymt</filename>
   <fileType>STREAMING_FILE</fileType>
   <registerAs>boot_flow/boot_launcher_flow</registerAs>
   <overlay value="false"/>
   <patchFile value="false"/>
  </Item>
)GFENTRY";
    const char onlineFilterRegistration[] = R"GFONLINE(  <Item>
   <filename>platform:/BettermentImprovements_data/remove_online_tab.ymt</filename>
   <fileType>STREAMING_FILE</fileType>
   <registerAs>landing_page/widgets/filter</registerAs>
   <overlay value="false"/>
   <patchFile value="false"/>
  </Item>
)GFONLINE";
    const char onlineFilterYmt[] = "";
    const char storyRegistration[] = R"GFSTORYREG(  <Item>
   <filename>platform:/BettermentImprovements_data/straight_to_story_mode.ymt</filename>
   <fileType>STREAMING_FILE</fileType>
   <registerAs>landing_page/flow/landing_launcher_flow</registerAs>
   <overlay value="false"/>
   <patchFile value="false"/>
  </Item>
)GFSTORYREG";
    const char storyModeYmt[] = R"GFSTORYYMT(<?xml version="1.0" encoding="UTF-8"?>
<rage__fwuiFlowBlock>
 <ID>landing_flow</ID>
 <EntryPoints>
  <Item><ID>default</ID><Target>input_context_switch.set_feedback_context.boot_flow_activity_sentinel.boot_flow_startup</Target></Item>
  <Item><ID>bye</ID><Target>input_context_switch.set_feedback_context.boot_flow_activity_sentinel.boot_flow_startup</Target></Item>
  <Item><ID>startup</ID><Target>input_context_switch.set_feedback_context.boot_flow_activity_sentinel.boot_flow_startup</Target></Item>
  <Item><ID>sign_out</ID><Target>input_context_switch.set_feedback_context.boot_flow_activity_sentinel.boot_flow_sign_out</Target></Item>
  <Item><ID>sp</ID><Target>input_context_switch.set_feedback_context.launch_sp</Target></Item>
  <Item><ID>mp</ID><Target>input_context_switch.set_feedback_context.launch_sp</Target></Item>
  <Item><ID>mp_direct</ID><Target>input_context_switch.set_feedback_context.launch_sp</Target></Item>
 </EntryPoints>
 <ExitPoints><Item><ID>exit</ID></Item></ExitPoints>
 <FlowRoot>
  <ID>landing_activity_sentinel</ID>
  <State type="rage__StateActivitySentinel"><ActivityID>landing_main</ActivityID></State>
  <Children>
   <Item>
    <ID>input_context_switch</ID>
    <State type="StateSetInputContext"><ContextType>LANDING_PAGE</ContextType></State>
    <Children>
     <Item>
      <ID>set_feedback_context</ID>
      <State type="StateSetFeedbackContext"><ContextType>LANDING_PAGE</ContextType></State>
      <Children>
       <Item>
        <ID>boot_flow_activity_sentinel</ID>
        <State type="rage__StateActivitySentinel"><ActivityID>boot_flow</ActivityID></State>
        <Children>
         <Item>
          <ID>boot_flow_startup</ID>
          <State type="rage__StateAppLauncher"><AppID>boot_flow</AppID><EntryPoint>default_entry</EntryPoint></State>
          <LinkMap><Item key="exit"><Target>^.^.launch_sp</Target></Item></LinkMap>
         </Item>
         <Item>
          <ID>boot_flow_sign_out</ID>
          <State type="rage__StateAppLauncher"><AppID>boot_flow</AppID><EntryPoint>sign_out</EntryPoint></State>
          <LinkMap><Item key="exit"><Target>^.^.launch_sp</Target></Item></LinkMap>
         </Item>
        </Children>
       </Item>
       <Item>
        <ID>launch_sp</ID>
        <State type="StateDispatchScriptEvent">
         <Channel>LANDING_PAGE</Channel>
         <Event><Type>UISCRIPTEVENTTYPE_ITEM_SELECTED</Type><HashParameter>LOAD_SP</HashParameter></Event>
        </State>
        <LinkMap><Item key="next"><Target>exit</Target><LinkInfo>LINK_TO_EXTERNAL</LinkInfo></Item></LinkMap>
       </Item>
      </Children>
     </Item>
    </Children>
   </Item>
  </Children>
 </FlowRoot>
</rage__fwuiFlowBlock>
)GFSTORYYMT";
    const char legalScreenYmt[] = R"GFYMT(<?xml version="1.0" encoding="UTF-8"?>
<rage__fwuiFlowBlock>
 <ID>boot_flow</ID>
 <EntryPoints>
  <Item>
   <ID>default_entry</ID>
   <Target>wait_for_profile</Target>
  </Item>
  <Item>
   <ID>sign_out</ID>
   <Target>wait_for_profile</Target>
  </Item>
 </EntryPoints>
 <ExitPoints>
  <Item>
   <ID>exit</ID>
  </Item>
 </ExitPoints>
 <FlowRoot>
  <ID>input_context_switch</ID>
  <State type="StateSetInputContext">
   <ContextType>BOOT_FLOW</ContextType>
  </State>
  <Children>
   <Item>
        <ID>wait_for_profile</ID>
        <State type="StateWaitForProfileLoad" />
        <LinkMap>
         <Item key="next">
          <Target>exit</Target>
          <LinkInfo>LINK_TO_EXTERNAL</LinkInfo>
         </Item>
         <Item key="exit">
          <Target>exit</Target>
          <LinkInfo>LINK_TO_EXTERNAL</LinkInfo>
         </Item>
         <Item key="yes">
          <Target>exit</Target>
          <LinkInfo>LINK_TO_EXTERNAL</LinkInfo>
         </Item>
        </LinkMap>
       </Item>
  </Children>
 </FlowRoot>
</rage__fwuiFlowBlock>)GFYMT";
    const char legacyStartupMeta[] = R"GFLMETA(<?xml version="1.0" encoding="UTF-8"?>
<CDataFileMgr__ContentsOfDataFileXml>
 <disabledFiles />
 <includedXmlFiles itemType="CDataFileMgr__DataFileArray" />
 <includedDataFiles />
 <dataFiles itemType="CDataFileMgr__DataFile">
  <Item>
   <filename>platform:/data/cdimages/scaleform_platform_pc.rpf</filename>
   <fileType>RPF_FILE</fileType>
  </Item>
  <Item>
   <filename>platform:/data/ui/value_conversion.rpf</filename>
   <fileType>RPF_FILE</fileType>
  </Item>
  <Item>
   <filename>platform:/data/ui/widgets.rpf</filename>
   <fileType>RPF_FILE</fileType>
  </Item>
  <Item>
   <filename>platform:/textures/ui/ui_photo_stickers.rpf</filename>
   <fileType>RPF_FILE</fileType>
  </Item>
  <Item>
   <filename>platform:/textures/ui/ui_platform.rpf</filename>
   <fileType>RPF_FILE</fileType>
  </Item>
  <Item>
   <filename>platform:/data/ui/stylesCatalog</filename>
   <fileType>aWeaponizeDisputants</fileType>
  </Item>
  <Item>
   <filename>platform:/data/cdimages/scaleform_frontend.rpf</filename>
   <fileType>RPF_FILE_PRE_INSTALL</fileType>
  </Item>
  <Item>
   <filename>platform:/textures/ui/ui_startup_textures.rpf</filename>
   <fileType>RPF_FILE</fileType>
  </Item>
  <Item>
   <filename>platform:/data/ui/startup_data.rpf</filename>
   <fileType>RPF_FILE</fileType>
  </Item>
  <Item>
   <filename>platform:/BettermentImprovements_data/boot_launcher_flow.ymt</filename>
   <fileType>STREAMING_FILE</fileType>
   <registerAs>boot_flow/boot_launcher_flow</registerAs>
   <overlay value="false"/>
   <patchFile value="false"/>
  </Item>
 </dataFiles>
 <contentChangeSets itemType="CDataFileMgr__ContentChangeSet" />
 <patchFiles />
</CDataFileMgr__ContentsOfDataFileXml>
)GFLMETA";
    const char legacyBootYmt[] = R"GFLBOOT(<?xml version="1.0" encoding="UTF-8"?>
<rage__fwuiFlowBlock>
 <ID>boot_flow</ID>
 <EntryPoints>
  <Item>
   <ID>default_entry</ID>
   <Target>wait_for_profile</Target>
  </Item>
  <Item>
   <ID>sign_out</ID>
   <Target>wait_for_profile</Target>
  </Item>
 </EntryPoints>
 <ExitPoints>
  <Item>
   <ID>exit</ID>
  </Item>
 </ExitPoints>
 <FlowRoot>
  <ID>input_context_switch</ID>
  <State type="StateSetInputContext">
   <ContextType>BOOT_FLOW</ContextType>
  </State>
  <Children>
   <Item>
    <ID>wait_for_profile</ID>
    <State type="StateWaitForProfileLoad" />
    <LinkMap>
     <Item key="next"><Target>exit</Target><LinkInfo>LINK_TO_EXTERNAL</LinkInfo></Item>
     <Item key="exit"><Target>exit</Target><LinkInfo>LINK_TO_EXTERNAL</LinkInfo></Item>
     <Item key="yes"><Target>exit</Target><LinkInfo>LINK_TO_EXTERNAL</LinkInfo></Item>
    </LinkMap>
   </Item>
  </Children>
 </FlowRoot>
</rage__fwuiFlowBlock>
)GFLBOOT";

    const char ownedFilename[] = "platform:/BettermentImprovements_data/legal_screen_patch.ymt";
    const char legacyFilename[] = "platform:/BettermentImprovements_data/boot_launcher_flow.ymt";
    const char storyFilename[] = "platform:/BettermentImprovements_data/straight_to_story_mode.ymt";
    const char onlineFilterFilename[] = "platform:/BettermentImprovements_data/remove_online_tab.ymt";
    const char flowRegistration[] = "<registerAs>boot_flow/boot_launcher_flow</registerAs>";
    const char storyFlowRegistration[] = "<registerAs>landing_page/flow/landing_launcher_flow</registerAs>";
    const char onlineFilterTarget[] = "<registerAs>landing_page/widgets/filter</registerAs>";

    std::wstring GameRoot()
    {
        wchar_t path[MAX_PATH] = {};
        const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
        if (length == 0 || length >= MAX_PATH)
            return {};
        std::wstring root(path, length);
        const size_t separator = root.find_last_of(L"\\/");
        if (separator == std::wstring::npos)
            return {};
        root.resize(separator + 1);
        return root;
    }

    bool Exists(const std::wstring& path)
    {
        return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
    }

    using BinkOpenWithOptionsFn = void* (*)(const char*, const void*, unsigned int);
    using BinkNextFrameFn = void (*)(void*);
    using BinkWaitFn = int (*)(void*);
    using BinkCloseFn = void (*)(void*);
    using BinkGotoFn = void (*)(void*, unsigned int, int);

    BinkOpenWithOptionsFn originalBinkOpen = nullptr;
    BinkNextFrameFn originalBinkNextFrame = nullptr;
    BinkWaitFn originalBinkWait = nullptr;
    BinkCloseFn originalBinkClose = nullptr;
    BinkGotoFn originalBinkGoto = nullptr;
    IMAGE_THUNK_DATA64* activeBinkSlots[4] = {};
    void* activeBinkHooks[4] = {};
    void* activeBinkOriginals[4] = {};
    bool binkHooksInstalled = false;
    std::atomic<bool> splashSeekEnabled{ false };

    struct StartupMovie
    {
        std::atomic<void*> handle{ nullptr };
        std::atomic<unsigned int> nextFrames{ 0 };
        std::atomic<unsigned int> waits{ 0 };
        std::atomic<ULONGLONG> openedAt{ 0 };
        std::atomic<ULONGLONG> lastWaitAt{ 0 };
        std::atomic<ULONGLONG> lastFrameAt{ 0 };
        std::atomic<int> lastWaitResult{ 0 };
        std::atomic<unsigned int> targetFrame{ 0 };
        std::atomic<bool> seekAttempted{ false };
    };

    StartupMovie startupMovies[2];
    std::atomic<unsigned int> trackedMovieCount{ 0 };
    std::atomic<unsigned int> binkOpensObserved{ 0 };
    std::atomic<ULONGLONG> splashHookStartedAt{ 0 };
    std::atomic<bool> startupHeartbeatEnabled{ false };

    void LogStartupHeartbeat(const char* event)
    {
        if (!Logger::IsDebugEnabled())
            return;

        const ULONGLONG now = GetTickCount64();
        const ULONGLONG started = splashHookStartedAt.load(std::memory_order_relaxed);
        if (started == 0)
            return;
        std::ostringstream line;
        line << "SkipIntroSplashes: " << event << ", startupElapsedMs="
            << (started != 0 && now >= started ? now - started : 0)
            << ", binkOpens=" << binkOpensObserved.load(std::memory_order_relaxed);
        for (size_t i = 0; i < 2; ++i)
        {
            const StartupMovie& movie = startupMovies[i];
            const ULONGLONG opened = movie.openedAt.load(std::memory_order_relaxed);
            const ULONGLONG lastWait = movie.lastWaitAt.load(std::memory_order_relaxed);
            const ULONGLONG lastFrame = movie.lastFrameAt.load(std::memory_order_relaxed);
            line << ", movie" << (i + 1)
                << "={opened:" << (opened != 0 ? "yes" : "no")
                << ",active:" << (movie.handle.load(std::memory_order_acquire) != nullptr ? "yes" : "no")
                << ",frames:" << movie.nextFrames.load(std::memory_order_relaxed)
                << ",waits:" << movie.waits.load(std::memory_order_relaxed)
                << ",lastWaitResult:" << movie.lastWaitResult.load(std::memory_order_relaxed)
                << ",lastWaitAgeMs:" << (lastWait != 0 && now >= lastWait ? now - lastWait : 0)
                << ",lastFrameAgeMs:" << (lastFrame != 0 && now >= lastFrame ? now - lastFrame : 0)
                << "}";
        }
        line << ".";
        Logger::Debug(line.str().c_str());
    }

    DWORD WINAPI StartupHeartbeatThread(LPVOID)
    {
        const ULONGLONG began = GetTickCount64();
        constexpr unsigned int checkpoints[] = { 45, 90 };
        for (const unsigned int seconds : checkpoints)
        {
            while (startupHeartbeatEnabled.load(std::memory_order_acquire))
            {
                const ULONGLONG now = GetTickCount64();
                if (now >= began && now - began >= static_cast<ULONGLONG>(seconds) * 1000)
                    break;
                Sleep(1000);
            }
            if (!startupHeartbeatEnabled.load(std::memory_order_acquire))
                return 0;
            LogStartupHeartbeat("startup heartbeat");
        }
        startupHeartbeatEnabled.store(false, std::memory_order_release);
        return 0;
    }

    void StartStartupHeartbeat()
    {
        if (!Logger::IsDebugEnabled() || !binkHooksInstalled ||
            !splashSeekEnabled.load(std::memory_order_acquire))
            return;

        bool expected = false;
        if (!startupHeartbeatEnabled.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            return;

        HANDLE thread = CreateThread(nullptr, 0, StartupHeartbeatThread, nullptr, 0, nullptr);
        if (thread == nullptr)
        {
            startupHeartbeatEnabled.store(false, std::memory_order_release);
            Logger::Log("[WARNING] SkipIntroSplashes: startup monitor unavailable; playback hooks unchanged.");
            return;
        }
        CloseHandle(thread);
        Logger::Debug("SkipIntroSplashes: startup monitor active (read-only).");
    }

    bool ReadableRegion(const void* pointer, size_t required, size_t* available = nullptr)
    {
        if (pointer == nullptr)
            return false;

        MEMORY_BASIC_INFORMATION information = {};
        if (VirtualQuery(pointer, &information, sizeof(information)) != sizeof(information) ||
            information.State != MEM_COMMIT || (information.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
            return false;

        const DWORD protection = information.Protect & 0xFF;
        if (protection != PAGE_READONLY && protection != PAGE_READWRITE &&
            protection != PAGE_WRITECOPY && protection != PAGE_EXECUTE_READ &&
            protection != PAGE_EXECUTE_READWRITE && protection != PAGE_EXECUTE_WRITECOPY)
            return false;

        const uintptr_t start = reinterpret_cast<uintptr_t>(pointer);
        const uintptr_t regionStart = reinterpret_cast<uintptr_t>(information.BaseAddress);
        if (start < regionStart || start - regionStart >= information.RegionSize)
            return false;

        const size_t remaining = information.RegionSize - (start - regionStart);
        if (available != nullptr)
            *available = remaining;
        return remaining >= required;
    }

    bool ReadMovieName(const char* name, std::string& movie)
    {
        size_t available = 0;
        if (!ReadableRegion(name, 1, &available))
            return false;

        const size_t limit = (std::min)(available, static_cast<size_t>(384));
        const char* terminator = static_cast<const char*>(std::memchr(name, 0, limit));
        if (terminator == nullptr || terminator == name)
            return false;

        for (const char* current = name; current != terminator; ++current)
        {
            if (static_cast<unsigned char>(*current) < 32 || static_cast<unsigned char>(*current) > 126)
                return false;
        }

        movie.assign(name, terminator);
        std::transform(movie.begin(), movie.end(), movie.begin(), [](unsigned char c) {
            return static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
        });
        return true;
    }

    struct MemoryMovieHeader
    {
        char magic[4];
        unsigned int fileSizeMinus8;
        unsigned int frames;
        unsigned int largestFrameSize;
        unsigned int internalFrames;
        unsigned int width;
        unsigned int height;
        unsigned int frameRate;
        unsigned int frameRateDiv;
    };

    bool ReadMemoryMovieHeader(const char* name, unsigned int flags, MemoryMovieHeader& header)
    {
        if ((flags & 0x04000000u) == 0 || !ReadableRegion(name, sizeof(header)))
            return false;

        std::memcpy(&header, name, sizeof(header));
        if (std::memcmp(header.magic, "KB2", 3) != 0 ||
            header.magic[3] < 'a' || header.magic[3] > 'z' ||
            header.fileSizeMinus8 < 4096 || header.fileSizeMinus8 > 0x40000000u ||
            header.frames < 2 || header.frames > 1800 ||
            header.width < 320 || header.width > 7680 ||
            header.height < 180 || header.height > 4320 ||
            header.frameRateDiv == 0 ||
            (static_cast<double>(header.frameRate) / header.frameRateDiv) < 10.0 ||
            (static_cast<double>(header.frameRate) / header.frameRateDiv) > 120.0)
            return false;
        return true;
    }

    StartupMovie* FindStartupMovie(void* handle)
    {
        if (handle == nullptr)
            return nullptr;
        for (auto& movie : startupMovies)
        {
            if (movie.handle.load(std::memory_order_acquire) == handle)
                return &movie;
        }
        return nullptr;
    }

    int BinkWaitHook(void* handle)
    {
        const int result = originalBinkWait(handle);
        StartupMovie* movie = FindStartupMovie(handle);
        if (movie != nullptr)
        {
            movie->lastWaitAt.store(GetTickCount64(), std::memory_order_relaxed);
            movie->lastWaitResult.store(result, std::memory_order_relaxed);
            const unsigned int count = movie->waits.fetch_add(1, std::memory_order_relaxed);
            if (count == 0 && Logger::IsDebugEnabled())
            {
                std::ostringstream line;
                line << "SkipIntroSplashes: startup movie #" << (movie - startupMovies + 1)
                    << " entered BinkWait, result=" << result << ".";
                Logger::Debug(line.str().c_str());
            }
        }
        return result;
    }

    void BinkNextFrameHook(void* handle)
    {
        StartupMovie* movie = FindStartupMovie(handle);
        originalBinkNextFrame(handle);
        if (movie != nullptr)
        {
            movie->lastFrameAt.store(GetTickCount64(), std::memory_order_relaxed);
            const unsigned int count = movie->nextFrames.fetch_add(1, std::memory_order_relaxed) + 1;
            if ((count == 1 || count == 30 ||
                (movie == &startupMovies[1] && (count == 150 || count == 300 || count == 450))) &&
                Logger::IsDebugEnabled())
            {
                std::ostringstream line;
                line << "SkipIntroSplashes: startup movie #" << (movie - startupMovies + 1)
                    << " BinkNextFrame count=" << count;
                if (movie == &startupMovies[1] && count >= 150)
                {
                    const ULONGLONG opened = movie->openedAt.load(std::memory_order_relaxed);
                    const ULONGLONG now = GetTickCount64();
                    line << ", elapsedMs=" << (now >= opened ? now - opened : 0);
                }
                line << ".";
                Logger::Debug(line.str().c_str());
            }
            if (count == 1 && movie == &startupMovies[0] &&
                splashSeekEnabled.load(std::memory_order_acquire) && originalBinkGoto != nullptr)
            {
                const unsigned int frame = movie->targetFrame.load(std::memory_order_relaxed);
                if (frame != 0 && !movie->seekAttempted.exchange(true, std::memory_order_acq_rel))
                {
                    Logger::Log("[INFO] SkipIntroSplashes: first logo reached; requesting Bink seek.");
                    originalBinkGoto(handle, frame, 1);
                    Logger::Log("[INFO] SkipIntroSplashes: Bink seek returned; waiting for playback.");
                }
            }
        }
    }

    void BinkCloseHook(void* handle)
    {
        StartupMovie* movie = FindStartupMovie(handle);
        if (movie != nullptr)
        {
            movie->handle.store(nullptr, std::memory_order_release);
            if (Logger::IsDebugEnabled())
            {
                std::ostringstream line;
                const ULONGLONG opened = movie->openedAt.load(std::memory_order_relaxed);
                const ULONGLONG now = GetTickCount64();
                line << "SkipIntroSplashes: startup movie #" << (movie - startupMovies + 1)
                    << " BinkClose, nextFrames=" << movie->nextFrames.load(std::memory_order_relaxed)
                    << ", waits=" << movie->waits.load(std::memory_order_relaxed)
                    << ", elapsedMs=" << (now >= opened ? now - opened : 0) << ".";
                Logger::Debug(line.str().c_str());
            }
        }
        originalBinkClose(handle);
    }

    void* BinkOpenHook(const char* name, const void* options, unsigned int flags)
    {
        std::string movie;
        const bool hasMovieName = (flags & 0x04000000u) == 0 && ReadMovieName(name, movie);
        MemoryMovieHeader sourceHeader = {};
        const bool hasMemoryHeader = ReadMemoryMovieHeader(name, flags, sourceHeader);
        void* handle = originalBinkOpen(name, options, flags);

        const unsigned int openIndex = binkOpensObserved.fetch_add(1);
        const ULONGLONG installTime = splashHookStartedAt.load();
        const ULONGLONG now = GetTickCount64();
        const bool withinStartup = installTime != 0 && now >= installTime && now - installTime <= 120000;
        const bool knownSplash = hasMemoryHeader && sourceHeader.width == 1920 &&
            ((sourceHeader.height == 1080 && sourceHeader.frames == 301) ||
                (sourceHeader.height == 440 && sourceHeader.frames == 451));
        if (handle != nullptr && withinStartup && knownSplash)
        {
            const unsigned int trackedIndex = trackedMovieCount.fetch_add(1, std::memory_order_relaxed);
            if (trackedIndex < 2)
            {
                StartupMovie& tracked = startupMovies[trackedIndex];
                tracked.nextFrames.store(0, std::memory_order_relaxed);
                tracked.waits.store(0, std::memory_order_relaxed);
                tracked.openedAt.store(now, std::memory_order_relaxed);
                tracked.lastWaitAt.store(0, std::memory_order_relaxed);
                tracked.lastFrameAt.store(0, std::memory_order_relaxed);
                tracked.lastWaitResult.store(0, std::memory_order_relaxed);
                tracked.targetFrame.store(splashSeekEnabled.load(std::memory_order_acquire) &&
                    trackedIndex == 0 && sourceHeader.width == 1920 &&
                    sourceHeader.height == 1080 && sourceHeader.frames == 301 ? sourceHeader.frames : 0,
                    std::memory_order_relaxed);
                tracked.seekAttempted.store(false, std::memory_order_relaxed);
                tracked.handle.store(handle, std::memory_order_release);
            }
        }
        if (Logger::IsDebugEnabled() && openIndex < 6)
        {
            std::ostringstream line;
            line << "SkipIntroSplashes: Bink open #" << (openIndex + 1)
                << ", source=" << (hasMovieName ? movie.substr(0, 140) :
                    (hasMemoryHeader ? "Bink2 memory" : "memory or unrecognized"))
                << ", flags=0x" << std::uppercase << std::hex << flags << std::dec
                << ", handle=" << (handle != nullptr ? "valid" : "null")
                << ", startupWindow=" << (withinStartup ? "yes" : "no");
            if (hasMemoryHeader)
                line << ", sourceVideo=" << sourceHeader.width << "x" << sourceHeader.height
                    << ", sourceFrames=" << sourceHeader.frames;
            line << ".";
            Logger::Debug(line.str().c_str());
        }

        return handle;
    }

    bool ImageStringEquals(const unsigned char* image, size_t imageSize, size_t offset,
        const char* expected, bool ignoreCase = false)
    {
        const size_t length = std::strlen(expected);
        if (offset >= imageSize || length >= imageSize - offset)
            return false;
        const char* value = reinterpret_cast<const char*>(image + offset);
        return ignoreCase ? _strnicmp(value, expected, length + 1) == 0 :
            std::memcmp(value, expected, length + 1) == 0;
    }

    bool InstallSplashHook()
    {
        splashSeekEnabled.store(true, std::memory_order_release);
        auto* base = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
        HMODULE bink = GetModuleHandleW(L"bink2w64.dll");
        if (base == nullptr || bink == nullptr)
        {
            Logger::Log("[WARNING] SkipIntroSplashes: game or Bink library not found.");
            return false;
        }

        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 4096)
            return false;

        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
            nt->OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_IMPORT)
            return false;

        const DWORD imageSize = nt->OptionalHeader.SizeOfImage;
        if (imageSize < sizeof(IMAGE_DOS_HEADER) ||
            static_cast<size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64) > imageSize)
            return false;
        const auto& imports = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (imports.VirtualAddress == 0 || imports.VirtualAddress >= imageSize ||
            imports.Size < sizeof(IMAGE_IMPORT_DESCRIPTOR) ||
            imports.Size > imageSize - imports.VirtualAddress)
            return false;

        originalBinkGoto = reinterpret_cast<BinkGotoFn>(GetProcAddress(bink, "BinkGoto"));
        if (originalBinkGoto == nullptr)
            Logger::Log("[WARNING] SkipIntroSplashes: BinkGoto unavailable; playback skips disabled.");

        constexpr const char* importedNames[4] = {
            "BinkOpenWithOptions", "BinkNextFrame", "BinkWait", "BinkClose"
        };
        void* hookAddresses[4] = {
            reinterpret_cast<void*>(&BinkOpenHook),
            reinterpret_cast<void*>(&BinkNextFrameHook),
            reinterpret_cast<void*>(&BinkWaitHook),
            reinterpret_cast<void*>(&BinkCloseHook)
        };
        IMAGE_THUNK_DATA64* slots[4] = {};
        void* originals[4] = {};

        const auto* descriptors = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + imports.VirtualAddress);
        const size_t descriptorCount = imports.Size / sizeof(IMAGE_IMPORT_DESCRIPTOR);
        for (size_t i = 0; i < descriptorCount && descriptors[i].Name != 0; ++i)
        {
            const auto& descriptor = descriptors[i];
            if (!ImageStringEquals(base, imageSize, descriptor.Name, "bink2w64.dll", true))
                continue;

            if (descriptor.OriginalFirstThunk == 0 || descriptor.FirstThunk == 0 ||
                descriptor.OriginalFirstThunk >= imageSize || descriptor.FirstThunk >= imageSize)
                break;

            const size_t count = (std::min)(static_cast<size_t>((imageSize - descriptor.OriginalFirstThunk) / sizeof(IMAGE_THUNK_DATA64)),
                static_cast<size_t>((imageSize - descriptor.FirstThunk) / sizeof(IMAGE_THUNK_DATA64)));
            const auto* names = reinterpret_cast<const IMAGE_THUNK_DATA64*>(base + descriptor.OriginalFirstThunk);
            auto* addresses = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + descriptor.FirstThunk);
            for (size_t j = 0; j < count && names[j].u1.AddressOfData != 0; ++j)
            {
                if (IMAGE_SNAP_BY_ORDINAL64(names[j].u1.Ordinal))
                    continue;

                const uintptr_t nameRva = names[j].u1.AddressOfData;
                if (nameRva >= imageSize || imageSize - nameRva < sizeof(IMAGE_IMPORT_BY_NAME))
                    continue;

                const size_t textRva = nameRva + offsetof(IMAGE_IMPORT_BY_NAME, Name);
                for (size_t k = 0; k < 4; ++k)
                {
                    if (ImageStringEquals(base, imageSize, textRva, importedNames[k]))
                    {
                        if (slots[k] != nullptr)
                        {
                            Logger::Log("[WARNING] SkipIntroSplashes: ambiguous Bink imports; game unchanged.");
                            return false;
                        }
                        slots[k] = &addresses[j];
                    }
                }
            }
            break;
        }

        for (size_t k = 0; k < 4; ++k)
        {
            if (slots[k] == nullptr || slots[k]->u1.Function == 0)
            {
                Logger::Log("[WARNING] SkipIntroSplashes: required Bink import missing; game unchanged.");
                return false;
            }
            originals[k] = reinterpret_cast<void*>(slots[k]->u1.Function);
            if (originals[k] == hookAddresses[k])
                return false;
        }

        uintptr_t first = reinterpret_cast<uintptr_t>(slots[0]);
        uintptr_t last = first + sizeof(*slots[0]);
        for (size_t k = 1; k < 4; ++k)
        {
            const uintptr_t address = reinterpret_cast<uintptr_t>(slots[k]);
            first = (std::min)(first, address);
            last = (std::max)(last, address + sizeof(*slots[k]));
        }

        MEMORY_BASIC_INFORMATION firstRegion = {};
        MEMORY_BASIC_INFORMATION lastRegion = {};
        if (VirtualQuery(reinterpret_cast<void*>(first), &firstRegion, sizeof(firstRegion)) != sizeof(firstRegion) ||
            VirtualQuery(reinterpret_cast<void*>(last - 1), &lastRegion, sizeof(lastRegion)) != sizeof(lastRegion) ||
            firstRegion.State != MEM_COMMIT || lastRegion.State != MEM_COMMIT ||
            firstRegion.Protect != lastRegion.Protect ||
            (firstRegion.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0 ||
            reinterpret_cast<uintptr_t>(firstRegion.BaseAddress) + firstRegion.RegionSize < last)
        {
            Logger::Log("[WARNING] SkipIntroSplashes: Bink imports span incompatible memory; game unchanged.");
            return false;
        }

        DWORD oldProtection = 0;
        if (!VirtualProtect(reinterpret_cast<void*>(first), last - first, PAGE_READWRITE, &oldProtection))
        {
            Logger::Log("[WARNING] SkipIntroSplashes: could not make Bink imports writable.");
            return false;
        }

        originalBinkOpen = reinterpret_cast<BinkOpenWithOptionsFn>(originals[0]);
        originalBinkNextFrame = reinterpret_cast<BinkNextFrameFn>(originals[1]);
        originalBinkWait = reinterpret_cast<BinkWaitFn>(originals[2]);
        originalBinkClose = reinterpret_cast<BinkCloseFn>(originals[3]);
        splashHookStartedAt.store(GetTickCount64(), std::memory_order_release);
        size_t changed = 0;
        for (; changed < 4; ++changed)
        {
            void* previous = InterlockedCompareExchangePointer(
                reinterpret_cast<PVOID volatile*>(&slots[changed]->u1.Function),
                hookAddresses[changed], originals[changed]);
            if (previous != originals[changed])
                break;
        }

        if (changed != 4)
        {
            for (size_t k = 0; k < changed; ++k)
            {
                InterlockedCompareExchangePointer(
                    reinterpret_cast<PVOID volatile*>(&slots[k]->u1.Function),
                    originals[k], hookAddresses[k]);
            }
        }

        DWORD restoredProtection = 0;
        const bool restored = VirtualProtect(reinterpret_cast<void*>(first), last - first, oldProtection, &restoredProtection) != FALSE;
        if (changed != 4)
        {
            Logger::Log("[WARNING] SkipIntroSplashes: Bink import changed during installation; hook changes rolled back.");
            return false;
        }

        for (size_t k = 0; k < 4; ++k)
        {
            activeBinkSlots[k] = slots[k];
            activeBinkHooks[k] = hookAddresses[k];
            activeBinkOriginals[k] = originals[k];
        }
        binkHooksInstalled = true;
        Logger::Log(restored ?
            "[SUCCESS] SkipIntroSplashes: Bink hooks installed; first-logo seek enabled when supported." :
            "[WARNING] Startup: Bink hooks installed, but import page protection could not be restored.");
        return true;
    }

    void RemoveSplashHook()
    {
        if (!binkHooksInstalled)
            return;

        uintptr_t first = reinterpret_cast<uintptr_t>(activeBinkSlots[0]);
        uintptr_t last = first + sizeof(*activeBinkSlots[0]);
        for (size_t i = 1; i < 4; ++i)
        {
            const uintptr_t address = reinterpret_cast<uintptr_t>(activeBinkSlots[i]);
            first = (std::min)(first, address);
            last = (std::max)(last, address + sizeof(*activeBinkSlots[i]));
        }

        DWORD oldProtection = 0;
        if (!VirtualProtect(reinterpret_cast<void*>(first), last - first, PAGE_READWRITE, &oldProtection))
        {
            Logger::Log("[WARNING] SkipIntroSplashes: unable to restore Bink imports after startup.");
            return;
        }

        size_t removed = 0;
        for (size_t i = 0; i < 4; ++i)
        {
            void* previous = InterlockedCompareExchangePointer(
                reinterpret_cast<PVOID volatile*>(&activeBinkSlots[i]->u1.Function),
                activeBinkOriginals[i], activeBinkHooks[i]);
            if (previous == activeBinkHooks[i] || previous == activeBinkOriginals[i])
                ++removed;
        }

        DWORD restoredProtection = 0;
        const bool protectedAgain = VirtualProtect(reinterpret_cast<void*>(first), last - first,
            oldProtection, &restoredProtection) != FALSE;
        binkHooksInstalled = removed != 4;
        Logger::Log(removed == 4 && protectedAgain ?
            "[SUCCESS] SkipIntroSplashes: temporary Bink hooks removed at script startup." :
            "[WARNING] SkipIntroSplashes: Bink hook cleanup incomplete; other hooks or page protection may have changed.");
    }

    bool ReadFileContents(const std::wstring& path, std::string& contents)
    {
        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return false;

        LARGE_INTEGER size = {};
        bool success = GetFileSizeEx(file, &size) != FALSE &&
            size.QuadPart >= 0 && size.QuadPart <= 1024 * 1024;
        if (success)
        {
            contents.resize(static_cast<size_t>(size.QuadPart));
            DWORD read = 0;
            success = contents.empty() || (ReadFile(file, contents.data(),
                static_cast<DWORD>(contents.size()), &read, nullptr) != FALSE &&
                read == contents.size());
        }
        CloseHandle(file);
        return success;
    }

    bool AtomicWrite(const std::wstring& path, const std::string& contents)
    {
        const size_t slash = path.find_last_of(L"\\/");
        if (slash == std::wstring::npos)
            return false;

        wchar_t temporary[MAX_PATH] = {};
        if (!GetTempFileNameW(path.substr(0, slash).c_str(), L"BMI", 0, temporary))
            return false;

        HANDLE file = CreateFileW(temporary, GENERIC_WRITE, 0, nullptr,
            TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        bool success = file != INVALID_HANDLE_VALUE;
        if (success)
        {
            DWORD written = 0;
            success = (contents.empty() || (WriteFile(file, contents.data(),
                static_cast<DWORD>(contents.size()), &written, nullptr) != FALSE &&
                written == contents.size())) && FlushFileBuffers(file) != FALSE;
            CloseHandle(file);
        }
        if (success)
            success = MoveFileExW(temporary, path.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
        if (!success)
            DeleteFileW(temporary);
        return success;
    }

    bool CreateOwnedDirectory(const std::wstring& directory)
    {
        if (CreateDirectoryW(directory.c_str(), nullptr))
            return true;
        return GetLastError() == ERROR_ALREADY_EXISTS &&
            (GetFileAttributesW(directory.c_str()) & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }

    bool IsExactFile(const std::wstring& path, const char* expected, size_t length)
    {
        std::string content;
        return ReadFileContents(path, content) &&
            content.size() == length && std::memcmp(content.data(), expected, length) == 0;
    }

    bool CompatibleMeta(const std::string& meta)
    {
        if (meta.size() >= 2 &&
            static_cast<unsigned char>(meta[0]) == 0xFF &&
            static_cast<unsigned char>(meta[1]) == 0xFE)
            return false;
        if (meta.find('\0') != std::string::npos)
            return false;
        return meta.find("<CDataFileMgr__ContentsOfDataFileXml>") != std::string::npos &&
            meta.find("<dataFiles") != std::string::npos &&
            meta.find("</dataFiles>") != std::string::npos &&
            meta.find("</CDataFileMgr__ContentsOfDataFileXml>") != std::string::npos;
    }

    struct Registration
    {
        size_t start = std::string::npos;
        size_t end = std::string::npos;
        bool conflict = false;
        bool legacy = false;
    };

    Registration FindRegistration(const std::string& meta)
    {
        Registration result;
        const size_t dataStart = meta.find("<dataFiles");
        const size_t dataEnd = meta.find("</dataFiles>", dataStart);
        if (dataStart == std::string::npos || dataEnd == std::string::npos)
        {
            result.conflict = true;
            return result;
        }

        size_t cursor = dataStart;
        while ((cursor = meta.find("<Item>", cursor)) != std::string::npos && cursor < dataEnd)
        {
            const size_t end = meta.find("</Item>", cursor);
            if (end == std::string::npos || end > dataEnd)
            {
                result.conflict = true;
                return result;
            }
            const size_t after = end + sizeof("</Item>") - 1;
            const std::string item = meta.substr(cursor, after - cursor);
            const bool ours = item.find(ownedFilename) != std::string::npos;
            const bool prior = item.find(legacyFilename) != std::string::npos;
            const bool bootFlow = item.find(flowRegistration) != std::string::npos;
            const bool anyBootFlow = item.find("boot_flow/boot_launcher_flow") != std::string::npos;
            if (ours || prior || anyBootFlow)
            {
                if (!bootFlow || (!ours && !prior) || result.start != std::string::npos)
                {
                    result.conflict = true;
                    return result;
                }
                result.start = cursor;
                result.end = after;
                result.legacy = prior;
            }
            cursor = after;
        }
        return result;
    }

    bool RemoveExactOwnedFile(const std::wstring& path, const char* bytes, size_t length)
    {
        if (!Exists(path))
            return true;
        if (!IsExactFile(path, bytes, length))
            return false;
        return DeleteFileW(path.c_str()) != FALSE;
    }

    Registration FindStoryRegistration(const std::string& meta)
    {
        Registration result;
        const size_t dataStart = meta.find("<dataFiles");
        const size_t dataEnd = meta.find("</dataFiles>", dataStart);
        if (dataStart == std::string::npos || dataEnd == std::string::npos)
        {
            result.conflict = true;
            return result;
        }

        size_t cursor = dataStart;
        while ((cursor = meta.find("<Item>", cursor)) != std::string::npos && cursor < dataEnd)
        {
            const size_t end = meta.find("</Item>", cursor);
            if (end == std::string::npos || end > dataEnd)
            {
                result.conflict = true;
                return result;
            }
            const size_t after = end + sizeof("</Item>") - 1;
            const std::string item = meta.substr(cursor, after - cursor);
            const bool ours = item.find(storyFilename) != std::string::npos;
            const bool target = item.find(storyFlowRegistration) != std::string::npos;
            const bool anyTarget = item.find("landing_page/flow/landing_launcher_flow") != std::string::npos;
            if (ours || anyTarget)
            {
                if (!ours || !target || result.start != std::string::npos)
                {
                    result.conflict = true;
                    return result;
                }
                result.start = cursor;
                result.end = after;
            }
            cursor = after;
        }
        return result;
    }

    Registration FindOnlineFilterRegistration(const std::string& meta)
    {
        Registration result;
        const size_t dataStart = meta.find("<dataFiles");
        const size_t dataEnd = meta.find("</dataFiles>", dataStart);
        if (dataStart == std::string::npos || dataEnd == std::string::npos)
        {
            result.conflict = true;
            return result;
        }
        size_t cursor = dataStart;
        while ((cursor = meta.find("<Item>", cursor)) != std::string::npos && cursor < dataEnd)
        {
            const size_t end = meta.find("</Item>", cursor);
            if (end == std::string::npos || end > dataEnd)
            {
                result.conflict = true;
                return result;
            }
            const size_t after = end + sizeof("</Item>") - 1;
            const std::string item = meta.substr(cursor, after - cursor);
            const bool ours = item.find(onlineFilterFilename) != std::string::npos;
            const bool target = item.find(onlineFilterTarget) != std::string::npos;
            if (ours)
            {
                if (!ours || !target || result.start != std::string::npos)
                {
                    result.conflict = true;
                    return result;
                }
                result.start = cursor;
                result.end = after;
            }
            cursor = after;
        }
        return result;
    }

    bool IsExactRegistration(const std::string& meta, const Registration& registration,
        const char* expected, size_t size)
    {
        if (registration.start == std::string::npos)
            return true;
        const std::string reference(expected, size);
        const size_t start = reference.find("<Item>");
        const size_t finish = reference.find("</Item>", start);
        if (start == std::string::npos || finish == std::string::npos)
            return false;
        const size_t length = finish + sizeof("</Item>") - 1 - start;
        return meta.compare(registration.start, registration.end - registration.start,
            reference, start, length) == 0;
    }

    void RemoveRegistration(std::string& meta, const Registration& registration)
    {
        if (registration.start == std::string::npos)
            return;
        size_t after = registration.end;
        if (after < meta.size() && meta[after] == '\r')
            ++after;
        if (after < meta.size() && meta[after] == '\n')
            ++after;
        size_t begin = registration.start;
        const size_t lastLine = meta.rfind('\n', begin);
        const size_t lineStart = lastLine == std::string::npos ? 0 : lastLine + 1;
        if (meta.find_first_not_of(" \t", lineStart) == begin)
            begin = lineStart;
        meta.erase(begin, after - begin);
    }

    void InsertRegistration(std::string& meta, const char* registration)
    {
        size_t insertion = meta.find("</dataFiles>");
        const size_t lastLine = meta.rfind('\n', insertion);
        const size_t lineStart = lastLine == std::string::npos ? 0 : lastLine + 1;
        if (meta.find_first_not_of(" \t", lineStart) == insertion)
            insertion = lineStart;
        meta.insert(insertion, registration);
    }

    bool MigrateLegacyStartupResources(const std::wstring& root, const std::wstring& metaPath,
        std::string& original, const char* legacyPrefix, const wchar_t* legacyDirectoryName,
        const char* legacyMarkerValue)
    {
        constexpr char currentPrefix[] = "BettermentImprovements_data";
        if (original.find(legacyPrefix) == std::string::npos)
            return true;

        std::string migrated = original;
        bool legalChanged = false;
        bool storyChanged = false;
        bool onlineChanged = false;
        bool bootChanged = false;

        const auto legacyVersion = [&](const char* current)
        {
            std::string prior(current);
            size_t index = 0;
            while ((index = prior.find(currentPrefix, index)) != std::string::npos)
            {
                prior.replace(index, sizeof(currentPrefix) - 1, legacyPrefix);
                index += std::strlen(legacyPrefix);
            }
            return prior;
        };

        const std::string oldMeta = legacyVersion(legacyStartupMeta);
        if (original == oldMeta)
        {
            migrated.assign(legacyStartupMeta, sizeof(legacyStartupMeta) - 1);
            bootChanged = true;
        }
        else
        {
            const auto changeRegistration = [&](const char* registration, bool& changed)
            {
                const std::string prior = legacyVersion(registration);
                const size_t index = migrated.find(prior);
                if (index == std::string::npos)
                    return true;
                if (migrated.find(prior, index + prior.size()) != std::string::npos)
                    return false;
                migrated.replace(index, prior.size(), registration);
                changed = true;
                return true;
            };
            if (!changeRegistration(ownedRegistration, legalChanged) ||
                !changeRegistration(storyRegistration, storyChanged) ||
                !changeRegistration(onlineFilterRegistration, onlineChanged))
            {
                Logger::Log("[WARNING] Startup: duplicate old registrations; migration cancelled.");
                return false;
            }
        }
        if (migrated.find(legacyPrefix) != std::string::npos || migrated == original)
        {
            Logger::Log("[WARNING] Startup: unknown old asset registration; migration cancelled.");
            return false;
        }

        const std::wstring legacyDirectory = root + L"x64\\" + legacyDirectoryName;
        const std::wstring currentDirectory = root + L"x64\\BettermentImprovements_data";
        const std::wstring legacyMarker = legacyDirectory + L"\\startup_meta_created.flag";
        const std::wstring currentMarker = currentDirectory + L"\\startup_meta_created.flag";
        const bool markerPresent = Exists(legacyMarker);
        if (markerPresent && !IsExactFile(legacyMarker, legacyMarkerValue, std::strlen(legacyMarkerValue)))
        {
            Logger::Log("[WARNING] Startup: old metadata ownership differs; migration cancelled.");
            return false;
        }
        if (!CreateOwnedDirectory(currentDirectory))
        {
            Logger::Log("[WARNING] Startup: cannot create renamed asset directory; migration cancelled.");
            return false;
        }

        const auto copyOwned = [&](bool needed, const wchar_t* name, const char* bytes, size_t length)
        {
            if (!needed)
                return true;
            const std::wstring oldPath = legacyDirectory + L"\\" + name;
            const std::wstring newPath = currentDirectory + L"\\" + name;
            if (!IsExactFile(oldPath, bytes, length))
                return false;
            if (Exists(newPath))
                return IsExactFile(newPath, bytes, length);
            return AtomicWrite(newPath, std::string(bytes, length));
        };

        if (!copyOwned(legalChanged, L"legal_screen_patch.ymt", legalScreenYmt, sizeof(legalScreenYmt) - 1) ||
            !copyOwned(storyChanged, L"straight_to_story_mode.ymt", storyModeYmt, sizeof(storyModeYmt) - 1) ||
            !copyOwned(onlineChanged, L"remove_online_tab.ymt", onlineFilterYmt, sizeof(onlineFilterYmt) - 1) ||
            !copyOwned(bootChanged, L"boot_launcher_flow.ymt", legacyBootYmt, sizeof(legacyBootYmt) - 1) ||
            (markerPresent && ((Exists(currentMarker) && !IsExactFile(currentMarker, "BettermentImprovements\n", sizeof("BettermentImprovements\n") - 1)) ||
                (!Exists(currentMarker) && !AtomicWrite(currentMarker, "BettermentImprovements\n")))))
        {
            Logger::Log("[WARNING] Startup: old assets could not be verified or copied; migration cancelled.");
            return false;
        }

        const std::wstring backup = metaPath + L".BettermentImprovements.bak";
        if ((!Exists(backup) && !CopyFileW(metaPath.c_str(), backup.c_str(), TRUE)) ||
            !AtomicWrite(metaPath, migrated))
        {
            Logger::Log("[WARNING] Startup: could not preserve or migrate startup.meta; old registration retained.");
            return false;
        }
        original.swap(migrated);

        if (legalChanged)
            RemoveExactOwnedFile(legacyDirectory + L"\\legal_screen_patch.ymt", legalScreenYmt, sizeof(legalScreenYmt) - 1);
        if (storyChanged)
            RemoveExactOwnedFile(legacyDirectory + L"\\straight_to_story_mode.ymt", storyModeYmt, sizeof(storyModeYmt) - 1);
        if (onlineChanged)
            RemoveExactOwnedFile(legacyDirectory + L"\\remove_online_tab.ymt", onlineFilterYmt, sizeof(onlineFilterYmt) - 1);
        if (bootChanged)
            RemoveExactOwnedFile(legacyDirectory + L"\\boot_launcher_flow.ymt", legacyBootYmt, sizeof(legacyBootYmt) - 1);
        if (markerPresent)
            RemoveExactOwnedFile(legacyMarker, legacyMarkerValue, std::strlen(legacyMarkerValue));
        RemoveDirectoryW(legacyDirectory.c_str());
        Logger::Log("[SUCCESS] Startup: YMT registrations moved to BettermentImprovements_data.");
        return true;
    }

    void ApplyStartupFilePatches(bool skipLegal, bool straightToStoryMode)
    {
        const std::wstring root = GameRoot();
        if (root.empty())
        {
            Logger::Log("[ERROR] Startup: could not resolve the game directory.");
            return;
        }

        const std::wstring directory = root + L"x64\\BettermentImprovements_data";
        const std::wstring metaPath = root + L"x64\\data\\startup.meta";
        const std::wstring legalPath = directory + L"\\legal_screen_patch.ymt";
        const std::wstring storyPath = directory + L"\\straight_to_story_mode.ymt";
        const std::wstring onlineFilterPath = directory + L"\\remove_online_tab.ymt";
        const std::wstring legacyPath = directory + L"\\boot_launcher_flow.ymt";
        const std::wstring createdMarker = directory + L"\\startup_meta_created.flag";

        const bool metaExists = Exists(metaPath);
        std::string original;
        if (metaExists && (!ReadFileContents(metaPath, original) || !CompatibleMeta(original)))
        {
            Logger::Log("[ERROR] Startup: startup.meta is unreadable or incompatible; no files changed.");
            return;
        }

        if (metaExists &&
            (!MigrateLegacyStartupResources(root, metaPath, original, "GameplayFixesRDR2_data",
                L"GameplayFixesRDR2_data", "GameplayFixesRDR2\n") ||
             !MigrateLegacyStartupResources(root, metaPath, original, "FusionFixRDR2_data",
                L"FusionFixRDR2_data", "FusionFixRDR2\n")))
            return;

        const bool legacyMeta = metaExists && original == std::string(legacyStartupMeta, sizeof(legacyStartupMeta) - 1);
        const std::string baseline = (legacyMeta || !metaExists) ?
            std::string(defaultStartupMeta, sizeof(defaultStartupMeta) - 1) : original;
        const Registration legal = FindRegistration(baseline);
        const Registration story = FindStoryRegistration(baseline);
        const Registration online = FindOnlineFilterRegistration(baseline);
        if (legal.conflict || story.conflict || online.conflict)
        {
            Logger::Log("[WARNING] Startup: conflicting boot or landing registration; no files changed.");
            return;
        }
        if ((!legal.legacy && !IsExactRegistration(baseline, legal, ownedRegistration, sizeof(ownedRegistration) - 1)) ||
            !IsExactRegistration(baseline, story, storyRegistration, sizeof(storyRegistration) - 1) ||
            !IsExactRegistration(baseline, online, onlineFilterRegistration, sizeof(onlineFilterRegistration) - 1))
        {
            Logger::Log("[WARNING] Startup: mod-owned registration changed; no files changed.");
            return;
        }
        if ((skipLegal && Exists(legalPath) && !IsExactFile(legalPath, legalScreenYmt, sizeof(legalScreenYmt) - 1)) ||
            (straightToStoryMode && Exists(storyPath) && !IsExactFile(storyPath, storyModeYmt, sizeof(storyModeYmt) - 1)) ||
            ((legacyMeta || legal.legacy) && Exists(legacyPath) && !IsExactFile(legacyPath, legacyBootYmt, sizeof(legacyBootYmt) - 1)))
        {
            Logger::Log("[WARNING] Startup: YMT differs from expected version; no files changed.");
            return;
        }

        std::string updated = baseline;
        if (legal.start != std::string::npos && (legal.legacy || !skipLegal))
        {
            RemoveRegistration(updated, legal);
        }
        if (!straightToStoryMode && story.start != std::string::npos)
        {
            const Registration current = FindStoryRegistration(updated);
            RemoveRegistration(updated, current);
        }
        if (online.start != std::string::npos)
            RemoveRegistration(updated, FindOnlineFilterRegistration(updated));
        if (skipLegal && (legal.start == std::string::npos || legal.legacy))
            InsertRegistration(updated, ownedRegistration);
        if (straightToStoryMode && story.start == std::string::npos)
            InsertRegistration(updated, storyRegistration);

        const bool onlineChanged = online.start != std::string::npos ||
            IsExactFile(onlineFilterPath, onlineFilterYmt, sizeof(onlineFilterYmt) - 1);
        const bool legalNew = skipLegal && !Exists(legalPath);
        const bool storyNew = straightToStoryMode && !Exists(storyPath);
        const bool markerPresent = IsExactFile(createdMarker, "BettermentImprovements\n", sizeof("BettermentImprovements\n") - 1);
        const std::string defaultMeta(defaultStartupMeta, sizeof(defaultStartupMeta) - 1);
        std::string previousInstallerMeta = defaultMeta;
        const size_t closingTag = previousInstallerMeta.find(" </dataFiles>");
        if (closingTag != std::string::npos)
            previousInstallerMeta.erase(closingTag, 1);
        const bool removeMeta = metaExists && !skipLegal && !straightToStoryMode &&
            (markerPresent || legacyMeta) &&
            (updated == defaultMeta || updated == previousInstallerMeta);
        const bool metadataChanged = (metaExists && (original != updated || removeMeta)) ||
            (!metaExists && (skipLegal || straightToStoryMode));
        const bool legalChanged = legalNew || (skipLegal != (legal.start != std::string::npos && !legal.legacy)) ||
            legacyMeta || legal.legacy;
        const bool storyChanged = storyNew || (straightToStoryMode != (story.start != std::string::npos));
        const bool needsDirectory = legalNew || storyNew || (!metaExists && (skipLegal || straightToStoryMode));
        if (!metaExists && (skipLegal || straightToStoryMode) && Exists(createdMarker) && !markerPresent)
        {
            Logger::Log("[WARNING] Startup: unknown metadata ownership marker; no files changed.");
            return;
        }

        if (needsDirectory && !CreateOwnedDirectory(directory))
        {
            Logger::Log("[ERROR] Startup: could not create the mod-owned data directory.");
            return;
        }
        if (legalNew && !AtomicWrite(legalPath, std::string(legalScreenYmt, sizeof(legalScreenYmt) - 1)))
        {
            Logger::Log("[ERROR] SkipLegal: could not install legal-screen YMT.");
            return;
        }
        if (storyNew && !AtomicWrite(storyPath, std::string(storyModeYmt, sizeof(storyModeYmt) - 1)))
        {
            Logger::Log("[ERROR] StraightToStoryMode: could not install Story Mode flow YMT.");
            if (legalNew)
                RemoveExactOwnedFile(legalPath, legalScreenYmt, sizeof(legalScreenYmt) - 1);
            return;
        }
        if (metadataChanged && metaExists && !Exists(metaPath + L".BettermentImprovements.bak") &&
            !CopyFileW(metaPath.c_str(), (metaPath + L".BettermentImprovements.bak").c_str(), TRUE))
        {
            Logger::Log("[ERROR] Startup: could not back up startup.meta; installation cancelled.");
            if (storyNew)
                RemoveExactOwnedFile(storyPath, storyModeYmt, sizeof(storyModeYmt) - 1);
            if (legalNew)
                RemoveExactOwnedFile(legalPath, legalScreenYmt, sizeof(legalScreenYmt) - 1);
            return;
        }

        bool markerNew = false;
        if (!metaExists && (skipLegal || straightToStoryMode) && !markerPresent)
        {
            if (!CreateOwnedDirectory(directory) ||
                !AtomicWrite(createdMarker, "BettermentImprovements\n"))
            {
                Logger::Log("[ERROR] Startup: could not record metadata ownership; installation cancelled.");
                if (storyNew)
                    RemoveExactOwnedFile(storyPath, storyModeYmt, sizeof(storyModeYmt) - 1);
                if (legalNew)
                    RemoveExactOwnedFile(legalPath, legalScreenYmt, sizeof(legalScreenYmt) - 1);
                return;
            }
            markerNew = true;
        }
        if (metadataChanged)
        {
            const bool success = removeMeta ? DeleteFileW(metaPath.c_str()) != FALSE :
                AtomicWrite(metaPath, updated);
            if (!success)
            {
                Logger::Log("[ERROR] Startup: could not update startup.meta; changes cancelled.");
                if (markerNew)
                    RemoveExactOwnedFile(createdMarker, "BettermentImprovements\n", sizeof("BettermentImprovements\n") - 1);
                if (storyNew)
                    RemoveExactOwnedFile(storyPath, storyModeYmt, sizeof(storyModeYmt) - 1);
                if (legalNew)
                    RemoveExactOwnedFile(legalPath, legalScreenYmt, sizeof(legalScreenYmt) - 1);
                return;
            }
        }
        if (!RemoveExactOwnedFile(onlineFilterPath, onlineFilterYmt, sizeof(onlineFilterYmt) - 1))
            Logger::Log("[WARNING] Startup: unknown or locked landing filter left untouched.");
        if (!skipLegal && !RemoveExactOwnedFile(legalPath, legalScreenYmt, sizeof(legalScreenYmt) - 1))
            Logger::Log("[WARNING] SkipLegal: custom or locked YMT left untouched.");
        if (!straightToStoryMode && !RemoveExactOwnedFile(storyPath, storyModeYmt, sizeof(storyModeYmt) - 1))
            Logger::Log("[WARNING] StraightToStoryMode: custom or locked YMT left untouched.");
        if (legacyMeta || legal.legacy)
        {
            if (!RemoveExactOwnedFile(legacyPath, legacyBootYmt, sizeof(legacyBootYmt) - 1))
                Logger::Log("[WARNING] SkipLegal: unknown or locked old YMT left untouched.");
        }
        if ((removeMeta || (!metaExists && !skipLegal && !straightToStoryMode)) && !Exists(metaPath))
            RemoveExactOwnedFile(createdMarker, "BettermentImprovements\n", sizeof("BettermentImprovements\n") - 1);
        if (!skipLegal && !straightToStoryMode)
            RemoveDirectoryW(directory.c_str());

        Logger::Log(skipLegal ?
            (legalChanged ? "[SUCCESS] SkipLegal: startup flow installed and active." :
                "[INFO] SkipLegal: startup flow already installed.") :
            (legalChanged ? "[SUCCESS] SkipLegal: startup flow removed; original legal screens restored." :
                "[INFO] SkipLegal: disabled; original legal screens unchanged."));
        Logger::Log(straightToStoryMode ?
            (storyChanged ? "[SUCCESS] StraightToStoryMode: startup flow installed." :
                "[INFO] StraightToStoryMode: startup flow already installed.") :
            (storyChanged ? "[SUCCESS] StraightToStoryMode: startup flow removed." :
                "[INFO] StraightToStoryMode: disabled; original landing menu unchanged."));
        if (onlineChanged)
            Logger::Log("[INFO] Startup: old landing filter checked and cleaned up.");
    }

}

namespace Main::SkipIntro
{
    void Initialize(bool skipIntroSplashes, bool skipLegal, bool straightToStoryMode)
    {
        ApplyStartupFilePatches(skipLegal, straightToStoryMode);
        if (skipIntroSplashes)
        {
            InstallSplashHook();
            StartStartupHeartbeat();
        }
        else
            Logger::Log("[INFO] SkipIntroSplashes: disabled; original video playback unchanged.");
    }

    void FinishStartup()
    {
        startupHeartbeatEnabled.store(false, std::memory_order_release);
        LogStartupHeartbeat("ScriptMain startup summary");
        RemoveSplashHook();
    }
}
