#pragma once

namespace Logger
{
    void LogModuleLoaded();
    void LogScriptEntry();
    void SetEnabled(bool enabled);
    void SetDebugEnabled(bool enabled);
    bool IsDebugEnabled();
    void Debug(const char* message);
    void LogStartup();
    void Log(const char* message);
    void NotifyLogWriteFailure();
    void FileWriteFailure(const wchar_t* path, const char* operation, unsigned long error);
    bool WriteProfileValue(const wchar_t* section, const wchar_t* key, const wchar_t* value, const wchar_t* path);
}
