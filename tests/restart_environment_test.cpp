#include "restart_environment.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>

static void check(bool ok,const char* message) {
    if(!ok){std::fprintf(stderr,"%s\n",message);std::exit(1);}
}
static void set(const char* name,const char* value) {
#if defined(_WIN32)
    check(SetEnvironmentVariableA(name,value),"set test environment");
#else
    check(setenv(name,value,1)==0,"set test environment");
#endif
}
static std::string get(const char* name) {
#if defined(_WIN32)
    char value[4096];const auto n=GetEnvironmentVariableA(name,value,sizeof value);
    return n && n<sizeof value?std::string(value,n):std::string();
#else
    const auto value=std::getenv(name);return value?value:"";
#endif
}
static constexpr const char* removed[]={"NX_TEST_QUIT_FILE","NX_LOG","NX_LAN_MODE","NX_NATIVE_LAN",
    "NX_STEAM_DEV","SteamAppId","SteamGameId","NX_INPUT_SCRIPT_P2","NX_LAUNCHER_TOUR",
    "NX_PROBE_PLAYER","NX_AUDIO_DUMP","NX_TRACE_SHADOWS","NX_HEADLESS","NX_NO_LAUNCHER",
    "NX_FRAMETRACE","NX_DUMP_PRESENTED","NX_LAN_TRACE_PHASES","NX_TEST_QUIT_FRAME","NX_HEADLESS_PRESENT"};
static constexpr const char* kept[]={"NX_DATA_ROOT","NX_GAMELIB","NX_RESOLUTION","NX_VSYNC",
    "NX_LANDED","NX_INPUTTING","NX_LOGBOOK","NX_STEAMSHIP","RESTART_HOST_VALUE","NX_REPLAY_THREAD"};
int main(int argc,char** argv) {
    if(argc>1 && std::string_view(argv[1])=="--child") {
        bool ok=argc==4 && std::string_view(argv[3])=="argument with spaces";
        for(auto name:removed)ok=ok && get(name).empty();
        for(auto name:kept)ok=ok && get(name)=="preserved";
#if defined(_WIN32)
        wchar_t unicode[128];
        const auto n=GetEnvironmentVariableW(L"RESTART_UNICODE_VALUE",unicode,128);
        ok=ok && n && n<128 && std::wstring_view(unicode)==L"C:\\\u00dcber\\\u4e16\u754c";
#endif
        // Publish last, so the parent never mistakes a partial write for success.
        const auto result=std::filesystem::path(argv[2]);
        const auto temp=result.string()+".tmp";
        {std::ofstream out(temp);out<<(ok?"pass":"fail");}
        std::filesystem::rename(temp,result);
        return ok?0:1;
    }
    for(auto name:removed)set(name,"old-session");
    for(auto name:kept)set(name,"preserved");
#if defined(_WIN32)
    set("nx_lan_mode","old-session");
    check(SetEnvironmentVariableW(L"RESTART_UNICODE_VALUE",L"C:\\\u00dcber\\\u4e16\u754c"),"set Unicode environment");
    check(nx::restart::transient<wchar_t>(L"nX_tEsT_QUIT_FRAME=0"),"Windows names ignore case");
    check(!nx::restart::transient<wchar_t>(L"=C:=C:\\working"),"preserve special drive entries");
    check(nx::restart::block({}).size()==2,"empty environment is double NUL terminated");
    const auto unicode=nx::restart::block({L"NX_DATA_ROOT=C:\\\u00dcber\\\u4e16\u754c"});
    check(std::wstring(unicode.data())==L"NX_DATA_ROOT=C:\\\u00dcber\\\u4e16\u754c","preserve Unicode preferences");
    const auto block=nx::restart::block({L"Z=last",L"=C:=C:\\working",L"a=first",L"NX_LOG=old"});
    check(block==std::vector<wchar_t>({'=','C',':','=','C',':','\\','w','o','r','k','i','n','g',0,'a','=','f','i','r','s','t',0,'Z','=','l','a','s','t',0,0}),"sorted explicit block preserves special variables and filters logs");
#endif
    const auto result=std::filesystem::temp_directory_path()/
        ("restart child "+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".txt");
    const auto executable=std::filesystem::absolute(argv[0]);
#if defined(_WIN32)
    check(!nx::restart::spawn(result.wstring()+L".missing.exe",L"missing"),"failed spawn is reported");
#endif
#if defined(_WIN32)
    check(nx::restart::spawn(executable.wstring(),L"\""+executable.wstring()+L"\" --child \""+result.wstring()+L"\" \"argument with spaces\""),"spawn real child");
#else
    check(nx::restart::spawn(executable.string(),{executable.string(),"--child",result.string(),"argument with spaces"}),"spawn real child");
#endif
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(!std::filesystem::exists(result) && std::chrono::steady_clock::now()<deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    std::string status;{std::ifstream in(result);in>>status;}
    std::filesystem::remove(result);
    check(status=="pass","real child receives clean environment and exact arguments");
    for(auto name:removed)check(get(name)=="old-session","parent session environment unchanged");
    for(auto name:kept)check(get(name)=="preserved","parent preferences unchanged");
}
