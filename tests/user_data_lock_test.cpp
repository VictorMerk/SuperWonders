#include "user_data_lock.h"
#include <chrono>
#include <cstdio>

static void check(bool ok, const char* message) {
    if(!ok) { std::fprintf(stderr,"%s\n",message); std::exit(1); }
}
static void child(const std::filesystem::path& data, bool allowed) {
    wchar_t executable[32768];
    check(GetModuleFileNameW(nullptr,executable,32768)!=0,"locate test executable");
    auto command=L"\""+std::wstring(executable)+L"\" \""+data.wstring()+L"\" "+(allowed?L"1":L"0");
    STARTUPINFOW startup{};startup.cb=sizeof startup;PROCESS_INFORMATION process{};
    check(CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process),"start independent lock contender");
    const auto wait=WaitForSingleObject(process.hProcess,5000);
    if(wait!=WAIT_OBJECT_0)TerminateProcess(process.hProcess,99);
    DWORD code=99;GetExitCodeProcess(process.hProcess,&code);
    CloseHandle(process.hThread);CloseHandle(process.hProcess);
    check(wait==WAIT_OBJECT_0 && code==0,"data-folder lock excludes another process and releases after owner exits");
}
int main(int argc,char** argv) {
    if(argc==3) {
        nx::UserDataLock lock;std::string error;
        const bool acquired=lock.acquire(std::filesystem::u8path(argv[1]),error);
        return acquired==(argv[2][0]=='1')?0:2;
    }
    const auto data=std::filesystem::temp_directory_path()/("wonder-lan-lock-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    check(std::filesystem::create_directory(data),"create unique lock fixture");
    {
        nx::UserDataLock lock;std::string error;
        check(lock.acquire(data,error),"first instance owns data folder");
        child(data,false);
    }
    child(data,true);
    std::filesystem::remove(data/"user-data.lock");std::filesystem::remove(data);
    std::puts("cross-process LAN launcher data lock passed");
}
