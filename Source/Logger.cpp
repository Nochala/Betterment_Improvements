#include "../Headers/Logger.h"
#include "../Main/Headers/NotifStats.h"
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cwchar>
#include <cstring>
#include <string>

namespace
{
    std::atomic<bool> loggingEnabled{true};
    std::atomic<bool> debugEnabled{false};
    std::atomic<DWORD> logWriteError{ERROR_SUCCESS};
    std::atomic<DWORD> pendingLogNotification{ERROR_SUCCESS};
    constexpr wchar_t logFilename[] = L"BettermentImprovements\\BettermentImprovements.log";

    bool BuildGamePath(const wchar_t* filename, wchar_t (&path)[MAX_PATH])
    {
        const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
        if (length == 0)
            return false;
        if (length >= MAX_PATH)
        {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return false;
        }

        DWORD directoryEnd = length;
        while (directoryEnd > 0 && path[directoryEnd - 1] != L'\\' && path[directoryEnd - 1] != L'/')
            --directoryEnd;
        if (directoryEnd == 0)
        {
            SetLastError(ERROR_BAD_PATHNAME);
            return false;
        }

        DWORD position = directoryEnd;
        for (DWORD i = 0; filename[i] != L'\0'; ++i)
        {
            if (position + 1 >= MAX_PATH)
            {
                SetLastError(ERROR_INSUFFICIENT_BUFFER);
                return false;
            }
            path[position++] = filename[i];
        }
        path[position] = L'\0';
        return true;
    }

    bool BuildIniReadPath(wchar_t (&path)[MAX_PATH])
    {
        constexpr const wchar_t* candidates[] = {
            L"BettermentImprovements\\BettermentImprovements.ini",
            L"BettermentImprovements.ini"
        };
        for (const auto* candidate : candidates)
        {
            if (!BuildGamePath(candidate, path))
                return false;
            const DWORD attributes = GetFileAttributesW(path);
            if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY))
                return true;
        }
        return BuildGamePath(candidates[0], path);
    }

    bool EnsureDataDirectory()
    {
        wchar_t path[MAX_PATH] = {};
        if (!BuildGamePath(L"BettermentImprovements", path))
            return false;
        if (CreateDirectoryW(path, nullptr))
            return true;
        if (GetLastError() != ERROR_ALREADY_EXISTS)
            return false;
        const DWORD attributes = GetFileAttributesW(path);
        if (attributes == INVALID_FILE_ATTRIBUTES)
            return false;
        if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            return true;
        SetLastError(ERROR_DIRECTORY);
        return false;
    }

    bool IsFalse(const wchar_t* value)
    {
        const wchar_t expected[] = L"false";
        for (unsigned int i = 0; i < 5; ++i)
        {
            const wchar_t character = value[i];
            if (character != expected[i] && character != expected[i] - (L'a' - L'A'))
                return false;
        }
        return value[5] == L'\0';
    }

    std::string FormatLine(const char* message)
    {
        SYSTEMTIME time = {};
        GetLocalTime(&time);
        char prefix[64] = {};
        std::snprintf(prefix, sizeof(prefix), "%04u-%02u-%02u %02u:%02u:%02u.%03u ",
            static_cast<unsigned int>(time.wYear), static_cast<unsigned int>(time.wMonth),
            static_cast<unsigned int>(time.wDay), static_cast<unsigned int>(time.wHour),
            static_cast<unsigned int>(time.wMinute), static_cast<unsigned int>(time.wSecond),
            static_cast<unsigned int>(time.wMilliseconds));
        return std::string(prefix) + message + "\r\n";
    }

    bool Append(const wchar_t* path, const char* content, DWORD length, DWORD& error, bool reset = false)
    {
        HANDLE file = CreateFileW(path, reset ? GENERIC_WRITE : FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, reset ? CREATE_ALWAYS : OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            error = GetLastError();
            return false;
        }

        DWORD written = 0;
        const BOOL wrote = WriteFile(file, content, length, &written, nullptr);
        error = !wrote ? GetLastError() : written == length ? ERROR_SUCCESS : ERROR_WRITE_FAULT;
        CloseHandle(file);
        return error == ERROR_SUCCESS;
    }

    std::string Utf8(const wchar_t* text)
    {
        if (!text || !*text)
            return {};
        const int length = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
        if (length == 0)
            return {};
        std::string result(length, '\0');
        WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), length, nullptr, nullptr);
        result.pop_back();
        return result;
    }

    std::string WriteErrorText(DWORD error)
    {
        std::string detail = "Win32 error " + std::to_string(error);
        switch (error)
        {
        case ERROR_ACCESS_DENIED: return detail + ": access denied";
        case ERROR_WRITE_PROTECT: return detail + ": write protected";
        case ERROR_SHARING_VIOLATION: return detail + ": file in use";
        case ERROR_PATH_NOT_FOUND: return detail + ": folder not found";
        case ERROR_DIRECTORY: return detail + ": invalid directory";
        case ERROR_DISK_FULL: return detail + ": disk full";
        default: return detail;
        }
    }

    void RecordLogWriteFailure(const wchar_t* path, DWORD error)
    {
        if (error == ERROR_SUCCESS)
            error = ERROR_WRITE_FAULT;

        DWORD expected = ERROR_SUCCESS;
        if (logWriteError.compare_exchange_strong(expected, error))
        {
            // DllMain can reach this path before the game can show notifications.
            pendingLogNotification.store(error);
            wchar_t message[MAX_PATH + 128] = {};
            swprintf_s(message, L"[BettermentImprovements] Cannot write log \"%ls\" (Win32 error %lu).\n", path, error);
            OutputDebugStringW(message);
        }
    }

    void WriteLogLine(const char* message)
    {
        if (!loggingEnabled)
            return;

        wchar_t path[MAX_PATH] = {};
        if (!BuildGamePath(logFilename, path))
        {
            RecordLogWriteFailure(logFilename, GetLastError());
            OutputDebugStringA(message);
            return;
        }

        const DWORD previousError = logWriteError.load();
        std::string line;
        if (previousError != ERROR_SUCCESS)
        {
            line = FormatLine(("[ERROR] Log write previously failed for \"" + Utf8(path) +
                "\" (" + WriteErrorText(previousError) + ").").c_str());
        }
        line += FormatLine(message);
        DWORD error = ERROR_SUCCESS;
        if (Append(path, line.c_str(), static_cast<DWORD>(line.size()), error))
        {
            if (previousError != ERROR_SUCCESS)
            {
                DWORD expected = previousError;
                logWriteError.compare_exchange_strong(expected, ERROR_SUCCESS);
            }
        }
        else
        {
            RecordLogWriteFailure(path, error);
            OutputDebugStringA(message);
        }
    }
}

namespace Logger
{
    void LogModuleLoaded()
    {
        wchar_t iniPath[MAX_PATH] = {};
        if (BuildIniReadPath(iniPath))
        {
            wchar_t value[16] = {};
            GetPrivateProfileStringW(L"General", L"Logging", L"true", value, 16, iniPath);
            if (IsFalse(value))
            {
                loggingEnabled = false;
                return;
            }
        }

        wchar_t logPath[MAX_PATH] = {};
        if (!BuildGamePath(logFilename, logPath))
        {
            RecordLogWriteFailure(logFilename, GetLastError());
            return;
        }
        if (!EnsureDataDirectory())
        {
            RecordLogWriteFailure(logPath, GetLastError());
            return;
        }

        constexpr char message[] = "[BOOT] BettermentImprovements loaded. Waiting for ScriptMain.\r\n";
        DWORD error = ERROR_SUCCESS;
        if (!Append(logPath, message, sizeof(message) - 1, error, true))
            RecordLogWriteFailure(logPath, error);
    }

    void LogScriptEntry()
    {
        wchar_t iniPath[MAX_PATH] = {};
        if (BuildIniReadPath(iniPath))
        {
            wchar_t value[16] = {};
            GetPrivateProfileStringW(L"General", L"Logging", L"true", value, 16, iniPath);
            if (IsFalse(value))
            {
                loggingEnabled = false;
                return;
            }
        }

        loggingEnabled = true;
        Log("[BOOT] ScriptMain started.");
    }

    void SetEnabled(bool enabled)
    {
        loggingEnabled = enabled;
    }

    void SetDebugEnabled(bool enabled)
    {
        debugEnabled = enabled;
    }

    bool IsDebugEnabled()
    {
        return loggingEnabled && debugEnabled;
    }

    void Debug(const char* message)
    {
        if (!IsDebugEnabled() || message == nullptr)
            return;

        const std::string line = std::string("[DEBUG] ") + message;
        Log(line.c_str());
    }

    void Log(const char* message)
    {
        if (message == nullptr)
            return;
        if (std::strncmp(message, "[ERROR] ", 8) == 0)
            Main::NotifStats::Warning(message + 8);
        else if (std::strncmp(message, "[WARNING] ", 10) == 0 &&
            (std::strstr(message, "patch skipped") ||
                std::strstr(message, "hook skipped") ||
                std::strstr(message, "patch could not") ||
                std::strstr(message, "could not be installed") ||
                std::strstr(message, "feature disabled") ||
                std::strstr(message, "no changes applied") ||
                std::strstr(message, "no changes made")))
            Main::NotifStats::Warning(message + 10);
        WriteLogLine(message);
    }

    void NotifyLogWriteFailure()
    {
        const DWORD error = pendingLogNotification.exchange(ERROR_SUCCESS);
        if (error != ERROR_SUCCESS)
            Main::NotifStats::Warning("Cannot write BettermentImprovements.log (" + WriteErrorText(error) + ").");
    }

    void FileWriteFailure(const wchar_t* path, const char* operation, unsigned long error)
    {
        const wchar_t* name = std::wcsrchr(path, L'\\');
        const std::string filename = Utf8(name ? name + 1 : path);
        const std::string detail = WriteErrorText(error == ERROR_SUCCESS ? ERROR_WRITE_FAULT : error);
        Main::NotifStats::Warning("Cannot write " + filename + " (" + detail + ").");
        WriteLogLine(("[ERROR] Could not " + std::string(operation) + " in \"" + Utf8(path) +
            "\" (" + detail + ").").c_str());
    }

    bool WriteProfileValue(const wchar_t* section, const wchar_t* key, const wchar_t* value, const wchar_t* path)
    {
        SetLastError(ERROR_SUCCESS);
        if (WritePrivateProfileStringW(section, key, value, path))
            return true;

        const DWORD error = GetLastError();
        std::string operation = value ? "write [" : "remove [";
        operation += Utf8(section) + "]";
        if (key)
            operation += " " + Utf8(key);
        FileWriteFailure(path, operation.c_str(), error);
        return false;
    }

    void LogStartup()
    {
        Log("[INFO] ScriptMain: initializing gameplay features.");
    }
}
