#pragma once
#include <filesystem>
#include <string>
#if defined(_WIN32)
#include <windows.h>
namespace nx {
// Process-lifetime guard: a launcher must not restore saves used by another instance.
class UserDataLock {
    HANDLE handle_=INVALID_HANDLE_VALUE;
public:
    UserDataLock()=default;
    UserDataLock(const UserDataLock&)=delete;
    UserDataLock& operator=(const UserDataLock&)=delete;
    ~UserDataLock(){if(handle_!=INVALID_HANDLE_VALUE)CloseHandle(handle_);}
    bool acquire(const std::filesystem::path& data,std::string& error) {
        if(handle_!=INVALID_HANDLE_VALUE)return true;
        handle_=CreateFileW((data/"user-data.lock").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(handle_!=INVALID_HANDLE_VALUE)return true;
        const auto code=GetLastError();
        error=code==ERROR_SHARING_VIOLATION ? "Another SuperWonders instance is using this data folder. Close it before managing profiles or saves."
            : "Could not protect this data folder (Windows error "+std::to_string(code)+"). Check its permissions.";
        return false;
    }
};
}
#endif
