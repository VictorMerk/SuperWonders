#pragma once

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
extern char** environ;
#endif

namespace nx::restart {
// Restarting a launcher is a new session. Keep data/render preferences, but
// never inherit the old room, automation controls or diagnostic output sinks.
template<class Char> bool transient(std::basic_string_view<Char> entry) {
    std::string name;
    for(auto ch:entry) {
        if(ch=='=')break;
        if(ch>127)return false;
#if defined(_WIN32)
        if(ch>='a' && ch<='z')ch-=('a'-'A');
#endif
        name.push_back(char(ch));
    }
    const auto exact=[&](std::string_view key){
        if(name.size()!=key.size())return false;
        for(size_t i=0;i<key.size();++i) {
            auto ch=key[i];
#if defined(_WIN32)
            if(ch>='a' && ch<='z')ch-=('a'-'A');
#endif
            if(name[i]!=ch)return false;
        }
        return true;
    };
    const auto family=[&](std::string_view key){return exact(key) || (name.size()>key.size() && name.compare(0,key.size(),key)==0 && name[key.size()]=='_');};
    for(auto key:{"NX_TEST","NX_QA","NX_LAN","NX_NATIVE_LAN","NX_NATIVE_NET_TRACE","NX_STEAM",
                  "NX_INPUT","NX_PROBE","NX_TRACE","NX_SNAP","NX_SNAPSHOT","NX_CAPTURE","NX_DUMP","NX_HEADLESS"})
        if(family(key))return true;
    for(auto key:{"NX_LOG","NX_FRAMETRACE","NX_AUDIO_DUMP","NX_SCREENSHOT","NX_HEADLESS","NX_NO_LAUNCHER",
                  "NX_PLAY_SHOTS","NX_COMPARE_TWIN","NX_THREAD_DUMP_AT","NX_STDERR_UNBUFFERED",
                  "NX_OVERLAY_DEMO","NX_OVERLAY_INPUT_PROBE","NX_OVERLAY_TIMING","NX_THROW_TRACE",
                  "NX_TERRAIN_BIND_TRACE","NX_TEXTURE_UPLOAD_TRACE","NX_PIPELINE_CACHE_TRACE",
                  "NX_PIPELINE_MISS_TRACE","SteamAppId","SteamGameId"}) {
        if(exact(key))return true;
    }
    // Tours and probes have several subsystem-specific names.
    if(name.starts_with("NX_") && (name.ends_with("_TOUR") || name.find("_TOUR_")!=name.npos ||
        name.ends_with("_PROBE") || name.ends_with("_TEST") || name.ends_with("_TRACE")))return true;
    return false;
}

#if defined(_WIN32)
inline std::vector<wchar_t> block(std::vector<std::wstring> entries) {
    std::erase_if(entries,[](const auto& entry){return transient<wchar_t>(entry);});
    std::sort(entries.begin(),entries.end(),[](const auto& a,const auto& b){
        return CompareStringOrdinal(a.c_str(),-1,b.c_str(),-1,TRUE)==CSTR_LESS_THAN;
    });
    std::vector<wchar_t> result;
    for(const auto& entry:entries){result.insert(result.end(),entry.begin(),entry.end());result.push_back(0);}
    if(result.empty())result.push_back(0);
    result.push_back(0);
    return result;
}
inline bool spawn(const std::wstring& executable,std::wstring command) try {
    const auto raw=GetEnvironmentStringsW();
    if(!raw)return false;
    const auto release=[](wchar_t* value){FreeEnvironmentStringsW(value);};
    std::unique_ptr<wchar_t,decltype(release)> owned(raw,release);
    std::vector<std::wstring> entries;
    for(auto p=raw;*p;p+=wcslen(p)+1)entries.emplace_back(p);
    auto environment=block(std::move(entries));
    STARTUPINFOW startup{};startup.cb=sizeof startup;
    PROCESS_INFORMATION process{};
    if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_UNICODE_ENVIRONMENT,
                       environment.data(),nullptr,&startup,&process))return false;
    CloseHandle(process.hThread);CloseHandle(process.hProcess);
    return true;
} catch(...) {return false;}
#else
inline bool spawn(const std::string& executable,std::vector<std::string> arguments) try {
    std::vector<std::string> entries;
    for(auto p=environ;*p;++p)if(!transient<char>(*p))entries.emplace_back(*p);
    std::vector<char*> environment,argv;
    for(auto& entry:entries)environment.push_back(entry.data());
    environment.push_back(nullptr);
    for(auto& argument:arguments)argv.push_back(argument.data());
    argv.push_back(nullptr);
    const auto path=executable.c_str();
    auto args=argv.data();auto env=environment.data();
    const auto child=fork();
    if(child<0)return false;
    // All storage is prepared before fork: no allocator or environment locks in
    // the child of a multithreaded game process.
    if(child==0){execve(path,args,env);_exit(127);}
    return true;
} catch(...) {return false;}
#endif
}
