#pragma once
#include <string>
#include <string_view>
#include <unordered_set>
#include <cstdint>

namespace nx::wonder {
// Audio streaming can reopen the current track a different number of times on
// each PC. It is not another scene transition and must not create another LAN
// barrier. A real scene load or death permits the same track to synchronize again.
class LoadingMusicGate {
    std::unordered_set<std::string> seen;
    unsigned category=0;
    uint64_t reopens=0;
public:
    uint64_t suppressed() const {return reopens;}
    bool observe(std::string_view path) {
        if (path.find("/Stage/StageLoadInfo/") != path.npos ||
            path.find("/Sound/Resource/Stream/BGM_Jingle_PlayerDown") != path.npos) {
            seen.clear();
            category=0;
            return false;
        }
        constexpr std::string_view prefix="/Sound/Resource/Stream/";
        const auto at=path.find(prefix);
        if(at==path.npos) return false;
        const auto stream=path.substr(at+prefix.size());
        if(!stream.starts_with("BGM_Title") && !stream.starts_with("BGM_World_") && !stream.starts_with("BGM_Course")) return false;
        const unsigned next=stream.starts_with("BGM_Title")?1:stream.starts_with("BGM_World_")?2:3;
        if(category!=next){seen.clear();category=next;}
        if(!seen.emplace(path).second){++reopens;return false;}
        return true;
    }
};
}
