#pragma once
#include <string>
#include <string_view>

namespace nx::wonder {
// Audio streaming can reopen the current track a different number of times on
// each PC. It is not another scene transition and must not create another LAN
// barrier. A real scene load or death permits the same track to synchronize again.
class LoadingMusicGate {
    std::string previous;
public:
    bool observe(std::string_view path) {
        if (path.find("/Stage/StageLoadInfo/") != path.npos ||
            path.find("/Sound/Resource/Stream/BGM_Jingle_PlayerDown") != path.npos) {
            previous.clear();
            return false;
        }
        constexpr std::string_view prefix="/Sound/Resource/Stream/";
        const auto at=path.find(prefix);
        if(at==path.npos) return false;
        const auto stream=path.substr(at+prefix.size());
        if(!stream.starts_with("BGM_Title") && !stream.starts_with("BGM_World_") && !stream.starts_with("BGM_Course")) return false;
        if(previous==path) return false;
        previous=path;
        return true;
    }
};
}
